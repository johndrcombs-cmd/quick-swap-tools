#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
CXX=${CXX_WINDOWS:-x86_64-w64-mingw32-g++}

command -v "$CXX" >/dev/null || {
  printf 'Missing %s. Install the MinGW-w64 GCC cross-compiler.\n' "$CXX" >&2
  exit 1
}

mkdir -p "$ROOT/build/windows"
COMMON_FLAGS=(
  -std=c++20 -O2 -s -Wall -Wextra -Wpedantic -Werror
  -DUNICODE -D_UNICODE
  -static -static-libgcc -static-libstdc++
)

"$CXX" "${COMMON_FLAGS[@]}" -mwindows -municode \
  "$ROOT/native/windows/quick-swap-tools.cpp" \
  -lshell32 \
  -o "$ROOT/build/windows/quick-swap-tools.exe"

"$CXX" "${COMMON_FLAGS[@]}" -mwindows -municode -DQST_CONFIG_ONLY -Wno-unused-function \
  "$ROOT/native/windows/quick-swap-tools.cpp" \
  -o "$ROOT/build/windows/quick-swap-config.exe"

printf 'Built Windows host and configurator in %s\n' "$ROOT/build/windows"
