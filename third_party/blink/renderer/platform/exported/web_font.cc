// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// PPAPI revival: vendored from Chromium 138 (Blink's WebFont was deleted in 140
// together with PPAPI) and adapted to the 140 Blink APIs:
//   * RuntimeEnabledFeatures::PlainTextPainterEnabled() no longer exists (the
//     feature shipped), so the deprecated Font fallbacks are gone.
//   * PlainTextPainter no longer exposes OffsetForPosition /
//     SelectionRectForText; both are implemented here on top of shaped
//     inline sizes (TextRun::SubRun) instead.

#include "third_party/blink/public/platform/web_font.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "base/compiler_specific.h"
#include "base/containers/span.h"
#include "cc/paint/paint_flags.h"
#include "third_party/blink/public/platform/web_font_description.h"
#include "third_party/blink/public/platform/web_text_run.h"
#include "third_party/blink/renderer/platform/fonts/font.h"
#include "third_party/blink/renderer/platform/fonts/font_cache.h"
#include "third_party/blink/renderer/platform/fonts/font_description.h"
#include "third_party/blink/renderer/platform/fonts/plain_text_painter.h"
#include "third_party/blink/renderer/platform/fonts/simple_font_data.h"
#include "third_party/blink/renderer/platform/text/text_direction.h"
#include "third_party/blink/renderer/platform/text/text_run.h"
#include "third_party/blink/renderer/platform/wtf/text/string_view.h"
#include "third_party/blink/renderer/platform/wtf/allocator/allocator.h"

namespace blink {

namespace {

// Shapes |text| (owned by the caller) for the given direction.
TextRun MakeTextRun(const std::u16string& text,
                    TextDirection direction,
                    bool directional_override) {
  // |text| keeps ownership; StringView only holds a view of its buffer.
  const base::span<const UChar> chars = UNSAFE_BUFFERS(
      base::span<const UChar>(reinterpret_cast<const UChar*>(text.data()),
                              text.size()));
  StringView view(chars);
  return TextRun(view, direction, directional_override);
}

TextDirection DirectionOf(const WebTextRun& run) {
  return run.rtl ? TextDirection::kRtl : TextDirection::kLtr;
}

// Shaped width of [start, start + length) inside |text|.
float SubWidth(const std::u16string& text,
               const Font& font,
               const WebTextRun& run,
               unsigned start,
               unsigned length) {
  if (length == 0)
    return 0.0f;
  TextRun full = MakeTextRun(text, DirectionOf(run), run.directional_override);
  TextRun sub = full.SubRun(start, length);
  return PlainTextPainter::Shared().ComputeInlineSizeWithoutBidi(sub, font);
}

}  // namespace

WebFont* WebFont::Create(const WebFontDescription& description) {
  return new WebFont(description);
}

class WebFont::Impl final : public GarbageCollected<WebFont::Impl> {
 public:
  explicit Impl(const WebFontDescription& description)
      : font_(MakeGarbageCollected<Font>(description)) {}

  void Trace(Visitor* visitor) const { visitor->Trace(font_); }

  const Font* GetFont() const { return font_; }

 private:
  Member<const Font> font_;
};

WebFont::WebFont(const WebFontDescription& description)
    : private_(MakeGarbageCollected<Impl>(description)) {}

WebFont::~WebFont() = default;

WebFontDescription WebFont::GetFontDescription() const {
  return WebFontDescription(private_->GetFont()->GetFontDescription());
}

static inline const SimpleFontData* GetFontData(const Font* font) {
  const SimpleFontData* font_data = font->PrimaryFont();
  DCHECK(font_data);
  return font_data;
}

int WebFont::Ascent() const {
  const SimpleFontData* font_data = GetFontData(private_->GetFont());
  return font_data ? font_data->GetFontMetrics().Ascent() : 0;
}

int WebFont::Descent() const {
  const SimpleFontData* font_data = GetFontData(private_->GetFont());
  return font_data ? font_data->GetFontMetrics().Descent() : 0;
}

int WebFont::Height() const {
  const SimpleFontData* font_data = GetFontData(private_->GetFont());
  return font_data ? font_data->GetFontMetrics().Height() : 0;
}

int WebFont::LineSpacing() const {
  const SimpleFontData* font_data = GetFontData(private_->GetFont());
  return font_data ? font_data->GetFontMetrics().LineSpacing() : 0;
}

float WebFont::XHeight() const {
  const SimpleFontData* font_data = private_->GetFont()->PrimaryFont();
  DCHECK(font_data);
  return font_data ? font_data->GetFontMetrics().XHeight() : 0;
}

void WebFont::DrawText(cc::PaintCanvas* canvas,
                       const WebTextRun& run,
                       const gfx::PointF& left_baseline,
                       SkColor color) const {
  FontCachePurgePreventer font_cache_purge_preventer;
  const std::u16string text = run.text.Utf16();
  if (text.empty())
    return;

  TextRun text_run =
      MakeTextRun(text, DirectionOf(run), run.directional_override);

  cc::PaintFlags flags;
  flags.setColor(color);
  flags.setAntiAlias(true);
  PlainTextPainter::Shared().DrawWithoutBidi(text_run, *private_->GetFont(),
                                             *canvas, left_baseline, flags);
}

int WebFont::CalculateWidth(const WebTextRun& run) const {
  const std::u16string text = run.text.Utf16();
  if (text.empty())
    return 0;
  return static_cast<int>(std::lround(
      SubWidth(text, *private_->GetFont(), run, 0,
               static_cast<unsigned>(text.size()))));
}

int WebFont::OffsetForPosition(const WebTextRun& run, float position) const {
  const std::u16string text = run.text.Utf16();
  const unsigned total = static_cast<unsigned>(text.size());
  if (total == 0 || position <= 0)
    return 0;

  const Font& font = *private_->GetFont();
  unsigned last = 0;
  for (unsigned i = 1; i <= total; ++i) {
    if (SubWidth(text, font, run, 0, i) > position)
      return static_cast<int>(last);
    last = i;
  }
  return static_cast<int>(total);
}

gfx::RectF WebFont::SelectionRectForText(const WebTextRun& run,
                                         const gfx::PointF& left_baseline,
                                         int height,
                                         int from,
                                         int to) const {
  const std::u16string text = run.text.Utf16();
  const unsigned total = static_cast<unsigned>(text.size());
  if (to == -1)
    to = static_cast<int>(total);
  from = std::clamp(from, 0, static_cast<int>(total));
  to = std::clamp(to, from, static_cast<int>(total));

  const Font& font = *private_->GetFont();
  const float begin = SubWidth(text, font, run, 0, static_cast<unsigned>(from));
  const float end = SubWidth(text, font, run, 0, static_cast<unsigned>(to));
  return gfx::RectF(left_baseline.x() + begin,
                    left_baseline.y() - static_cast<float>(height),
                    end - begin, static_cast<float>(height));
}

}  // namespace blink
