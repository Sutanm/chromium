#pragma allow_unsafe_buffers
#ifdef UNSAFE_BUFFERS_BUILD
// Allow legacy buffer arithmetic in the vendored PPAPI sources.
#pragma allow_unsafe_buffers
#endif
// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ppapi/host/ppapi_host.h"

#include <stddef.h>

#include <utility>

#include "base/check.h"
#include "base/notreached.h"
#include "ppapi/c/pp_errors.h"
#include "ppapi/host/host_factory.h"
#include "ppapi/host/host_message_context.h"
#include "ppapi/host/instance_message_filter.h"
#include "ppapi/host/resource_host.h"
#include "ppapi/proxy/ppapi_messages.h"
#include "ppapi/proxy/resource_message_params.h"
#include "ppapi/proxy/serialized_handle.h"
#include "ppapi/shared_impl/host_resource.h"



namespace ppapi {
namespace host {

using proxy::SerializedHandle;

// 【实验探针】把 Flash 私有 IPC 消息的 ID 映射成名字，用于观测"哪些 Flash
// 私有能力被目标内容真实调用了、而宿主并不存在"。
namespace {

struct FlashMsgName {
  int id;
  const char* name;
};

#define BAO_FLASH_MSG_NAME(tag) {PpapiHostMsg_##tag::ID, #tag}
const FlashMsgName kFlashMsgNames[] = {
    BAO_FLASH_MSG_NAME(Flash_Create),
    BAO_FLASH_MSG_NAME(Flash_UpdateActivity),
    BAO_FLASH_MSG_NAME(Flash_GetProxyForURL),
    BAO_FLASH_MSG_NAME(Flash_GetLocalTimeZoneOffset),
    BAO_FLASH_MSG_NAME(Flash_GetLocalDataRestrictions),
    BAO_FLASH_MSG_NAME(Flash_SetInstanceAlwaysOnTop),
    BAO_FLASH_MSG_NAME(Flash_DrawGlyphs),
    BAO_FLASH_MSG_NAME(Flash_Navigate),
    BAO_FLASH_MSG_NAME(Flash_IsRectTopmost),
    BAO_FLASH_MSG_NAME(Flash_InvokePrinting),
    BAO_FLASH_MSG_NAME(FlashClipboard_Create),
    BAO_FLASH_MSG_NAME(FlashClipboard_RegisterCustomFormat),
    BAO_FLASH_MSG_NAME(FlashClipboard_IsFormatAvailable),
    BAO_FLASH_MSG_NAME(FlashClipboard_ReadData),
    BAO_FLASH_MSG_NAME(FlashClipboard_WriteData),
    BAO_FLASH_MSG_NAME(FlashClipboard_GetSequenceNumber),
    BAO_FLASH_MSG_NAME(FlashDRM_Create),
    BAO_FLASH_MSG_NAME(FlashDRM_GetDeviceID),
    BAO_FLASH_MSG_NAME(FlashDRM_GetHmonitor),
    BAO_FLASH_MSG_NAME(FlashDRM_GetVoucherFile),
    BAO_FLASH_MSG_NAME(FlashDRM_MonitorIsExternal),
    BAO_FLASH_MSG_NAME(FlashFile_Create),
    BAO_FLASH_MSG_NAME(FlashFile_OpenFile),
    BAO_FLASH_MSG_NAME(FlashFile_RenameFile),
    BAO_FLASH_MSG_NAME(FlashFile_DeleteFileOrDir),
    BAO_FLASH_MSG_NAME(FlashFile_CreateDir),
    BAO_FLASH_MSG_NAME(FlashFile_QueryFile),
    BAO_FLASH_MSG_NAME(FlashFile_GetDirContents),
    BAO_FLASH_MSG_NAME(FlashFile_CreateTemporaryFile),
    BAO_FLASH_MSG_NAME(FlashFontFile_Create),
    BAO_FLASH_MSG_NAME(FlashFontFile_GetFontTable),
    BAO_FLASH_MSG_NAME(FlashFullscreen_Create),
    BAO_FLASH_MSG_NAME(FlashFullscreen_SetFullscreen),
    BAO_FLASH_MSG_NAME(FlashMenu_Create),
    BAO_FLASH_MSG_NAME(FlashMenu_Show),
};
#undef BAO_FLASH_MSG_NAME

const char* FlashMsgNameOf(int type) {
  for (const auto& entry : kFlashMsgNames) {
    if (entry.id == type)
      return entry.name;
  }
  return nullptr;  // 未知（非 Flash 私有）消息由调用方跳过
}

}  // namespace

namespace {

// Put a cap on the maximum number of resources so we don't explode if the
// renderer starts spamming us.
const size_t kMaxResourcesPerPlugin = 1 << 14;

}  // namespace

PpapiHost::PpapiHost(IPC::Sender* sender,
                     const PpapiPermissions& perms)
    : sender_(sender),
      permissions_(perms),
      next_pending_resource_host_id_(1) {
}

PpapiHost::~PpapiHost() {
  // Delete these explicitly before destruction since then the host is still
  // technically alive in case one of the filters accesses us from the
  // destructor.
  instance_message_filters_.clear();

  // The resources may also want to use us in their destructors.
  resources_.clear();
  pending_resource_hosts_.clear();
}

bool PpapiHost::Send(IPC::Message* msg) {
  return sender_->Send(msg);
}

bool PpapiHost::OnMessageReceived(const IPC::Message& msg) {
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP(PpapiHost, msg)
    IPC_MESSAGE_HANDLER(PpapiHostMsg_ResourceCall,
                        OnHostMsgResourceCall)
    IPC_MESSAGE_HANDLER(PpapiHostMsg_InProcessResourceCall,
                        OnHostMsgInProcessResourceCall)
    IPC_MESSAGE_HANDLER_DELAY_REPLY(PpapiHostMsg_ResourceSyncCall,
                                    OnHostMsgResourceSyncCall)
    IPC_MESSAGE_HANDLER(PpapiHostMsg_ResourceCreated,
                        OnHostMsgResourceCreated)
    IPC_MESSAGE_HANDLER(PpapiHostMsg_AttachToPendingHost,
                        OnHostMsgAttachToPendingHost)
    IPC_MESSAGE_HANDLER(PpapiHostMsg_ResourceDestroyed,
                        OnHostMsgResourceDestroyed)
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()

  if (!handled) {
    for (size_t i = 0; i < instance_message_filters_.size(); i++) {
      if (instance_message_filters_[i]->OnInstanceMessageReceived(msg)) {
        handled = true;
        break;
      }
    }
  }

  if (!handled) {
    // PPAPI revival 诊断：只在消息属于 Flash 私有接口时才报告。标准 PPAPI 消息
    // 由其他路由处理，全量打印只会产生噪声（实测一次页面加载 639 条）。
    if (const char* name = FlashMsgNameOf(msg.type())) {
      LOG(ERROR) << "[ppexp] UNHANDLED Flash host message type=" << msg.type()
                 << " name=" << name;
    }
  }

  return handled;
}

void PpapiHost::SendReply(const ReplyMessageContext& context,
                          const IPC::Message& msg) {
  TRACE_EVENT2("ppapi_proxy", "PpapiHost::SendReply", "Class",
               IPC_MESSAGE_ID_CLASS(msg.type()), "Line",
               IPC_MESSAGE_ID_LINE(msg.type()));
  if (context.sync_reply_msg) {
    PpapiHostMsg_ResourceSyncCall::WriteReplyParams(context.sync_reply_msg,
                                                    context.params, msg);
    Send(context.sync_reply_msg);
  } else {
    if (context.routing_id != MSG_ROUTING_NONE) {
      Send(new PpapiHostMsg_InProcessResourceReply(context.routing_id,
                                                   context.params,
                                                   msg));
    } else {
      Send(new PpapiPluginMsg_ResourceReply(context.params, msg));
    }
  }
}

void PpapiHost::SendUnsolicitedReply(PP_Resource resource,
                                     const IPC::Message& msg) {
  std::vector<SerializedHandle> empty;
  SendUnsolicitedReplyWithHandles(resource, msg, &empty);
}

void PpapiHost::SendUnsolicitedReplyWithHandles(
    PP_Resource resource,
    const IPC::Message& msg,
    std::vector<SerializedHandle>* handles) {
  TRACE_EVENT2("ppapi_proxy", "PpapiHost::SendUnsolicitedReplyWithHandles",
               "Class", IPC_MESSAGE_ID_CLASS(msg.type()), "Line",
               IPC_MESSAGE_ID_LINE(msg.type()));
  if (!resource) {
    // Host probably still in a pending state, nothing we can do.
    return;
  }

  proxy::ResourceMessageReplyParams params(resource, 0);
  for (auto& handle : *handles)
    params.AppendHandle(std::move(handle));
  Send(new PpapiPluginMsg_ResourceReply(params, msg));
}

std::unique_ptr<ResourceHost> PpapiHost::CreateResourceHost(
    PP_Resource resource,
    PP_Instance instance,
    const IPC::Message& nested_msg) {
  std::unique_ptr<ResourceHost> resource_host;
  DCHECK(!host_factory_filters_.empty());  // Caller forgot to add a factory.
  for (size_t i = 0; i < host_factory_filters_.size(); i++) {
    resource_host = host_factory_filters_[i]->CreateResourceHost(
        this, resource, instance, nested_msg);
    if (resource_host.get())
      break;
  }
  return resource_host;
}

int PpapiHost::AddPendingResourceHost(
    std::unique_ptr<ResourceHost> resource_host) {
  // The resource ID should not be assigned.
  if (!resource_host.get() || resource_host->pp_resource() != 0) {
    NOTREACHED();
  }

  if (pending_resource_hosts_.size() + resources_.size()
      >= kMaxResourcesPerPlugin) {
    return 0;
  }

  int pending_id = next_pending_resource_host_id_++;
  pending_resource_hosts_[pending_id] = std::move(resource_host);
  return pending_id;
}

void PpapiHost::AddHostFactoryFilter(std::unique_ptr<HostFactory> filter) {
  host_factory_filters_.push_back(std::move(filter));
}

void PpapiHost::AddInstanceMessageFilter(
    std::unique_ptr<InstanceMessageFilter> filter) {
  instance_message_filters_.push_back(std::move(filter));
}

void PpapiHost::OnHostMsgResourceCall(
    const proxy::ResourceMessageCallParams& params,
    const IPC::Message& nested_msg) {
  TRACE_EVENT2("ppapi_proxy", "PpapiHost::OnHostMsgResourceCall", "Class",
               IPC_MESSAGE_ID_CLASS(nested_msg.type()), "Line",
               IPC_MESSAGE_ID_LINE(nested_msg.type()));
  HostMessageContext context(params);
  HandleResourceCall(params, nested_msg, &context);
}

void PpapiHost::OnHostMsgInProcessResourceCall(
    int routing_id,
    const proxy::ResourceMessageCallParams& params,
    const IPC::Message& nested_msg) {
  TRACE_EVENT2("ppapi_proxy", "PpapiHost::OnHostMsgInProcessResourceCall",
               "Class", IPC_MESSAGE_ID_CLASS(nested_msg.type()), "Line",
               IPC_MESSAGE_ID_LINE(nested_msg.type()));
  HostMessageContext context(routing_id, params);
  HandleResourceCall(params, nested_msg, &context);
}

void PpapiHost::OnHostMsgResourceSyncCall(
    const proxy::ResourceMessageCallParams& params,
    const IPC::Message& nested_msg,
    IPC::Message* reply_msg) {
  TRACE_EVENT2("ppapi_proxy", "PpapiHost::OnHostMsgResourceSyncCall", "Class",
               IPC_MESSAGE_ID_CLASS(nested_msg.type()), "Line",
               IPC_MESSAGE_ID_LINE(nested_msg.type()));
  // Sync messages should always have callback set because they always expect
  // a reply from the host.
  if (!params.has_callback()) {
    DLOG(WARNING) << "Expected callback for sync message.";
    return;
  }

  // Stash the |reply_msg| in the context so that it can be used to reply
  // to the sync message.
  HostMessageContext context(params, reply_msg);
  HandleResourceCall(params, nested_msg, &context);
}

void PpapiHost::HandleResourceCall(
    const proxy::ResourceMessageCallParams& params,
    const IPC::Message& nested_msg,
    HostMessageContext* context) {
  ResourceHost* resource_host = GetResourceHost(params.pp_resource());
  if (resource_host) {
    // CAUTION: Handling the message may cause the destruction of this object.
    resource_host->HandleMessage(nested_msg, context);
  } else {
    // PPAPI revival 诊断：插件调用了没有宿主实现的 Flash 私有 API。
    if (const char* name = FlashMsgNameOf(nested_msg.type())) {
      LOG(ERROR) << "[ppexp] Flash resource host MISSING: type="
                 << nested_msg.type() << " name=" << name;
    }
    if (context->params.has_callback()) {
      ReplyMessageContext reply_context = context->MakeReplyMessageContext();
      reply_context.params.set_result(PP_ERROR_BADRESOURCE);
      SendReply(reply_context, context->reply_msg);
    }
  }
}

void PpapiHost::OnHostMsgResourceCreated(
    const proxy::ResourceMessageCallParams& params,
    PP_Instance instance,
    const IPC::Message& nested_msg) {
  TRACE_EVENT2("ppapi_proxy", "PpapiHost::OnHostMsgResourceCreated", "Class",
               IPC_MESSAGE_ID_CLASS(nested_msg.type()), "Line",
               IPC_MESSAGE_ID_LINE(nested_msg.type()));

  if (pending_resource_hosts_.size() + resources_.size()
      >= kMaxResourcesPerPlugin) {
    return;
  }

  // Run through all filters until one grabs this message.
  std::unique_ptr<ResourceHost> resource_host =
      CreateResourceHost(params.pp_resource(), instance, nested_msg);

  if (!resource_host.get()) {
    // PPAPI revival 诊断：插件请求了一个本构架里没有宿主的 Flash 资源。
    if (const char* name = FlashMsgNameOf(nested_msg.type())) {
      LOG(ERROR) << "[ppexp] Flash resource host creation FAILED: type="
                 << nested_msg.type() << " name=" << name;
    }
    DLOG(WARNING) << "Resource host creation failed.";
    return;
  }

  // Resource should have been assigned a nonzero PP_Resource.
  DCHECK(resource_host->pp_resource());

  resources_[params.pp_resource()] = std::move(resource_host);
}

void PpapiHost::OnHostMsgAttachToPendingHost(PP_Resource pp_resource,
                                             int pending_host_id) {
  PendingHostResourceMap::iterator found =
      pending_resource_hosts_.find(pending_host_id);
  if (found == pending_resource_hosts_.end()) {
    // Plugin sent a bad ID.
    DLOG(WARNING) << "Bad pending host ID = " << pending_host_id;
    return;
  }
  found->second->SetPPResourceForPendingHost(pp_resource);
  resources_[pp_resource] = std::move(found->second);
  pending_resource_hosts_.erase(found);
}

void PpapiHost::OnHostMsgResourceDestroyed(PP_Resource resource) {
  ResourceMap::iterator found = resources_.find(resource);
  if (found == resources_.end()) {
    DLOG(WARNING) << "Unknown resource ID " << resource;
    return;
  }
  // Invoking the HostResource destructor might result in looking up the
  // PP_Resource in resources_. std::map is not well specified as to whether the
  // element will be there or not. Therefore, we delay destruction of the
  // HostResource until after we've made sure the map no longer contains
  // |resource|.
  std::unique_ptr<ResourceHost> delete_at_end_of_scope(
      std::move(found->second));
  resources_.erase(found);
}

ResourceHost* PpapiHost::GetResourceHost(PP_Resource resource) const {
  ResourceMap::const_iterator found = resources_.find(resource);
  return found == resources_.end() ? NULL : found->second.get();
}

}  // namespace host
}  // namespace ppapi
