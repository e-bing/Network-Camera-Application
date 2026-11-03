#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

[[ "${1:-}" == "clean" ]] && rm -rf build

cmake -S . -B build
cmake --build build -j"$(nproc)"
(cd build && ctest --output-on-failure)

./build/network-camera