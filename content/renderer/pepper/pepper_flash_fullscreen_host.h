// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// PPAPI revival: renderer-side host for the Flash-private fullscreen API
// (PPB_Flash_Fullscreen). Ported from Chromium M80
// (chrome/renderer/pepper/pepper_flash_fullscreen_host.cc) because Flash 29
// uses this API instead of the standard PPB_Fullscreen.

#ifndef CONTENT_RENDERER_PEPPER_PEPPER_FLASH_FULLSCREEN_HOST_H_
#define CONTENT_RENDERER_PEPPER_PEPPER_FLASH_FULLSCREEN_HOST_H_

#include <stdint.h>

#include "base/memory/weak_ptr.h"
#include "ppapi/host/resource_host.h"

namespace content {

class RendererPpapiHost;

class PepperFlashFullscreenHost : public ppapi::host::ResourceHost {
 public:
  PepperFlashFullscreenHost(RendererPpapiHost* host,
                            PP_Instance instance,
                            PP_Resource resource);
  ~PepperFlashFullscreenHost() override;

  // ppapi::host::ResourceHost overrides.
  int32_t OnResourceMessageReceived(
      const IPC::Message& msg,
      ppapi::host::HostMessageContext* context) override;

 private:
  int32_t OnSetFullscreen(ppapi::host::HostMessageContext* context,
                          bool fullscreen);

  raw_ptr<RendererPpapiHost> renderer_ppapi_host_;

  PepperFlashFullscreenHost(const PepperFlashFullscreenHost&) = delete;
  PepperFlashFullscreenHost& operator=(const PepperFlashFullscreenHost&) =
      delete;
};

}  // namespace content

#endif  // CONTENT_RENDERER_PEPPER_PEPPER_FLASH_FULLSCREEN_HOST_H_
