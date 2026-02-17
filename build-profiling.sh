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
  -DCMAKE_CXX_FLAGS_RELEASE="-O3 -DNDEBUG -pg -g" \
  -DCMAKE_C_FLAGS_RELEASE="-O3 -DNDEBUG -pg -g" \
  -DCMAKE_EXE_LINKER_FLAGS="-pg -g" \
  -DCMAKE_SHARED_LINKER_FLAGS="-pg -g" \
  -DENABLE_ADDRESS_SANITIZER=0 \
  -DCMAKE_BUILD_TYPE=Release

ninja -C "$BUILD_DIR"
