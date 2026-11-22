#pragma allow_unsafe_buffers
// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/renderer/pepper/url_response_info_util.h"

#include <stdint.h>

#include "ppapi/shared_impl/url_response_info_data.h"
#include "ppapi/trace/flash_trace.h"
#include "third_party/blink/public/platform/web_http_header_visitor.h"
#include "third_party/blink/public/platform/web_string.h"
#include "third_party/blink/public/platform/web_url.h"
#include "third_party/blink/public/platform/web_url_response.h"

using blink::WebHTTPHeaderVisitor;
using blink::WebString;
using blink::WebURLResponse;

namespace content {

namespace {

class HeadersToString : public WebHTTPHeaderVisitor {
 public:
  HeadersToString() {}
  ~HeadersToString() override {}

  const std::string& buffer() const { return buffer_; }

  void VisitHeader(const WebString& name, const WebString& value) override {
    if (!buffer_.empty())
      buffer_.append("\n");
    buffer_.append(name.Utf8());
    buffer_.append(": ");
    buffer_.append(value.Utf8());
  }

 private:
  std::string buffer_;
};

bool IsRedirect(int32_t status) { return status >= 300 && status <= 399; }

}  // namespace

ppapi::URLResponseInfoData DataFromWebURLResponse(
    const WebURLResponse& response) {
  ppapi::URLResponseInfoData data;
  // PPAPI revival: Chromium 87 used the response's own URL here. 142's
  // CurrentRequestUrl() is only populated when the request URL was recorded,
  // which never happens for a main-resource (document) load - so the main SWF's
  // response URL came through empty, Flash had no base URL for relative loads,
  // and it fell back to the string "null" ("nullassets/config/revisions.dat",
  // 404 loop on 4399 hxjy). Keep CurrentRequestUrl() as the fallback and prefer
  // the response URL, which is what 87 effectively read.
  const std::string response_url = response.ResponseUrl().GetString().Utf8();
  const std::string request_url =
      response.CurrentRequestUrl().GetString().Utf8();
  data.url = response_url.empty() ? request_url : response_url;
  // FlashTrace: both candidates, so an empty base can never be silent again.
  flash_trace::Record(flash_trace::kNote, 0, 0,
                      static_cast<int64_t>(response_url.size()),
                      static_cast<int64_t>(request_url.size()),
                      response_url.c_str(),
                      flash_trace::kFlagMain |
                          (data.url.empty() ? flash_trace::kFlagError : 0u));
  data.status_code = response.HttpStatusCode();
  data.status_text = response.HttpStatusText().Utf8();
  if (IsRedirect(data.status_code)) {
    data.redirect_url =
        response.HttpHeaderField(WebString::FromUTF8("Location")).Utf8();
  }

  HeadersToString headers_to_string;
  response.VisitHttpHeaderFields(&headers_to_string);
  data.headers = headers_to_string.buffer();
  return data;
}

}  // namespace content
