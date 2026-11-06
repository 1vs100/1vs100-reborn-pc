#!/usr/bin/env bash
# Packs a macOS build dir into a per-arch .app zip for distribution.
#
# Usage: make_mac_app.sh <build_dir> <out_file> [version]
#
# Bundle stays flat in Contents/MacOS so SDL_GetBasePath and
# GetExecutableFolder agree. userdata is never shipped;
# first run creates userdata/profiles.
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
if [ ! -d "$build_dir/content" ]; then
  echo "missing $build_dir/content" >&2
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
mkdir -p "$app/content" "$stage/1vs100.app/Contents/Resources"
cp "$build_dir/onevs100" "$build_dir/launcher" "$app/"
cp "$build_dir"/*.dylib "$app/"
cp "$build_dir/onevs100.toml" "$build_dir/RUN.md" "$build_dir/manifest.txt" "$app/"
if [ -f "$build_dir/content/.keep" ]; then
  cp "$build_dir/content/.keep" "$app/content/"
fi
# SDK stages the Vulkan runtime under vulkan/ next to the executable.
# Preserve that subtree so DetectMacOSVulkanRuntimePaths finds it
# via executable_dir/vulkan. The ICD library_path is relative
# (../../../lib/libMoltenVK.dylib), so the share/ to lib/ layout must stay intact.
if [ -d "$build_dir/vulkan" ]; then
  for req in "vulkan/lib/libvulkan.1.dylib" "vulkan/lib/libMoltenVK.dylib" "vulkan/share/vulkan/icd.d/MoltenVK_icd.json"; do
    if [ ! -f "$build_dir/$req" ]; then
      echo "missing $build_dir/$req" >&2
      exit 1
    fi
  done
  mkdir -p "$app/vulkan"
  cp -pR "$build_dir/vulkan/." "$app/vulkan/"
else
  # Flat layout predating the SDK vulkan/ staging change.
  for f in MoltenVK_icd.json 'libvulkan*.dylib'; do
    found=0
    for src in "$build_dir"/$f; do
      if [ -f "$src" ]; then
        cp "$src" "$app/"
        found=1
      fi
    done
    if [ "$found" -ne 1 ] && [ "$f" = "MoltenVK_icd.json" ]; then
      echo "warning: $build_dir/MoltenVK_icd.json absent, Vulkan may miss driver" >&2
    fi
  done
fi
sed -e "s/__VERSION__/$version/g" -e "s/__MIN_MACOS__/$minimum_os/g" \
  "$script_dir/mac/Info.plist.template" > "$stage/1vs100.app/Contents/Info.plist"
chmod +x "$app/onevs100" "$app/launcher"
if ! codesign --force -s - "$app"/*.dylib 2>/dev/null; then
  echo "warning: dylib codesign failed" >&2
fi
vulkan_signed=0
for vdylib in "$app"/vulkan/lib/*.dylib; do
  if [ -f "$vdylib" ]; then
    vulkan_signed=1
    break
  fi
done
if [ "$vulkan_signed" -eq 1 ]; then
  if ! codesign --force -s - "$app"/vulkan/lib/*.dylib 2>/dev/null; then
    echo "warning: vulkan dylib codesign failed" >&2
  fi
fi
if ! codesign --force -s - "$app/onevs100" "$app/launcher" 2>/dev/null; then
  echo "warning: exe codesign failed" >&2
fi
codesign --force -s - "$stage/1vs100.app" 2>/dev/null || true
rm -f "$out_file"
if command -v ditto >/dev/null 2>&1; then
  ditto -c -k --sequesterRsrc --keepParent "$stage/1vs100.app" "$out_file"
else
  # Linux pack path (osxcross legs): plain zip, symlinks preserved.
  (cd "$stage" && zip -qr -y "$out_file" 1vs100.app)
fi
echo "$out_file"
