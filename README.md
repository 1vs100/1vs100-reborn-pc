# 1 vs 100 PC

It is built with the [ReXGlue](https://github.com/rexglue/rexglue-sdk) static recompiler, plus everything around it that the game needs now that its Xbox LIVE service is gone: offline shows with bots, online parties, Avatars, a launcher and tools.

> **Status: private alpha.** It runs and plays full Extended Play shows offline and in Steam parties of up to four. Live (Primetime) mode is experimental. Gameserver mode works on Linux: two clients played a full round together, with ads and the live host audio stream. Expect bugs.

## Features

- Native Windows and Linux build of the Xbox 360 game **1 vs 100** (title 584A07D1).
- Offline Extended Play shows with up to 10,000 bots.
- Steam parties of up to four players: one shared show, scores, Top 10, dancing in the lobby.
- Online play on a community [gameserver](https://github.com/1vs100/gameserver), together with Xbox 360 players on [1vs100-reborn-x360](https://github.com/1vs100/1vs100-reborn-x360).
- Each player's own Avatar is sent to the party. Marketplace items a player lacks show as a similar standard item.
- Avatar Editor integration: the original dashboard editor, including Avatar items imported from your console.
- Original host voice lines from your own show package.
- Keyboard, SDL controllers and a direct USB DualSense backup.
- Debug tab: original development switches (show skins, Host Tool, debug menu), mods.
- No game content in this repository. A copy of the game is required.

## Platform support

| Mode | Windows | Linux |
|---|---|---|
| Offline shows with bots | yes | yes |
| Steam parties | yes | yes, with the Steamworks SDK |
| Gameserver | not yet | yes |

Gameserver mode is Linux only for now. Its socket layer is not implemented for Windows yet.

## What you need

From **your own** Xbox 360 and copy of the game (none of this is provided here):

- The 1 vs 100 game package (`default.xex` and its media).
- For host voice lines: the show package for your language (folder `dlcs/shows`, for example `7BEC809CCC282E8C9D18EFAD8F4407AA6204DFC8` for US English). Copy its `media/audio/vo.xwb` and `vo.xsb` to `assets/Show/media/audio/`.
- For the Avatar Editor: dashboard 2.0.12581.0 files (see `avatar-editor/README.txt`).
- For Avatar items: the item packages from your console's `Content` folder.
- For gameserver mode: the show and episode packages, and the stock retail 2.0.1149.0 game files.

## Starting the game on Linux

Run the executable from its build folder and point it at your game files:

```
cd build/linux
./onevs100 --game_data_root=../../native/assets
```

Without `--game_data_root` the game looks for an `assets` folder next to the executable. Every setting in `onevs100.toml` can also be given on the command line, for example `--online_service=gameserver --gameserver_host=1vs100.example.org`.

## Playing on a gameserver

Gameserver mode runs the game's own online code against a [gameserver](https://github.com/1vs100/gameserver). It needs the stock retail 2.0.1149.0 game files with their original Lua scripts, not the Lua from this repository.

Put these settings in `onevs100.toml` next to the executable:

```
[Online]
online_service = "gameserver"
gameserver_host = "1vs100.example.org"

[Content]
content_install = "/path/to/packages"
```

- `gameserver_host` is the server's host name or IPv4 address.
- `content_install` names a show or episode package, or a folder of them. They are installed when the game starts.
- To run two copies on one PC, give each its own `guest_bind_address`, for example `127.0.0.1` and `127.0.0.2`.

[docs/gameserver-mode.md](docs/gameserver-mode.md) has the developer details.

## Linux notes

- The game window uses SDL's X11 driver. On a Wayland desktop it runs through XWayland. Set `SDL_VIDEO_DRIVER` to override it.
- There is no profile window. Set your gamertag in `profiles/local-name.txt` in the player data folder.
- The intro video overlay, local WAV dialogue and the direct DualSense reader are Windows only. Controllers work through SDL.

## What's here

| Folder | What it is |
|---|---|
| `native/` | The PC side of the game: CMake project for the recompiled executable and our C++ (offline profile, gameserver mode, Steam party transport, Avatar provider and closet, controls, video). |
| `lua/` | Lua we added to the game: `NativeRoom.lua` (shared shows and parties), `NativeDebug.lua` (debug switches, skins), `NativeLiveBots.lua` (Primetime Mob bots). |
| `launcher/` | `1vs100 Launcher.exe` (Windows): Play / Display / Controls / Show / Online / Debug tabs. |
| `tools/` | Launch, packaging, mods (XZP), export, log-collection scripts; `tools/test/` Lua unit tests; `tools/room/` transport tests. |
| `avatar-editor/` | Build script for the [Avatar Editor recompilation](https://github.com/panedivitasei/AvatarEditorRecomp) and the Avatar item (closet) importer. |
| `docs/` | Developer notes, such as gameserver mode. |

## Building

You need:

- Windows 10/11 with Visual Studio 18 (C++ workload, CMake, Ninja) and LLVM/clang 23, or Linux with clang 22, CMake 3.25+ and Ninja.
- A [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk) nightly build for your platform, `nightly-20261002-bd833a2a` or newer. To build against an SDK source tree instead, such as [our fork](https://github.com/1vs100/rexglue-sdk), pass `-DREXSDK_DIR=<rexglue-sdk>` in place of `CMAKE_PREFIX_PATH`.
- Optionally the [Steamworks SDK](https://partner.steamgames.com/) for parties (development app ID 480). Without it the game reports Steam as unavailable.

Steps:

1. Fetch the submodules: `git submodule update --init`
2. Put your game files in `native/assets/` (the folder `onevs100_manifest.toml` points at).
3. Generate the recompiled code from **your** `default.xex`: `rexglue codegen native/onevs100_manifest.toml`
4. Configure and build. On Windows:
   ```
   cmake -S native -B build -G Ninja -DCMAKE_BUILD_TYPE=Release ^
     -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl ^
     -DCMAKE_PREFIX_PATH=<ReXGlue SDK>\win-amd64 -DONEVS100_STEAM_SDK=<Steamworks>\sdk
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
6. For offline and party play, copy the files from `lua/` into the game's `lua` folder.

### Not published yet

Many offline gameplay fixes are edits to the game's **original** Lua scripts (offline service, bot scoring, startup, menus). Those scripts belong to Microsoft, so the edits are not in this repository yet. We are working out a way to publish them as changes you apply to your own copy. Until then, a build from this repository alone will not play a full offline show.

## Tests

Lua unit tests run with any Lua 5.3+ (`texlua` works). They need the game's own `luamockserver.lua` from your copy:

```
texlua tools/test/native-party-test.lua lua/NativeRoom.lua <your game>/lua/luamockserver.lua
texlua tools/test/native-livebots-test.lua lua/NativeLiveBots.lua <your game>/lua/luamockserver.lua
```

## Privacy and services

The build never contacts Xbox LIVE or other Microsoft production services. Parties use Steam networking between friends. Gameserver mode talks only to the server you configure. Your profile, Avatar and stats stay on your PC.

## Licenses

Our code is under the [MIT License](LICENSE). Third-party parts keep their own licenses. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). 1 vs 100, Xbox, Xbox 360 and Xbox LIVE are trademarks of their owners. This is an unofficial fan project, not affiliated with or endorsed by Microsoft or the game's developers.
