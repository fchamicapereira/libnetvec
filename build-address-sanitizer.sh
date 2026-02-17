#!/bin/bash

set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$PROJECT_DIR/build"

if [ -d "$BUILD_DIR" ]; then
  rm -rf $BUILD_DIR/CMakeCache.txt $BUILD_DIR/CMakeFiles $BUILD_DIR/compile_commands.json || true
fi

cmake \
  -G Ninja \
  -B "$BUILD_DIR" \
  -S "$PROJECT_DIR" \
  -DENABLE_ADDRESS_SANITIZER=1 \
  -DCMAKE_BUILD_TYPE=Debug

ninja -C "$BUILD_DIR"
