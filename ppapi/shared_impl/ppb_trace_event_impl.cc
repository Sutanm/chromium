#pragma allow_unsafe_buffers
#ifdef UNSAFE_BUFFERS_BUILD
// Allow legacy buffer arithmetic in the vendored PPAPI sources.
#pragma allow_unsafe_buffers
#endif
// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ppapi/shared_impl/ppb_trace_event_impl.h"

#include "base/threading/platform_thread.h"
#include "base/trace_event/trace_event.h"
#include "ppapi/thunk/thunk.h"

namespace ppapi {

// PPB_Trace_Event_Dev is a shared implementation because Trace Events can be
// sent from either the plugin process or renderer process depending on whether
// the plugin is in- or out-of-process.
//
// Chromium 144 moved tracing to Perfetto and removed the legacy
// trace_event_internal::AddTraceEvent / TraceLog entry points this file used.
// PPB_Trace_Event_Dev is a debugging-only dev interface (Flash never depends on
// it for rendering), so the revival degrades it to no-ops instead of porting it
// to the Perfetto tracing macro API.

// static
void* TraceEventImpl::GetCategoryEnabled(const char* category_name) {
  // Returning a non-null pointer keeps callers that store the value happy.
  static const unsigned char kDummyCategoryEnabled = 1;
  (void)category_name;
  return const_cast<void*>(static_cast<const void*>(&kDummyCategoryEnabled));
}

// static
void TraceEventImpl::AddTraceEvent(int8_t phase,
                                   const void* category_enabled,
                                   const char* name,
                                   uint64_t id,
                                   uint32_t num_args,
                                   const char* arg_names[],
                                   const uint8_t arg_types[],
                                   const uint64_t arg_values[],
                                   uint8_t flags) {
  (void)phase;
  (void)category_enabled;
  (void)name;
  (void)id;
  (void)num_args;
  (void)arg_names;
  (void)arg_types;
  (void)arg_values;
  (void)flags;
}

// static
void TraceEventImpl::AddTraceEventWithThreadIdAndTimestamp(
    int8_t phase,
    const void* category_enabled,
    const char* name,
    uint64_t id,
    int32_t thread_id,
    int64_t timestamp,
    uint32_t num_args,
    const char* arg_names[],
    const uint8_t arg_types[],
    const uint64_t arg_values[],
    uint8_t flags) {
  (void)phase;
  (void)category_enabled;
  (void)name;
  (void)id;
  (void)thread_id;
  (void)timestamp;
  (void)num_args;
  (void)arg_names;
  (void)arg_types;
  (void)arg_values;
  (void)flags;
}

// static
int64_t TraceEventImpl::Now() {
  return base::TimeTicks::Now().ToInternalValue();
}

// static
void TraceEventImpl::SetThreadName(const char* thread_name) {
  base::PlatformThread::SetName(thread_name);
}

namespace {

const PPB_Trace_Event_Dev_0_1 g_ppb_trace_event_thunk_0_1 = {
    &TraceEventImpl::GetCategoryEnabled, &TraceEventImpl::AddTraceEvent,
    &TraceEventImpl::SetThreadName,
};

const PPB_Trace_Event_Dev_0_2 g_ppb_trace_event_thunk_0_2 = {
    &TraceEventImpl::GetCategoryEnabled,
    &TraceEventImpl::AddTraceEvent,
    &TraceEventImpl::AddTraceEventWithThreadIdAndTimestamp,
    &TraceEventImpl::Now,
    &TraceEventImpl::SetThreadName,
};

}  // namespace

}  // namespace ppapi

namespace ppapi {
namespace thunk {

const PPB_Trace_Event_Dev_0_1* GetPPB_Trace_Event_Dev_0_1_Thunk() {
  return &g_ppb_trace_event_thunk_0_1;
}

const PPB_Trace_Event_Dev_0_2* GetPPB_Trace_Event_Dev_0_2_Thunk() {
  return &g_ppb_trace_event_thunk_0_2;
}

}  // namespace thunk
}  // namespace ppapi
