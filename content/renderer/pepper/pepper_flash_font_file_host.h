// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// PPAPI revival: renderer-side host for the Flash-private font-file API
// (PPB_Flash_FontFile). Ported from Chromium M80
// (chrome/renderer/pepper/pepper_flash_font_file_host.cc), Windows path only.

#ifndef CONTENT_RENDERER_PEPPER_PEPPER_FLASH_FONT_FILE_HOST_H_
#define CONTENT_RENDERER_PEPPER_PEPPER_FLASH_FONT_FILE_HOST_H_

#include <stddef.h>
#include <stdint.h>

#include "base/memory/raw_ptr.h"
#include "ppapi/c/private/ppb_flash_font_file.h"
#include "ppapi/host/resource_host.h"
#include "third_party/skia/include/core/SkRefCnt.h"

class SkTypeface;

namespace ppapi {
namespace proxy {
struct SerializedFontDescription;
}
}  // namespace ppapi

namespace content {
class RendererPpapiHost;
}

class PepperFlashFontFileHost : public ppapi::host::ResourceHost {
 public:
  PepperFlashFontFileHost(
      content::RendererPpapiHost* host,
      PP_Instance instance,
      PP_Resource resource,
      const ppapi::proxy::SerializedFontDescription& description,
      PP_PrivateFontCharset charset);
  ~PepperFlashFontFileHost() override;

  // ppapi::host::ResourceHost override.
  int32_t OnResourceMessageReceived(
      const IPC::Message& msg,
      ppapi::host::HostMessageContext* context) override;

 private:
  bool GetFontData(uint32_t table, void* buffer, size_t* length);
  int32_t OnGetFontTable(ppapi::host::HostMessageContext* context,
                         uint32_t table);

  sk_sp<SkTypeface> typeface_;

  PepperFlashFontFileHost(const PepperFlashFontFileHost&) = delete;
  PepperFlashFontFileHost& operator=(const PepperFlashFontFileHost&) = delete;
};

#endif  // CONTENT_RENDERER_PEPPER_PEPPER_FLASH_FONT_FILE_HOST_H_
