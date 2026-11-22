// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// PPAPI revival: renderer-side host for the Flash-private PPB_Flash singleton
// (GetProxyForURL / SetInstanceAlwaysOnTop / Navigate / IsRectTopmost /
// InvokePrinting / DrawGlyphs). Ported from Chromium M80
// (chrome/renderer/pepper/pepper_flash_renderer_host.cc); Chromium had already
// dropped the chrome-layer implementation before PPAPI itself was removed.

#ifndef CONTENT_RENDERER_PEPPER_PEPPER_FLASH_RENDERER_HOST_H_
#define CONTENT_RENDERER_PEPPER_PEPPER_FLASH_RENDERER_HOST_H_

#include <stdint.h>

#include <string>
#include <vector>

#include "base/memory/weak_ptr.h"
#include "ppapi/host/resource_host.h"

namespace ppapi {
namespace proxy {
struct PPBFlash_DrawGlyphs_Params;
}  // namespace proxy
}  // namespace ppapi


struct PP_Rect;

namespace ppapi {
struct URLRequestInfoData;
}

namespace content {
class RendererPpapiHost;
}

class PepperFlashRendererHost : public ppapi::host::ResourceHost {
 public:
  PepperFlashRendererHost(content::RendererPpapiHost* host,
                          PP_Instance instance,
                          PP_Resource resource);
  ~PepperFlashRendererHost() override;

  // ppapi::host::ResourceHost overrides.
  int32_t OnResourceMessageReceived(
      const IPC::Message& msg,
      ppapi::host::HostMessageContext* context) override;

 private:
  int32_t OnGetProxyForURL(ppapi::host::HostMessageContext* host_context,
                           const std::string& url);
  int32_t OnSetInstanceAlwaysOnTop(
      ppapi::host::HostMessageContext* host_context,
      bool on_top);

  int32_t OnNavigate(ppapi::host::HostMessageContext* host_context,
                     const ppapi::URLRequestInfoData& data,
                     const std::string& target,
                     bool from_user_action);
  int32_t OnIsRectTopmost(ppapi::host::HostMessageContext* host_context,
                          const PP_Rect& rect);
  int32_t OnInvokePrinting(ppapi::host::HostMessageContext* host_context);

  // PPAPI revival: PPB_Flash::DrawGlyphs. Ported from Chromium 76
  // (chrome/renderer/pepper/pepper_flash_renderer_host.cc). Flash uses it to
  // rasterize text into an ImageData with an arbitrary transform; without it
  // Flash falls back to a slower path.
  int32_t OnDrawGlyphs(
      ppapi::host::HostMessageContext* host_context,
      ppapi::proxy::PPBFlash_DrawGlyphs_Params params);

  // A stack of ReplyMessageContexts to track Navigate() calls which have not
  // yet been replied to.
  std::vector<ppapi::host::ReplyMessageContext> navigate_replies_;

  raw_ptr<content::RendererPpapiHost> host_;
  base::WeakPtrFactory<PepperFlashRendererHost> weak_factory_{this};

  PepperFlashRendererHost(const PepperFlashRendererHost&) = delete;
  PepperFlashRendererHost& operator=(const PepperFlashRendererHost&) = delete;
};

#endif  // CONTENT_RENDERER_PEPPER_PEPPER_FLASH_RENDERER_HOST_H_
