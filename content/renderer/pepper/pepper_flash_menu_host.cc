// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// PP_Flash_Menu exposes a C array of items; indexing it is the only way to
// walk the tree.
#ifdef UNSAFE_BUFFERS_BUILD
#pragma allow_unsafe_buffers
#endif

#include "content/renderer/pepper/pepper_flash_menu_host.h"

#include <stddef.h>

#include <utility>

#include "base/strings/utf_string_conversions.h"
#include "content/public/renderer/render_frame.h"
#include "content/public/renderer/renderer_ppapi_host.h"
#include "ipc/ipc_message.h"
#include "ppapi/c/private/ppb_flash_menu.h"
#include "ppapi/host/dispatch_host_message.h"
#include "ppapi/host/host_message_context.h"
#include "ppapi/host/ppapi_host.h"
#include "ppapi/proxy/ppapi_messages.h"
#include "ppapi/proxy/serialized_flash_menu.h"
#include "third_party/blink/public/web/web_local_frame.h"
#include "ui/gfx/geometry/point.h"

namespace content {

namespace {

// Maximum depth of submenus allowed (e.g. 1 indicates that submenus are
// allowed, but not sub-submenus).
constexpr size_t kMaxMenuDepth = 2;

// Maximum number of entries in any single menu (including separators).
constexpr size_t kMaxMenuEntries = 50;

// Maximum total number of entries in the |menu_id_map| (see below).
// (Limit to 500 real entries; reserve the 0 action as an invalid entry.)
constexpr size_t kMaxMenuIdMapEntries = 501;

blink::mojom::CustomContextMenuItemType ToMenuItemType(
    PP_Flash_MenuItem_Type type,
    bool* ok) {
  *ok = true;
  switch (type) {
    case PP_FLASH_MENUITEM_TYPE_NORMAL:
      return blink::mojom::CustomContextMenuItemType::kOption;
    case PP_FLASH_MENUITEM_TYPE_CHECKBOX:
      return blink::mojom::CustomContextMenuItemType::kCheckableOption;
    case PP_FLASH_MENUITEM_TYPE_SEPARATOR:
      return blink::mojom::CustomContextMenuItemType::kSeparator;
    case PP_FLASH_MENUITEM_TYPE_SUBMENU:
      return blink::mojom::CustomContextMenuItemType::kSubMenu;
    default:
      *ok = false;
      return blink::mojom::CustomContextMenuItemType::kOption;
  }
}

// Converts |in_menu| into |out_menu|.
//  - |menu_id_map| is such that |menu_id_map[item.action] == input_item.id|.
bool ConvertMenuData(
    const PP_Flash_Menu* in_menu,
    size_t depth,
    std::vector<blink::mojom::CustomContextMenuItemPtr>* out_menu,
    std::vector<int32_t>* menu_id_map) {
  if (depth > kMaxMenuDepth || !in_menu)
    return false;

  out_menu->clear();

  if (!in_menu->count)
    return true;  // Nothing else to do.

  if (!in_menu->items || in_menu->count > kMaxMenuEntries)
    return false;

  for (uint32_t i = 0; i < in_menu->count; i++) {
    bool type_ok = true;
    auto item = blink::mojom::CustomContextMenuItem::New();
    item->type = ToMenuItemType(in_menu->items[i].type, &type_ok);
    if (!type_ok)
      return false;

    if (in_menu->items[i].name)
      item->label = base::UTF8ToUTF16(in_menu->items[i].name);
    if (menu_id_map->size() >= kMaxMenuIdMapEntries)
      return false;
    item->action = static_cast<int>(menu_id_map->size());
    menu_id_map->push_back(in_menu->items[i].id);
    item->enabled = PP_ToBool(in_menu->items[i].enabled);
    item->checked = PP_ToBool(in_menu->items[i].checked);

    if (in_menu->items[i].type == PP_FLASH_MENUITEM_TYPE_SUBMENU) {
      if (!ConvertMenuData(in_menu->items[i].submenu, depth + 1, &item->submenu,
                           menu_id_map)) {
        return false;
      }
    }

    out_menu->push_back(std::move(item));
  }

  return true;
}

}  // namespace

PepperFlashMenuHost::PepperFlashMenuHost(
    RendererPpapiHost* host,
    PP_Instance instance,
    PP_Resource resource,
    const ppapi::proxy::SerializedFlashMenu& serial_menu)
    : ppapi::host::ResourceHost(host->GetPpapiHost(), instance, resource),
      renderer_ppapi_host_(host) {
  menu_id_map_.push_back(0);  // Reserve |menu_id_map_[0]|.
  if (!ConvertMenuData(serial_menu.pp_menu(), 0, &menu_data_, &menu_id_map_)) {
    menu_data_.clear();
    menu_id_map_.clear();
  }
}

PepperFlashMenuHost::~PepperFlashMenuHost() = default;

int32_t PepperFlashMenuHost::OnResourceMessageReceived(
    const IPC::Message& msg,
    ppapi::host::HostMessageContext* context) {
  PPAPI_BEGIN_MESSAGE_MAP(PepperFlashMenuHost, msg)
    PPAPI_DISPATCH_HOST_RESOURCE_CALL(PpapiHostMsg_FlashMenu_Show,
                                      OnHostMsgShow)
  PPAPI_END_MESSAGE_MAP()
  return PP_ERROR_FAILED;
}

int32_t PepperFlashMenuHost::OnHostMsgShow(
    ppapi::host::HostMessageContext* context,
    const PP_Point& location) {
  // Note that all early returns must do a SendMenuReply. The sync result for
  // this message isn't used, so to forward the error to the plugin, we need to
  // additionally call SendMenuReply explicitly.
  if (menu_data_.empty()) {
    SendMenuReply(PP_ERROR_FAILED, -1);
    return PP_ERROR_FAILED;
  }
  if (showing_context_menu_) {
    SendMenuReply(PP_ERROR_INPROGRESS, -1);
    return PP_ERROR_INPROGRESS;
  }

  RenderFrame* render_frame =
      renderer_ppapi_host_->GetRenderFrameForInstance(pp_instance());
  if (!render_frame)
    return PP_ERROR_FAILED;
  blink::WebLocalFrame* web_frame = render_frame->GetWebFrame();
  if (!web_frame)
    return PP_ERROR_FAILED;

  // Transform the position to be in the render frame's coordinates.
  const gfx::Point render_frame_pt =
      renderer_ppapi_host_->PluginPointToRenderFrame(
          pp_instance(), gfx::Point(location.x, location.y));

  blink::UntrustworthyContextMenuParams params;
  params.x = render_frame_pt.x();
  params.y = render_frame_pt.y();
  params.custom_items = std::move(menu_data_);

  showing_context_menu_ = true;
  web_frame->ShowContextMenuFromExternal(
      params, menu_client_.BindNewEndpointAndPassRemote());

  // Note: the show message is sync so this OK is for the sync reply which we
  // don't actually use (see the comment in the resource file for this). The
  // async message containing the context menu action will be sent in the
  // future.
  return PP_OK;
}

void PepperFlashMenuHost::CustomContextMenuAction(uint32_t action) {
  // Just save the action.
  DCHECK(!has_saved_context_menu_action_);
  has_saved_context_menu_action_ = true;
  saved_context_menu_action_ = action;
}

void PepperFlashMenuHost::ContextMenuClosed(
    const GURL& link_followed,
    const std::optional<blink::Impression>& impression) {
  if (has_saved_context_menu_action_ &&
      saved_context_menu_action_ < menu_id_map_.size()) {
    SendMenuReply(PP_OK, menu_id_map_[saved_context_menu_action_]);
    has_saved_context_menu_action_ = false;
    saved_context_menu_action_ = 0;
  } else {
    SendMenuReply(PP_ERROR_USERCANCEL, -1);
  }

  showing_context_menu_ = false;
}

void PepperFlashMenuHost::SendMenuReply(int32_t result, int action) {
  ppapi::host::ReplyMessageContext reply_context(
      ppapi::proxy::ResourceMessageReplyParams(pp_resource(), 0), nullptr,
      MSG_ROUTING_NONE);
  reply_context.params.set_result(result);
  host()->SendReply(reply_context, PpapiPluginMsg_FlashMenu_ShowReply(action));
}

}  // namespace content
