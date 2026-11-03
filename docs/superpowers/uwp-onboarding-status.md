# UWP first-run onboarding: status

UWP/Xbox builds cannot ship game files, so the `.appx` carries none.
On first run the game shows a setup screen (browser upload via QR code
or on-device USB/file pickers, every file hash-verified) before booting.
Adapted from tuxuser/sdl-uwp-deployment (MIT, see THIRD_PARTY_NOTICES.md).

Decisions (user-confirmed): in-process gate (onboarding runs inside
`onevs100.exe`, then the game starts), full hash manifest, full file set
(game GOD, show + episode packs, avatar assets) into `LocalState/`.

## Done

- Common manifest: `package/manifest.txt` (12 entries, generated 2026-11-03
  from `build/linux-src/content` + `avatar-content` via
  `tools/uwp/make_manifest.py --store ... --kinds
  tools/uwp/manifest-kinds.txt`). Format: `<sha256> <size> <kind>
  <path>` with kinds `game|launch|show|episode|avatar`; paths are
  relative to the user-data root (`content/…`, `avatar-content/…`).
  The desktop launcher's Files tab reads the same file (exact name +
  size match, role from kind; header heuristics remain the fallback).
- Vendored `native/src/uwp_onboarding/` (manifest, net, storage, ui,
  thirdparty/qrcodegen, web + `LICENSE.upstream`): 4-field `Parse` with
  kind validation; `App` takes the store root and adds
  `CheckNow`/`AllReady`/`IsDone`; new `gate.{h,cpp}` with
  `uwp_onboarding::EnsureGameFiles()` (fast-path skip when everything
  verifies, own SDL window + loop otherwise, tears SDL down before the
  game starts; never destroys the App, like upstream).
- Native UWP paths: relative `content` resolves under the user folder
  (`native/src/content_install.cpp`); `native_avatar_content` defaults to
  `avatar-content` on UWP and resolves under the user folder, staying
  empty when the dir is absent (`native/src/avatar_native/native_provider.cpp`).
- Gate call in `Onevs100App::OnConfigurePaths` (WINRT only), after the
  toml seed. Quitting onboarding falls through to the existing "no game
  files" error.
- CMake (`native/CMakeLists.txt`, WINDOWS_STORE only): onboarding
  sources, `/EHsc` (C++/WinRT coroutines), embed steps for
  `embedded_manifest.h` + `web_page.h` via `tools/uwp/embed.cmake`,
  include dirs, `ws2_32` + `bcrypt` links, SDL3-uwp include dir when
  `REXGLUE_SDL3_UWP_PREFIX` is defined.
- `tools/uwp/AppxManifest.xml`: `removableStorage` +
  `rescap:broadFileSystemAccess`.
- `tools/package_uwp.sh`: no longer ships game data (`CONTENT_DIR`,
  `ASSETS_DIR`, `AVATAR_TOC` removed); keeps `CONFIG_TOML` seed.
- CI (`.github/workflows/build.yaml`): `package/manifest.txt` staged
  into every build dir and listed in all six archives.
- Docs: `tools/uwp/README.md` (flow, manifest regen, layout,
  packaging); third-party notice.

## Verified locally

- Embed script + adapted 4-field `Parse` against the real manifest:
  12 entries, kinds correct (throwaway host test, not committed).
- `content_install.cpp` syntax-checks with and without
  `REX_PLATFORM_WINRT=1`; `native_provider.cpp` and `onevs100_app.h`
  (with gate call) check with `REX_PLATFORM_WINRT=1`.
- Full UWP build with the shipped SDL3-uwp prefix (SDL 3.4.18,
  `bin/SDL3.dll` + `lib/SDL3.lib`): `cmake -S native -B build/uwp` with
  `tools/uwp/clang-cl-toolchain.cmake`, `REXGLUE_SDL3_UWP_PREFIX` and the
  local MSVC install links `onevs100.exe` (clang 22.1.8, MSVC 14.51,
  SDK 10.0.26100; 638 steps, from a clean build dir). Only SDK-header
  `-Wc++98-compat` noise.
- The resulting image is `/APPCONTAINER`, subsystem Windows GUI, and
  imports only the `api-ms-win-*` umbrellas plus the VCLibs Store CRT
  (`MSVCP140_APP`, `VCRUNTIME140_APP`), `bcrypt`, `WS2_32`,
  `rexruntime.dll`, `SDL3.dll` and `api-ms-win-core-file-fromapp` (the
  `broadFileSystemAccess` path).
- First build found one UWP-only break: `file_store.cpp` built a
  `std::u8string`, which MSVC's STL omits under the toolchain's
  `-fno-char8_t`; manifest paths now use `std::filesystem::u8path`, which
  still takes a checked UTF-8 `std::string` and is not deprecated while
  `char8_t` is off.
- `tools/package_uwp.sh` produces a signed `.appx` (19.9 MB, no game
  data) using makemsix built from source. Block map, content types, data
  and central directory hashes verify; `osslsigncode` fails only on the
  self-signed test certificate.

## Not done / next session

1. **Device test** (Xbox dev-mode or Windows UWP sideload): first-run UI
   (QR page, upload, pickers), skip-UI fast path on second run, then
   handoff to the game. Open risks: SDL re-init after the onboarding
   teardown reuses the CoreWindow; picker behavior without USB on Xbox;
   the activation class. `AppxManifest.xml` names `OneVs100.App` (as
   upstream does), the exe is a clang-cl `WinMain` image with no `.winmd`,
   and whether the app host resolves that entry point or falls back to
   the standard entry point is unverified. Confirm at install time that
   the app starts at all.
2. **Install the appx**: the package declares the `Microsoft.VCLibs.140.00`
   dependency, so a device needs that appx too, plus the test certificate
   in Trusted People. Untested; only the local pack and hash verify ran.
3. **Manifest canonicality**: the committed hashes come from local dumps
   (game v2.0.1149 + regional show/episode packs + avatar TOCs). If
   reference files change, regenerate with `tools/uwp/make_manifest.py`
   and commit the new `package/manifest.txt`.
4. Optional: link `tools/uwp/README.md` from `RUN.md` (currently only
   referenced from the manifest tooling and the launcher tab).
