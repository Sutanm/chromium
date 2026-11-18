// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/renderer_host/pepper/pepper_flash_browser_host.h"

#include "content/public/browser/browser_ppapi_host.h"
#include "ppapi/c/pp_errors.h"
#include "ppapi/host/dispatch_host_message.h"
#include "ppapi/host/host_message_context.h"
#include "ppapi/host/ppapi_host.h"
#include "ppapi/proxy/ppapi_messages.h"
#include "ppapi/shared_impl/time_conversion.h"

namespace content {

PepperFlashBrowserHost::PepperFlashBrowserHost(BrowserPpapiHost* host,
                                               PP_Instance instance,
                                               PP_Resource resource)
    : ResourceHost(host->GetPpapiHost(), instance, resource), host_(host) {
  int unused = 0;
  host->GetRenderFrameIDsForInstance(instance, &render_process_id_, &unused);
}

PepperFlashBrowserHost::~PepperFlashBrowserHost() = default;

int32_t PepperFlashBrowserHost::OnResourceMessageReceived(
    const IPC::Message& msg,
    ppapi::host::HostMessageContext* context) {
  PPAPI_BEGIN_MESSAGE_MAP(PepperFlashBrowserHost, msg)
    PPAPI_DISPATCH_HOST_RESOURCE_CALL_0(PpapiHostMsg_Flash_UpdateActivity,
                                        OnUpdateActivity)
    PPAPI_DISPATCH_HOST_RESOURCE_CALL(PpapiHostMsg_Flash_GetLocalTimeZoneOffset,
                                      OnGetLocalTimeZoneOffset)
    PPAPI_DISPATCH_HOST_RESOURCE_CALL_0(
        PpapiHostMsg_Flash_GetLocalDataRestrictions, OnGetLocalDataRestrictions)
  PPAPI_END_MESSAGE_MAP()
  return PP_ERROR_FAILED;
}

int32_t PepperFlashBrowserHost::OnUpdateActivity(
    ppapi::host::HostMessageContext* host_context) {
  // M80 requested a power-save wake lock here. Content has no wake lock
  // implementation, so this is a no-op; not sleeping is only a battery
  // concern, not a correctness one.
  return PP_OK;
}

int32_t PepperFlashBrowserHost::OnGetLocalTimeZoneOffset(
    ppapi::host::HostMessageContext* host_context,
    const base::Time& t) {
  host_context->reply_msg =
      PpapiPluginMsg_Flash_GetLocalTimeZoneOffsetReply(
          ppapi::PPGetLocalTimeZoneOffset(t));
  return PP_OK;
}

int32_t PepperFlashBrowserHost::OnGetLocalDataRestrictions(
    ppapi::host::HostMessageContext* context) {
  // Chrome derived these restrictions from the profile's CookieSettings, which
  // content does not have. Report "no restrictions" so Flash treats its local
  // storage as always allowed (this is what happens for a first-party plugin).
  context->reply_msg =
      PpapiPluginMsg_Flash_GetLocalDataRestrictionsReply(
          static_cast<int32_t>(PP_FLASHLSORESTRICTIONS_NONE));
  return PP_OK;
}

}  // namespace content
