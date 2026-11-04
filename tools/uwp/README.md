# UWP build and first-run onboarding

- The Store/Xbox package ships no game files, so the `.appx` carries none.
- On first run the app shows a setup screen before the game starts.
- Files arrive by browser upload (QR code, any device on the LAN) or the on-device file and folder pickers.
- Every file is checked against `package/manifest.txt` (SHA-256 and size) and only verified files reach `LocalState/`.
- Later starts skip the UI when everything still verifies.

## Building

The Store build cross-compiles from Linux with clang-cl. It needs an [msvc-wine](https://github.com/mstorsjo/msvc-wine) install in `MSVC_ROOT` for the STL, the Store CRT and its libraries, an [xwin](https://github.com/Jake-Shadle/xwin) splat in `XWIN_ROOT` for the C++/WinRT headers, and a shared SDL3-uwp install prefix built with MSVC.

```
cmake -S native -B build/uwp -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
    -DCMAKE_DISABLE_PRECOMPILE_HEADERS=ON \
    -DREXSDK_DIR=../rexglue-sdk \
    -DCMAKE_TOOLCHAIN_FILE="$PWD/tools/uwp/clang-cl-toolchain.cmake" \
    -DREXGLUE_SDL3_UWP_PREFIX="$PWD/build/sdl3-uwp-prefix"
cmake --build build/uwp --target onevs100 --parallel
```

The prefix is an install tree with `bin/SDL3.dll` and `lib/SDL3.lib` at its root. CI unpacks the `SDL3_UWP_PREFIX_URL` secret into `sdl3-uwp-prefix/`.

## Manifest

`package/manifest.txt` is the single file list for both this onboarding and the desktop launcher's Files tab. Lines are `<sha256-hex> <size> <kind> <relative/path>`, with paths relative to the user-data root (`content/...`).

Content packages are named `<content id>58`, the name the game looks for. Edit the file by hand and keep it identical to the table in the 1vs100-reborn-x360 INSTALL.md. It is embedded in the UWP build and shipped in the desktop release archives for the launcher.

## Code layout

`native/src/uwp_onboarding/` is adapted from sdl-uwp-onboarding (MIT, see `THIRD_PARTY_NOTICES.md`). It holds manifest parsing, the HTTP upload server, the USB and file pickers and the progress UI. It compiles into `onevs100.exe` for the `WindowsStore` toolchain only and runs from `Onevs100App::OnConfigurePaths` via `uwp_onboarding::EnsureGameFiles()`, before the game creates its window.

UWP-only path rules live next to the generic ones. `content/` and avatar content resolve under the user folder (`native/src/content_install.cpp`, `native/src/avatar_native/native_provider.cpp`).

## Packaging

`tools/package_uwp.sh <build_dir> <out_dir>` builds the signed `.appx` from the CI `uwp` leg output plus `tools/uwp/AppxManifest.xml`. The manifest declares `removableStorage` for the USB pickers and omits `broadFileSystemAccess` (restricted capability, Store blocker, unneeded for picker-granted access). The package intentionally contains no game files.

`makemsix` comes from [microsoft/msix-packaging](https://github.com/microsoft/msix-packaging) built with `MSIX_PACK=on`. Its vendored xerces and SDK sources pin C++14 while current ICU headers need C++17, so raise `CMAKE_CXX_STANDARD` in the top-level and `lib/xerces` CMakeLists before building.

The signing step needs `osslsigncode`. Without `CERT_PFX` the script creates a self-signed certificate in `~/.local/share/onevs100-uwp-cert` whose subject matches the manifest `Publisher`. The device has to trust that certificate before it installs the package.
