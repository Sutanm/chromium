#pragma allow_unsafe_buffers
// Copyright 2026 The BaoFlashBrowser Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// PPAPI revival: default implementation of the renderer-client pepper hook that
// Chromium removed together with PPAPI.

#include "content/public/renderer/content_renderer_client.h"

namespace content {

bool ContentRendererClient::IsPluginAllowedToUseCameraDeviceAPI(
    const GURL& url) {
  // PPAPI revival: allow Flash camera/microphone access (video chat content).
  (void)url;
  return true;
}

bool ContentRendererClient::IsExternalPepperPlugin(
    const std::string& module_name) {
  // PPAPI revival: Flash is the out-of-process (external) pepper plugin; this
  // flag enables external document loading for it.
  (void)module_name;
  return true;
}

}  // namespace content
