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

## How to install

This takes one double-click on a Windows PC, plus a few things to get ready first.

### What you need

- **A Switch that can run homebrew** (Atmosphère custom firmware). If yours can't yet, follow the
  [NH Switch Guide](https://nh-server.github.io/switch-guide/) first. Not every Switch can be modded.
- **Your own Xbox 360 Europe copy of the game**, extracted to a folder so you can see `default.xex` and `data` inside.
  If you only have a disc image (`.iso`), extract it first with an Xbox 360 ISO extraction tool. Other regions won't
  work; the installer checks this for you.
- **A Windows 10 or 11 PC** with 16 GB of RAM recommended, a CPU that supports AVX2 (most from about 2015 or newer),
  25 GB of free disk space and an internet connection.
- **A microSD card** with about 6 GB free, in a card reader or your PC's card slot.

### Install it

1. Download `rb2-switch-installer-v0.2.0.zip` from the
   [latest release](https://github.com/brickyup/raging-blast-2-switch/releases/latest).
2. Right-click the ZIP, choose **Extract All**, and open the extracted folder.
3. Double-click **`INSTALL.bat`**. If Windows shows a "protected your PC" message, it's because this file is new and
   unsigned. Click **More info**, then **Run anyway** if you're happy to run it. You can read the script first in
   `rb2/switch-p2/tools/install_windows.ps1`.
4. Pick your game folder when asked.
5. If you don't have Ubuntu on WSL yet (Windows' built-in Linux), type **y**. When that finishes, restart if asked, open
   Ubuntu from the Start menu, choose any username and password, then double-click `INSTALL.bat` again.
6. Wait for the build. It can take 30 minutes or more the first time. Leave the window open.
7. Pick your SD card when asked. The installer copies everything to the right place.

### Play it

1. Put the SD card back in your Switch and turn it on in Atmosphère.
2. **Hold R** while opening any installed game. The Homebrew Menu appears.
3. Pick the app and start it.

Don't start it from the Album: that gives it too little memory and it will crash.

The first time you see a new scene it may hitch briefly while graphics are prepared. It gets smoother after that.

### Updating

Download the newest ZIP from the releases page and run `INSTALL.bat` again. Your settings on the SD card are kept.

### Build it by hand (Linux or advanced users)

Install git, Docker and the other packages listed in `rb2/switch-p2/tools/easy_build.sh`, start Docker, then:

```bash
git clone https://github.com/brickyup/raging-blast-2-switch rb2-switch
```

```bash
cd rb2-switch && GAME="/path/to/your/game" bash rb2/switch-p2/build_switch_p2.sh
```

The results are `rb2.nro` and `rb2.toml` in `rb2/switch-p2/out/`. Put a 256x256 `icon.jpg` in `rb2/switch-p2/` first if
you want a custom icon. On the SD card, the files go here:

```
sdmc:/switch/rb2-nx/
    rb2.nro
    rb2.toml
    game/
        default.xex
        data/
        ...everything else from your extracted game folder
```

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
| "default.xex isn't in that folder" | Pick the folder that directly contains `default.xex`. If you only have an `.iso`, extract it first. |
| "This default.xex isn't supported" | Only the Xbox 360 Europe disc works. |
| WSL won't install | Virtualization may be turned off in your PC's BIOS. Search your PC model plus "enable virtualization". |
| The build fails | Run `INSTALL.bat` again, since network errors are common. If it still fails, open an issue and attach `build.log`. |
| Crash right after launch | Hold R while opening a game (see Play it). Don't use the Album. |
| Crash or black screen when the game loads | Check that `switch/rb2-nx/game/default.xex` and `game/data` are on the SD card. |
| Low frame rate | Check `rb2_cpu_mhz` and `rb2_gpu_460` in Settings, then lower the resolution. If sys-clk is installed, make sure it isn't overriding the clocks. |

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
