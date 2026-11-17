// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_PUBLIC_COMMON_CONTENT_PLUGIN_INFO_H_
#define CONTENT_PUBLIC_COMMON_CONTENT_PLUGIN_INFO_H_

#include <stdint.h>

#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "content/common/content_export.h"
#include "content/public/common/buildflags.h"
#include "content/public/common/webplugininfo.h"
#include "ppapi/buildflags/buildflags.h"

#if !BUILDFLAG(ENABLE_PLUGINS)
#error "Plugins should be enabled"
#endif

#if BUILDFLAG(ENABLE_PPAPI)
#include "ppapi/c/pp_module.h"
#include "ppapi/c/ppb.h"
#endif  // BUILDFLAG(ENABLE_PPAPI)

namespace content {

struct CONTENT_EXPORT ContentPluginInfo {
  ContentPluginInfo();
  ContentPluginInfo(const ContentPluginInfo& other);
  ContentPluginInfo(ContentPluginInfo&& other) noexcept;
  ~ContentPluginInfo();

  WebPluginInfo ToWebPluginInfo() const;

  // Indicates internal plugins for which there's not actually a library.
  // These plugins are implemented in the Chrome binary using a separate set
  // of entry points (see internal_entry_points below).
  // Defaults to false.
#if BUILDFLAG(ENABLE_PPAPI)
  // PPAPI revival: plugin entry points restored from the pre-M140 layout.
  typedef const void* (*GetInterfaceFunc)(const char*);
  typedef int (*PPP_InitializeModuleFunc)(PP_Module, PPB_GetInterface);
  typedef void (*PPP_ShutdownModuleFunc)();

  struct EntryPoints {
    CONTENT_EXPORT EntryPoints();

    GetInterfaceFunc get_interface = nullptr;
    PPP_InitializeModuleFunc initialize_module = nullptr;
    PPP_ShutdownModuleFunc shutdown_module = nullptr;  // Optional, may be NULL.
  };
#endif  // BUILDFLAG(ENABLE_PPAPI)
  bool is_internal = false;

  // PPAPI revival: pepper plugin hints restored from the pre-M140 layout.
  bool is_out_of_process = false;

  // Permission bits for the pepper plugin (see ppapi::PpapiPermissions).
  uint32_t permissions = 0;

#if BUILDFLAG(ENABLE_PPAPI)
  // Entry points for internal plugins (is_internal == true).
  EntryPoints internal_entry_points;
#endif  // BUILDFLAG(ENABLE_PPAPI)

  base::FilePath path;  // Internal plugins have "internal-[name]" as path.
  std::string name;
  std::string description;
  std::string version;
  std::vector<WebPluginMimeType> mime_types;
};

}  // namespace content

#endif  // CONTENT_PUBLIC_COMMON_CONTENT_PLUGIN_INFO_H_
