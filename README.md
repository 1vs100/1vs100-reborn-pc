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

1. Put the 1 vs 100 game files where the executable finds them. Copy `default.xex` and its media into `native/assets/` before building, or into an `assets/` folder next to the built executable. You can also pass `--game_data_root=<folder>`.
2. Point the game at your avatar content. Set `ONEVS100_NATIVE_AVATAR_CONTENT` to the content folder holding `0000000000000000/FFFE07DF/...`, to the `AvatarAssetPack.toc` file itself, or to the `FFFE07DF00000002` package from a dashboard system update. Without it the game runs with no avatars. Make yours at [360sona.com](https://360sona.com) (**My Sonas → Import / Export → Save the description (.bin)**) and save it as `profiles/avatar.bin` in the player data folder, otherwise the game builds one from your profile id. The asset files come from Xbox 360 System Update 17559 ([CD + USB download](https://archive.org/details/xbox-360-system-update-17559-cd-usb)).
3. For host voice lines, copy `vo.xwb` and `vo.xsb` from your show package (`dlcs/shows/<id>/media/audio/`, for example `7BEC809CCC282E8C9D18EFAD8F4407AA6204DFC8` for US English) to `assets/Show/media/audio/`.
4. For online play you need the stock retail 2.0.1149.0 game files with their original Lua scripts, plus the show and episode packages. Put these settings in `onevs100.toml` next to the executable:
```
[Online]
online_service = "gameserver"
gameserver_host = "1vs100.example.org"

[Content]
content_install = "/path/to/packages"
```
`gameserver_host` is the server's host name or IPv4 address. `content_install` names a show or episode package file, or a folder of them. The game installs them at startup. To run two copies on one PC, give each its own `guest_bind_address`, for example `127.0.0.1` and `127.0.0.2`.
5. Run the game:
```
cd build/linux
./onevs100
```
Every setting in `onevs100.toml` also works as a command line flag, for example `--online_service=gameserver --gameserver_host=1vs100.example.org`.
6. Set your gamertag. On Linux there is no profile window, so write it in `profiles/local-name.txt` in the player data folder. On Windows use `Edit profile`, or pass `--edit_profile`.

[docs/gameserver-mode.md](docs/gameserver-mode.md) has the developer details.
## Linux notes

- The game window uses SDL's X11 driver. On a Wayland desktop it runs through XWayland. Set `SDL_VIDEO_DRIVER` to override it.
- There is no profile window. Set your gamertag in `profiles/local-name.txt` in the player data folder.
- The intro video overlay, local WAV dialogue and the direct DualSense reader are Windows only. Controllers work through SDL.

## What's here

| Folder | What it is |
|---|---|
| `native/` | The PC side of the game: CMake project for the recompiled executable and our C++ (gameserver mode, Avatar provider, controls, video). |
| `lua/` | Lua we added to the game: `NativeDebug.lua` (debug switches, skins). |
| `launcher/` | `1vs100 Launcher.exe` (Windows): Play / Display / Controls / Show / Online / Debug tabs. |
| `tools/` | Launch, packaging, mods (XZP), export, log-collection scripts; `tools/test/` Lua unit tests. |
| `docs/` | Developer notes, such as gameserver mode. |

## Building

You need:

- Windows 10/11 with Visual Studio 18 (C++ workload, CMake, Ninja) and LLVM/clang 23, or Linux with clang 22, CMake 3.25+ and Ninja.
- A [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk) nightly build for your platform, `nightly-20261002-bd833a2a` or newer. To build against an SDK source tree instead, such as [our fork](https://github.com/1vs100/rexglue-sdk), pass `-DREXSDK_DIR=<rexglue-sdk>` in place of `CMAKE_PREFIX_PATH`.

Steps:

1. Fetch the submodules: `git submodule update --init`
2. Put your game files in `native/assets/` (the folder `onevs100_manifest.toml` points at).
3. Generate the recompiled code from **your** `default.xex`: `rexglue codegen native/onevs100_manifest.toml`
4. Configure and build. On Windows:
   ```
   cmake -S native -B build -G Ninja -DCMAKE_BUILD_TYPE=Release ^
     -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl ^
     -DCMAKE_PREFIX_PATH=<ReXGlue SDK>\win-amd64
   cmake --build build --target onevs100
   ```
   On Linux:
   ```
   cmake -S native -B build/linux -G Ninja -DCMAKE_BUILD_TYPE=Release \
     -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
     -DCMAKE_PREFIX_PATH=<ReXGlue SDK linux-amd64 folder>
   cmake --build build/linux --target onevs100
   ```
5. Launcher (Windows): `launcher\build_launcher.cmd` (set `LLVM_BIN` or put clang-cl on PATH).
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
