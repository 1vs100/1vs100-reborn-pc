# 1 vs 100 PC

A native Windows build of the Xbox 360 game **1 vs 100** (title 584A07D1), made with the
ReXGlue static recompiler, plus everything around it that
the game needs now that its Xbox LIVE service is gone: offline shows with bots, online
parties over Steam, Avatars, a launcher and tools.

> **Status: private alpha.** It runs and plays full Extended Play shows offline and in
> Steam parties of up to four. Live (Primetime) mode is experimental. Expect bugs.

**No game content is in this repository.** You need your own copy of 1 vs 100 and
build it yourself. See [What you need](#what-you-need).

## What's here

| Folder | What it is |
|---|---|
| `native/` | The PC side of the game: CMake project for the recompiled executable and our C++ (offline profile, Steam party transport, Avatar provider and closet, controls, video). |
| `lua/` | Lua we added to the game: `NativeRoom.lua` (shared shows and parties), `NativeDebug.lua` (debug switches, skins), `NativeLiveBots.lua` (Primetime Mob bots). |
| `launcher/` | `1vs100 Launcher.exe`: Play / Display / Controls / Show / Online / Debug tabs. |
| `tools/` | Launch, packaging, mods (XZP), export, log-collection scripts; `tools/test/` Lua unit tests; `tools/room/` transport tests. |
| `avatar-editor/` | Build script for the [Avatar Editor recompilation](https://github.com/panedivitasei/AvatarEditorRecomp) and the Avatar item (closet) importer. |

## Features

- Full Extended Play shows offline, with up to 10,000 bots.
- Steam parties (up to 4 players): one shared show, scores, Top 10, dancing in the lobby.
- Each player's own Avatar is sent to the party. Marketplace items a player lacks
  show as a similar standard item.
- Avatar Editor integration: edit your Avatar with the original dashboard editor,
  including Avatar items you own (imported from your console).
- Original host voice lines from your own show package (newly wired up; see below).
- Keyboard, SDL controllers and a direct USB DualSense backup.
- Debug tab: original development switches (show skins, Host Tool, debug menu), mods.

## What you need

From **your own** Xbox 360 and copy of the game (none of this is provided here):

- The 1 vs 100 game package (`default.xex` and its media).
- For host voice lines: the show package for your language (folder `dlcs/shows`, e.g.
  `7BEC809CCC282E8C9D18EFAD8F4407AA6204DFC8` for US English); copy its
  `media/audio/vo.xwb` and `vo.xsb` to `assets/Show/media/audio/`.
- For the Avatar Editor: dashboard 2.0.12581.0 files (see `avatar-editor/README.txt`).
- For Avatar items: the item packages from your console's `Content` folder.

Tools:

- Windows 10/11, Visual Studio 18 (C++ workload, CMake, Ninja), LLVM/clang 23.
- ReXGlue SDK 0.10.0.
- The [Steamworks SDK](https://partner.steamgames.com/) (for parties; development app ID 480).

## Building

> The build has so far been run only on the maintainer's PC. These steps describe it;
> please open an issue when something here does not work for you.

1. Put your game files in `native/assets/` (the folder `onevs100_manifest.toml` points at).
2. Generate the recompiled code from **your** `default.xex`:
   `rexglue codegen native/onevs100_manifest.toml`
3. Configure and build:
   ```
   cmake -S native -B build -G Ninja -DCMAKE_BUILD_TYPE=Release ^
     -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl ^
     -DCMAKE_PREFIX_PATH=<ReXGlue SDK>\win-amd64 -DONEVS100_STEAM_SDK=<Steamworks>\sdk
   cmake --build build --target onevs100
   ```
4. Launcher: `launcher\build_launcher.cmd` (set `LLVM_BIN` or put clang-cl on PATH).
5. Copy the files from `lua/` into the game's `lua` folder.

### Not published yet

Many gameplay fixes are edits to the game's **original** Lua scripts (offline service,
bot scoring, startup, menus). Those scripts belong to Microsoft, so the edits are not in
this repository yet; we are working out a way to publish them as changes you apply to
your own copy. Until then, a build from this repository alone will not play a full show.

## Tests

Lua unit tests run with any Lua 5.3+ (`texlua` works). They need the game's own
`luamockserver.lua` from your copy:

```
texlua tools/test/native-party-test.lua lua/NativeRoom.lua <your game>/lua/luamockserver.lua
texlua tools/test/native-livebots-test.lua lua/NativeLiveBots.lua <your game>/lua/luamockserver.lua
```

## Privacy and services

The build never contacts Xbox LIVE or other Microsoft production services. Parties use
Steam networking between friends. Your profile, Avatar and stats stay on your PC.

## Licenses

Our code is under the [MIT License](LICENSE). Third-party parts keep their own licenses;
see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). 1 vs 100, Xbox, Xbox 360 and Xbox
LIVE are trademarks of their owners; this is an unofficial fan project, not affiliated
with or endorsed by Microsoft or the game's developers.
