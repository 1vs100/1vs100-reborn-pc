# macOS builds: native and cross

Two routes produce the same per-arch `.app` zips. CI uses the native route
on macOS runners. Developers without a Mac use the cross route from Linux.

## Native (macOS host)

```sh
brew install ninja nasm pkg-config molten-vk vulkan-loader
sudo xcode-select -s "$(ls -d /Applications/Xcode*.app | sort -V | tail -1)"
cmake -S native -B build/mac-arm64 -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DREXSDK_DIR=$SDK -DCMAKE_DISABLE_PRECOMPILE_HEADERS=ON \
  -DONEVS100_NATIVE_GPU=OFF
cmake --build build/mac-arm64 --target onevs100
```

The build stages the Vulkan runtime (`vulkan/lib`, `vulkan/share`) next to the
executable from the SDK's own MoltenVK and loader build; `make_mac_app.sh`
packs it into `1vs100.app`. There is no DXC release for macOS, so the shipped
zips keep the native renderer off (`-DONEVS100_NATIVE_GPU=OFF`, also the default
on Darwin): the game runs from prebuilt shader packs. `-DONEVS100_NATIVE_GPU=ON`
also builds and links on both routes, with a stub `DxcCreateInstance` and the
Vulkan surface coming from SDL and MoltenVK, but no macOS build has run it.

## Cross (Linux host, osxcross)

Needs an osxcross install plus your own macOS SDK tarball (Apple forbids
redistributing the SDK, so fetch it yourself and keep it out of the repo):

```sh
git clone https://github.com/tpoechtrager/osxcross.git
# put your MacOSX*.sdk.tar.xz in osxcross/tarballs/, then:
cd osxcross && ./build.sh && ENABLE_COMPILER_RT_INSTALL=1 ./build_compiler_rt.sh
# install the runtimes where the toolchain expects them (needs root for the
# system clang dir; without this, linking fails on ___isPlatformVersionAtLeast):
#   sudo mkdir -p /usr/lib/clang/22/lib/darwin   # match your clang version
#   sudo cp build/compiler-rt/compiler-rt/build_<arch>/lib/darwin/* ...
# or, without root, assemble ~/.local/share/osxcross/clang-res/22 as a copy
# of /usr/lib/clang/<ver> plus the built darwin runtimes and sanitizer
# headers; the toolchain picks it up automatically.
export OSXCROSS_ROOT=$PWD MAC_ARCH=arm64
cmake -S native -B build/mac-arm64 -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=$PWD/tools/mac/osxcross-toolchain.cmake \
  -DONEVS100_NATIVE_GPU=OFF -DREXSDK_DIR=$SDK
cmake --build build/mac-arm64 --target onevs100
```

`minimum_os.txt` holds the oldest macOS the build targets and the `.app`
claims; both routes and the plist read it. Override a single build with
`MACOS_DEPLOYMENT_TARGET` on the cross route or `-DCMAKE_OSX_DEPLOYMENT_TARGET`
on either.

Use `MAC_ARCH=x86_64` and `build/mac-amd64` for the Intel slice. The
pinned osxcross and SDK versions live in `build.yaml` (`OSXCROSS_VERSION`,
`MACOS_SDK_VERSION`); match them locally so both routes agree.
Packaging works from either host: `make_mac_app.sh` uses `ditto` on macOS
and falls back to `zip` on Linux.
