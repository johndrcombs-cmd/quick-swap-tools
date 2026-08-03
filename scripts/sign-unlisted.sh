#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
ARTIFACTS="$ROOT/dist/signed"

if [[ -z "${AMO_JWT_ISSUER:-}" ]]; then
  read -r -p "AMO JWT issuer: " AMO_JWT_ISSUER
fi
if [[ -z "${AMO_JWT_SECRET:-}" ]]; then
  read -r -s -p "AMO JWT secret: " AMO_JWT_SECRET
  printf '\n'
fi
if [[ -z "$AMO_JWT_ISSUER" || -z "$AMO_JWT_SECRET" ]]; then
  printf '%s\n' 'Both AMO credentials are required.' >&2
  exit 2
fi

umask 077
mkdir -p "$ARTIFACTS"

cd "$ROOT"
pnpm run check

# web-ext reads these through its WEB_EXT environment prefix. Keeping the
# credentials out of CLI arguments prevents the secret appearing in ps output.
export WEB_EXT_API_KEY="$AMO_JWT_ISSUER"
export WEB_EXT_API_SECRET="$AMO_JWT_SECRET"
unset AMO_JWT_ISSUER AMO_JWT_SECRET

pnpm exec web-ext sign \
  --source-dir extension \
  --artifacts-dir "$ARTIFACTS" \
  --channel unlisted \
  --approval-timeout 900000 \
  --no-input

printf '%s\n' 'Signed artifacts:'
shopt -s nullglob
for artifact in "$ARTIFACTS"/*.xpi; do
  printf '  %s\n' "$artifact"
done
