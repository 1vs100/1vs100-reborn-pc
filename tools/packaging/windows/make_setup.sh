#!/usr/bin/env bash
# Builds the Windows installer from a build dir via Inno Setup.
#
# Usage: make_setup.sh <build_dir> <out_dir> [version]
#
# Stages the 8 files from the windows CI artifact into a temp stage/
# then runs ISCC. Needs ISCC on PATH (windows runner with Inno Setup 6).
# userdata is never staged; first run creates userdata/profiles.
set -euo pipefail

if [ $# -lt 2 ] || [ $# -gt 3 ]; then
  echo "usage: $0 <build_dir> <out_dir> [version]" >&2
  exit 2
fi

build_dir=$(realpath "$1")
mkdir -p "$2"
if out_dir=$(cd "$2" && pwd -W 2>/dev/null); then
  :
else
  out_dir=$(cd "$2" && pwd)
fi
version=${3:-0.1.0}
if ! [[ "$version" =~ ^[0-9]+(\.[0-9]+){0,3}$ ]]; then
  echo "bad version $version, want 1 to 4 numeric parts" >&2
  exit 2
fi
export MSYS2_ARG_CONV_EXCL='*'

for f in onevs100.exe launcher.exe rexruntime.dll rexgpu-xenos.dll onevs100.toml RUN.md manifest.txt; do
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

script_dir=$(dirname "$(realpath "$0")")
stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
stage_win=$(cd "$stage" && pwd -W 2>/dev/null || echo "$stage")
mkdir -p "$stage/stage/content"
cp "$build_dir/onevs100.exe" "$build_dir/launcher.exe" "$stage/stage/"
cp "$build_dir/rexruntime.dll" "$build_dir/rexgpu-xenos.dll" "$stage/stage/"
cp "$build_dir/onevs100.toml" "$build_dir/RUN.md" "$build_dir/manifest.txt" "$stage/stage/"
if [ -f "$build_dir/content/.keep" ]; then
  cp "$build_dir/content/.keep" "$stage/stage/content/"
fi
cp "$script_dir/1vs100.iss" "$stage/"
iscc "/DMyAppVersion=$version" "/O$out_dir" "$stage_win/1vs100.iss"
