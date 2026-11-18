// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// PPAPI revival: browser-side host for the Flash-private "FlashFile" API
// (PPB_Flash_File / pp::FileModuleLocal). Chromium used to ship this in
// content (see M80), it was removed long before 140; Flash still needs it for
// its writable data root (LSO / SharedObject, FileReference, temporary files).

#ifndef CONTENT_BROWSER_RENDERER_HOST_PEPPER_PEPPER_FLASH_FILE_MESSAGE_FILTER_H_
#define CONTENT_BROWSER_RENDERER_HOST_PEPPER_PEPPER_FLASH_FILE_MESSAGE_FILTER_H_

#include <stdint.h>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_refptr.h"
#include "base/process/process.h"
#include "base/task/sequenced_task_runner.h"
#include "ppapi/c/pp_instance.h"
#include "ppapi/host/resource_host.h"
#include "ppapi/host/resource_message_filter.h"

namespace IPC {
class Message;
}

namespace ppapi {
class PepperFilePath;
}

namespace ppapi {
namespace host {
struct HostMessageContext;
}  // namespace host
}  // namespace ppapi

namespace content {

class BrowserPpapiHost;

// Handles the Flash file messages on a blocking thread pool.
class PepperFlashFileMessageFilter : public ppapi::host::ResourceMessageFilter {
 public:
  PepperFlashFileMessageFilter(PP_Instance instance, BrowserPpapiHost* host);

  static base::FilePath GetDataDirName(const base::FilePath& profile_path);

 private:
  using CheckPermissionsCallback =
      base::RepeatingCallback<bool(int, const base::FilePath&)>;

  ~PepperFlashFileMessageFilter() override;

  // ppapi::host::ResourceMessageFilter overrides.
  scoped_refptr<base::SequencedTaskRunner> OverrideTaskRunnerForMessage(
      const IPC::Message& msg) override;
  int32_t OnResourceMessageReceived(
      const IPC::Message& msg,
      ppapi::host::HostMessageContext* context) override;

  int32_t OnOpenFile(ppapi::host::HostMessageContext* context,
                     const ppapi::PepperFilePath& path,
                     int pp_open_flags);
  int32_t OnRenameFile(ppapi::host::HostMessageContext* context,
                       const ppapi::PepperFilePath& from_path,
                       const ppapi::PepperFilePath& to_path);
  int32_t OnDeleteFileOrDir(ppapi::host::HostMessageContext* context,
                            const ppapi::PepperFilePath& path,
                            bool recursive);
  int32_t OnCreateDir(ppapi::host::HostMessageContext* context,
                      const ppapi::PepperFilePath& path);
  int32_t OnQueryFile(ppapi::host::HostMessageContext* context,
                      const ppapi::PepperFilePath& path);
  int32_t OnGetDirContents(ppapi::host::HostMessageContext* context,
                           const ppapi::PepperFilePath& path);
  int32_t OnCreateTemporaryFile(ppapi::host::HostMessageContext* context);

  base::FilePath ValidateAndConvertPepperFilePath(
      const ppapi::PepperFilePath& pepper_path,
      const CheckPermissionsCallback& check_permissions_callback) const;

  base::FilePath plugin_data_directory_;
  int render_process_id_ = -1;
  base::Process plugin_process_;

  PepperFlashFileMessageFilter(const PepperFlashFileMessageFilter&) = delete;
  PepperFlashFileMessageFilter& operator=(const PepperFlashFileMessageFilter&) =
      delete;
};

}  // namespace content

#endif  // CONTENT_BROWSER_RENDERER_HOST_PEPPER_PEPPER_FLASH_FILE_MESSAGE_FILTER_H_
