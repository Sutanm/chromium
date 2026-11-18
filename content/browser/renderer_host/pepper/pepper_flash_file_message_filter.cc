// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/renderer_host/pepper/pepper_flash_file_message_filter.h"

#include <utility>

#include "base/files/file.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/task/thread_pool.h"
#include "build/build_config.h"
#include "content/browser/child_process_security_policy_impl.h"
#include "content/browser/renderer_host/pepper/pepper_security_helper.h"
#include "content/public/browser/browser_ppapi_host.h"
#include "ipc/ipc_platform_file.h"
#include "ppapi/c/pp_errors.h"
#include "ppapi/host/dispatch_host_message.h"
#include "ppapi/host/host_message_context.h"
#include "ppapi/host/ppapi_host.h"
#include "ppapi/proxy/ppapi_messages.h"
#include "ppapi/shared_impl/dir_contents.h"
#include "ppapi/shared_impl/file_path.h"
#include "ppapi/shared_impl/file_type_conversion.h"

namespace content {

namespace {

// Directory under the profile data directory where PPAPI plugin data lives.
// Matches what Chrome used (<profile>/Pepper Data/<plugin name>).
const char kPepperDataDirname[] = "Pepper Data";

bool CanRead(int process_id, const base::FilePath& path) {
  return ChildProcessSecurityPolicyImpl::GetInstance()->CanReadFile(process_id,
                                                                    path);
}

bool CanCreateReadWrite(int process_id, const base::FilePath& path) {
  return ChildProcessSecurityPolicyImpl::GetInstance()->CanCreateReadWriteFile(
      process_id, path);
}

}  // namespace

PepperFlashFileMessageFilter::PepperFlashFileMessageFilter(
    PP_Instance instance,
    BrowserPpapiHost* host)
    : plugin_process_(host->GetPluginProcess().Duplicate()) {
  int unused = 0;
  host->GetRenderFrameIDsForInstance(instance, &render_process_id_, &unused);
  base::FilePath profile_data_directory = host->GetProfileDataDirectory();
  std::string plugin_name = host->GetPluginName();

  if (profile_data_directory.empty() || plugin_name.empty()) {
    // Without these we would build a wrong path and could give access to the
    // wrong files. Leave |plugin_data_directory_| empty so that
    // ValidateAndConvertPepperFilePath() fails closed (access denied) instead
    // of crashing the browser.
    LOG(ERROR) << "[flash-file] missing profile data dir (" <<
        profile_data_directory << ") or plugin name (" << plugin_name
               << "); Flash local storage will be unavailable";
    return;
  }

  plugin_data_directory_ = GetDataDirName(profile_data_directory)
                               .Append(base::FilePath::FromUTF8Unsafe(plugin_name));
  LOG(INFO) << "[flash-file] plugin data dir = " << plugin_data_directory_;
}

PepperFlashFileMessageFilter::~PepperFlashFileMessageFilter() = default;

// static
base::FilePath PepperFlashFileMessageFilter::GetDataDirName(
    const base::FilePath& profile_path) {
  return profile_path.AppendASCII(kPepperDataDirname);
}

scoped_refptr<base::SequencedTaskRunner>
PepperFlashFileMessageFilter::OverrideTaskRunnerForMessage(
    const IPC::Message& msg) {
  // File operations run on the blocking pool instead of a single thread, since
  // the plugin sends these synchronously and cannot issue a new message until
  // the current one returns.
  return base::ThreadPool::CreateSequencedTaskRunner(
      {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
       base::TaskShutdownBehavior::BLOCK_SHUTDOWN});
}

int32_t PepperFlashFileMessageFilter::OnResourceMessageReceived(
    const IPC::Message& msg,
    ppapi::host::HostMessageContext* context) {
  PPAPI_BEGIN_MESSAGE_MAP(PepperFlashFileMessageFilter, msg)
    PPAPI_DISPATCH_HOST_RESOURCE_CALL(PpapiHostMsg_FlashFile_OpenFile,
                                      OnOpenFile)
    PPAPI_DISPATCH_HOST_RESOURCE_CALL(PpapiHostMsg_FlashFile_RenameFile,
                                      OnRenameFile)
    PPAPI_DISPATCH_HOST_RESOURCE_CALL(PpapiHostMsg_FlashFile_DeleteFileOrDir,
                                      OnDeleteFileOrDir)
    PPAPI_DISPATCH_HOST_RESOURCE_CALL(PpapiHostMsg_FlashFile_CreateDir,
                                      OnCreateDir)
    PPAPI_DISPATCH_HOST_RESOURCE_CALL(PpapiHostMsg_FlashFile_QueryFile,
                                      OnQueryFile)
    PPAPI_DISPATCH_HOST_RESOURCE_CALL(PpapiHostMsg_FlashFile_GetDirContents,
                                      OnGetDirContents)
    PPAPI_DISPATCH_HOST_RESOURCE_CALL_0(
        PpapiHostMsg_FlashFile_CreateTemporaryFile, OnCreateTemporaryFile)
  PPAPI_END_MESSAGE_MAP()
  return PP_ERROR_FAILED;
}

int32_t PepperFlashFileMessageFilter::OnOpenFile(
    ppapi::host::HostMessageContext* context,
    const ppapi::PepperFilePath& path,
    int pp_open_flags) {
  base::FilePath full_path = ValidateAndConvertPepperFilePath(
      path, base::BindRepeating(&CanOpenWithPepperFlags, pp_open_flags));
  if (full_path.empty()) {
    return ppapi::FileErrorToPepperError(base::File::FILE_ERROR_ACCESS_DENIED);
  }

  uint32_t platform_file_flags = 0;
  if (!ppapi::PepperFileOpenFlagsToPlatformFileFlags(pp_open_flags,
                                                     &platform_file_flags)) {
    return base::File::FILE_ERROR_FAILED;
  }

  base::File file(full_path, platform_file_flags);
  if (!file.IsValid())
    return ppapi::FileErrorToPepperError(file.error_details());

  // Make sure we didn't try to open a directory: directory fd shouldn't be
  // passed to untrusted processes because they open security holes.
  base::File::Info info;
  if (!file.GetInfo(&info) || info.is_directory) {
    // When in doubt, throw it out.
    return ppapi::FileErrorToPepperError(base::File::FILE_ERROR_ACCESS_DENIED);
  }

  IPC::PlatformFileForTransit transit_file =
      IPC::TakePlatformFileForTransit(std::move(file));
  ppapi::host::ReplyMessageContext reply_context =
      context->MakeReplyMessageContext();
  reply_context.params.AppendHandle(ppapi::proxy::SerializedHandle(
      ppapi::proxy::SerializedHandle::FILE, transit_file));
  SendReply(reply_context, IPC::Message());
  return PP_OK_COMPLETIONPENDING;
}

int32_t PepperFlashFileMessageFilter::OnRenameFile(
    ppapi::host::HostMessageContext* context,
    const ppapi::PepperFilePath& from_path,
    const ppapi::PepperFilePath& to_path) {
  base::FilePath from_full_path = ValidateAndConvertPepperFilePath(
      from_path, base::BindRepeating(&CanCreateReadWrite));
  base::FilePath to_full_path = ValidateAndConvertPepperFilePath(
      to_path, base::BindRepeating(&CanCreateReadWrite));
  if (from_full_path.empty() || to_full_path.empty()) {
    return ppapi::FileErrorToPepperError(base::File::FILE_ERROR_ACCESS_DENIED);
  }

  bool result = base::Move(from_full_path, to_full_path);
  return ppapi::FileErrorToPepperError(
      result ? base::File::FILE_OK : base::File::FILE_ERROR_ACCESS_DENIED);
}

int32_t PepperFlashFileMessageFilter::OnDeleteFileOrDir(
    ppapi::host::HostMessageContext* context,
    const ppapi::PepperFilePath& path,
    bool recursive) {
  base::FilePath full_path = ValidateAndConvertPepperFilePath(
      path, base::BindRepeating(&CanCreateReadWrite));
  if (full_path.empty()) {
    return ppapi::FileErrorToPepperError(base::File::FILE_ERROR_ACCESS_DENIED);
  }

  bool result = recursive ? base::DeletePathRecursively(full_path)
                          : base::DeleteFile(full_path);
  return ppapi::FileErrorToPepperError(
      result ? base::File::FILE_OK : base::File::FILE_ERROR_ACCESS_DENIED);
}

int32_t PepperFlashFileMessageFilter::OnCreateDir(
    ppapi::host::HostMessageContext* context,
    const ppapi::PepperFilePath& path) {
  base::FilePath full_path = ValidateAndConvertPepperFilePath(
      path, base::BindRepeating(&CanCreateReadWrite));
  if (full_path.empty()) {
    return ppapi::FileErrorToPepperError(base::File::FILE_ERROR_ACCESS_DENIED);
  }

  bool result = base::CreateDirectory(full_path);
  return ppapi::FileErrorToPepperError(
      result ? base::File::FILE_OK : base::File::FILE_ERROR_ACCESS_DENIED);
}

int32_t PepperFlashFileMessageFilter::OnQueryFile(
    ppapi::host::HostMessageContext* context,
    const ppapi::PepperFilePath& path) {
  base::FilePath full_path =
      ValidateAndConvertPepperFilePath(path, base::BindRepeating(&CanRead));
  if (full_path.empty()) {
    return ppapi::FileErrorToPepperError(base::File::FILE_ERROR_ACCESS_DENIED);
  }

  base::File::Info info;
  bool result = base::GetFileInfo(full_path, &info);
  context->reply_msg = PpapiPluginMsg_FlashFile_QueryFileReply(info);
  return ppapi::FileErrorToPepperError(
      result ? base::File::FILE_OK : base::File::FILE_ERROR_ACCESS_DENIED);
}

int32_t PepperFlashFileMessageFilter::OnGetDirContents(
    ppapi::host::HostMessageContext* context,
    const ppapi::PepperFilePath& path) {
  base::FilePath full_path =
      ValidateAndConvertPepperFilePath(path, base::BindRepeating(&CanRead));
  if (full_path.empty()) {
    return ppapi::FileErrorToPepperError(base::File::FILE_ERROR_ACCESS_DENIED);
  }

  ppapi::DirContents contents;
  base::FileEnumerator enumerator(full_path, false,
                                  base::FileEnumerator::FILES |
                                      base::FileEnumerator::DIRECTORIES |
                                      base::FileEnumerator::INCLUDE_DOT_DOT);

  for (base::FilePath name = enumerator.Next(); !name.empty();
       name = enumerator.Next()) {
    base::FileEnumerator::FileInfo info = enumerator.GetInfo();
    ppapi::DirEntry entry;
    entry.name = info.GetName();
    entry.is_dir = info.IsDirectory();
    contents.push_back(entry);
  }

  context->reply_msg = PpapiPluginMsg_FlashFile_GetDirContentsReply(contents);
  return PP_OK;
}

int32_t PepperFlashFileMessageFilter::OnCreateTemporaryFile(
    ppapi::host::HostMessageContext* context) {
  ppapi::PepperFilePath dir_path(ppapi::PepperFilePath::DOMAIN_MODULE_LOCAL,
                                 base::FilePath());
  base::FilePath validated_dir_path = ValidateAndConvertPepperFilePath(
      dir_path, base::BindRepeating(&CanCreateReadWrite));
  if (validated_dir_path.empty() ||
      (!base::DirectoryExists(validated_dir_path) &&
       !base::CreateDirectory(validated_dir_path))) {
    return ppapi::FileErrorToPepperError(base::File::FILE_ERROR_ACCESS_DENIED);
  }

  base::FilePath file_path;
  if (!base::CreateTemporaryFileInDir(validated_dir_path, &file_path))
    return ppapi::FileErrorToPepperError(base::File::FILE_ERROR_FAILED);

  int file_flags = base::File::FLAG_CREATE_ALWAYS | base::File::FLAG_READ |
                   base::File::FLAG_WRITE | base::File::FLAG_DELETE_ON_CLOSE;
#if BUILDFLAG(IS_WIN)
  file_flags |= base::File::FLAG_WIN_TEMPORARY;
#endif
  base::File file(file_path, file_flags);

  if (!file.IsValid())
    return ppapi::FileErrorToPepperError(file.error_details());

  IPC::PlatformFileForTransit transit_file =
      IPC::TakePlatformFileForTransit(std::move(file));
  ppapi::host::ReplyMessageContext reply_context =
      context->MakeReplyMessageContext();
  reply_context.params.AppendHandle(ppapi::proxy::SerializedHandle(
      ppapi::proxy::SerializedHandle::FILE, transit_file));
  SendReply(reply_context, IPC::Message());
  return PP_OK_COMPLETIONPENDING;
}

base::FilePath PepperFlashFileMessageFilter::ValidateAndConvertPepperFilePath(
    const ppapi::PepperFilePath& pepper_path,
    const CheckPermissionsCallback& check_permissions_callback) const {
  base::FilePath file_path;  // Empty path returned on error.
  switch (pepper_path.domain()) {
    case ppapi::PepperFilePath::DOMAIN_ABSOLUTE:
      if (pepper_path.path().IsAbsolute() &&
          check_permissions_callback.Run(render_process_id_,
                                         pepper_path.path()))
        file_path = pepper_path.path();
      break;
    case ppapi::PepperFilePath::DOMAIN_MODULE_LOCAL:
      // This filter provides the module name portion of the path to prevent
      // plugins from accessing each other's data.
      if (!plugin_data_directory_.empty() && !pepper_path.path().IsAbsolute() &&
          !pepper_path.path().ReferencesParent())
        file_path = plugin_data_directory_.Append(pepper_path.path());
      break;
    default:
      // Fail closed instead of NOTREACHED(): an unexpected domain from the
      // plugin must not take down the browser process.
      LOG(ERROR) << "[flash-file] unexpected path domain "
                 << static_cast<int>(pepper_path.domain());
      break;
  }
  return file_path;
}

}  // namespace content
