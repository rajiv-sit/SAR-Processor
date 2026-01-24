#!/usr/bin/env bash
set -euo pipefail

BUILD_DIR="${1:-build}"
CONFIG="${2:-Debug}"

cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$CONFIG" -DCMAKE_CXX_FLAGS="--coverage"
cmake --build "$BUILD_DIR"
ctest --test-dir "$BUILD_DIR" --output-on-failure

LLVM_PROFDATA="$(command -v llvm-profdata || true)"
LLVM_COV="$(command -v llvm-cov || true)"
if [[ -z "$LLVM_PROFDATA" || -z "$LLVM_COV" ]]; then
  echo "llvm-profdata/llvm-cov not found in PATH."
  exit 1
fi

find "$BUILD_DIR" -name "*.profraw" -print0 | xargs -0 "$LLVM_PROFDATA" merge -sparse -o "$BUILD_DIR/coverage.profdata"
"$LLVM_COV" report "$BUILD_DIR/sar_core_tests" \
  -instr-profile="$BUILD_DIR/coverage.profdata" \
  -ignore-filename-regex="visualizer|build|tests|SarTape2Generator/src/main.cpp"
