#!/bin/bash
# Run perft validation on the Revolution engine.
# Builds the engine if needed and executes the existing test suite.

set -e
ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
ENGINE_DEFAULT="$ROOT_DIR/src/revolution v2.74-dev-250925-tsd1"
ENGINE="${ENGINE:-$ENGINE_DEFAULT}"
TEST_DIR="$ROOT_DIR/tests"

if [ ! -x "$ENGINE" ]; then
  echo "Building engine..."
  (cd "$ROOT_DIR/src" && make -j2 build ARCH=x86-64 >/dev/null)
fi

(cd "$TEST_DIR" && ./perft.sh "$ENGINE")
