#!/usr/bin/env bash
# Installs the pinned clang, lld and LLVM tools from the upstream release
# tarball into <dest>/bin. Only the needed tools are extracted, because the
# whole archive unpacks to about 11.5 GB.
#
# Usage: install_llvm.sh <dest>
set -euo pipefail
dest=${1:?usage: install_llvm.sh <dest>}
. "$(dirname "$0")/versions.env"

case "$(uname -m)" in
  x86_64) arch=X64 ;;
  aarch64|arm64) arch=ARM64 ;;
  *) echo "unsupported architecture $(uname -m)" >&2; exit 1 ;;
esac
p="LLVM-$LLVM_RELEASE-Linux-$arch"
mkdir -p "$dest"
curl -fsSL --retry 5 --retry-all-errors --retry-delay 10 \
    "https://github.com/llvm/llvm-project/releases/download/llvmorg-$LLVM_RELEASE/$p.tar.xz" \
  | tar -xJ -C "$dest" --strip-components=1 --wildcards \
    "$p/bin/clang" "$p/bin/clang++" "$p/bin/clang-cl" "$p/bin/clang-$CLANG_VERSION" \
    "$p/bin/lld" "$p/bin/lld-link" "$p/bin/llvm-ar" "$p/bin/llvm-lib" \
    "$p/bin/llvm-rc" "$p/bin/llvm-mt" "$p/lib/clang/*"
"$dest/bin/clang" --version
