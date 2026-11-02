// Raging Blast 2 for Nintendo Switch with the native "p2" renderer (from the Prototype 2 / Silent Hill Switch
// ports, GPL-3.0): the game's Direct3D ring is drawn straight with Vulkan, without the SDK's Xenos emulation.
#pragma once

#include "native_p2/p2_native_system.h"

#include <filesystem>
#include <string>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/rex_app.h>

#include <rex/ui/switch_apm.h>

REXCVAR_DECLARE(std::string, render_target_path_vulkan);
REXCVAR_DECLARE(std::string, rb2_renderer);
REXCVAR_DECLARE(bool, rb2_gpu_460);
REXCVAR_DECLARE(uint32_t, rb2_cpu_mhz);
extern "C" void Rb2RequestCpuHz(uint32_t hz);  // switch_release_perf.cpp
void proto_ApplySpeedSettings();  // proto_speed.cpp

class Rb2SwitchApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<Rb2SwitchApp>(new Rb2SwitchApp(ctx, "rb2", PPCImageConfig));
  }

  // Without a gpu_plugin the Vd* kernel calls are ignored and nothing renders.
  void OnPostInitLogging() override {
    if (!rex::cvar::HasNonDefaultValue("gpu_plugin")) {
      rex::cvar::SetFlagByName("gpu_plugin", "xenos");
    }
  }

  void OnPreSetup(rex::RuntimeConfig& config) override {
    proto_ApplySpeedSettings();
    // Nintendo's official 460.8 MHz handheld GPU profile (+50 % GPU; docked is already faster). Not an overclock.
    if (REXCVAR_GET(rb2_gpu_460)) {
      RexSwitchApmRequestGpuMhz(460);
      RexSwitchApmApply();
    }
    Rb2RequestCpuHz(REXCVAR_GET(rb2_cpu_mhz) * 1000000u);
    if (REXCVAR_GET(rb2_renderer) == "p2" && p2::native::Enabled()) {
      config.graphics = p2::native::CreateGraphicsSystem();
      REXLOG_INFO("RB2 Switch renderer: native (Prototype 2 renderer)");
    } else {
      REXLOG_INFO("RB2 Switch renderer: SDK Xenos emulation");
    }
    if (rex::cvar::GetFlagSource("render_target_path_vulkan") == rex::cvar::Source::kDefault &&
        REXCVAR_GET(render_target_path_vulkan).empty()) {
      REXCVAR_SET(render_target_path_vulkan, std::string("fsi"));
    }
  }

  // The extracted disc lives in game/ next to the NRO (sdmc:/switch/rb2-nx/game).
  void OnConfigurePaths(rex::PathConfig& paths) override {
    if (!paths.game_data_root.empty()) return;
    std::error_code ec;
    auto dir = rex::filesystem::GetExecutableFolder() / "game";
    if (std::filesystem::is_directory(dir, ec)) {
      paths.game_data_root = dir;
      return;
    }
    // Some NRO loaders (forwarders) give no argv[0].
    if (std::filesystem::is_directory("/switch/rb2-nx/game", ec)) paths.game_data_root = "/switch/rb2-nx/game";
  }
};
