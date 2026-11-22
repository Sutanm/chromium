// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/renderer_host/pepper/pepper_flash_clipboard_host.h"

#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "base/strings/utf_string_conversions.h"
#include "content/public/browser/browser_ppapi_host.h"
#include "ppapi/c/pp_errors.h"
#include "ppapi/c/private/ppb_flash_clipboard.h"
#include "ppapi/host/dispatch_host_message.h"
#include "ppapi/host/host_message_context.h"
#include "ppapi/host/ppapi_host.h"
#include "ppapi/proxy/ppapi_messages.h"
#include "ui/base/clipboard/clipboard.h"
#include "ui/base/clipboard/clipboard_buffer.h"
#include "ui/base/clipboard/clipboard_format_type.h"
#include "ui/base/clipboard/clipboard_sequence_number_token.h"
#include "ui/base/clipboard/scoped_clipboard_writer.h"

namespace content {

namespace {

// Maps the Flash-private clipboard selector onto the platform one. Flash only
// ever uses the standard clipboard; the selection buffer is X11-only and is
// treated as the standard clipboard everywhere else.
ui::ClipboardBuffer ToClipboardBuffer(uint32_t clipboard_type) {
  return clipboard_type == PP_FLASH_CLIPBOARD_TYPE_SELECTION
             ? ui::ClipboardBuffer::kSelection
             : ui::ClipboardBuffer::kCopyPaste;
}

bool IsValidClipboardType(uint32_t clipboard_type) {
  return clipboard_type == PP_FLASH_CLIPBOARD_TYPE_STANDARD ||
         clipboard_type == PP_FLASH_CLIPBOARD_TYPE_SELECTION;
}

// Flash hands custom format names to the browser and expects a stable uint32_t
// back. The plugin side (FlashClipboardResource) caches whatever we return, so
// any stable mapping works as long as it never collides with the predefined
// formats (1..3) and is non-zero.
uint32_t CustomFormatIdForName(const std::string& format_name) {
  uint32_t hash = static_cast<uint32_t>(std::hash<std::string>{}(format_name));
  return 0x10000u + (hash & 0xffffu);
}

}  // namespace

PepperFlashClipboardHost::PepperFlashClipboardHost(BrowserPpapiHost* host,
                                                   PP_Instance instance,
                                                   PP_Resource resource)
    : ppapi::host::ResourceHost(host->GetPpapiHost(), instance, resource) {}

PepperFlashClipboardHost::~PepperFlashClipboardHost() = default;

int32_t PepperFlashClipboardHost::OnResourceMessageReceived(
    const IPC::Message& msg,
    ppapi::host::HostMessageContext* context) {
  PPAPI_BEGIN_MESSAGE_MAP(PepperFlashClipboardHost, msg)
    PPAPI_DISPATCH_HOST_RESOURCE_CALL(
        PpapiHostMsg_FlashClipboard_RegisterCustomFormat,
        OnMsgRegisterCustomFormat)
    PPAPI_DISPATCH_HOST_RESOURCE_CALL(
        PpapiHostMsg_FlashClipboard_IsFormatAvailable, OnMsgIsFormatAvailable)
    PPAPI_DISPATCH_HOST_RESOURCE_CALL(PpapiHostMsg_FlashClipboard_ReadData,
                                     OnMsgReadData)
    PPAPI_DISPATCH_HOST_RESOURCE_CALL(PpapiHostMsg_FlashClipboard_WriteData,
                                     OnMsgWriteData)
    PPAPI_DISPATCH_HOST_RESOURCE_CALL(
        PpapiHostMsg_FlashClipboard_GetSequenceNumber, OnMsgGetSequenceNumber)
  PPAPI_END_MESSAGE_MAP()
  return PP_ERROR_FAILED;
}

int32_t PepperFlashClipboardHost::OnMsgRegisterCustomFormat(
    ppapi::host::HostMessageContext* context,
    const std::string& format_name) {
  context->reply_msg = PpapiPluginMsg_FlashClipboard_RegisterCustomFormatReply(
      CustomFormatIdForName(format_name));
  return PP_OK;
}

int32_t PepperFlashClipboardHost::OnMsgIsFormatAvailable(
    ppapi::host::HostMessageContext* context,
    uint32_t clipboard_type,
    uint32_t format) {
  if (!IsValidClipboardType(clipboard_type))
    return PP_ERROR_BADARGUMENT;

  ui::Clipboard* clipboard = ui::Clipboard::GetForCurrentThread();
  if (!clipboard)
    return PP_ERROR_FAILED;

  ui::ClipboardBuffer buffer = ToClipboardBuffer(clipboard_type);
  bool available = false;
  switch (format) {
    case PP_FLASH_CLIPBOARD_FORMAT_PLAINTEXT:
      available = clipboard->IsFormatAvailable(
          ui::ClipboardFormatType::PlainTextType(), buffer,
          /*data_dst=*/nullptr);
      break;
    case PP_FLASH_CLIPBOARD_FORMAT_HTML:
      available = clipboard->IsFormatAvailable(
          ui::ClipboardFormatType::HtmlType(), buffer, /*data_dst=*/nullptr);
      break;
    case PP_FLASH_CLIPBOARD_FORMAT_RTF:
      available = clipboard->IsFormatAvailable(
          ui::ClipboardFormatType::RtfType(), buffer, /*data_dst=*/nullptr);
      break;
    default:
      // Custom formats are not enumerable through ui::Clipboard on all
      // platforms; report "unavailable" rather than lying.
      available = false;
      break;
  }

  // This message has no reply payload; the plugin reads the sync-call result.
  return available ? PP_OK : PP_ERROR_FAILED;
}

int32_t PepperFlashClipboardHost::OnMsgReadData(
    ppapi::host::HostMessageContext* context,
    uint32_t clipboard_type,
    uint32_t format) {
  if (!IsValidClipboardType(clipboard_type))
    return PP_ERROR_BADARGUMENT;

  ui::Clipboard* clipboard = ui::Clipboard::GetForCurrentThread();
  if (!clipboard)
    return PP_ERROR_FAILED;

  // ui::Clipboard reads are asynchronous in Chromium 140, but the PPAPI
  // message is a sync call. Defer the reply and return COMPLETIONPENDING.
  ppapi::host::ReplyMessageContext reply_context =
      context->MakeReplyMessageContext();
  ui::ClipboardBuffer buffer = ToClipboardBuffer(clipboard_type);

  switch (format) {
    case PP_FLASH_CLIPBOARD_FORMAT_PLAINTEXT:
      clipboard->ReadText(
          buffer, /*data_dst=*/nullptr,
          base::BindOnce(&PepperFlashClipboardHost::OnReadTextReply,
                         weak_factory_.GetWeakPtr(),
                         std::move(reply_context)));
      return PP_OK_COMPLETIONPENDING;
    case PP_FLASH_CLIPBOARD_FORMAT_HTML:
      clipboard->ReadHTML(
          buffer, /*data_dst=*/nullptr,
          base::BindOnce(&PepperFlashClipboardHost::OnReadHtmlReply,
                         weak_factory_.GetWeakPtr(),
                         std::move(reply_context)));
      return PP_OK_COMPLETIONPENDING;
    case PP_FLASH_CLIPBOARD_FORMAT_RTF:
      clipboard->ReadRTF(
          buffer, /*data_dst=*/nullptr,
          base::BindOnce(&PepperFlashClipboardHost::OnReadRtfReply,
                         weak_factory_.GetWeakPtr(),
                         std::move(reply_context)));
      return PP_OK_COMPLETIONPENDING;
    default:
      // Custom formats: report an empty result.
      OnReadCustomDataReply(std::move(reply_context), std::string());
      return PP_OK_COMPLETIONPENDING;
  }
}

int32_t PepperFlashClipboardHost::OnMsgWriteData(
    ppapi::host::HostMessageContext* context,
    uint32_t clipboard_type,
    const std::vector<uint32_t>& formats,
    const std::vector<std::string>& data_items) {
  if (!IsValidClipboardType(clipboard_type))
    return PP_ERROR_BADARGUMENT;
  if (formats.size() != data_items.size())
    return PP_ERROR_BADARGUMENT;

  ui::Clipboard* clipboard = ui::Clipboard::GetForCurrentThread();
  if (!clipboard)
    return PP_ERROR_FAILED;

  // ui::Clipboard's write API is protected; the supported way in is
  // ScopedClipboardWriter. It writes one representation at a time, so prefer
  // the richest format Flash offered.
  std::string text;
  std::string html;
  std::string rtf;
  for (size_t i = 0; i < formats.size(); ++i) {
    switch (formats[i]) {
      case PP_FLASH_CLIPBOARD_FORMAT_PLAINTEXT:
        text = data_items[i];
        break;
      case PP_FLASH_CLIPBOARD_FORMAT_HTML:
        html = data_items[i];
        break;
      case PP_FLASH_CLIPBOARD_FORMAT_RTF:
        rtf = data_items[i];
        break;
      default:
        break;
    }
  }

  if (html.empty() && rtf.empty() && text.empty()) {
    // Nothing usable in this write; treat as success (Flash does the same).
    return PP_OK;
  }

  ui::ScopedClipboardWriter writer(ToClipboardBuffer(clipboard_type));
  if (!html.empty()) {
    writer.WriteHTML(base::UTF8ToUTF16(html), /*source_url=*/std::string());
  } else if (!rtf.empty()) {
    writer.WriteRTF(rtf);
  } else {
    writer.WriteText(base::UTF8ToUTF16(text));
  }

  return PP_OK;
}

int32_t PepperFlashClipboardHost::OnMsgGetSequenceNumber(
    ppapi::host::HostMessageContext* context,
    uint32_t clipboard_type) {
  if (!IsValidClipboardType(clipboard_type))
    return PP_ERROR_BADARGUMENT;

  ui::Clipboard* clipboard = ui::Clipboard::GetForCurrentThread();
  if (!clipboard)
    return PP_ERROR_FAILED;

  // Flash only compares the sequence number for inequality, so folding the
  // opaque token down to 64 bits is sufficient.
  const ui::ClipboardSequenceNumberToken& token =
      clipboard->GetSequenceNumber(ToClipboardBuffer(clipboard_type));
  uint64_t sequence_number =
      static_cast<uint64_t>(std::hash<std::string>{}(token.ToString()));

  context->reply_msg =
      PpapiPluginMsg_FlashClipboard_GetSequenceNumberReply(sequence_number);
  return PP_OK;
}

void PepperFlashClipboardHost::OnReadTextReply(
    ppapi::host::ReplyMessageContext reply_context,
    std::u16string text) {
  reply_context.params.set_result(PP_OK);
  SendReply(reply_context, PpapiPluginMsg_FlashClipboard_ReadDataReply(
                               base::UTF16ToUTF8(text)));
}

void PepperFlashClipboardHost::OnReadHtmlReply(
    ppapi::host::ReplyMessageContext reply_context,
    std::u16string markup,
    GURL src_url,
    uint32_t fragment_start,
    uint32_t fragment_end) {
  reply_context.params.set_result(PP_OK);
  SendReply(reply_context, PpapiPluginMsg_FlashClipboard_ReadDataReply(
                               base::UTF16ToUTF8(markup)));
}

void PepperFlashClipboardHost::OnReadRtfReply(
    ppapi::host::ReplyMessageContext reply_context,
    std::string rtf) {
  reply_context.params.set_result(PP_OK);
  SendReply(reply_context, PpapiPluginMsg_FlashClipboard_ReadDataReply(rtf));
}

void PepperFlashClipboardHost::OnReadCustomDataReply(
    ppapi::host::ReplyMessageContext reply_context,
    std::string data) {
  reply_context.params.set_result(PP_OK);
  SendReply(reply_context, PpapiPluginMsg_FlashClipboard_ReadDataReply(data));
}

}  // namespace content
