// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/renderer/pepper/pepper_flash_renderer_host.h"

#include <stddef.h>

#include "content/public/renderer/pepper_plugin_instance.h"
#include "content/public/renderer/renderer_ppapi_host.h"
#include "ppapi/c/pp_errors.h"
#include "ppapi/host/dispatch_host_message.h"
#include "ppapi/host/host_message_context.h"
#include "ppapi/host/ppapi_host.h"
#include "ppapi/proxy/host_dispatcher.h"
#include "ppapi/proxy/ppapi_messages.h"
#include "ppapi/proxy/serialized_structs.h"
#include "ppapi/shared_impl/url_request_info_data.h"
#include "ui/gfx/geometry/rect.h"

PepperFlashRendererHost::PepperFlashRendererHost(
    content::RendererPpapiHost* host,
    PP_Instance instance,
    PP_Resource resource)
    : ResourceHost(host->GetPpapiHost(), instance, resource), host_(host) {}

PepperFlashRendererHost::~PepperFlashRendererHost() {
  // This object may be destroyed while a Navigate is in flight (it is a sync
  // call which can re-enter and destroy the instance). Reply to any outstanding
  // messages so the plugin process does not stay blocked.
  for (size_t i = 0; i < navigate_replies_.size(); ++i)
    SendReply(navigate_replies_[i], IPC::Message());
  navigate_replies_.clear();
}

int32_t PepperFlashRendererHost::OnResourceMessageReceived(
    const IPC::Message& msg,
    ppapi::host::HostMessageContext* context) {
  PPAPI_BEGIN_MESSAGE_MAP(PepperFlashRendererHost, msg)
    PPAPI_DISPATCH_HOST_RESOURCE_CALL(PpapiHostMsg_Flash_GetProxyForURL,
                                      OnGetProxyForURL)
    PPAPI_DISPATCH_HOST_RESOURCE_CALL(PpapiHostMsg_Flash_SetInstanceAlwaysOnTop,
                                      OnSetInstanceAlwaysOnTop)

    PPAPI_DISPATCH_HOST_RESOURCE_CALL(PpapiHostMsg_Flash_Navigate, OnNavigate)
    PPAPI_DISPATCH_HOST_RESOURCE_CALL(PpapiHostMsg_Flash_IsRectTopmost,
                                      OnIsRectTopmost)
    PPAPI_DISPATCH_HOST_RESOURCE_CALL_0(PpapiHostMsg_Flash_InvokePrinting,
                                        OnInvokePrinting)
  PPAPI_END_MESSAGE_MAP()
  return PP_ERROR_FAILED;
}

int32_t PepperFlashRendererHost::OnGetProxyForURL(
    ppapi::host::HostMessageContext* host_context,
    const std::string& url) {
  // Chromium 140 removed content::RenderThread::ResolveProxy() (proxy
  // resolution moved to the network service), and this renderer has no direct
  // handle to a proxy resolver. Reporting failure makes Flash fall back to its
  // own behaviour, which is what happened before this host existed. Flash's
  // actual HTTP traffic still goes through PPB_URLLoader and therefore still
  // picks up the embedder's proxy configuration.
  LOG(ERROR) << "[flash] GetProxyForURL unsupported for " << url;
  return PP_ERROR_FAILED;
}

int32_t PepperFlashRendererHost::OnSetInstanceAlwaysOnTop(
    ppapi::host::HostMessageContext* host_context,
    bool on_top) {
  content::PepperPluginInstance* plugin_instance =
      host_->GetPluginInstance(pp_instance());
  if (plugin_instance)
    plugin_instance->SetAlwaysOnTop(on_top);
  // Since no reply is sent for this message, it doesn't make sense to return an
  // error.
  return PP_OK;
}


int32_t PepperFlashRendererHost::OnNavigate(
    ppapi::host::HostMessageContext* host_context,
    const ppapi::URLRequestInfoData& data,
    const std::string& target,
    bool from_user_action) {
  // If our PepperPluginInstance is already destroyed, just return a failure.
  content::PepperPluginInstance* plugin_instance =
      host_->GetPluginInstance(pp_instance());
  if (!plugin_instance)
    return PP_ERROR_FAILED;

  // Navigate may call into JavaScript (e.g. with a "javascript:" URL) or
  // navigate the page away, either of which re-enters the plugin. That is
  // expected behaviour (equivalent to NPN_GetURL).
  ppapi::proxy::HostDispatcher* host_dispatcher =
      ppapi::proxy::HostDispatcher::GetForInstance(pp_instance());
  if (host_dispatcher)
    host_dispatcher->set_allow_plugin_reentrancy();

  // Grab a weak pointer so we can tell whether we survived the Navigate call.
  base::WeakPtr<PepperFlashRendererHost> weak_ptr = weak_factory_.GetWeakPtr();
  navigate_replies_.push_back(host_context->MakeReplyMessageContext());
  plugin_instance->Navigate(data, target.c_str(), from_user_action);
  // This object may have been destroyed by this point; if so the reply is sent
  // from the destructor.
  if (weak_ptr.get()) {
    SendReply(navigate_replies_.back(), IPC::Message());
    navigate_replies_.pop_back();
  }

  // Return PP_OK_COMPLETIONPENDING so no automatic reply is sent.
  return PP_OK_COMPLETIONPENDING;
}

int32_t PepperFlashRendererHost::OnIsRectTopmost(
    ppapi::host::HostMessageContext* host_context,
    const PP_Rect& rect) {
  content::PepperPluginInstance* plugin_instance =
      host_->GetPluginInstance(pp_instance());
  if (plugin_instance &&
      plugin_instance->IsRectTopmost(gfx::Rect(
          rect.point.x, rect.point.y, rect.size.width, rect.size.height)))
    return PP_OK;
  return PP_ERROR_FAILED;
}

int32_t PepperFlashRendererHost::OnInvokePrinting(
    ppapi::host::HostMessageContext* host_context) {
  // Flash's print dialog is not implemented in this build (Chrome used the PDF
  // component for this, which content does not ship). Report success so the
  // plugin keeps running; nothing is printed.
  LOG(ERROR) << "[flash] InvokePrinting ignored (not implemented)";
  return PP_OK;
}
