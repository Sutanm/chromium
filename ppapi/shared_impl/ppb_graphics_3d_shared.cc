#pragma allow_unsafe_buffers
#ifdef UNSAFE_BUFFERS_BUILD
// Allow legacy buffer arithmetic in the vendored PPAPI sources.
#pragma allow_unsafe_buffers
#endif
// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ppapi/shared_impl/ppb_graphics_3d_shared.h"

#include "base/logging.h"
#include "gpu/GLES2/gl2extchromium.h"
#include "gpu/command_buffer/client/gles2_cmd_helper.h"
#include "gpu/command_buffer/client/gpu_control.h"
#include "gpu/command_buffer/client/gles2_implementation.h"
#include "gpu/command_buffer/client/shared_memory_limits.h"
#include "gpu/command_buffer/client/transfer_buffer.h"
#include "gpu/command_buffer/common/sync_token.h"
#include "ppapi/c/pp_errors.h"

namespace ppapi {

PPB_Graphics3D_Shared::PPB_Graphics3D_Shared(PP_Instance instance)
    : Resource(OBJECT_IS_IMPL, instance) {}

PPB_Graphics3D_Shared::PPB_Graphics3D_Shared(const HostResource& host_resource,
                                             const gfx::Size& size)
    : Resource(OBJECT_IS_PROXY, host_resource), size_(size) {}

PPB_Graphics3D_Shared::~PPB_Graphics3D_Shared() {
  // Make sure that GLES2 implementation has already been destroyed.
  DCHECK(!gles2_helper_.get());
  DCHECK(!transfer_buffer_.get());
  DCHECK(!gles2_impl_.get());
}

thunk::PPB_Graphics3D_API* PPB_Graphics3D_Shared::AsPPB_Graphics3D_API() {
  return this;
}

int32_t PPB_Graphics3D_Shared::GetAttribs(int32_t attrib_list[]) {
  // TODO(alokp): Implement me.
  return PP_ERROR_FAILED;
}

int32_t PPB_Graphics3D_Shared::SetAttribs(const int32_t attrib_list[]) {
  // TODO(alokp): Implement me.
  return PP_ERROR_FAILED;
}

int32_t PPB_Graphics3D_Shared::GetError() {
  // TODO(alokp): Implement me.
  return PP_ERROR_FAILED;
}

int32_t PPB_Graphics3D_Shared::ResizeBuffers(int32_t width, int32_t height) {
  if ((width < 0) || (height < 0))
    return PP_ERROR_BADARGUMENT;

  size_ = gfx::Size(width, height);

  DoResize(size_);

  // TODO(alokp): Check if resize succeeded and return appropriate error code.
  return PP_OK;
}

int32_t PPB_Graphics3D_Shared::SwapBuffers(
    scoped_refptr<TrackedCallback> callback) {
  return SwapBuffersWithSyncToken(callback, gpu::SyncToken(), size_);
}

int32_t PPB_Graphics3D_Shared::SwapBuffersWithSyncToken(
    scoped_refptr<TrackedCallback> callback,
    const gpu::SyncToken& sync_token,
    const gfx::Size& size) {
  if (HasPendingSwap()) {
    Log(PP_LOGLEVEL_ERROR,
        "PPB_Graphics3D.SwapBuffers: Plugin attempted swap "
        "with previous swap still pending.");
    // Already a pending SwapBuffers that hasn't returned yet.
    return PP_ERROR_INPROGRESS;
  }

  swap_callback_ = callback;
  return DoSwapBuffers(sync_token, size);
}

int32_t PPB_Graphics3D_Shared::GetAttribMaxValue(int32_t attribute,
                                                 int32_t* value) {
  // TODO(alokp): Implement me.
  return PP_ERROR_FAILED;
}

void* PPB_Graphics3D_Shared::MapTexSubImage2DCHROMIUM(GLenum target,
                                                      GLint level,
                                                      GLint xoffset,
                                                      GLint yoffset,
                                                      GLsizei width,
                                                      GLsizei height,
                                                      GLenum format,
                                                      GLenum type,
                                                      GLenum access) {
  return gles2_impl_->MapTexSubImage2DCHROMIUM(
      target, level, xoffset, yoffset, width, height, format, type, access);
}

void PPB_Graphics3D_Shared::UnmapTexSubImage2DCHROMIUM(const void* mem) {
  gles2_impl_->UnmapTexSubImage2DCHROMIUM(mem);
}

gpu::gles2::GLES2Interface* PPB_Graphics3D_Shared::gles2_interface() {
  return gles2_impl_.get();
}

void PPB_Graphics3D_Shared::SwapBuffersACK(int32_t pp_error) {
  DCHECK(HasPendingSwap());
  swap_callback_->Run(pp_error);
}

bool PPB_Graphics3D_Shared::HasPendingSwap() const {
  return TrackedCallback::IsPending(swap_callback_);
}

bool PPB_Graphics3D_Shared::CreateGLES2Impl(
    gpu::gles2::GLES2Implementation* share_gles2) {
  gpu::SharedMemoryLimits limits;
  gpu::CommandBuffer* command_buffer = GetCommandBuffer();
  DCHECK(command_buffer);

  // Create the GLES2 helper, which writes the command buffer protocol.
  gles2_helper_ = std::make_unique<gpu::gles2::GLES2CmdHelper>(command_buffer);
  const auto helper_result =
      gles2_helper_->Initialize(limits.command_buffer_size);
  if (helper_result != gpu::ContextResult::kSuccess) {
    LOG(ERROR) << "[g3d] GLES2CmdHelper::Initialize failed result="
               << static_cast<int>(helper_result)
               << " cmd_buf_size=" << limits.command_buffer_size;
    return false;
  }

  // Create a transfer buffer used to copy resources between the renderer
  // process and the GPU process.
  transfer_buffer_ = std::make_unique<gpu::TransferBuffer>(gles2_helper_.get());

  // PPAPI revival: the GPU service reports whether resource names are generated
  // on the service side (bind_generates_resource). Chromium 138+ reports false
  // for the passthrough decoder, while GLES2Implementation::Initialize hard
  // fails on a mismatch with its ShareGroup ("bind_generates_resource
  // mismatch" -> kFatalFailure), which used to make every PPB_Graphics3D
  // creation fail in the plugin process and pushed Flash Stage3D back to its
  // software renderer. Chromium 142 moved this state onto ShareGroup, so the
  // capability can no longer be queried or forwarded from here.
  const bool lose_context_when_out_of_memory = false;

  // Create the object exposing the OpenGL API.
  // PPAPI revival: Chromium 142 dropped the bind_generates_resource and
  // support_client_side_arrays constructor parameters (the flag now lives on
  // ShareGroup), so nothing has to be threaded through here any more.
  gles2_impl_ = std::make_unique<gpu::gles2::GLES2Implementation>(
      gles2_helper_.get(), share_gles2 ? share_gles2->share_group() : nullptr,
      transfer_buffer_.get(), lose_context_when_out_of_memory, GetGpuControl());

  const auto impl_result = gles2_impl_->Initialize(limits);
  if (impl_result != gpu::ContextResult::kSuccess) {
    LOG(ERROR) << "[g3d] GLES2Implementation::Initialize failed result="
               << static_cast<int>(impl_result);
    return false;
  }

  gles2_impl_->TraceBeginCHROMIUM("gpu_toplevel", "PPAPIContext");

  return true;
}

void PPB_Graphics3D_Shared::DestroyGLES2Impl() {
  gles2_impl_.reset();
  transfer_buffer_.reset();
  gles2_helper_.reset();
}

}  // namespace ppapi
