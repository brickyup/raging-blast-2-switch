// Settings the p2 renderer reads whose owners were game modules in the Silent Hill port, plus RB2's own switches.
#include <string>

#include <rex/cvar.h>

REXCVAR_DEFINE_STRING(rb2_renderer, "p2", "RB2",
                      "\"p2\" = native Vulkan renderer (fast), \"xenos\" = the SDK's Xbox 360 GPU emulation (slow)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_BOOL(rb2_gpu_460, true, "RB2",
                    "Request Nintendo's official 460.8 MHz handheld GPU profile (not an overclock)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

REXCVAR_DEFINE_UINT32(rb2_cpu_mhz, 1785, "RB2",
                      "CPU clock while the game runs: 1785 = the top of Nintendo's own clock table (what the console "
                      "uses while loading), 1020 = the stock game clock, 0 = leave it to the system or sys-clk. "
                      "Fights need the CPU, not the GPU")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// Internal render resolution of the native renderer: only its Vulkan images are smaller, the game keeps its
// own 1280x720 coordinates (native_p2/native_render_scale.cpp).
REXCVAR_DEFINE_UINT32(shd_render_width, 720, "RB2",
                      "Internal render width (game: 1280). Multiple of 80, 640..1280; restart required")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_UINT32(shd_render_height, 405, "RB2",
                      "Internal render height, 360..720 (the renderer derives the exact 16:9 height from the width)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
