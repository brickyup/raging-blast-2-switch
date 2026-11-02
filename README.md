# Dragon Ball: Raging Blast 2 on Nintendo Switch

An unofficial native Switch port of the Xbox 360 game, made with static recompilation. It is not emulation: the game's
code is translated to C++ and built into a Switch homebrew app, and a native Vulkan renderer draws the game.

Made by [@brickyup on TikTok](https://www.tiktok.com/@brickyup).

**This repository contains no game files, and no ready-to-run download is provided.** The app has to be built from
your own copy of the game, because the finished `rb2.nro` contains code translated from the game itself. Please don't
upload or share built NROs or game files.

## Status

| | |
|---|---|
| Boots, menus, fights | Working (tested on one original Switch, model v1) |
| Menus | 60 fps |
| Fights | Usually 44-56 fps, about 31 fps in the heaviest scene seen so far |
| Local versus | Works; single sideways Joy-Cons not yet tested on a console |
| Online versus | Tested only between two PC builds on one machine, **not yet on two Switches** |

Other Switch models (OLED, Lite, v2) have not been tested. Lite has no docked mode.

## What you need

- A Switch running [Atmosphère](https://github.com/Atmosphere-NX/Atmosphere) custom firmware with the Homebrew Menu.
  This guide doesn't cover installing it; see the [NH Switch Guide](https://nh-server.github.io/switch-guide/).
- A microSD card with about 6 GB free for the game.
- Your own **Xbox 360 Europe disc** of the game, extracted to a folder. Other regions won't work, because the port
  patches addresses in that one executable. The build checks this and stops if `default.xex` doesn't match:
  ```
  SHA-256: a43d7fa4404041deb82ecabb32a22d639b96dac351d4269114a7bc395e1b8973
  ```
- A PC to build on: Windows 10/11 with WSL 2 (Ubuntu), or Linux. The build downloads and compiles a full toolchain, so expect it to take a long time and use a lot of disk space.

## Step 1: set up the build tools

On Windows, install WSL and open Ubuntu as root (in PowerShell):

```bash
wsl --install -d Ubuntu
```

```bash
wsl -u root
```

Then, inside Ubuntu, install the packages and start Docker:

```bash
apt-get update
```

```bash
apt-get install -y git rsync file docker.io cmake ninja-build clang lld python3-pip pkg-config glslang-tools libx11-xcb-dev libwayland-dev libxkbcommon-dev libgtk-3-dev libxrandr-dev libxi-dev libxcursor-dev libxinerama-dev libxss-dev libasound2-dev libpulse-dev libudev-dev libdbus-1-dev libunwind-dev liblz4-dev
```

```bash
service docker start
```

## Step 2: build

Clone this repository inside Ubuntu, not on the Windows drive. Replace `<this repository's URL>` with the address
shown under the green **Code** button on this page:

```bash
cd ~ && git clone <this repository's URL> rb2-switch
```

Run the build. Set `GAME` to your extracted game folder. In WSL, `C:\` is `/mnt/c/`:

```bash
cd ~/rb2-switch && GAME="/mnt/c/Games/Raging Blast 2" bash rb2/switch-p2/build_switch_p2.sh
```

The script downloads the toolchain, translates your `default.xex` and builds everything. If it stops on a network
error, run it again; finished steps are skipped. When it's done you'll have:

```
rb2/switch-p2/out/rb2.nro
rb2/switch-p2/out/rb2.toml
```

**Optional icon:** put a 256x256 `icon.jpg` in `rb2/switch-p2/` before building. Without one, the app gets the default
homebrew icon.

## Step 3: copy to the SD card

```
sdmc:/switch/rb2-nx/
    rb2.nro
    rb2.toml
    game/
        default.xex
        data/
        ...everything else from your extracted game folder
```

## Step 4: run it

Start it with **full application memory**. Hold **R** while launching any installed game, then pick the app in the
Homebrew Menu.

Don't launch it from the Album. That mode gives the app only about 400 MB and it will crash.

The first launch of a new scene can hitch while shaders are built. They are saved and prepared at startup next time.

## Settings

Edit `rb2.toml` next to the NRO, then restart the game. The file explains every option. The ones that matter most:

| Setting | Default | What it does |
|---|---|---|
| `shd_render_width` / `shd_render_height` | 640 / 360 | Internal resolution, upscaled with FSR. Lower is faster. |
| `rb2_cpu_mhz` | 1785 | CPU clock. Set `1020` for the stock game clock (slower, and fights run in slow motion), or `0` to leave it to the system or sys-clk. |
| `rb2_gpu_460` | true | Uses the 460.8 MHz handheld GPU profile. |

The higher clocks draw more power and make more heat, so the battery drains faster. Lower them if your console gets
too warm.

## Multiplayer

**Local:** up to 4 controllers. Pair single Joy-Cons in **HOME > Controllers > Change Grip/Order** first. If a sideways
Joy-Con's stick moves the wrong way, set `input_joycon_rotate_stick = false`.

**Online:** both players need the same build from the same source, and the same game files. A Switch and a PC can't
play each other. There is no public server, so one of you (or a VPS) runs one. The server needs Python 3.8+ and no
other packages:

```bash
python3 rb2/server/rb2_relay.py --port 7777
```

Open UDP port 7777 to that machine. Players don't need port forwarding. In `rb2.toml` on both consoles:

```toml
rb2_netplay = "server"
rb2_netplay_server = "your.server.address:7777"
rb2_netplay_room = "pick-a-code"   # empty = quick match with anyone waiting
```

You can also connect directly without a server: player 1 sets `rb2_netplay = "host"` and forwards UDP 7777 to their
console; player 2 sets `rb2_netplay = "join"` and `rb2_netplay_peer = "<player 1's IP>"`.

If it stutters, raise `rb2_netplay_delay` (1-7 frames) on both consoles. Nothing detects it if the two games drift out
of sync yet.

## Troubleshooting

| Problem | Try this |
|---|---|
| Build says `default.xex` isn't supported | You need the Europe disc's executable, with the hash above. |
| Build can't reach Docker | Run `service docker start` and make sure you're root (`wsl -u root`). |
| Crash right after launch | Launch with full application memory (Step 4), not from the Album. |
| Crash or black screen when the game loads | Check that `game/default.xex` and `game/data/` are in the right place (Step 3). |
| Low frame rate | Check `rb2_cpu_mhz` and `rb2_gpu_460`, then lower the resolution. If sys-clk is installed, make sure it isn't overriding the clocks. |

**Reporting a problem:** open an issue with your console model and firmware, and attach the files from
`sdmc:/switch/rb2-nx/logs` plus `rex_crash.log` and `rex_stderr.log` if you have them. Don't post your keys or game
files.

## Credits

This port stands on other people's work:

- [ReXGlue](https://github.com/rexglue/rexglue-sdk): the static recompilation toolkit
- [masseffect-nx](https://github.com/nebadasSwifty/masseffect-nx): the Switch (Horizon) layer for ReXGlue
- [prototype-switch](https://github.com/fungalspores/prototype-switch) and
  [silent-hill-downpour-switch](https://github.com/fungalspores/silent-hill-downpour-switch): the native renderer,
  SDK and NVK patches, and build tools
- [mgr-nx-deko](https://github.com/tuancapoo/mgr-nx-deko): post-codegen speed passes
- [WistfulHopes/RB2](https://github.com/WistfulHopes/RB2) and
  [RB2-Android](https://github.com/moddedrounds/RB2-Android): the original Raging Blast 2 recompilation work

## License

GPL-3.0 ([LICENSE](LICENSE)). Parts derived from the original RB2 recompilation project are under the BSD 3-Clause
license ([rb2/LICENSE.RB2](rb2/LICENSE.RB2)); `rb2/switch-p2/tools/mgr` keeps its own license file.

This is an unofficial fan project, not affiliated with or endorsed by Nintendo, Microsoft or the owners of Dragon Ball
and Raging Blast 2. All trademarks belong to their owners. You need your own legally obtained copy of the game.
