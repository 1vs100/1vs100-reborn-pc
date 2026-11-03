# Release artifacts and launcher: status

Release archives get a `content/` folder, the player-facing `RUN.md` (the only document in the archive), a preset `onevs100.toml` and a new multi-platform launcher. The `content_install` setting becomes `content`. The old Windows launcher moves to `tools/dev-launcher/`.

- [x] `content/` folder in the release archive: empty `content/.keep` placeholder (upload step drops empty folders), uploaded with `include-hidden-files: true`.
- [x] `content_install` setting renamed to `content`. It defaults to `content/` next to the executable on all platforms (`native/src/content_install.cpp`, `.h`).
- [x] `README.md` updated for the rename and the launcher move.
- [x] Discord link: https://discord.gg/tV3ajhwFTf
- [x] `package/onevs100.toml`: `online_service = "gameserver"`, `gameserver_host = "1vs100.fans"`, `fullscreen = true`. Keys are top-level, matching the cvar names (the loader only reads flat keys).
- [x] `RUN.md` rewritten as the end-user guide (Discord, https://1vs100.fans, `content/`, launcher, avatar). Developer run details stay in `README.md`.
- [x] CI: "Stage release files" step copies `RUN.md` and `onevs100.toml` into each build folder, and all three archives list them.
- [x] Old Windows launcher moved to `tools/dev-launcher/`. `build_launcher.cmd` output path and `tools/Build-TestPackage.ps1` updated.
- [x] New launcher in `launcher/` written and builds on Linux (Linux and Windows, SDL3 3.4.8 + Dear ImGui 1.91.9b + toml++ fetched by CMake. The SDK's SDL is built without a renderer.)
  - [x] Player: gamertag (1-15 chars, game rules), saved to `userdata/profiles/local-name.txt`
  - [x] Player: avatar `.bin` picker (1000 or 1020 bytes), copied to `userdata/profiles/avatar.bin`, with remove and 360sona link
  - [x] Settings: gameserver host, default `1vs100.fans`, reset button
  - [x] Settings: windowed mode checkbox, written as `fullscreen` in `onevs100.toml`
  - [x] Launch button: starts `onevs100` from the launcher folder
  - [x] Roboto font, Discord and website links
  - [x] Colors from 1vs100.fans and docs.1vs100.fans (dark palette, show gradient on header bar and Launch)
- [x] Launcher output named `launcher` / `launcher.exe`. Windows cross-build verified locally (GUI subsystem, same VC runtime as the game). Portable UTF-8 path helpers, because the Windows toolchain builds with `-fno-char8_t`.
- [x] CI: "Build launcher" step for Linux and Windows writes into the build folder; archives list `launcher` / `launcher.exe`. No launcher for UWP. Verified locally via the same configure flags (Linux + clang-cl cross); not yet run in CI.
- [x] Build and test on Linux: offscreen screenshots of both tabs and the error state; Launch saves gamertag and `onevs100.toml` and starts the game in the launcher folder.
- [x] Adversarial code review with a fresh subagent. Findings fixed: flat-key docs (README example used [Online]/[Content] sections the loader never reads), /EHsc for the Windows build, oversized input buffers so overlong gamertags/hosts fail validation instead of truncating, empty-host rejection, wide-path TOML parse, dialog-event guard, clipped gradient button, LF gamertag file, host format hints. Two findings were wrong on verification: B1 was inverted (`ApplyTomlTable` joins nested tables with `_`, so only top-level keys match cvar names — verified in rexglue-sdk `src/core/cvar.cpp` + `cvar_test.cpp`), and M6's "leak" was not one (SDL 3.4.8 caches the base path and frees it in `SDL_Quit`; freeing it ourselves double-frees on shutdown — reproduced in isolation, fix verified offscreen).
- [x] Commits: setting rename and docs, dev-launcher move, launcher, CI packaging.
