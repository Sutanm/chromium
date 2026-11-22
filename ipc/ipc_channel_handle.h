// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IPC_IPC_CHANNEL_HANDLE_H_
#define IPC_IPC_CHANNEL_HANDLE_H_

#include "build/build_config.h"
#include "ipc/ipc_mojo_param_traits.h"
#include "mojo/public/cpp/system/message_pipe.h"

namespace IPC {

struct ChannelHandle {
  ChannelHandle() {}
  ChannelHandle(mojo::MessagePipeHandle h) : mojo_handle(h) {}

  bool is_mojo_channel_handle() const { return mojo_handle.is_valid(); }

  mojo::MessagePipeHandle mojo_handle;
};

// PPAPI revival: upstream 142 deleted IPC::ChannelHandle together with its
// ParamTraits (no Chromium message carries one any more), but ppapi's
// PpapiHostMsg_ChannelCreated still does. The mojo handle traits survive, so
// the wrapper simply delegates to them.
template <>
struct ParamTraits<ChannelHandle> {
  using param_type = ChannelHandle;

  static void Write(base::Pickle* m, const param_type& p) {
    ParamTraits<mojo::MessagePipeHandle>::Write(m, p.mojo_handle);
  }

  static bool Read(const base::Pickle* m,
                   base::PickleIterator* iter,
                   param_type* r) {
    return ParamTraits<mojo::MessagePipeHandle>::Read(m, iter, &r->mojo_handle);
  }
};

}  // namespace IPC

#endif  // IPC_IPC_CHANNEL_HANDLE_H_
