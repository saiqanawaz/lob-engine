#!/usr/bin/env bash
# Build the C++ engine to a single-file ES6 WebAssembly module.
# Output is committed so the site deploys as pure static files.
set -euo pipefail
cd "$(dirname "$0")/../.."

em++ -O2 -std=c++20 -Iinclude \
  src/order_book.cpp src/matching_engine.cpp web/wasm/embind.cpp \
  --bind \
  -s MODULARIZE=1 \
  -s EXPORT_ES6=1 \
  -s EXPORT_NAME=createLobModule \
  -s SINGLE_FILE=1 \
  -s ALLOW_MEMORY_GROWTH=1 \
  -s ENVIRONMENT=web \
  -o web/js/lob-engine.js

echo "wrote web/js/lob-engine.js"
