#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
IMAGE="quick-swap-tools-build:archlinux"

command -v docker >/dev/null || {
  printf 'Docker is required for a portable release build.\n' >&2
  exit 1
}

docker build --pull \
  --file "$ROOT/packaging/linux-kde/Dockerfile.build" \
  --tag "$IMAGE" \
  "$ROOT/packaging/linux-kde"
docker run --rm \
  --user "$(id -u):$(id -g)" \
  --volume "$ROOT:/src" \
  "$IMAGE" \
  bash scripts/build-native.sh

if readelf -n "$ROOT/build/quick-swap-host" | grep -q 'ISA needed:.*x86-64-v3'; then
  printf 'Portable build unexpectedly requires x86-64-v3.\n' >&2
  exit 1
fi
printf 'Verified portable x86-64 native host.\n'
