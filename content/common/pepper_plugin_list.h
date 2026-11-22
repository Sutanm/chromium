// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_COMMON_PEPPER_PLUGIN_LIST_H_
#define CONTENT_COMMON_PEPPER_PLUGIN_LIST_H_

#include <vector>

#include "ppapi/buildflags/buildflags.h"

#if !BUILDFLAG(ENABLE_PLUGINS)
#error "Plugins should be enabled"
#endif

namespace content {

struct ContentPluginInfo;
struct WebPluginInfo;

// Constructs a Pepper-specific `ContentPluginInfo` from a `WebPluginInfo`.
// Returns false if the operation is not possible, in particular the
// `WebPluginInfo::type` must be one of the Pepper types.
bool MakePepperPluginInfo(const WebPluginInfo& webplugin_info,
                          ContentPluginInfo* pepper_info);

// Computes the list of known pepper plugins.
//
// PPAPI revival (BaoFlashBrowser Next): `check_file_exists` must be false in
// sandboxed processes. The command-line plugin path is a file outside the
// renderer sandbox's read allowlist on Windows, so `base::PathExists()` fails
// there and the plugin would be dropped silently. The browser process already
// validated the path when it parsed the switch, so the renderer only needs the
// metadata.
void ComputePepperPluginList(std::vector<ContentPluginInfo>* plugins,
                             bool check_file_exists = true);

}  // namespace content

#endif  // CONTENT_COMMON_PEPPER_PLUGIN_LIST_H_
