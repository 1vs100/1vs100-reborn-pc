#!/usr/bin/env bash
# Packs build/<target> from tools/ci/build.sh into out/.
#
# Usage: package.sh <linux-amd64|linux-arm64|windows|uwp|mac-arm64|mac-amd64>
set -euo pipefail
target=${1:?usage: package.sh <target>}
ci=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$ci/../.." && pwd)
build=$root/build/$target
out=$root/out
name=onevs100-$target-$(git -C "$root" rev-parse --short=8 HEAD)
version=$(tr -d '[:space:]' < "$root/VERSION")
pack=$root/tools/packaging

case $target in
  linux-amd64|linux-arm64) "$pack/make_tar_xz.sh" "$build" "$out/$name.tar.xz" ;;
  windows) "$pack/make_win_zip.sh" "$build" "$out/$name.zip" ;;
  mac-arm64|mac-amd64) "$pack/make_mac_app.sh" "$build" "$out/$name.zip" "$version" ;;
  uwp)
    "$ci/deps.sh" makemsix vclibs
    if [ -n "${CERT_PFX_B64:-}" ]; then
      CERT_PFX=$(mktemp)
      export CERT_PFX
      trap 'rm -f "$CERT_PFX"' EXIT
      echo "$CERT_PFX_B64" | base64 -d > "$CERT_PFX"
    fi
    deps=${DEPS_DIR:-$root/build/deps}
    MAKEMSIX=$deps/makemsix/bin/makemsix VCLIBS_APPX=$deps/vclibs/Microsoft.VCLibs.x64.14.00.appx \
      "$root/tools/package_uwp.sh" "$build" "$out/uwp"
    mv "$out/uwp/onevs100.appx" "$out/$name.appx"
    mv "$out/uwp/Microsoft.VCLibs.x64.14.00.appx" "$out/"
    rm -rf "$out/uwp"
    ;;
  *)
    echo "unknown target $target" >&2
    exit 2
    ;;
esac
