#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
mkdir -p "$ROOT/build"

# pkg-config emits one shell-safe compiler/linker argument per whitespace token.
# shellcheck disable=SC2046
g++ -std=c++20 -O2 -fPIC -no-pie -march=x86-64 -mtune=generic \
  -Wall -Wextra -Wpedantic \
  "$ROOT/native/quick-swap-host.cpp" \
  -o "$ROOT/build/quick-swap-host" \
  $(pkg-config --cflags --libs Qt6Core Qt6Gui Qt6DBus) \
  -I/usr/include/KF6/KGlobalAccel \
  -lKF6GlobalAccel

printf 'Built %s\n' "$ROOT/build/quick-swap-host"
