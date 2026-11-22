// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IPC_PARAM_TRAITS_LOG_MACROS_H_
#define IPC_PARAM_TRAITS_LOG_MACROS_H_

// PPAPI revival (Chromium 142): upstream removed the legacy IPC logging
// machinery -- IPC::ParamTraits<T>::Log and IPC::LogParam no longer exist -- so
// this pass generates nothing. The header is kept because *_messages.h includes
// it as the last step of the declare/write/read/log macro cycle.
#include "ipc/ipc_message_null_macros.h"

#endif  // IPC_PARAM_TRAITS_LOG_MACROS_H_
