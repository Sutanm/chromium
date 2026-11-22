// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/renderer/pepper/pepper_flash_font_file_host.h"

#include "base/sys_byteorder.h"
#include "content/public/renderer/renderer_ppapi_host.h"
#include "ppapi/c/pp_errors.h"
#include "ppapi/host/dispatch_host_message.h"
#include "ppapi/host/host_message_context.h"
#include "ppapi/host/ppapi_host.h"
#include "ppapi/proxy/ppapi_messages.h"
#include "ppapi/proxy/serialized_structs.h"
#include "skia/ext/font_utils.h"
#include "third_party/skia/include/core/SkFontMgr.h"
#include "third_party/skia/include/core/SkFontStyle.h"
#include "third_party/skia/include/core/SkTypeface.h"

PepperFlashFontFileHost::PepperFlashFontFileHost(
    content::RendererPpapiHost* host,
    PP_Instance instance,
    PP_Resource resource,
    const ppapi::proxy::SerializedFontDescription& description,
    PP_PrivateFontCharset charset)
    : ResourceHost(host->GetPpapiHost(), instance, resource) {
  int weight = description.weight;
  if (weight == FW_DONTCARE)
    weight = SkFontStyle::kNormal_Weight;
  SkFontStyle style(weight, SkFontStyle::kNormal_Width,
                    description.italic ? SkFontStyle::kItalic_Slant
                                       : SkFontStyle::kUpright_Slant);
  sk_sp<SkFontMgr> font_mgr = skia::DefaultFontMgr();
  if (font_mgr) {
    typeface_ = font_mgr->matchFamilyStyle(description.face.c_str(), style);
  }
  if (!typeface_) {
    LOG(ERROR) << "[flash-font] could not match font family '"
               << description.face << "'";
  }
}

PepperFlashFontFileHost::~PepperFlashFontFileHost() = default;

int32_t PepperFlashFontFileHost::OnResourceMessageReceived(
    const IPC::Message& msg,
    ppapi::host::HostMessageContext* context) {
  PPAPI_BEGIN_MESSAGE_MAP(PepperFlashFontFileHost, msg)
    PPAPI_DISPATCH_HOST_RESOURCE_CALL(PpapiHostMsg_FlashFontFile_GetFontTable,
                                      OnGetFontTable)
  PPAPI_END_MESSAGE_MAP()
  return PP_ERROR_FAILED;
}

bool PepperFlashFontFileHost::GetFontData(uint32_t table,
                                          void* buffer,
                                          size_t* length) {
  if (!typeface_)
    return false;

  // Flash passes the OpenType table tag in big-endian byte order.
  SkFontTableTag tag = base::ByteSwap(table);
  if (buffer == nullptr) {
    *length = typeface_->getTableSize(tag);
    return *length > 0;
  }
  size_t new_length = typeface_->getTableData(tag, 0, *length, buffer);
  return new_length == *length;
}

int32_t PepperFlashFontFileHost::OnGetFontTable(
    ppapi::host::HostMessageContext* context,
    uint32_t table) {
  std::string contents;
  int32_t result = PP_ERROR_FAILED;
  size_t length = 0;
  if (GetFontData(table, nullptr, &length)) {
    contents.resize(length);
    // getTableData() writes into the string's storage; const_cast is safe
    // because the string is resized to the exact length first.
    uint8_t* contents_ptr =
        reinterpret_cast<uint8_t*>(const_cast<char*>(contents.data()));
    if (GetFontData(table, contents_ptr, &length)) {
      result = PP_OK;
    } else {
      contents.clear();
    }
  }
  context->reply_msg = PpapiPluginMsg_FlashFontFile_GetFontTableReply(contents);
  return result;
}
