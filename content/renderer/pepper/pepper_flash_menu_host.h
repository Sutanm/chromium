// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// PPAPI revival: renderer-side host for the Flash-private PPB_Flash_Menu
// interface.
//
// Ported from Chromium 76~89 (chrome/renderer/pepper/pepper_flash_menu_host),
// deleted in Chromium 90 by "[Flash] Remove some Flash APIs" (81da804125bfb).
// Electron never builds //chrome, so the host was never available there even on
// the Flash-capable branches.
//
// IMPORTANT: PPB_Flash_Menu is *not* a dead interface. AS3 `ContextMenu` is
// drawn by Flash itself and does not use it, which is why a naive probe shows
// nothing. But real content does call it: a SiteLock-encrypted SWF from
// drmsoft.cn triggered FlashMenu_Create + FlashMenu_Show in this build (see
// docs/reports §三十七), so the host is required for those pages to work.
//
// The 87 version differs from the 76 one by exactly two lines (a type rename).
// This port adapts three APIs that moved from //content to //blink:
//   content::ContextMenuParams   -> blink::UntrustworthyContextMenuParams
//   content::MenuItem            -> blink::mojom::CustomContextMenuItem
//   RenderFrame::ShowContextMenu -> blink::WebLocalFrame::ShowContextMenuFromExternal

#ifndef CONTENT_RENDERER_PEPPER_PEPPER_FLASH_MENU_HOST_H_
#define CONTENT_RENDERER_PEPPER_PEPPER_FLASH_MENU_HOST_H_

#include <stdint.h>

#include <vector>

#include "base/memory/raw_ptr.h"
#include "mojo/public/cpp/bindings/associated_receiver.h"
#include "ppapi/c/pp_point.h"
#include "ppapi/host/host_message_context.h"
#include "ppapi/host/resource_host.h"
#include "third_party/blink/public/common/context_menu_data/untrustworthy_context_menu_params.h"
#include "third_party/blink/public/mojom/context_menu/context_menu.mojom.h"
#include "url/gurl.h"

namespace ppapi {
namespace proxy {
class SerializedFlashMenu;
}
}  // namespace ppapi

namespace content {

class RendererPpapiHost;

class PepperFlashMenuHost : public ppapi::host::ResourceHost,
                            public blink::mojom::ContextMenuClient {
 public:
  PepperFlashMenuHost(RendererPpapiHost* host,
                      PP_Instance instance,
                      PP_Resource resource,
                      const ppapi::proxy::SerializedFlashMenu& serial_menu);
  ~PepperFlashMenuHost() override;

  // ppapi::host::ResourceHost overrides.
  int32_t OnResourceMessageReceived(
      const IPC::Message& msg,
      ppapi::host::HostMessageContext* context) override;

  // blink::mojom::ContextMenuClient overrides.
  void CustomContextMenuAction(uint32_t action) override;
  void ContextMenuClosed(
      const GURL& link_followed,
      const std::optional<blink::Impression>& impression) override;

 private:
  int32_t OnHostMsgShow(ppapi::host::HostMessageContext* context,
                        const PP_Point& location);

  void SendMenuReply(int32_t result, int action);

  raw_ptr<RendererPpapiHost> renderer_ppapi_host_;

  bool showing_context_menu_ = false;

  std::vector<blink::mojom::CustomContextMenuItemPtr> menu_data_;

  // |CustomContextMenuItem| has an |int| "action" field instead of an
  // |int32_t| ID. This maps actions back to PP_Flash_MenuItem ids.
  std::vector<int32_t> menu_id_map_;

  // Used to send a single context menu "completion" upon menu close.
  bool has_saved_context_menu_action_ = false;
  uint32_t saved_context_menu_action_ = 0;

  // Receives CustomContextMenuAction()/ContextMenuClosed() from the browser.
  mojo::AssociatedReceiver<blink::mojom::ContextMenuClient> menu_client_{this};

  PepperFlashMenuHost(const PepperFlashMenuHost&) = delete;
  PepperFlashMenuHost& operator=(const PepperFlashMenuHost&) = delete;
};

}  // namespace content

#endif  // CONTENT_RENDERER_PEPPER_PEPPER_FLASH_MENU_HOST_H_
