#!/usr/bin/env bash
# Packs a Linux build dir into a tar.xz for distribution.
#
# Usage: make_tar_xz.sh <build_dir> <out_file>
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
for f in onevs100 launcher librexruntime.so librexgpu-xenos.so onevs100.toml RUN.md manifest.txt; do
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
cp "$build_dir/onevs100" "$build_dir/launcher" "$root/"
cp "$build_dir/librexruntime.so" "$build_dir/librexgpu-xenos.so" "$root/"
cp "$build_dir/onevs100.toml" "$build_dir/RUN.md" "$build_dir/manifest.txt" "$root/"
if [ -f "$build_dir/content/.keep" ]; then
  cp "$build_dir/content/.keep" "$root/content/"
fi
chmod +x "$root/onevs100" "$root/launcher"
tar -cJf "$out_file" -C "$stage" 1vs100
echo "$out_file"
