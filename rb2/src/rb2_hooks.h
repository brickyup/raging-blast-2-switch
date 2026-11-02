// Raging Blast 2 game hooks (one translation unit includes this: the app registration file).
//
// From WistfulHopes/RB2 src/hooks.cpp, ported to the SDK 0.10 hook API. The CRT replacements
// (memcpy/memset/strings, heap, file I/O) moved to the manifest's [entrypoint.rexcrt] table; the
// printf-family hooks were dropped (they passed guest va_lists to the host CRT).

#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <thread>
#include <unordered_map>

#include <rex/cvar.h>
#include <rex/hook.h>

#include "rb2_netplay.h"

REXCVAR_DEFINE_BOOL(rb2_vsync_timer, true, "RB2",
                    "Replace SpGameServer::_vsyncWait with a sleep to the next 1/rb2_vsync_hz "
                    "boundary (the original project's frame-rate fix); false = the game's own wait");
REXCVAR_DEFINE_UINT32(rb2_vsync_hz, 60, "RB2", "Tick rate of the replaced vsync wait");

REXCVAR_DEFINE_BOOL(rb2_trace_loads, false, "RB2",
                    "Log the middleware's asynchronous file loads (path, destination, status)");

extern "C" REX_FUNC(__imp__sub_8249A410);
extern "C" REX_FUNC(__imp__sub_824680B8);
extern "C" REX_FUNC(__imp__sub_82467EE8);

// Boot loader: sub_823E24F0 starts an asynchronous load (sub_824680B8(loader, path, 0, dest)) and
// polls sub_82467EE8(loader) until it is not 2 (busy); 4 = error, which makes the game's init fail.
REX_HOOK_RAW(sub_824680B8) {
  REX_FUNC_PROLOGUE();
  if (REXCVAR_GET(rb2_trace_loads)) {
    const char* path = ctx.r4.u32 ? reinterpret_cast<const char*>(base + ctx.r4.u32) : "(null)";
    REXLOG_INFO("rb2 load: loader={:08X} path='{}' r5={:08X} r6={:08X}", ctx.r3.u32, path,
                ctx.r5.u32, ctx.r6.u32);
  }
  __imp__sub_824680B8(ctx, base);
}

REX_HOOK_RAW(sub_82467EE8) {
  REX_FUNC_PROLOGUE();
  const uint32_t loader = ctx.r3.u32;
  __imp__sub_82467EE8(ctx, base);
  if (REXCVAR_GET(rb2_trace_loads)) {
    static thread_local uint32_t last = 0xFFFFFFFFu;
    if (ctx.r3.u32 != last) {
      REXLOG_INFO("rb2 load: loader={:08X} status {}", loader, ctx.r3.u32);
      last = ctx.r3.u32;
    }
  }
  // Online versus: a finished load is reported as still busy (2) until the other machine has finished it too.
  auto& net = rb2::netplay::Get();
  if (net.stepping()) {
    static std::mutex mutex;
    static std::unordered_map<uint32_t, uint32_t> ordinal_of;  // loader -> ordinal of its current completion
    std::lock_guard<std::mutex> lock(mutex);
    if (ctx.r3.u32 == 2) {
      ordinal_of.erase(loader);  // busy again: a new request on this loader
    } else {
      auto [it, first] = ordinal_of.try_emplace(loader, 0);
      if (first) it->second = net.CountLoad();
      if (!net.LoadReleased(it->second)) ctx.r3.u64 = 2;
    }
  }
}

#if defined(__ANDROID__)
// android/native/src/android_main.cpp: blocks while the app is in the background.
extern "C" void rex_wait_while_in_background();
#endif
#if defined(__SWITCH__)
extern "C" uint32_t svcGetInfo(uint64_t* out, uint32_t id0, uint32_t handle, uint64_t id1);  // libnx
#endif

// SpGameServer::_vsyncWait
REX_HOOK_RAW(sub_8249A410) {
  REX_FUNC_PROLOGUE();
#if defined(__ANDROID__)
  rex_wait_while_in_background();
#endif
  if (!REXCVAR_GET(rb2_vsync_timer)) {
    __imp__sub_8249A410(ctx, base);
    return;
  }
  const uint32_t hz = REXCVAR_GET(rb2_vsync_hz) ? REXCVAR_GET(rb2_vsync_hz) : 60;
  const auto interval = std::chrono::nanoseconds(1000000000LL / hz);
  // Pace to one interval after the previous frame instead of the next fixed tick: a frame that runs a
  // little over 1/60 s then costs only its own time, not a whole extra tick (60 -> 30 fps, half-speed
  // game logic). A frame that is already late starts the next one at once, without banking the debt.
  static std::chrono::steady_clock::time_point deadline{};
  // Where a frame's time goes on the game thread: work since the last wait vs. time slept here (log every 600 frames).
  static std::chrono::steady_clock::time_point last_end{};
  static int64_t work_ns = 0, sleep_ns = 0, frames = 0, worst_ns = 0;
  const auto now = std::chrono::steady_clock::now();
  if (last_end.time_since_epoch().count()) {
    const int64_t w = std::chrono::duration_cast<std::chrono::nanoseconds>(now - last_end).count();
    work_ns += w;
    worst_ns = std::max(worst_ns, w);
  }
  deadline += interval;
  if (deadline > now) {
    std::this_thread::sleep_until(deadline);
  } else {
    deadline = now;
  }
  last_end = std::chrono::steady_clock::now();
  sleep_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(last_end - now).count();
  if (++frames == 600) {
    REXLOG_WARN("[frame] game thread per frame: work {:.2f} ms (worst {:.2f}), paced sleep {:.2f} ms",
                work_ns / 600e6, worst_ns / 1e6, sleep_ns / 600e6);
#if defined(__SWITCH__)
    // CPU time, not wall time: tells real game-thread work from time blocked inside "work" (19.2 MHz ticks).
    static uint64_t last_ticks = 0;
    uint64_t ticks = 0;
    svcGetInfo(&ticks, 0xF0000002u /* InfoType_ThreadTickCount */, 0xFFFF8000u /* current thread */, ~0ull);
    REXLOG_WARN("[frame] game thread CPU {:.2f} ms per frame", (ticks - last_ticks) / 600.0 / 19200.0);
    last_ticks = ticks;
#endif
    work_ns = sleep_ns = frames = worst_ns = 0;
  }
}

// --- Middleware error trace (rb2_trace_cri) --------------------------------------------------
// The CRI ADX / file-system libraries report problems by passing message strings to a handful of
// error sinks (found by mapping the "E2005..."-style strings to their callees). With the cvar on,
// every distinct message is logged (first 20 times each).
REXCVAR_DEFINE_BOOL(rb2_trace_cri, false, "RB2",
                    "Log the CRI ADX / file-system middleware's error messages");

#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>

namespace rb2 {
inline const char* GuestStr(uint8_t* base, uint32_t addr) {
  if (addr < 0x40000000u || addr >= 0xA0000000u) return nullptr;
  const char* s = reinterpret_cast<const char*>(base + addr);
  for (int i = 0; i < 4; ++i)
    if (static_cast<unsigned char>(s[i]) < 0x20 || static_cast<unsigned char>(s[i]) > 0x7E)
      return nullptr;
  return s;
}
inline void TraceCri(const char* sink, uint8_t* base, uint32_t a, uint32_t b, uint32_t c) {
  const char* s[3] = {GuestStr(base, a), GuestStr(base, b), GuestStr(base, c)};
  if (!s[0] && !s[1] && !s[2]) return;
  std::string msg;
  for (auto* p : s) {
    if (!p) continue;
    if (!msg.empty()) msg += " | ";
    msg.append(p, strnlen(p, 200));
  }
  static std::mutex m;
  static std::unordered_map<std::string, uint32_t> seen;
  uint32_t n;
  {
    std::lock_guard<std::mutex> lock(m);
    n = ++seen[msg];
  }
  if (n <= 20) REXLOG_WARN("rb2 cri {} #{}: {}", sink, n, msg);
}
}  // namespace rb2

#define RB2_CRI_SINK(addr)                                                      \
  extern "C" REX_FUNC(__imp__sub_##addr);                                       \
  REX_HOOK_RAW(sub_##addr) {                                                    \
    REX_FUNC_PROLOGUE();                                                        \
    if (REXCVAR_GET(rb2_trace_cri))                                             \
      rb2::TraceCri(#addr, base, ctx.r3.u32, ctx.r4.u32, ctx.r5.u32);           \
    __imp__sub_##addr(ctx, base);                                               \
  }

RB2_CRI_SINK(8246A0E8)  // criErr notify (151 messages)
RB2_CRI_SINK(82471A70)  // ADXERR 1-string
RB2_CRI_SINK(82471AF8)  // ADXERR 2-string
RB2_CRI_SINK(8247A758)
RB2_CRI_SINK(82478968)
RB2_CRI_SINK(8246E930)  // SVM
RB2_CRI_SINK(82476380)  // "IXAudio2SourceVoice_SubmitSourceBuffer failed" (message in r5)
RB2_CRI_SINK(8246E870)

#include "rb2_resolution.h"
