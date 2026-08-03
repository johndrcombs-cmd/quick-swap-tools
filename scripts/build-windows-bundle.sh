#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
VERSION=${1:-0.2.0-dev}
OUTPUT_ROOT=${2:-"$ROOT/dist/windows"}

PYTHON=
for Candidate in python3 python; do
  if command -v "$Candidate" >/dev/null &&
      "$Candidate" -c 'import sys; raise SystemExit(sys.version_info < (3, 8))' >/dev/null 2>&1; then
    PYTHON=$Candidate
    break
  fi
done
if [[ -z $PYTHON ]]; then
  printf 'Python 3 is required to build the Windows bundle\n' >&2
  exit 1
fi

"$ROOT/scripts/build-windows.sh" >/dev/null

shopt -s nullglob
XPI_FILES=("$ROOT"/dist/release/Quick-Swap-Tools-*-firefox.xpi)
shopt -u nullglob
[[ ${#XPI_FILES[@]} -eq 1 ]] || {
  printf 'Expected exactly one Mozilla-signed Firefox XPI in dist/release\n' >&2
  exit 1
}

"$PYTHON" - "${XPI_FILES[0]}" <<'PY'
import json
import sys
import zipfile

path = sys.argv[1]
required_signatures = {
    "META-INF/cose.sig",
    "META-INF/manifest.mf",
    "META-INF/mozilla.rsa",
    "META-INF/mozilla.sf",
}
with zipfile.ZipFile(path) as archive:
    names = set(archive.namelist())
    if not required_signatures.issubset(names):
        raise SystemExit("The Firefox XPI is missing Mozilla signature metadata")
    manifest = json.loads(archive.read("manifest.json"))
    extension_id = manifest["browser_specific_settings"]["gecko"]["id"]
    if extension_id != "quick-swap-tools@onibyts.com":
        raise SystemExit("The Firefox XPI has the wrong extension ID")
PY

mkdir -p "$OUTPUT_ROOT"
OUTPUT_ROOT=$(cd -- "$OUTPUT_ROOT" && pwd)
STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT

install -m 0755 "$ROOT/build/windows/quick-swap-tools.exe" "$STAGE/quick-swap-tools.exe"
install -m 0755 "$ROOT/build/windows/quick-swap-config.exe" "$STAGE/quick-swap-config.exe"
install -m 0644 "$ROOT/packaging/windows/install.ps1" "$STAGE/install.ps1"
install -m 0644 "$ROOT/packaging/windows/uninstall.ps1" "$STAGE/uninstall.ps1"
install -m 0644 "$ROOT/packaging/windows/README.md" "$STAGE/README.md"
install -m 0644 "$ROOT/LICENSE" "$STAGE/LICENSE"
install -m 0644 "$ROOT/THIRD_PARTY_NOTICES.md" "$STAGE/THIRD_PARTY_NOTICES.md"
install -m 0644 "${XPI_FILES[0]}" "$STAGE/${XPI_FILES[0]##*/}"

"$PYTHON" - "$STAGE" "${XPI_FILES[0]##*/}" <<'PY'
import hashlib
import pathlib
import sys

stage = pathlib.Path(sys.argv[1])
names = [
    "LICENSE",
    "README.md",
    "THIRD_PARTY_NOTICES.md",
    sys.argv[2],
    "install.ps1",
    "quick-swap-config.exe",
    "quick-swap-tools.exe",
    "uninstall.ps1",
]
lines = [f"{hashlib.sha256((stage / name).read_bytes()).hexdigest()}  {name}\n" for name in names]
(stage / "SHA256SUMS").write_bytes("".join(lines).encode("utf-8"))
PY

ARCHIVE="$OUTPUT_ROOT/quick-swap-tools-${VERSION}-windows-x86_64-development.zip"
rm -f "$ARCHIVE"
"$PYTHON" - "$STAGE" "$ARCHIVE" <<'PY'
import pathlib
import stat
import sys
import zipfile

stage = pathlib.Path(sys.argv[1])
archive_path = pathlib.Path(sys.argv[2])
with zipfile.ZipFile(
    archive_path, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9
) as archive:
    for path in sorted(stage.iterdir(), key=lambda candidate: candidate.name):
        info = zipfile.ZipInfo(path.name, date_time=(1980, 1, 1, 0, 0, 0))
        info.compress_type = zipfile.ZIP_DEFLATED
        info.create_system = 3
        mode = 0o755 if path.suffix == ".exe" else 0o644
        info.external_attr = (stat.S_IFREG | mode) << 16
        archive.writestr(info, path.read_bytes(), compresslevel=9)
PY

printf '%s\n' "$ARCHIVE"
