#pragma allow_unsafe_buffers
#ifdef UNSAFE_BUFFERS_BUILD
// Allow legacy buffer arithmetic in the vendored PPAPI sources.
#pragma allow_unsafe_buffers
#endif
// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ppapi/proxy/url_response_info_resource.h"

#include <stdint.h>

#include "ppapi/proxy/file_ref_resource.h"
#include "ppapi/shared_impl/var.h"
#include "ppapi/thunk/enter.h"
#include "ppapi/thunk/resource_creation_api.h"
#include "ppapi/trace/flash_trace.h"

namespace ppapi {
namespace proxy {

namespace {

bool IsRedirect(int32_t status) {
  return status >= 300 && status <= 399;
}

}  // namespace

URLResponseInfoResource::URLResponseInfoResource(
    Connection connection,
    PP_Instance instance,
    const URLResponseInfoData& data)
    : PluginResource(connection, instance), data_(data) {}

URLResponseInfoResource::~URLResponseInfoResource() {
}

thunk::PPB_URLResponseInfo_API*
URLResponseInfoResource::AsPPB_URLResponseInfo_API() {
  return this;
}

PP_Var URLResponseInfoResource::GetProperty(PP_URLResponseProperty property) {
  // FlashTrace: what Flash reads as the URLLoader response URL. This is the last
  // PPAPI source of a base URL for relative loads; an empty data_.url here is
  // what pushes Flash onto its literal "null" fallback.
  if (flash_trace::Enabled() && property == PP_URLRESPONSEPROPERTY_URL) {
    flash_trace::Record(flash_trace::kNote, pp_instance(), pp_resource(),
                        static_cast<int64_t>(data_.url.size()),
                        static_cast<int64_t>(data_.status_code), data_.url.c_str(),
                        flash_trace::kFlagMain |
                            (data_.url.empty() ? flash_trace::kFlagError : 0u));
  }
  switch (property) {
    case PP_URLRESPONSEPROPERTY_URL:
      return StringVar::StringToPPVar(data_.url);
    case PP_URLRESPONSEPROPERTY_REDIRECTURL:
      if (IsRedirect(data_.status_code))
        return StringVar::StringToPPVar(data_.redirect_url);
      break;
    case PP_URLRESPONSEPROPERTY_REDIRECTMETHOD:
      if (IsRedirect(data_.status_code))
        return StringVar::StringToPPVar(data_.status_text);
      break;
    case PP_URLRESPONSEPROPERTY_STATUSCODE:
      return PP_MakeInt32(data_.status_code);
    case PP_URLRESPONSEPROPERTY_STATUSLINE:
      return StringVar::StringToPPVar(data_.status_text);
    case PP_URLRESPONSEPROPERTY_HEADERS:
      return StringVar::StringToPPVar(data_.headers);
  }
  // The default is to return an undefined PP_Var.
  return PP_MakeUndefined();
}

PP_Resource URLResponseInfoResource::GetBodyAsFileRef() {
  return 0;
}

}  // namespace proxy
}  // namespace ppapi
