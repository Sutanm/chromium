#pragma allow_unsafe_buffers
// Copyright 2026 The BaoFlashBrowser Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// PPAPI revival stub for blink::WebPepperSocket.
//
// Chromium 140 deleted the Blink pepper socket implementation together with
// PPAPI. The PPAPI PPB_WebSocket host still links against this interface, so we
// provide a stub that fails every operation instead of crashing. Flash content
// that uses the browser WebSocket API (rare; most games use XMLSocket /
// PPB_TCPSocket) will observe a connection error.

#include "third_party/blink/public/web/web_pepper_socket.h"

#include <memory>

#include "base/memory/raw_ptr.h"
#include <utility>

#include "third_party/blink/public/platform/web_string.h"
#include "third_party/blink/public/platform/web_url.h"
#include "third_party/blink/public/web/web_array_buffer.h"
#include "third_party/blink/public/web/web_document.h"
#include "third_party/blink/public/web/web_pepper_socket_client.h"

namespace blink {

namespace {

class WebPepperSocketStub final : public WebPepperSocket {
 public:
  WebPepperSocketStub(const WebDocument& document,
                      WebPepperSocketClient* client)
      : client_(client) {}

  ~WebPepperSocketStub() override = default;

  void Connect(const WebURL& url, const WebString& protocol) override {
    if (client_) {
      client_->DidClose(WebPepperSocketClient::kClosingHandshakeIncomplete,
                        kCloseEventCodeAbnormalClosure, WebString());
    }
  }

  WebString Subprotocol() override { return WebString(); }

  bool SendText(const WebString& text) override { return false; }

  bool SendArrayBuffer(const WebArrayBuffer& buffer) override { return false; }

  void Close(int code, const WebString& reason) override {
    if (client_) {
      client_->DidClose(WebPepperSocketClient::kClosingHandshakeComplete,
                        static_cast<uint16_t>(code), reason);
    }
  }

  void Fail(const WebString& reason) override {
    if (client_) {
      client_->DidClose(WebPepperSocketClient::kClosingHandshakeIncomplete,
                        kCloseEventCodeAbnormalClosure, reason);
    }
  }

  void Disconnect() override {
    if (client_) {
      client_->DidClose(WebPepperSocketClient::kClosingHandshakeComplete,
                        kCloseEventCodeNormalClosure, WebString());
    }
  }

 private:
  raw_ptr<WebPepperSocketClient> client_ = nullptr;
};

}  // namespace

// static
std::unique_ptr<WebPepperSocket> WebPepperSocket::Create(
    const WebDocument& document,
    WebPepperSocketClient* client) {
  return std::make_unique<WebPepperSocketStub>(document, client);
}

}  // namespace blink
