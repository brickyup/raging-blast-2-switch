// Internal render resolution.
//
// Raging Blast 2 renders at a fixed 1280x720. The renderer setup (sub_823D9E20) stores 1280 / 720 as
// its render size (renderer +24 / +28, also the back buffer size handed to the D3D device), and every
// full-screen surface is built by one of two constructors called with literal 1280 / 720:
//   sub_823D2538(obj, width, height, type, count)        - render targets (5 call sites)
//   sub_823D26F0(obj, ?, width, height, format, levels)  - textures (4 call sites)
// Scaling both, plus the stored render size (mid-function hook RB2RenderSizeHook on the
// "stw r9,24(r31)" at 0x823D9F14, added by tools/android/mkmanifest.py), renders the whole frame
// smaller; the presenter scales the guest front buffer to the screen (FSR 1 or bilinear).
// Only exact 1280x720 requests change: smaller buffers (bloom, shadows, UI) keep their sizes.

#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging/macros.h>

REXCVAR_DEFINE_INT32(rb2_render_scale, 100, "RB2",
                     "Internal render resolution in percent of the game's 1280x720 (50-100), "
                     "applied at startup")
    .range(50, 100);

namespace rb2::resolution {

inline bool ScaledSize(uint32_t width, uint32_t height, uint32_t* out_width, uint32_t* out_height) {
  const int scale = REXCVAR_GET(rb2_render_scale);
  if (scale >= 100 || width != 1280 || height != 720) {
    return false;
  }
  // Multiples of 16 keep the EDRAM tiles (80x16 samples) and resolve copies aligned; 16:9 within 1 %.
  *out_width = std::max<uint32_t>(640, (1280 * uint32_t(scale) / 100 + 8) & ~15u);
  *out_height = std::max<uint32_t>(360, (720 * uint32_t(scale) / 100 + 8) & ~15u);
  return true;
}

inline void Note(const char* what, uint32_t w, uint32_t h, uint32_t nw, uint32_t nh) {
  static std::atomic<int> logged{0};
  if (logged.fetch_add(1) < 16) {
    REXLOG_INFO("render resolution: {} {}x{} -> {}x{}", what, w, h, nw, nh);
  }
}

}  // namespace rb2::resolution

// The render size stored by the renderer setup (r9 = width, r8 = height before "stw r9,24(r31)").
void RB2RenderSizeHook(PPCRegister& r9, PPCRegister& r8) {
  uint32_t w, h;
  if (rb2::resolution::ScaledSize(r9.u32, r8.u32, &w, &h)) {
    rb2::resolution::Note("renderer size", r9.u32, r8.u32, w, h);
    r9.u64 = w;
    r8.u64 = h;
  }
}

extern "C" REX_FUNC(__imp__sub_823D2538);
REX_HOOK_RAW(sub_823D2538) {
  REX_FUNC_PROLOGUE();
  uint32_t w, h;
  if (rb2::resolution::ScaledSize(ctx.r4.u32, ctx.r5.u32, &w, &h)) {
    rb2::resolution::Note("render target", ctx.r4.u32, ctx.r5.u32, w, h);
    ctx.r4.u64 = w;
    ctx.r5.u64 = h;
  }
  __imp__sub_823D2538(ctx, base);
}

extern "C" REX_FUNC(__imp__sub_823D26F0);
REX_HOOK_RAW(sub_823D26F0) {
  REX_FUNC_PROLOGUE();
  uint32_t w, h;
  if (rb2::resolution::ScaledSize(ctx.r5.u32, ctx.r6.u32, &w, &h)) {
    rb2::resolution::Note("texture", ctx.r5.u32, ctx.r6.u32, w, h);
    ctx.r5.u64 = w;
    ctx.r6.u64 = h;
  }
  __imp__sub_823D26F0(ctx, base);
}
