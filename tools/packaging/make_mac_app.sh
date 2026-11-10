#!/usr/bin/env bash
# Packs a macOS build dir into a per-arch .app zip for distribution.
#
# Usage: make_mac_app.sh <build_dir> <out_file> [version]
#
# Code goes in Contents/MacOS and the shipped files in Contents/Resources,
# since codesign accepts only code in Contents/MacOS. The game and launcher
# write config, userdata and content to ~/Library/Application Support/1vs100,
# seeding onevs100.toml from the bundled copy, so the bundle stays read-only.
set -euo pipefail

if [ $# -lt 2 ] || [ $# -gt 3 ]; then
  echo "usage: $0 <build_dir> <out_file> [version]" >&2
  exit 2
fi

build_dir=$(realpath "$1")
mkdir -p "$(dirname "$2")"
out_file=$(cd "$(dirname "$2")" && pwd)/$(basename "$2")
version=${3:-0.1.0}
if ! [[ "$version" =~ ^[0-9]+(\.[0-9]+){0,3}$ ]]; then
  echo "bad version $version, want 1 to 4 numeric parts" >&2
  exit 2
fi

for f in onevs100 launcher onevs100.toml RUN.md manifest.txt; do
  if [ ! -f "$build_dir/$f" ]; then
    echo "missing $build_dir/$f" >&2
    exit 1
  fi
done
dylib_found=0
for d in "$build_dir"/*.dylib; do
  if [ -f "$d" ]; then
    dylib_found=1
    break
  fi
done
if [ "$dylib_found" -ne 1 ]; then
  echo "missing $build_dir/*.dylib" >&2
  exit 1
fi

if [ -e "$build_dir/userdata" ]; then
  echo "refusing to pack userdata in $build_dir" >&2
  exit 1
fi

script_dir=$(dirname "$(realpath "$0")")

minimum_os=$(tr -d '[:space:]' < "$script_dir/../mac/minimum_os.txt")
if ! [[ "$minimum_os" =~ ^[0-9]+\.[0-9]+$ ]]; then
  echo "bad minimum_os.txt: '$minimum_os'" >&2
  exit 1
fi

stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
app="$stage/1vs100.app/Contents/MacOS"
mkdir -p "$app" "$stage/1vs100.app/Contents/Resources"
cp "$build_dir/onevs100" "$build_dir/launcher" "$app/"
cp "$build_dir"/*.dylib "$app/"
cp "$build_dir/onevs100.toml" "$build_dir/RUN.md" "$build_dir/manifest.txt" "$stage/1vs100.app/Contents/Resources/"
# SDK stages the Vulkan runtime under vulkan/ next to the executable, where
# DetectMacOSVulkanRuntimePaths finds the libraries.
icd=vulkan/share/vulkan/icd.d/MoltenVK_icd.json
for req in "vulkan/lib/libvulkan.1.dylib" "vulkan/lib/libMoltenVK.dylib" "$icd"; do
  if [ ! -f "$build_dir/$req" ]; then
    echo "missing $build_dir/$req" >&2
    exit 1
  fi
done
mkdir -p "$app/vulkan"
cp -pR "$build_dir/vulkan/lib" "$app/vulkan/"
# codesign takes the icd.d folder in Contents/MacOS for a nested bundle and
# refuses to sign the app, so the manifest goes where
# DetectMacOSVulkanRuntimePaths looks in a bundle. library_path is relative to
# the manifest.
bundle_icd="$stage/1vs100.app/Contents/Resources/vulkan/icd.d"
mkdir -p "$bundle_icd"
sed 's|"library_path" *: *"[^"]*"|"library_path": "../../../MacOS/vulkan/lib/libMoltenVK.dylib"|' \
  "$build_dir/$icd" > "$bundle_icd/MoltenVK_icd.json"
grep -q '"../../../MacOS/vulkan/lib/libMoltenVK.dylib"' "$bundle_icd/MoltenVK_icd.json" || {
  echo "cannot rewrite library_path in $build_dir/$icd" >&2
  exit 1
}

sed -e "s/__VERSION__/$version/g" -e "s/__MIN_MACOS__/$minimum_os/g" \
  "$script_dir/mac/Info.plist.template" > "$stage/1vs100.app/Contents/Info.plist"
chmod +x "$app/onevs100" "$app/launcher"
# Ad-hoc, inside out: arm64 code does not run without a signature. The
# release identity is applied once, by vpk in tools/packaging/make_velopack.sh.
if command -v codesign >/dev/null 2>&1; then
  codesign --force -s - "$app"/*.dylib "$app"/vulkan/lib/*.dylib "$app/onevs100"
  codesign --force -s - "$stage/1vs100.app"
  codesign --verify --strict --deep "$stage/1vs100.app"
else
  # Linux pack (osxcross): each Mach-O keeps the ad-hoc signature of the linker.
  echo "warning: no codesign, the bundle is not sealed" >&2
fi
rm -f "$out_file"
if command -v ditto >/dev/null 2>&1; then
  ditto -c -k --sequesterRsrc --keepParent "$stage/1vs100.app" "$out_file"
else
  # Linux pack path (osxcross legs): plain zip, symlinks preserved.
  (cd "$stage" && zip -qr -y "$out_file" 1vs100.app)
fi
echo "$out_file"
