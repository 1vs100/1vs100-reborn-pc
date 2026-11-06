#!/usr/bin/env bash
# Prints the CI image tag: a hash of everything that goes into the image.
set -euo pipefail
ci=$(cd "$(dirname "$0")" && pwd)
cat "$ci/Dockerfile" "$ci/apt-packages.txt" "$ci/versions.env" "$ci/install_llvm.sh" \
  | sha256sum | cut -c1-12
