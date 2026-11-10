#!/usr/bin/env bash
# Repacks a release archive from tools/ci/package.sh as Velopack packages,
# which update themselves from GitHub releases.
#
# Usage: make_velopack.sh <target> <archive> <out_dir> <version>
#
# windows: Setup.exe from the zip. linux-*: AppImage from
# the tar.xz. mac-*: portable zip from the app zip; this one needs macOS.
# Each target is its own channel, so one release holds every package.
# VPK names the vpk command (default: vpk on PATH).
set -euo pipefail

if [ $# -ne 4 ]; then
  echo "usage: $0 <target> <archive> <out_dir> <version>" >&2
  exit 2
fi
target=$1
archive=$(realpath "$2")
mkdir -p "$3"
out_dir=$(cd "$3" && pwd)
version=$4
vpk=${VPK:-vpk}

if ! [[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo "bad version $version, want three numeric parts" >&2
  exit 2
fi

# The install folder and package cache are named after the id. It differs
# from the Inno Setup folder (%LocalAppData%\1vs100) so the two do not mix.
pack_id=onevs100
title="1 vs 100"
args=()
directive=()
case $target in
  windows)
    runtime=win-x64; directive=("[win]"); main=launcher.exe
    # Its player data would go to %APPDATA% like an install, not along with
    # the portable folder. The plain zip covers portable use.
    args+=(--noPortable)
    ;;
  linux-amd64) runtime=linux-x64; main=launcher ;;
  linux-arm64) runtime=linux-arm64; main=launcher ;;
  mac-arm64|mac-amd64)
    [ "$target" = mac-arm64 ] && runtime=osx-arm64 || runtime=osx-x64
    main=launcher
    # The bundle is named after the title; keep the 1vs100.app name.
    title=1vs100
    # Velopack adds files to the bundle, so it is signed again, ad-hoc as
    # tools/packaging/make_mac_app.sh does. An unsigned .pkg would not pass
    # Gatekeeper, so only the portable zip is made.
    args+=(--signAppIdentity - --noInst)
    ;;
  *)
    echo "unknown target $target" >&2
    exit 2
    ;;
esac

stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
case $target in
  windows)
    unzip -q "$archive" -d "$stage"
    pack_dir=$stage/1vs100
    ;;
  linux-*)
    tar -xJf "$archive" -C "$stage"
    pack_dir=$stage/1vs100
    ;;
  mac-*)
    ditto -x -k "$archive" "$stage"
    pack_dir=$stage/1vs100.app
    ;;
esac
if [ ! -e "$pack_dir" ]; then
  echo "$archive does not hold $(basename "$pack_dir")" >&2
  exit 1
fi

# The +expansions keep empty arrays working under set -u in the bash 3.2 of macOS.
"$vpk" ${directive[@]+"${directive[@]}"} --legacyConsole --skip-updates pack \
  --packId "$pack_id" --packVersion "$version" --packDir "$pack_dir" \
  --packTitle "$title" --packAuthors "1vs100" --mainExe "$main" \
  --runtime "$runtime" --channel "$runtime" --outputDir "$out_dir" ${args[@]+"${args[@]}"}
