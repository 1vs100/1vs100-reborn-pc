# 1 vs 100 PC

The Xbox 360 game 1 vs 100, rebuilt for the PC with the [ReXGlue](https://github.com/rexglue/rexglue-sdk) static recompiler, plus everything around it that the game needs now that its Xbox LIVE service is gone: gameserver play, Avatars, a launcher and tools.

> **Status: private alpha.** Gameserver mode works on Linux: two clients played a full round together, with ads and the live host audio stream. On Windows it reaches the lobby and plays a round, tested so far under Wine. Expect bugs.

## Features

- Native Windows and Linux build of the Xbox 360 game **1 vs 100** (title 584A07D1).
- Online play on a community [gameserver](https://github.com/1vs100/gameserver), together with Xbox 360 players on [1vs100-reborn-x360](https://github.com/1vs100/1vs100-reborn-x360).
- Each player's own Avatar is sent through the gameserver. Marketplace items a player lacks show as a similar standard item.
- Original host voice lines from your own show package.
- Keyboard, SDL controllers and a direct USB DualSense backup.
- Debug tab: original development switches (show skins, Host Tool, debug menu), mods.
- No game content in this repository. A copy of the game is required.

## Platform support

| Mode | Windows | Linux |
|---|---|---|
| Gameserver | yes | yes |

## Setup

You need files from your own Xbox 360. This repository ships no game content.

1. Put the 1 vs 100 game files where the executable finds them. Put the untouched game package in the `content/` folder next to the executable (used as-is, no unpacking), or unpack `default.xex` and its media into an `assets/` folder next to the built executable. You can also pass `--game_data_root=<folder>`.
2. Put the avatar pack table in the same `content/` folder. Use either `AvatarAssetPack.toc` or the `FFFE07DF00000002` package it comes in. The game needs it to start. To make yours, create an account at [360sona.com](https://360sona.com) and open **My Sonas > your avatar > Import / Export** in the editor. Copy the 360sona avatar text and paste it on the launcher's Avatar tab, or save the description (.bin) and open that file there. Without the launcher, save the description as `profiles/avatar.bin` in the player data folder, otherwise the game builds one from your profile id. The asset files come from Xbox 360 System Update 17559 ([CD + USB download](https://archive.org/details/xbox-360-system-update-17559-cd-usb)).
3. Host voice lines need no copying: with the show package in `content`, the game reads `vo.xwb`/`vo.xsb` from the package itself.
4. For online play you need the stock retail 2.0.1149.0 game files with their original Lua scripts, plus the show and episode packages. Put these settings in `onevs100.toml` next to the executable:
```
# onevs100.toml uses flat keys (no [sections]): each line is `name = value`.
online_service = "gameserver"
gameserver_host = "1vs100.example.org"
content = "/path/to/packages"
```
`gameserver_host` is the server's host name or IPv4 address. `content` names the game package, a show or episode package file, or a folder of them. It defaults to `content/` next to the executable. The game uses them in place at startup, never extracted. To run two copies on one PC, give each its own `guest_bind_address`, for example `127.0.0.1` and `127.0.0.2`.
5. Run the game:
```
cd build/linux-amd64
./onevs100
```
Every setting in `onevs100.toml` also works as a command line flag, for example `--online_service=gameserver --gameserver_host=1vs100.example.org`.
6. Set your gamertag. On Linux there is no profile window, so write it in `profiles/local-name.txt` in the player data folder. On Windows use `Edit profile`, or pass `--edit_profile` (Windows only). Names take 1-15 characters: letters, numbers, spaces, `_` or `-`, with no leading or trailing space.
7. The player data folder is `userdata/` next to the executable, whether you run `onevs100` directly or press Launch. A relative `content` path resolves against the executable folder. Give avatar paths as absolute paths.

The game also reads these environment variables: `ONEVS100_PLAYER_DATA` (player data folder), `ONEVS100_PROFILE_SLOT` (second copy identity), `ONEVS100_PROFILE_XUID` (forced profile id, hex).

## Controls and settings

Any standard controller works through SDL out of the box: Xbox, DualSense/DualShock (via HIDAPI), Switch Pro and generic DirectInput pads, with rumble. On Windows you can use the native XInput driver instead with `input_backend = "xinput"`.

Keyboard play is off by default. Turn it on with `mnk_mode = true` (below). Defaults:

| Pad | Keys |
|---|---|
| Left stick | `W` `A` `S` `D` |
| Right stick | Arrow keys, or the mouse with `mnk_mouse = true` (`mnk_sensitivity`, 0.01–10) |
| A / B / X / Y | `;` or `Space` / `'` or `Backspace` / `L` / `P` |
| LB / RB | `1` / `3` |
| LT / RT | `Q` or `I` / `E` or `O` |
| LS / RS press | `F` / `K` |
| D-pad | `Shift` + arrows |
| Back / Start | `Z` or `Tab` / `X` or `Enter` |

Every binding is remappable with a `keybind_*` setting (e.g. `keybind_a = "Space"`), same names as the table.

Windows only: `ONEVS100_DUALSENSE_HID=1` reads a USB DualSense directly as a backup if SDL doesn't claim the pad (close DS4Windows first), and `ONEVS100_MOUSE_PANEL=1` adds a small always-on-top window of clickable answer/menu buttons. Both merge into player 1's input while the game window has focus.

### Settings reference

`onevs100.toml` next to the executable holds flat `name = value` keys; every key also works as a `--name=value` flag. The launcher sets `gameserver_host`, `fullscreen` and the keyboard keys itself. Its "Show all settings" switch lists every other key for editing. Hand-written comments do not survive a Launch.

| Setting | Default | What it does |
|---|---|---|
| `online_service` / `gameserver_host` / `guest_bind_address` | `gameserver` / unset | server play; server host/IP; second copy's bind IP |
| `content` | `content` | game/show/episode package(s), used in place |
| `fullscreen` | `true` | fullscreen or windowed |
| `mnk_mode` / `mnk_mouse` / `mnk_sensitivity` / `mnk_passthrough` | off | keyboard-pad emulation (above) |
| `input_backend` | `sdl` | `sdl` or (Windows) `xinput` |
| `keybind_*` | see table | keyboard remap |
| `audio_backend` | `sdl` | audio driver |
| `lua_print_log` | `false` | write the title's Lua `print` output to the log |

Environment overrides: `ONEVS100_PLAYER_DATA`, `ONEVS100_PROFILE_SLOT`, `ONEVS100_PROFILE_XUID`, `ONEVS100_DUALSENSE_HID`, `ONEVS100_MOUSE_PANEL`, and `SDL_VIDEO_DRIVER` on Linux. Gamertag and avatar live in `userdata/profiles/` (`local-name.txt`, `avatar.bin`). The remaining `vulkan_*` / `d3d12_*` / `gpu_*` knobs come from the ReXGlue SDK and keep their tested defaults; the debug-show switches (skins, Host Tool, debug menu) live in `lua/NativeDebug.lua` and the dev launcher's Debug tab.
[docs/gameserver-mode.md](docs/gameserver-mode.md) has the developer details.

### Performance switches

These run parts of the game as native code or change how frames are handed over. The defaults are the tested setup. If you see glitches, turn one off at a time to find the cause.

| Setting | Default | What it does |
|---|---|---|
| `guest_crt_hooks` | `true` | runs the game's memory copy, memory fill, floor and ceil as native code |
| `guest_lib_hooks` | `true` | runs the game's string compare, matrix multiply and reference counting as native code |
| `guest_atomic_window_lock` | `false` | takes the global lock around the game's atomic updates, as the original code did |
| `guest_keep_flush_mode` | `true` | keeps tiny float values flushed to zero all the time instead of switching per code block |
| `gpu_queued_frames` | `3` | frames the game may finish ahead of the renderer, from 1 to 4 |
| `gpu_prepare_workers` | `3` | threads that prepare draws for the renderer |
| `gpu_skip_guest_draw` | `false` | skips the game's own draw packet building; it hides the avatars, so leave it off |

## Linux notes

- The game window uses SDL's X11 driver. On a Wayland desktop it runs through XWayland. Set `SDL_VIDEO_DRIVER` to override it.
- There is no profile window. Set your gamertag in `profiles/local-name.txt` in the player data folder.
- The intro video overlay, local WAV dialogue and the direct DualSense reader are Windows only. Controllers work through SDL.

## What's here

| Folder | What it is |
|---|---|
| `native/` | The PC side of the game: CMake project for the recompiled executable and our C++ (gameserver mode, Avatar provider, controls, video). |
| `lua/` | Lua we added to the game: `NativeDebug.lua` (debug switches, skins). |
| `launcher/` | The player launcher for Windows and Linux: gamertag, avatar, gameserver host, windowed mode, keyboard controls. |
| `tools/dev-launcher/` | `1vs100 Launcher.exe` (Windows, development): Play / Display / Controls / Show / Online / Debug tabs. |
| `tools/` | Launch, packaging, mods (XZP), export, log-collection scripts; `tools/test/` Lua unit tests. |
| `docs/` | Developer notes, such as gameserver mode. |

## Building

You need:

- Windows 10/11 with Visual Studio 18 (C++ workload, CMake, Ninja) and LLVM/clang 22.1.8, or Linux with clang 22.1.8, CMake 3.28.3 (`pip install cmake==3.28.3`) and Ninja. Releases build in the `tools/ci-image` Ubuntu 24.04 container, which holds this toolchain and the system/wine build deps (ccache, the MSVC install, the xwin splat and the SDL3-uwp prefix are still provisioned per build); run it through Podman so your checkout matches CI.
- The [ReXGlue SDK](https://github.com/1vs100/rexglue-sdk) arrives as a pinned submodule in `rexglue-sdk/`. Pass it with `-DREXSDK_DIR=$PWD/rexglue-sdk`; a prebuilt SDK package does not work, the in-place content support needs the newer source.

Steps:

1. Fetch the submodules, including the SDK's own: `git submodule update --init --recursive`
2. Put your game files in `native/assets/` (the folder `onevs100_manifest.toml` points at).
3. Generate the recompiled code from **your** `default.xex`: `rexglue codegen native/onevs100_manifest.toml`
4. Configure and build. On Windows:
   ```
    cmake -S native -B build -G Ninja -DCMAKE_BUILD_TYPE=Release ^
      -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl ^
      -DREXSDK_DIR=$PWD/rexglue-sdk ^
      -DCMAKE_DISABLE_PRECOMPILE_HEADERS=ON
   cmake --build build --target onevs100
   ```
   On Linux:
   ```
    cmake -S native -B build/linux-amd64 -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
      -DREXSDK_DIR=$PWD/rexglue-sdk \
      -DCMAKE_DISABLE_PRECOMPILE_HEADERS=ON
   cmake --build build/linux-amd64 --target onevs100
   ```
5. Development launcher (Windows): `tools\dev-launcher\build_launcher.cmd` (set `LLVM_BIN` or put clang-cl on PATH).
6. For debug options, copy the files from `lua/` into the game's `lua` folder.

### Not published yet

Gameserver mode needs the stock retail 2.0.1149.0 game files with their original Lua scripts.

## Tests

Lua unit tests run with any Lua 5.3+ (`texlua` works). They need the game's own `luamockserver.lua` from your copy:

```
texlua tools/test/native-storage-test.lua <your game>/lua/luamockserver.lua
```

## Privacy and services

The build never contacts Xbox LIVE or other Microsoft production services. Gameserver mode talks only to the server you configure. Your profile, Avatar and stats stay on your PC.

## Licenses

Our code is under the [MIT License](LICENSE). Third-party parts keep their own licenses. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). 1 vs 100, Xbox, Xbox 360 and Xbox LIVE are trademarks of their owners. This is an unofficial fan project, not affiliated with or endorsed by Microsoft or the game's developers.
