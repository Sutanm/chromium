// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// PPAPI revival: browser-side host for the Flash-private PPB_Flash messages
// that are addressed to the browser process (UpdateActivity,
// GetLocalTimeZoneOffset, GetLocalDataRestrictions). Ported in spirit from
// Chromium M80 (chrome/browser/renderer_host/pepper/pepper_flash_browser_host),
// reduced to what content can implement: no wake lock, and LSO restrictions are
// always reported as unrestricted (content has no CookieSettings/profile).

#ifndef CONTENT_BROWSER_RENDERER_HOST_PEPPER_PEPPER_FLASH_BROWSER_HOST_H_
#define CONTENT_BROWSER_RENDERER_HOST_PEPPER_PEPPER_FLASH_BROWSER_HOST_H_

#include <stdint.h>

#include "base/memory/raw_ptr.h"
#include "base/time/time.h"
#include "ppapi/host/resource_host.h"

namespace content {

class BrowserPpapiHost;

class PepperFlashBrowserHost : public ppapi::host::ResourceHost {
 public:
  PepperFlashBrowserHost(BrowserPpapiHost* host,
                         PP_Instance instance,
                         PP_Resource resource);
  ~PepperFlashBrowserHost() override;

  // ppapi::host::ResourceHost overrides.
  int32_t OnResourceMessageReceived(
      const IPC::Message& msg,
      ppapi::host::HostMessageContext* context) override;

 private:
  int32_t OnUpdateActivity(ppapi::host::HostMessageContext* host_context);
  int32_t OnGetLocalTimeZoneOffset(ppapi::host::HostMessageContext* host_context,
                                   const base::Time& t);
  int32_t OnGetLocalDataRestrictions(
      ppapi::host::HostMessageContext* context);

  raw_ptr<BrowserPpapiHost> host_;
  int render_process_id_ = -1;

  PepperFlashBrowserHost(const PepperFlashBrowserHost&) = delete;
  PepperFlashBrowserHost& operator=(const PepperFlashBrowserHost&) = delete;
};

}  // namespace content

#endif  // CONTENT_BROWSER_RENDERER_HOST_PEPPER_PEPPER_FLASH_BROWSER_HOST_H_
