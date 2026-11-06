#!/usr/bin/env bash
# Downloads the SDL3-uwp install prefix that matches the SDL3-uwp commit pinned
# in rexglue-sdk. It is published by the SDL3-uwp repository because its
# C++/CX sources compile only with MSVC on Windows.
#
# Usage: fetch_sdl3_uwp.sh <prefix_dir>
set -euo pipefail

if [ $# -ne 1 ]; then
  echo "usage: $0 <prefix_dir>" >&2
  exit 2
fi
dest=$1
root=$(cd "$(dirname "$0")/../.." && pwd)
read -r tag sha256 < "$root/tools/uwp/sdl3-uwp-prefix.txt"

pinned=$(git -C "$root/rexglue-sdk" ls-tree HEAD thirdparty/sdl3-uwp | awk '{ print $3 }')
if [ -z "$pinned" ]; then
  echo "rexglue-sdk is not checked out; run git submodule update --init --recursive" >&2
  exit 1
fi
if [ "$tag" != "prefix-${pinned:0:9}" ]; then
  echo "tools/uwp/sdl3-uwp-prefix.txt names $tag, rexglue-sdk pins SDL3-uwp $pinned" >&2
  exit 1
fi
if [ "$(cat "$dest/.sha256" 2>/dev/null)" = "$sha256" ]; then
  exit 0
fi
# The directory is replaced, so only accept one this script created.
if [ -n "$(ls -A "$dest" 2>/dev/null)" ] && [ ! -f "$dest/.sha256" ]; then
  echo "$dest is not empty and was not created by this script" >&2
  exit 1
fi

zip="sdl3-uwp-x64-${tag#prefix-}.zip"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
curl -fsSL --retry 3 -o "$tmp/$zip" "https://github.com/1vs100/SDL3-uwp/releases/download/$tag/$zip"
echo "$sha256  $tmp/$zip" | sha256sum -c -
rm -rf "$dest"
mkdir -p "$dest"
unzip -q "$tmp/$zip" -d "$dest"
echo "$sha256" > "$dest/.sha256"
