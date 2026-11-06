#!/usr/bin/env bash
# Builds onevs100 and the launcher for one target into build/<target> and
# adds the files every release package carries.
#
# Usage: build.sh <linux-amd64|linux-arm64|windows|uwp|mac-arm64|mac-amd64>
#
# windows and uwp cross-compile from Linux; see tools/ci/deps.sh for what
# they download.
set -euo pipefail
target=${1:?usage: build.sh <target>}
ci=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$ci/../.." && pwd)
deps=${DEPS_DIR:-$root/build/deps}
out=$root/build/$target

game=(-DREXSDK_DIR="$root/rexglue-sdk" -DCMAKE_DISABLE_PRECOMPILE_HEADERS=ON)
compilers=(-DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++)
launcher=yes
case $target in
  linux-amd64|linux-arm64)
    game+=("${compilers[@]}")
    ;;
  mac-arm64|mac-amd64)
    # No DXC release exists for macOS.
    game+=("${compilers[@]}" -DONEVS100_NATIVE_GPU=OFF)
    ;;
  windows)
    "$ci/deps.sh" xwin
    export XWIN_ROOT=$deps/xwin
    compilers=(-DCMAKE_TOOLCHAIN_FILE="$root/tools/win/clang-cl-toolchain.cmake")
    game+=("${compilers[@]}")
    ;;
  uwp)
    "$ci/deps.sh" xwin msvc sdl3-uwp
    . "$ci/versions.env"
    export XWIN_ROOT=$deps/xwin MSVC_ROOT=$deps/msvc WINSDK_VERSION
    MSVC_VERSION=$(ls "$MSVC_ROOT/VC/Tools/MSVC" | grep -E '^[0-9]')
    export MSVC_VERSION
    game+=(-DCMAKE_TOOLCHAIN_FILE="$root/tools/uwp/clang-cl-toolchain.cmake"
           -DREXGLUE_SDL3_UWP_PREFIX="$deps/sdl3-uwp")
    launcher=no
    ;;
  *)
    echo "unknown target $target" >&2
    exit 2
    ;;
esac

cmake -S "$root/native" -B "$out" -G Ninja -DCMAKE_BUILD_TYPE=Release "${game[@]}"
cmake --build "$out" --target onevs100 --parallel

if [ "$launcher" = yes ]; then
  cmake -S "$root/launcher" -B "$out-launcher" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_RUNTIME_OUTPUT_DIRECTORY="$out" "${compilers[@]}"
  cmake --build "$out-launcher" --parallel
fi

mkdir -p "$out/content"
touch "$out/content/.keep"
cp "$root/RUN.md" "$root/package/onevs100.toml" "$root/package/manifest.txt" "$out/"
