#pragma allow_unsafe_buffers
#ifdef UNSAFE_BUFFERS_BUILD
// Allow legacy buffer arithmetic in the vendored PPAPI sources.
#pragma allow_unsafe_buffers
#endif
// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ppapi/shared_impl/ppapi_switches.h"

namespace switches {

// Enables the testing interface for PPAPI.
const char kEnablePepperTesting[] = "enable-pepper-testing";

}  // namespace switches
