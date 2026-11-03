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
for f in MoltenVK_icd.json libvulkan*.dylib; do
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
sed "s/__VERSION__/$version/g" "$script_dir/Info.plist.template" > "$stage/1vs100.app/Contents/Info.plist"
chmod +x "$app/onevs100" "$app/launcher"
if ! codesign --force -s - "$app"/*.dylib 2>/dev/null; then
  echo "warning: dylib codesign failed" >&2
fi
if ! codesign --force -s - "$app/onevs100" "$app/launcher" 2>/dev/null; then
  echo "warning: exe codesign failed" >&2
fi
codesign --force -s - "$stage/1vs100.app" 2>/dev/null || true
rm -f "$out_file"
ditto -c -k --sequesterRsrc --keepParent "$stage/1vs100.app" "$out_file"
echo "$out_file"
