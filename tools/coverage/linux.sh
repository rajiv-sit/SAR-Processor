#!/usr/bin/env bash
set -euo pipefail

BUILD_DIR="${1:-build}"
CONFIG="${2:-Debug}"

cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$CONFIG" -DCMAKE_CXX_FLAGS="--coverage"
cmake --build "$BUILD_DIR"
ctest --test-dir "$BUILD_DIR" --output-on-failure

llvm-profdata merge -sparse "$BUILD_DIR"/*.profraw -o "$BUILD_DIR"/coverage.profdata || true
llvm-cov report "$BUILD_DIR"/sar_core_tests \
  -instr-profile="$BUILD_DIR"/coverage.profdata \
  -ignore-filename-regex="build|tests"
