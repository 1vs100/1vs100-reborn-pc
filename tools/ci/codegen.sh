#!/usr/bin/env bash
# Builds the rexglue tool and regenerates native/generated from
# native/assets/default.xex.
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
cd "$root"
cmake -S native -B build/codegen -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_DISABLE_PRECOMPILE_HEADERS=ON -DREXSDK_DIR="$root/rexglue-sdk"
cmake --build build/codegen --target rexglue --parallel
rexglue=$(find rexglue-sdk/out -maxdepth 3 -name rexglue -type f -executable | head -n 1)
if [ -z "$rexglue" ]; then
  echo "rexglue binary not found under rexglue-sdk/out" >&2
  exit 1
fi
"$rexglue" codegen native/onevs100_manifest.toml
