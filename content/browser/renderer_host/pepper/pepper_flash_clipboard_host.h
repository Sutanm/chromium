// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// PPAPI revival: browser-side host for the Flash-private PPB_Flash_Clipboard
// interface.
//
// Chromium 138/140 ship the plugin side of this interface
// (ppapi/proxy/flash_clipboard_resource.cc) but never had a browser-side host,
// so every message fell on the floor: WriteData() "succeeded" (it is an async
// Post, and the plugin assumes success to avoid a sync IPC) while the system
// clipboard was never touched, and ReadData()/IsFormatAvailable() always
// reported "nothing there".
//
// Note that Flash itself denies clipboard access when there is no user gesture
// (it throws Error #2176 before reaching this host) — that is expected
// behaviour, not a bug. This host only serves the post-gesture calls.

#ifndef CONTENT_BROWSER_RENDERER_HOST_PEPPER_PEPPER_FLASH_CLIPBOARD_HOST_H_
#define CONTENT_BROWSER_RENDERER_HOST_PEPPER_PEPPER_FLASH_CLIPBOARD_HOST_H_

#include <stdint.h>

#include <string>
#include <vector>

#include "base/memory/weak_ptr.h"
#include "ppapi/host/host_message_context.h"
#include "ppapi/host/resource_host.h"
#include "url/gurl.h"

namespace content {

class BrowserPpapiHost;

class PepperFlashClipboardHost : public ppapi::host::ResourceHost {
 public:
  PepperFlashClipboardHost(BrowserPpapiHost* host,
                           PP_Instance instance,
                           PP_Resource resource);
  ~PepperFlashClipboardHost() override;

  // ppapi::host::ResourceHost overrides.
  int32_t OnResourceMessageReceived(
      const IPC::Message& msg,
      ppapi::host::HostMessageContext* context) override;

 private:
  int32_t OnMsgRegisterCustomFormat(
      ppapi::host::HostMessageContext* context,
      const std::string& format_name);
  int32_t OnMsgIsFormatAvailable(ppapi::host::HostMessageContext* context,
                                 uint32_t clipboard_type,
                                 uint32_t format);
  int32_t OnMsgReadData(ppapi::host::HostMessageContext* context,
                        uint32_t clipboard_type,
                        uint32_t format);
  int32_t OnMsgWriteData(ppapi::host::HostMessageContext* context,
                         uint32_t clipboard_type,
                         const std::vector<uint32_t>& formats,
                         const std::vector<std::string>& data_items);
  int32_t OnMsgGetSequenceNumber(ppapi::host::HostMessageContext* context,
                                 uint32_t clipboard_type);

  // Asynchronous completion of a ReadData() request.
  void OnReadTextReply(ppapi::host::ReplyMessageContext reply_context,
                       std::u16string text);
  void OnReadHtmlReply(ppapi::host::ReplyMessageContext reply_context,
                       std::u16string markup,
                       GURL src_url,
                       uint32_t fragment_start,
                       uint32_t fragment_end);
  void OnReadRtfReply(ppapi::host::ReplyMessageContext reply_context,
                      std::string rtf);
  void OnReadCustomDataReply(ppapi::host::ReplyMessageContext reply_context,
                             std::string data);

  PepperFlashClipboardHost(const PepperFlashClipboardHost&) = delete;
  PepperFlashClipboardHost& operator=(const PepperFlashClipboardHost&) = delete;

  base::WeakPtrFactory<PepperFlashClipboardHost> weak_factory_{this};
};

}  // namespace content

#endif  // CONTENT_BROWSER_RENDERER_HOST_PEPPER_PEPPER_FLASH_CLIPBOARD_HOST_H_
