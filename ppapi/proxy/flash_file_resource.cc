// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ppapi/proxy/flash_file_resource.h"

#include <stddef.h>

#include "base/files/file_path.h"
#include "ipc/ipc_message.h"
#include "ppapi/c/pp_errors.h"
#include "ppapi/proxy/ppapi_messages.h"
#include "ppapi/shared_impl/file_type_conversion.h"
#include "ppapi/shared_impl/scoped_pp_var.h"
#include "ppapi/shared_impl/time_conversion.h"
#include "ppapi/shared_impl/var.h"
#include "ppapi/thunk/enter.h"
#include "ppapi/thunk/ppb_file_ref_api.h"
#include "ppapi/trace/flash_trace.h"

namespace ppapi {
namespace proxy {

namespace {

// FlashTrace operation ids for the kFlashFile kind (BEGIN carries the id in
// arg0, the matching END carries the PP_Error in arg1). Kept local on purpose:
// these are a probe detail, not part of the PPAPI surface.
enum FlashFileTraceOp : int64_t {
  kTraceOpen = 1,
  kTraceRename = 2,
  kTraceDelete = 3,
  kTraceCreateDir = 4,
  kTraceQuery = 5,
  kTraceGetDirContents = 6,
  kTraceCreateTemporaryFile = 7,
};

// Returns the path that a PPB_FileRef resource references as long as it is an
// PP_FILESYSTEMTYPE_EXTERNAL path. Returns an empty string on error.
std::string GetPathFromFileRef(PP_Resource file_ref) {
  thunk::EnterResourceNoLock<thunk::PPB_FileRef_API> enter(file_ref, true);
  if (enter.failed())
    return std::string();
  if (enter.object()->GetFileSystemType() != PP_FILESYSTEMTYPE_EXTERNAL)
    return std::string();
  ScopedPPVar var(ScopedPPVar::PassRef(), enter.object()->GetAbsolutePath());
  StringVar* string_var = StringVar::FromPPVar(var.get());
  if (!string_var)
    return std::string();
  return string_var->value();
}

}  // namespace

FlashFileResource::FlashFileResource(Connection connection,
                                     PP_Instance instance)
    : PluginResource(connection, instance) {
  SendCreate(BROWSER, PpapiHostMsg_FlashFile_Create());
}

FlashFileResource::~FlashFileResource() {
}

thunk::PPB_Flash_File_API* FlashFileResource::AsPPB_Flash_File_API() {
  return this;
}

int32_t FlashFileResource::OpenFile(PP_Instance /*instance*/,
                                    const char* path,
                                    int32_t mode,
                                    PP_FileHandle* file) {
  return OpenFileHelper(path, PepperFilePath::DOMAIN_MODULE_LOCAL, mode, file);
}

int32_t FlashFileResource::RenameFile(PP_Instance /*instance*/,
                                      const char* path_from,
                                      const char* path_to) {
  PepperFilePath pepper_from(PepperFilePath::DOMAIN_MODULE_LOCAL,
                             base::FilePath::FromUTF8Unsafe(path_from));
  PepperFilePath pepper_to(PepperFilePath::DOMAIN_MODULE_LOCAL,
                           base::FilePath::FromUTF8Unsafe(path_to));

  // FlashTrace: both sides of the rename. The old collector recorded only
  // path_from, so "renamed to where" was unknowable.
  if (flash_trace::Enabled()) {
    flash_trace::Record(flash_trace::kFlashFile, pp_instance(), pp_resource(),
                        kTraceRename, 0, path_from, flash_trace::kFlagBegin);
    flash_trace::Record(flash_trace::kNote, pp_instance(), pp_resource(),
                        kTraceRename, 1, path_to, flash_trace::kFlagBegin);
  }
  int32_t error = SyncCall<IPC::Message>(
      BROWSER, PpapiHostMsg_FlashFile_RenameFile(pepper_from, pepper_to));
  flash_trace::Record(flash_trace::kFlashFile, pp_instance(), pp_resource(),
                      kTraceRename, error, path_from,
                      flash_trace::kFlagEnd |
                          (error != PP_OK ? flash_trace::kFlagError : 0u));

  return error;
}

int32_t FlashFileResource::DeleteFileOrDir(PP_Instance /*instance*/,
                                           const char* path,
                                           PP_Bool recursive) {
  PepperFilePath pepper_path(PepperFilePath::DOMAIN_MODULE_LOCAL,
                             base::FilePath::FromUTF8Unsafe(path));

  // FlashTrace: arg1 carries `recursive`, which the old collector dropped.
  flash_trace::ScopedOp op(flash_trace::kFlashFile, pp_instance(), pp_resource(),
                           kTraceDelete, PP_ToBool(recursive) ? 1 : 0, path);
  int32_t error = SyncCall<IPC::Message>(
      BROWSER, PpapiHostMsg_FlashFile_DeleteFileOrDir(pepper_path,
                                                      PP_ToBool(recursive)));
  op.set_result(error);

  return error;
}

int32_t FlashFileResource::CreateDir(PP_Instance /*instance*/,
                                     const char* path) {
  PepperFilePath pepper_path(PepperFilePath::DOMAIN_MODULE_LOCAL,
                             base::FilePath::FromUTF8Unsafe(path));

  flash_trace::ScopedOp op(flash_trace::kFlashFile, pp_instance(), pp_resource(),
                           kTraceCreateDir, 0, path);
  int32_t error = SyncCall<IPC::Message>(BROWSER,
      PpapiHostMsg_FlashFile_CreateDir(pepper_path));
  op.set_result(error);

  return error;
}

int32_t FlashFileResource::QueryFile(PP_Instance /*instance*/,
                                     const char* path,
                                     PP_FileInfo* info) {
  return QueryFileHelper(path, PepperFilePath::DOMAIN_MODULE_LOCAL, info);
}

int32_t FlashFileResource::GetDirContents(PP_Instance /*instance*/,
                                          const char* path,
                                          PP_DirContents_Dev** contents) {
  ppapi::DirContents entries;
  PepperFilePath pepper_path(PepperFilePath::DOMAIN_MODULE_LOCAL,
                             base::FilePath::FromUTF8Unsafe(path));

  // FlashTrace: directory listing was never observed, so "Flash enumerated its
  // own game directory" could not be confirmed or denied.
  flash_trace::ScopedOp op(flash_trace::kFlashFile, pp_instance(), pp_resource(),
                           kTraceGetDirContents, 0, path);
  int32_t error = SyncCall<PpapiPluginMsg_FlashFile_GetDirContentsReply>(
      BROWSER, PpapiHostMsg_FlashFile_GetDirContents(pepper_path), &entries);
  op.set_result(error);
  if (flash_trace::Enabled()) {
    flash_trace::Record(flash_trace::kNote, pp_instance(), pp_resource(),
                        kTraceGetDirContents,
                        static_cast<int64_t>(entries.size()), path,
                        flash_trace::kFlagEnd);
  }

  if (error == PP_OK) {
    // Copy the serialized dir entries to the output struct.
    *contents = new PP_DirContents_Dev;
    (*contents)->count = static_cast<int32_t>(entries.size());
    (*contents)->entries = new PP_DirEntry_Dev[entries.size()];
    for (size_t i = 0; i < entries.size(); i++) {
      const ppapi::DirEntry& source = entries[i];
      PP_DirEntry_Dev* dest = &(*contents)->entries[i];
      std::string name = source.name.AsUTF8Unsafe();
      char* name_copy = new char[name.size() + 1];
      memcpy(name_copy, name.c_str(), name.size() + 1);
      dest->name = name_copy;
      dest->is_dir = PP_FromBool(source.is_dir);
    }
  }

  return error;
}

void FlashFileResource::FreeDirContents(PP_Instance /*instance*/,
                                        PP_DirContents_Dev* contents) {
  for (int32_t i = 0; i < contents->count; ++i)
    delete[] contents->entries[i].name;
  delete[] contents->entries;
  delete contents;
}

int32_t FlashFileResource::CreateTemporaryFile(PP_Instance /*instance*/,
                                               PP_FileHandle* file) {
  flash_trace::ScopedOp op(flash_trace::kFlashFile, pp_instance(), pp_resource(),
                           kTraceCreateTemporaryFile, 0, "(temp)");
  if (!file) {
    op.set_result(PP_ERROR_BADARGUMENT);
    return PP_ERROR_BADARGUMENT;
  }

  IPC::Message unused;
  ResourceMessageReplyParams reply_params;
  int32_t error = GenericSyncCall(BROWSER,
      PpapiHostMsg_FlashFile_CreateTemporaryFile(), &unused, &reply_params);
  if (error != PP_OK) {
    op.set_result(error);
    return error;
  }

  IPC::PlatformFileForTransit transit_file;
  if (!reply_params.TakeFileHandleAtIndex(0, &transit_file)) {
    op.set_result(PP_ERROR_FAILED);
    return PP_ERROR_FAILED;
  }

  *file = IPC::PlatformFileForTransitToPlatformFile(transit_file);
  op.set_result(PP_OK);
  flash_trace::Record(
      flash_trace::kNote, pp_instance(), pp_resource(), kTraceCreateTemporaryFile,
      static_cast<int64_t>(reinterpret_cast<intptr_t>(*file)), "(temp)",
      flash_trace::kFlagEnd);
  return PP_OK;
}

int32_t FlashFileResource::OpenFileRef(PP_Instance /*instance*/,
                                       PP_Resource file_ref,
                                       int32_t mode,
                                       PP_FileHandle* file) {
  return OpenFileHelper(GetPathFromFileRef(file_ref),
                        PepperFilePath::DOMAIN_ABSOLUTE, mode, file);
}

int32_t FlashFileResource::QueryFileRef(PP_Instance /*instance*/,
                                        PP_Resource file_ref,
                                        PP_FileInfo* info) {
  return QueryFileHelper(GetPathFromFileRef(file_ref),
                         PepperFilePath::DOMAIN_ABSOLUTE, info);
}

int32_t FlashFileResource::OpenFileHelper(const std::string& path,
                                          PepperFilePath::Domain domain_type,
                                          int32_t mode,
                                          PP_FileHandle* file) {
  // FlashTrace: this was completely blind before. Without the returned handle
  // and the error code, "did the game ever manage to read its config?" was
  // unanswerable, which is exactly the question §9.6 left open.
  flash_trace::ScopedOp op(flash_trace::kFlashFile, pp_instance(), pp_resource(),
                           kTraceOpen, mode, path.c_str());
  int32_t error;
  if (path.empty() ||
      !ppapi::PepperFileOpenFlagsToPlatformFileFlags(mode, NULL) ||
      !file) {
    op.set_result(PP_ERROR_BADARGUMENT);
    return PP_ERROR_BADARGUMENT;
  }

  PepperFilePath pepper_path(domain_type, base::FilePath::FromUTF8Unsafe(path));

  IPC::Message unused;
  ResourceMessageReplyParams reply_params;
  error = GenericSyncCall(BROWSER,
      PpapiHostMsg_FlashFile_OpenFile(pepper_path, mode), &unused,
      &reply_params);
  if (error != PP_OK) {
    op.set_result(error);
    return error;
  }

  IPC::PlatformFileForTransit transit_file;
  if (!reply_params.TakeFileHandleAtIndex(0, &transit_file)) {
    op.set_result(PP_ERROR_FAILED);
    return PP_ERROR_FAILED;
  }

  *file = IPC::PlatformFileForTransitToPlatformFile(transit_file);
  op.set_result(PP_OK);
  // arg1 on this extra note is the handle value, so "opened but writes went
  // nowhere" is visible instead of being inferred.
  flash_trace::Record(flash_trace::kNote, pp_instance(), pp_resource(),
                      kTraceOpen,
                      static_cast<int64_t>(reinterpret_cast<intptr_t>(*file)),
                      path.c_str(), flash_trace::kFlagEnd);
  return PP_OK;
}

int32_t FlashFileResource::QueryFileHelper(const std::string& path,
                                           PepperFilePath::Domain domain_type,
                                           PP_FileInfo* info) {
  flash_trace::ScopedOp op(flash_trace::kFlashFile, pp_instance(), pp_resource(),
                           kTraceQuery, 0, path.c_str());
  if (path.empty() || !info) {
    op.set_result(PP_ERROR_BADARGUMENT);
    return PP_ERROR_BADARGUMENT;
  }

  base::File::Info file_info;
  PepperFilePath pepper_path(domain_type, base::FilePath::FromUTF8Unsafe(path));

  int32_t error = SyncCall<PpapiPluginMsg_FlashFile_QueryFileReply>(BROWSER,
      PpapiHostMsg_FlashFile_QueryFile(pepper_path), &file_info);
  op.set_result(error);
  if (flash_trace::Enabled()) {
    // The size is what tells "exists and is the real config" from "exists and
    // is empty".
    flash_trace::Record(flash_trace::kNote, pp_instance(), pp_resource(),
                        kTraceQuery,
                        error == PP_OK ? static_cast<int64_t>(file_info.size)
                                       : -1,
                        path.c_str(), flash_trace::kFlagEnd);
  }

  if (error == PP_OK) {
    info->size = file_info.size;
    info->creation_time = TimeToPPTime(file_info.creation_time);
    info->last_access_time = TimeToPPTime(file_info.last_accessed);
    info->last_modified_time = TimeToPPTime(file_info.last_modified);
    info->system_type = PP_FILESYSTEMTYPE_EXTERNAL;
    if (file_info.is_directory)
      info->type = PP_FILETYPE_DIRECTORY;
    else
      info->type = PP_FILETYPE_REGULAR;
  }

  return error;
}

}  // namespace proxy
}  // namespace ppapi
