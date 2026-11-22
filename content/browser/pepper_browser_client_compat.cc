// Copyright 2026 The BaoFlashBrowser Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// PPAPI revival: default implementations of the pepper-related
// ContentBrowserClient hooks that Chromium removed together with PPAPI.

#include "content/public/browser/content_browser_client.h"

namespace content {

BrowserPpapiHost* ContentBrowserClient::GetExternalBrowserPpapiHost(
    int render_process_id) {
  return nullptr;
}

bool ContentBrowserClient::AllowPepperSocketAPI(
    BrowserContext* browser_context,
    const GURL& url,
    bool private_api,
    const SocketPermissionRequest* params) {
  // PPAPI revival: Flash content relies on PPB_TCPSocket / PPB_UDPSocket /
  // PPB_HostResolver (XMLSocket multi-player games), so allow the socket API.
  (void)browser_context;
  (void)url;
  (void)private_api;
  (void)params;
  return true;
}

bool ContentBrowserClient::IsPluginAllowedToCallRequestOSFileHandle(
    BrowserContext* browser_context,
    const GURL& url) {
  // PPAPI revival: Flash needs OS file handles for local storage/save games.
  (void)browser_context;
  (void)url;
  return true;
}

bool ContentBrowserClient::IsPluginAllowedToUseDevChannelAPIs(
    BrowserContext* browser_context,
    const GURL& url) {
  return true;
}

}  // namespace content
