// Copyright 2026 The BaoFlashBrowser Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PPAPI_TRACE_FLASH_TRACE_H_
#define PPAPI_TRACE_FLASH_TRACE_H_

// FlashTrace: a low-overhead, per-process recorder for the PPAPI Flash data
// path.
//
// Why it exists (2026-11-21, see docs/reports/2026-11-21-142-flash-runtime.md
// §9.4/§9.5): the previous ad-hoc `ppdbg-*` probes wrote one file per record,
// carried no run identity and no timestamps, hooked only
// PluginDispatcher::Send (which resources bypass via sender()), and never split
// the nested message type out of PpapiHostMsg_ResourceCall. Two runs' logs were
// silently mixed and eight wrong conclusions followed. This recorder fixes the
// measurement before anyone reasons about the site again.
//
// Deliberate design constraints:
//   * Header-only and inline: the same singleton is shared by every translation
//     unit of a binary, and no BUILD.gn edge has to be re-plumbed through
//     ppapi_proxy / content_renderer / content_browser.
//   * Never one write per record. Entries go into a fixed-size in-memory ring
//     buffer; the buffer is appended to the dump file in batches (at most once
//     every kAppendIntervalNs, or when the ring is about to be overwritten), so
//     timing is not perturbed by disk and a hard kill still leaves a usable
//     capture. A final append + trailer happens at exit when we get one.
//     (The first version only wrote at exit; the observed reality is that PPAPI
//     plugin and renderer children are terminated, so atexit never ran and the
//     capture was empty. Batched appends are the fix.)
//   * Identity on every entry: run id (from the launcher), pid, tid, monotonic
//     timestamp, PP_Instance and PP_Resource where the site has them. The file
//     header also pins wall clock to TimeTicks so a trace can be lined up with
//     the renderer-side probe log.
//   * Mutable global state is a trivially-destructible base::NoDestructor, so
//     `-Wexit-time-destructors` (an error in this tree) stays quiet.
//   * Disabled by default: with FLASH_TRACE_DIR unset every hook is one
//     predicted branch plus a call that returns immediately.
//
// File format (little endian, packed):
//   FileHeader (see struct), magic "FTB3"
//   then a stream of Entry records (sizeof(Entry) bytes each)
//   the last entry may be a trailer: kind == kSession, flags & kFlagEnd, with
//   arg0 = entries appended and arg1 = entries written.

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <atomic>
#include <cstdlib>
#include <optional>
#include <string>

#include "base/command_line.h"
#include "base/environment.h"
#include "base/no_destructor.h"
#include "base/process/process_handle.h"
#include "base/synchronization/lock.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/threading/platform_thread.h"
#include "base/time/time.h"
#include "build/build_config.h"

namespace flash_trace {

// What an entry describes. Values are written to disk; keep them stable.
enum Kind : uint32_t {
  kNote = 0,              // free-form marker
  kSession = 1,           // recorder start/stop, process identity
  kPluginMsgOut = 2,      // plugin -> host IPC leaving the plugin process
  kPluginMsgIn = 3,       // host -> plugin IPC entering the plugin process
  kBrowserSenderOut = 4,  // outbound message sent through BrowserSender
  kResourceCall = 5,      // plugin -> BROWSER async nested call
  kResourceSyncCall = 6,  // plugin -> BROWSER sync nested call
  kFlashFile = 7,         // PPB_Flash_File_* entry/exit
  kFileIo = 8,            // PPB_FileIO read/write path (SharedObject .sol)
  kUrlLoaderPlugin = 9,   // plugin-side PPB_URLLoader API entry
  kUrlLoaderHost = 10,    // renderer-side PPB_URLLoader host entry
  kDidCreate = 11,        // PPP_Instance::DidCreate (real arguments)
  kView = 12,             // view / browser-window bounds request + result
  kJsToFlash = 13,        // PluginObject::Call (JS -> Flash)
  kFlashToJs = 14,        // PPB_Var_Deprecated call (Flash -> JS)
  kGraphics3D = 15,       // Stage3D context / swap path
  kSwfData = 16,          // main SWF bytes / EOF accounting
  kFlashSend = 17,        // plugin-originated SendAndStayLocked path
};

// Per-entry modifiers. Values are written to disk; keep them stable.
enum Flags : uint32_t {
  kFlagBegin = 1u << 0,
  kFlagEnd = 1u << 1,
  kFlagError = 1u << 2,
  kFlagSync = 1u << 3,
  kFlagReject = 1u << 4,  // refused locally, never reached the wire
  kFlagMain = 1u << 5,    // main document / main SWF
  kFlagWrap = 1u << 6,    // trailer only: the ring wrapped during the run
};

enum : size_t {
  kTextLen = 40,
  // 128 Ki entries * 88 bytes = 11 MiB per process: comfortably more than the
  // tens of thousands of events in a 60 s capture window at this site's rate,
  // small enough for three processes to coexist.
  kCapacity = 128 * 1024,
  // Append the ring to the dump at most this often (2 s). Caps disk timing
  // influence to one batched write per interval.
  kAppendIntervalNs = 2ull * 1000 * 1000 * 1000,
};

#pragma pack(push, 1)
struct Entry {
  uint64_t ts_ns;  // base::TimeTicks: monotonic, comparable across processes
  uint32_t pid;
  uint32_t tid;
  uint32_t kind;
  uint32_t flags;
  uint32_t instance;    // PP_Instance, 0 when the site has none
  uint32_t resource;    // PP_Resource, 0 when the site has none
  int64_t arg0;         // message type / error / handle / offset ...
  int64_t arg1;         // nested type / destination / bytes / result ...
  char text[kTextLen];  // path, method name, url (truncated, NUL padded)
};

struct FileHeader {
  char magic[8];  // "FTB3"
  uint32_t pid;
  uint32_t reserved;
  uint32_t flush_interval_ns_low;  // low 32 bits, informational
  uint32_t reserved2;
  // Wall-clock reference, so a trace can be lined up with the renderer-side
  // probe log (Date.now) and with webRequest timestamps:
  //   wall_us(entry) = wall_ref_us + (entry.ts_ns - ticks_ref_ns) / 1000
  int64_t wall_ref_us;
  uint64_t ticks_ref_ns;
  char run_id[40];
  char role[16];
};
#pragma pack(pop)

namespace internal {

struct Buffer {
  Buffer() = default;

  base::Lock lock;
  std::atomic<uint32_t> next{0};       // entries ever appended to the ring
  std::atomic<uint32_t> flushed{0};    // entries already written to disk
  std::atomic<bool> wrapped{false};
  std::atomic<bool> started{false};
  std::atomic<bool> finished{false};
  std::atomic<int64_t> last_append_ns{0};
  FILE* file = nullptr;  // guarded by |lock|
  Entry entries[kCapacity];
};

inline bool EnvEnabled() {
  static const bool enabled = [] {
    std::optional<std::string> dir =
        base::Environment::Create()->GetVar("FLASH_TRACE_DIR");
    return dir.has_value() && !dir->empty();
  }();
  return enabled;
}

inline const std::string& TraceDir() {
  static base::NoDestructor<std::string> dir([] {
    return base::Environment::Create()->GetVar("FLASH_TRACE_DIR").value_or("");
  }());
  return *dir;
}

inline const std::string& RunId() {
  static base::NoDestructor<std::string> run_id([] {
    std::string value =
        base::Environment::Create()->GetVar("FLASH_TRACE_RUN").value_or("");
    if (value.empty())
      value = base::NumberToString(base::GetCurrentProcId());
    return value;
  }());
  return *run_id;
}

// "browser" / "renderer" / "ppapi" / "gpu-process" ...: identifies the writing
// process without needing a pid table later.
inline const std::string& Role() {
  static base::NoDestructor<std::string> role([] {
    std::string value =
        base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII("type");
    return value.empty() ? std::string("browser") : value;
  }());
  return *role;
}

inline Buffer& Get() {
  static base::NoDestructor<Buffer> buffer;
  return *buffer;
}

inline int64_t TicksNowNs() {
  return base::TimeTicks::Now().since_origin().InNanoseconds();
}

// Writes every ring entry that has not been written yet. Caller holds |lock|.
inline void AppendLocked(Buffer& buffer) {
  if (!buffer.file)
    return;
  const uint32_t next = buffer.next.load(std::memory_order_relaxed);
  uint32_t from = buffer.flushed.load(std::memory_order_relaxed);
  if (next <= from)
    return;
  // Entries older than (next - kCapacity) have been overwritten in the ring.
  const uint32_t oldest_kept =
      next > kCapacity ? next - static_cast<uint32_t>(kCapacity) : 0u;
  if (from < oldest_kept) {
    from = oldest_kept;
    buffer.wrapped.store(true, std::memory_order_relaxed);
  }
  for (uint32_t i = from; i < next; ++i)
    fwrite(&buffer.entries[i % kCapacity], sizeof(Entry), 1, buffer.file);
  buffer.flushed.store(next, std::memory_order_relaxed);
  fflush(buffer.file);
  buffer.last_append_ns.store(TicksNowNs(), std::memory_order_relaxed);
}

inline bool OpenLocked(Buffer& buffer) {
  if (buffer.file)
    return true;
  const std::string path = TraceDir() + "\\ft-" + RunId() + "-" +
                           base::NumberToString(base::GetCurrentProcId()) +
                           "-" + Role() + ".bin";
  buffer.file = fopen(path.c_str(), "wb");
  if (!buffer.file)
    return false;
  FileHeader header{};
  memcpy(header.magic, "FTB3", 4);
  header.pid = static_cast<uint32_t>(base::GetCurrentProcId());
  header.flush_interval_ns_low =
      static_cast<uint32_t>(kAppendIntervalNs & 0xFFFFFFFFu);
  header.wall_ref_us = base::Time::Now().since_origin().InMicroseconds();
  header.ticks_ref_ns = static_cast<uint64_t>(TicksNowNs());
  base::strlcpy(header.run_id, RunId().c_str(), sizeof(header.run_id));
  base::strlcpy(header.role, Role().c_str(), sizeof(header.role));
  fwrite(&header, sizeof(header), 1, buffer.file);
  fflush(buffer.file);
  return true;
}

// Final flush + trailer. Idempotent; safe to call from atexit.
inline void Finish() {
  Buffer& buffer = Get();
  bool expected = false;
  if (!buffer.finished.compare_exchange_strong(expected, true))
    return;
  base::AutoLock auto_lock(buffer.lock);
  if (!OpenLocked(buffer))
    return;
  AppendLocked(buffer);
  Entry trailer{};
  trailer.ts_ns = static_cast<uint64_t>(TicksNowNs());
  trailer.pid = static_cast<uint32_t>(base::GetCurrentProcId());
  trailer.tid = static_cast<uint32_t>(base::PlatformThread::CurrentId().raw());
  trailer.kind = static_cast<uint32_t>(kSession);
  trailer.flags = kFlagEnd | (buffer.wrapped.load() ? kFlagWrap : 0u);
  trailer.arg0 = static_cast<int64_t>(buffer.next.load());
  trailer.arg1 = static_cast<int64_t>(buffer.flushed.load());
  fwrite(&trailer, sizeof(Entry), 1, buffer.file);
  fclose(buffer.file);
  buffer.file = nullptr;
}

inline void RegisterAtExit() {
  Buffer& buffer = Get();
  bool expected = false;
  if (buffer.started.compare_exchange_strong(expected, true))
    std::atexit(&Finish);
}

inline void Put(const Entry& entry) {
  Buffer& buffer = Get();
  const uint32_t slot = buffer.next.fetch_add(1, std::memory_order_relaxed);
  if (slot >= kCapacity)
    buffer.wrapped.store(true, std::memory_order_relaxed);
  {
    base::AutoLock auto_lock(buffer.lock);
    buffer.entries[slot % kCapacity] = entry;
    // Batch append: only every kAppendIntervalNs, never per entry.
    const int64_t now = TicksNowNs();
    if (now - buffer.last_append_ns.load(std::memory_order_relaxed) >=
        static_cast<int64_t>(kAppendIntervalNs)) {
      if (!buffer.file)
        OpenLocked(buffer);
      AppendLocked(buffer);
    }
  }
}

}  // namespace internal

inline bool Enabled() {
  return internal::EnvEnabled();
}

inline uint64_t NowNs() {
  return static_cast<uint64_t>(internal::TicksNowNs());
}

// The single recording entry point. `text` may be null.
inline void Record(Kind kind,
                   uint32_t instance,
                   uint32_t resource,
                   int64_t arg0,
                   int64_t arg1,
                   const char* text,
                   uint32_t flags = 0) {
  if (!Enabled())
    return;
  internal::RegisterAtExit();
  internal::Buffer& buffer = internal::Get();
  if (buffer.finished.load(std::memory_order_relaxed))
    return;
  Entry entry{};
  entry.ts_ns = static_cast<uint64_t>(internal::TicksNowNs());
  entry.pid = static_cast<uint32_t>(base::GetCurrentProcId());
  entry.tid = static_cast<uint32_t>(base::PlatformThread::CurrentId().raw());
  entry.kind = static_cast<uint32_t>(kind);
  entry.flags = flags;
  entry.instance = instance;
  entry.resource = resource;
  entry.arg0 = arg0;
  entry.arg1 = arg1;
  if (text)
    base::strlcpy(entry.text, text, kTextLen);
  internal::Put(entry);
}

inline void RecordS(Kind kind,
                    uint32_t instance,
                    uint32_t resource,
                    int64_t arg0,
                    int64_t arg1,
                    const std::string& text,
                    uint32_t flags = 0) {
  Record(kind, instance, resource, arg0, arg1, text.c_str(), flags);
}

// RAII begin/end pair for operations, so the timeline carries duration and the
// decoder can pair the halves by (pid, tid, kind, resource, arg0).
//
// Convention: the BEGIN entry carries the *inputs* (arg0 = operation, arg1 =
// mode/recursive/destination...), the END entry carries the *result* (arg1 =
// PP_OK / PP_ERROR_*, plus kFlagError when non-zero). Decoders rely on this.
class ScopedOp {
 public:
  ScopedOp(Kind kind,
           uint32_t instance,
           uint32_t resource,
           int64_t arg0,
           int64_t arg1,
           const char* text)
      : enabled_(Enabled()),
        kind_(kind),
        instance_(instance),
        resource_(resource),
        arg0_(arg0),
        arg1_(arg1) {
    if (!enabled_)
      return;
    if (text)
      base::strlcpy(text_, text, kTextLen);
    Record(kind_, instance_, resource_, arg0_, arg1_, text_, kFlagBegin);
  }

  ScopedOp(const ScopedOp&) = delete;
  ScopedOp& operator=(const ScopedOp&) = delete;

  ~ScopedOp() {
    if (!enabled_)
      return;
    Record(kind_, instance_, resource_, arg0_, result_, text_,
           kFlagEnd | (result_ != 0 ? kFlagError : 0u));
  }

  // Record the outcome (PP_OK, or a negative PP_Error value).
  void set_result(int64_t result) { result_ = result; }

 private:
  bool enabled_;
  Kind kind_;
  uint32_t instance_;
  uint32_t resource_;
  int64_t arg0_;
  int64_t arg1_;
  int64_t result_ = 0;
  char text_[kTextLen] = {};
};

inline void Flush() {
  if (!Enabled())
    return;
  internal::Finish();
}

}  // namespace flash_trace

#endif  // PPAPI_TRACE_FLASH_TRACE_H_
