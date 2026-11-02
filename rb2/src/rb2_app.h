// rb2 - Dragon Ball: Raging Blast 2, recompiled with ReXGlue (SDK 0.10).
//
// The original project (WistfulHopes/RB2) predates the SDK's 0.2 overhaul and replaced the game's
// heap and file I/O by hand (src/bdengine). SDK 0.10 ships the same thing as its rexcrt layer, so the
// codegen manifest (config/rb2_manifest.toml) maps the game's CRT routines to it instead.

#pragma once

#include <rex/cvar.h>
#include <rex/filesystem.h>

#include <filesystem>
#include <rex/logging.h>
#include <rex/perf/counter.h>
#include <rex/rex_app.h>

class Rb2App : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<Rb2App>(new Rb2App(ctx, "rb2", PPCImageConfig));
  }

  // RB2 always needs the Xenos GPU emulation; --gpu_plugin still overrides it.
  void OnPreSetup(rex::RuntimeConfig& config) override {
    if (config.gpu_plugin.empty()) config.gpu_plugin = "xenos";
  }

  // The Switch has no command line: without --game_data_root, use game/ next to the executable
  // (sdmc:/switch/rb2-nx/game/ on the SD card).
  void OnConfigurePaths(rex::PathConfig& paths) override {
    if (!paths.game_data_root.empty()) return;
    std::error_code ec;
    auto game = rex::filesystem::GetExecutableFolder() / "game";
    if (std::filesystem::is_directory(game, ec)) paths.game_data_root = game;
  }

  // The runtime compiles the per-frame perf-counter CSV writer in, but nothing in the SDK opens
  // the --perf_log_csv file: wire it once logging is up (same as Project8Recomp).
  void OnPostSetup() override {
#ifndef __SWITCH__  // the Switch SDK fork compiles the perf counters out of Release builds
    std::string csv_path = REXCVAR_QUERY(std::string, perf_log_csv);
    if (!csv_path.empty()) {
      rex::perf::SetCsvLogPath(csv_path);
      REXLOG_INFO("perf: csv -> {}", csv_path);
    }
#endif
  }
};
