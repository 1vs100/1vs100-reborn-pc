#!/usr/bin/env bash
# Runs a command in the CI build image, built locally from tools/ci when it
# is missing. The checkout is mounted at the same path as on the host.
#
# Usage: tools/ci/container.sh tools/ci/build.sh uwp
set -euo pipefail
ci=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$ci/../.." && pwd)
engine=$(command -v podman || command -v docker) || { echo "podman or docker is required" >&2; exit 1; }
tag=onevs100-ci:$("$ci/image_tag.sh")

if ! "$engine" image inspect "$tag" >/dev/null 2>&1; then
  "$engine" build --platform linux/amd64 -t "$tag" "$ci"
fi
# HOME persists so tools/package_uwp.sh reuses its test certificate.
mkdir -p "$root/build/home"
user=()
if [ "$(basename "$engine")" = docker ]; then
  user=(--user "$(id -u):$(id -g)")
fi
exec "$engine" run --rm --platform linux/amd64 "${user[@]}" -e HOME="$root/build/home" \
  -v "$root:$root:z" -w "$root" \
  -e CCACHE_DIR="$root/build/ccache" \
  -e CMAKE_C_COMPILER_LAUNCHER=ccache -e CMAKE_CXX_COMPILER_LAUNCHER=ccache \
  "$tag" "$@"
