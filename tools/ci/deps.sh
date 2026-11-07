#!/usr/bin/env bash
# Provisions build dependencies into $DEPS_DIR (default build/deps). Each one
# is skipped when its directory already matches the pins in versions.env.
#
# Usage: deps.sh <xwin|msvc|sdl3-uwp|makemsix|vclibs>...
set -euo pipefail
ci=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$ci/../.." && pwd)
. "$ci/versions.env"
deps=${DEPS_DIR:-$root/build/deps}
mkdir -p "$deps"

# done_with <name> <stamp>: true when <name> was provisioned with <stamp>.
done_with() { [ "$(cat "$deps/$1/.stamp" 2>/dev/null)" = "$2" ]; }
mark() { echo "$2" > "$deps/$1/.stamp"; }

xwin() {
  local stamp="$XWIN_VERSION $WINSDK_VERSION"
  done_with xwin "$stamp" && return
  local name="xwin-$XWIN_VERSION-$(uname -m)-unknown-linux-musl"
  local url="https://github.com/Jake-Shadle/xwin/releases/download/$XWIN_VERSION/$name.tar.gz"
  local tmp; tmp=$(mktemp -d)
  curl -fsSL -o "$tmp/$name.tar.gz" "$url"
  # The .sha256 file holds only the hash.
  echo "$(curl -fsSL "$url.sha256" | awk '{ print $1 }')  $tmp/$name.tar.gz" | sha256sum -c -
  tar -xzf "$tmp/$name.tar.gz" -C "$tmp"
  rm -rf "$deps/xwin"
  # xwin moves files out of ./.xwin-cache, so that must be on the same file
  # system as the output.
  (cd "$deps" && "$tmp/$name/xwin" --accept-license --sdk-version "${WINSDK_VERSION%.0}" \
    splat --output "$deps/xwin")
  rm -rf "$tmp" "$deps/.xwin-cache"
  if [ ! -d "$deps/xwin/sdk/include/cppwinrt" ] || [ ! -d "$deps/xwin/crt/lib/x86_64" ]; then
    echo "xwin splat in $deps/xwin is incomplete" >&2
    exit 1
  fi
  mark xwin "$stamp"
}

msvc() {
  local stamp="$MSVC_MAJOR $MSVC_SELECTOR $MSVC_TOOLSET $WINSDK_VERSION $MSVC_WINE_COMMIT"
  done_with msvc "$stamp" && return
  local tmp; tmp=$(mktemp -d)
  git init -q "$tmp/msvc-wine"
  git -C "$tmp/msvc-wine" fetch -q --depth 1 https://github.com/mstorsjo/msvc-wine.git "$MSVC_WINE_COMMIT"
  git -C "$tmp/msvc-wine" checkout -q FETCH_HEAD
  rm -rf "$deps/msvc"
  python3 "$tmp/msvc-wine/vsdownload.py" --accept-license \
    --major "$MSVC_MAJOR" --msvc-version "$MSVC_SELECTOR" \
    --sdk-version "${WINSDK_VERSION%.0}" --architecture x64 \
    --dest "$deps/msvc" --cache "$tmp/dl"
  bash "$tmp/msvc-wine/install.sh" "$deps/msvc"
  rm -rf "$tmp"
  local have; have=$(ls "$deps/msvc/VC/Tools/MSVC" | grep -E '^[0-9]')
  if [[ "$have" != "$MSVC_TOOLSET".+([0-9.]) ]]; then
    echo "MSVC $have is not the pinned toolset $MSVC_TOOLSET" >&2
    exit 1
  fi
  if [ ! -d "$deps/msvc/Windows Kits/10/Include/$WINSDK_VERSION" ]; then
    echo "Windows SDK $WINSDK_VERSION is missing from $deps/msvc" >&2
    exit 1
  fi
  mark msvc "$stamp"
}

sdl3-uwp() {
  "$root/tools/uwp/fetch_sdl3_uwp.sh" "$deps/sdl3-uwp"
}

makemsix() {
  local stamp="$MSIX_PACKAGING_COMMIT"
  done_with makemsix "$stamp" && return
  local tmp; tmp=$(mktemp -d)
  python3 -m venv "$tmp/venv"
  "$tmp/venv/bin/pip" install -q "cmake==$MSIX_CMAKE_VERSION"
  git init -q "$tmp/src"
  git -C "$tmp/src" fetch -q --depth 1 https://github.com/microsoft/msix-packaging.git "$MSIX_PACKAGING_COMMIT"
  git -C "$tmp/src" checkout -q FETCH_HEAD
  # The vendored sources pin C++14 while current ICU headers need C++17. The
  # relative rpath lets makemsix find libmsix.so after it is moved.
  "$tmp/venv/bin/cmake" -S "$tmp/src" -B "$tmp/build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DMSIX_PACK=on -DUSE_VALIDATION_PARSER=on \
    -DCMAKE_CXX_STANDARD=17 -DCMAKE_BUILD_RPATH_USE_ORIGIN=ON
  "$tmp/venv/bin/cmake" --build "$tmp/build" --target makemsix --parallel
  rm -rf "$deps/makemsix"
  mkdir -p "$deps/makemsix/bin" "$deps/makemsix/lib"
  cp "$tmp/build/bin/makemsix" "$deps/makemsix/bin/"
  cp "$tmp/build/lib/libmsix.so" "$deps/makemsix/lib/"
  rm -rf "$tmp"
  mark makemsix "$stamp"
}

vclibs() {
  local stamp="$VCLIBS_SHA256"
  done_with vclibs "$stamp" && return
  local tmp; tmp=$(mktemp -d)
  git init -q "$tmp/msvc-wine"
  git -C "$tmp/msvc-wine" fetch -q --depth 1 https://github.com/mstorsjo/msvc-wine.git "$MSVC_WINE_COMMIT"
  git -C "$tmp/msvc-wine" checkout -q FETCH_HEAD
  python3 "$tmp/msvc-wine/vsdownload.py" --accept-license --major "$MSVC_MAJOR" \
    --with-default no --only-download --cache "$tmp/dl" Microsoft.VisualCpp.CRT.Appx.Msi
  # vsdownload exits 0 when the package is missing from the channel.
  local msi=("$tmp"/dl/Microsoft.VisualCpp.CRT.Appx.Msi-*/VC_CRT.Appx.msi)
  if [ ${#msi[@]} -ne 1 ] || [ ! -f "${msi[0]}" ]; then
    echo "vclibs: VC_CRT.Appx.msi not found in the VS $MSVC_MAJOR channel" >&2
    exit 1
  fi
  mkdir "$tmp/msi"
  (cd "$tmp/msi" && msiextract "${msi[0]}" >/dev/null)
  local found; mapfile -t found < <(find "$tmp/msi" -path '*/Appx/Retail/x64/Microsoft.VCLibs.x64.14.00.appx')
  if [ ${#found[@]} -ne 1 ]; then
    echo "vclibs: expected one x64 retail appx in ${msi[0]}, found ${#found[@]}" >&2
    exit 1
  fi
  local appx=${found[0]} sum
  sum=$(sha256sum "$appx" | cut -d' ' -f1)
  # The download follows the live VS channel, so a servicing update can change the file.
  if [ "$sum" != "$VCLIBS_SHA256" ]; then
    echo "vclibs: the VS $MSVC_MAJOR channel now ships an appx with sha256 $sum;" \
      "VCLIBS_SHA256 in versions.env is $VCLIBS_SHA256. Check the new file, then update the pin." >&2
    exit 1
  fi
  rm -rf "$deps/vclibs"
  mkdir -p "$deps/vclibs"
  cp "$appx" "$deps/vclibs/"
  rm -rf "$tmp"
  mark vclibs "$stamp"
}

for dep in "$@"; do
  case $dep in
    xwin|msvc|sdl3-uwp|makemsix|vclibs) "$dep" ;;
    *) echo "unknown dependency $dep" >&2; exit 2 ;;
  esac
done
