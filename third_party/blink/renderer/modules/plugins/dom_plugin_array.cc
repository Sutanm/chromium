/*
 *  Copyright (C) 2008 Nokia Corporation and/or its subsidiary(-ies)
 *  Copyright (C) 2008 Apple Inc. All rights reserved.
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 *  MA 02110-1301 USA
 */

#include "third_party/blink/renderer/modules/plugins/dom_plugin_array.h"

#include "third_party/blink/public/common/features.h"
#include "third_party/blink/renderer/core/frame/local_dom_window.h"
#include "third_party/blink/renderer/core/frame/local_frame.h"
#include "third_party/blink/renderer/core/frame/navigator.h"
#include "third_party/blink/renderer/core/page/page.h"
#include "third_party/blink/renderer/core/page/plugin_data.h"
#include "third_party/blink/renderer/modules/plugins/dom_mime_type_array.h"
#include "third_party/blink/renderer/modules/plugins/navigator_plugins.h"
#include "third_party/blink/renderer/platform/heap/garbage_collected.h"
#include "third_party/blink/renderer/platform/wtf/text/atomic_string.h"
#include "third_party/blink/renderer/platform/wtf/vector.h"

namespace blink {

namespace {
DOMPlugin* MakeFakePlugin(String plugin_name, LocalDOMWindow* window) {
  String description = "Portable Document Format";
  String filename = "internal-pdf-viewer";
  auto* plugin_info =
      MakeGarbageCollected<PluginInfo>(plugin_name, filename, description,
                                       /*background_color=*/Color::kTransparent,
                                       /*may_use_external_handler=*/false);
  Vector<String> extensions{"pdf"};
  for (const char* mime_type : {"application/pdf", "text/pdf"}) {
    auto* mime_info = MakeGarbageCollected<MimeClassInfo>(
        mime_type, description, *plugin_info, extensions);
    plugin_info->AddMimeType(mime_info);
  }
  return MakeGarbageCollected<DOMPlugin>(window, *plugin_info);
}
}  // namespace

DOMPluginArray::DOMPluginArray(LocalDOMWindow* window) : window_(window) {}

// 【移植】恢复 Chromium 87 的 PluginData 枚举，让 PPAPI Flash 重新出现在
// navigator.plugins / navigator.mimeTypes 里。
void DOMPluginArray::EnsurePlugins() const {
  if (plugins_initialized_)
    return;
  plugins_initialized_ = true;

  PluginData* data = GetPluginData();
  bool has_pdf = false;
  if (data) {
    data->UpdatePluginList();
    for (const Member<PluginInfo>& info : data->Plugins()) {
      dom_plugins_.push_back(MakeGarbageCollected<DOMPlugin>(window_, *info));
    }
    for (const Member<MimeClassInfo>& mime : data->Mimes()) {
      if (mime->Type() == "application/pdf") {
        has_pdf = true;
        break;
      }
    }
  }

  if (!dom_plugins_.empty())
    return;

  // 没有真实插件时，仍给出 spec 规定的 PDF 假列表。
  if (has_pdf) {
    Vector<String> plugins{"PDF Viewer", "Chrome PDF Viewer",
                           "Chromium PDF Viewer", "Microsoft Edge PDF Viewer",
                           "WebKit built-in PDF"};
    for (auto name : plugins) {
      dom_plugins_.push_back(MakeFakePlugin(name, window_));
    }
  }
}

void DOMPluginArray::Trace(Visitor* visitor) const {
  visitor->Trace(window_);
  visitor->Trace(dom_plugins_);
  ScriptWrappable::Trace(visitor);
}

unsigned DOMPluginArray::length() const {
  EnsurePlugins();
  return dom_plugins_.size();
}

DOMPlugin* DOMPluginArray::item(unsigned index) {
  EnsurePlugins();
  if (index >= dom_plugins_.size()) {
    return nullptr;
  }
  return dom_plugins_[index].Get();
}

DOMPlugin* DOMPluginArray::namedItem(const AtomicString& property_name) {
  EnsurePlugins();
  for (const auto& plugin : dom_plugins_) {
    if (plugin->name() == property_name) {
      return plugin.Get();
    }
  }
  return nullptr;
}

void DOMPluginArray::NamedPropertyEnumerator(Vector<String>& property_names,
                                             ExceptionState&) const {
  EnsurePlugins();
  property_names.ReserveInitialCapacity(dom_plugins_.size());
  for (const auto& plugin : dom_plugins_) {
    property_names.UncheckedAppend(plugin->name());
  }
}

bool DOMPluginArray::NamedPropertyQuery(const AtomicString& property_name,
                                        ExceptionState& exception_state) const {
  Vector<String> properties;
  NamedPropertyEnumerator(properties, exception_state);
  return properties.Contains(property_name);
}

void DOMPluginArray::refresh(bool reload) {
  if (!window_) {
    return;
  }
  dom_plugins_.clear();
  plugins_initialized_ = false;
  PluginData::RefreshBrowserSidePluginCache();
  if (PluginData* data = GetPluginData()) {
    data->ResetPluginData();
  }
  if (reload && window_->GetFrame()) {
    window_->GetFrame()->Reload(WebFrameLoadType::kReload);
  }
}

PluginData* DOMPluginArray::GetPluginData() const {
  return (window_ && window_->GetFrame()) ? window_->GetFrame()->GetPluginData()
                                          : nullptr;
}

HeapVector<Member<DOMMimeType>> DOMPluginArray::GetFixedMimeTypeArray() {
  HeapVector<Member<DOMMimeType>> mimetypes;
  if (dom_plugins_.empty())
    return mimetypes;
  DCHECK_EQ(dom_plugins_[0]->length(), 2u);
  mimetypes.push_back(dom_plugins_[0]->item(0));
  mimetypes.push_back(dom_plugins_[0]->item(1));
  return mimetypes;
}

bool DOMPluginArray::IsPdfViewerAvailable() {
  auto* data = GetPluginData();
  if (!data)
    return false;
  for (const Member<MimeClassInfo>& mime_info : data->Mimes()) {
    if (mime_info->Type() == "application/pdf")
      return true;
  }
  return false;
}

}  // namespace blink
