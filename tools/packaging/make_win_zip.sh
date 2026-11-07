#!/usr/bin/env bash
# Packs a Windows build dir into a zip for distribution.
#
# Usage: make_win_zip.sh <build_dir> <out_file>
#
# Layout is flat beside the executables. userdata is never shipped;
# first run creates userdata/profiles.
set -euo pipefail

if [ $# -ne 2 ]; then
  echo "usage: $0 <build_dir> <out_file>" >&2
  exit 2
fi

build_dir=$(realpath "$1")
mkdir -p "$(dirname "$2")"
out_file=$(cd "$(dirname "$2")" && pwd)/$(basename "$2")

for f in onevs100.exe launcher.exe rexruntime.dll rexgpu-xenos.dll dxcompiler.dll dxil.dll onevs100.toml RUN.md manifest.txt; do
  if [ ! -f "$build_dir/$f" ]; then
    echo "missing $build_dir/$f" >&2
    exit 1
  fi
done
if [ ! -d "$build_dir/content" ]; then
  echo "missing $build_dir/content" >&2
  exit 1
fi

if [ -e "$build_dir/userdata" ]; then
  echo "refusing to pack userdata in $build_dir" >&2
  exit 1
fi

stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
root="$stage/1vs100"
mkdir -p "$root/content"
cp "$build_dir/onevs100.exe" "$build_dir/launcher.exe" "$root/"
cp "$build_dir/rexruntime.dll" "$build_dir/rexgpu-xenos.dll" "$build_dir/dxcompiler.dll" "$build_dir/dxil.dll" "$root/"
cp "$build_dir/onevs100.toml" "$build_dir/RUN.md" "$build_dir/manifest.txt" "$root/"
if [ -f "$build_dir/content/.keep" ]; then
  cp "$build_dir/content/.keep" "$root/content/"
fi
rm -f "$out_file"
(cd "$stage" && zip -qr "$out_file" 1vs100)
echo "$out_file"
