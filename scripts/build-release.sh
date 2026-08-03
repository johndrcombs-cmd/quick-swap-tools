#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
VERSION=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["version"])' \
  "$ROOT/package.json")
XPI=${1:-"$ROOT/dist/release/Quick-Swap-Tools-$VERSION-firefox.xpi"}
BUNDLE_NAME="quick-swap-tools-$VERSION-linux-kde-x86_64"
STAGE="$ROOT/dist/release/$BUNDLE_NAME"
ARCHIVE="$ROOT/dist/release/$BUNDLE_NAME.tar.gz"

[[ -z "$(git -C "$ROOT" status --porcelain)" ]] || {
  printf 'Release builds require a clean working tree.\n' >&2
  exit 1
}
[[ "$(git -C "$ROOT" describe --tags --exact-match 2>/dev/null || true)" == "v$VERSION" ]] || {
  printf 'Release builds require HEAD to be tagged v%s.\n' "$VERSION" >&2
  exit 1
}

[[ -f "$XPI" ]] || {
  printf 'Signed XPI not found: %s\n' "$XPI" >&2
  exit 1
}

python3 - "$XPI" "$VERSION" "$ROOT/extension" <<'PY'
import json
import sys
import zipfile
from pathlib import Path

xpi = Path(sys.argv[1])
expected_version = sys.argv[2]
source_root = Path(sys.argv[3])
with zipfile.ZipFile(xpi) as archive:
    names = set(archive.namelist())
    manifest = json.loads(archive.read("manifest.json"))
    required_signatures = {
        "META-INF/cose.manifest",
        "META-INF/cose.sig",
        "META-INF/manifest.mf",
        "META-INF/mozilla.rsa",
        "META-INF/mozilla.sf",
    }
    missing_signatures = required_signatures - names
    assert not missing_signatures, f"Mozilla signature metadata missing: {sorted(missing_signatures)}"
    source_files = {
        path.relative_to(source_root).as_posix(): path
        for path in source_root.rglob("*")
        if path.is_file() and not path.name.startswith(".")
    }
    packaged_files = {name for name in names if not name.startswith("META-INF/")}
    assert packaged_files == set(source_files), "signed XPI does not match extension source files"
    for name, source_path in source_files.items():
        if name == "manifest.json":
            assert json.loads(archive.read(name)) == json.loads(source_path.read_bytes()), \
                "signed XPI manifest differs from source"
        else:
            assert archive.read(name) == source_path.read_bytes(), \
                f"signed XPI differs from source: {name}"
assert manifest["version"] == expected_version, manifest["version"]
assert manifest["browser_specific_settings"]["gecko"]["id"] == "quick-swap-tools@onibyts.com"
PY

"$ROOT/scripts/build-native-portable.sh"
rm -rf "$STAGE" "$ARCHIVE"
mkdir -p "$STAGE"
install -m 0755 "$ROOT/build/quick-swap-host" "$STAGE/quick-swap-host"
install -m 0755 "$ROOT/packaging/linux-kde/install.sh" "$STAGE/install.sh"
install -m 0755 "$ROOT/packaging/linux-kde/uninstall.sh" "$STAGE/uninstall.sh"
install -m 0644 "$ROOT/packaging/linux-kde/README.md" "$STAGE/README.md"
install -m 0644 "$ROOT/LICENSE" "$STAGE/LICENSE"
install -m 0644 "$XPI" "$STAGE/Quick-Swap-Tools-$VERSION-firefox.xpi"
(
  cd "$STAGE"
  sha256sum \
    quick-swap-host install.sh uninstall.sh README.md LICENSE \
    "Quick-Swap-Tools-$VERSION-firefox.xpi" > SHA256SUMS
)
tar --sort=name --owner=0 --group=0 --numeric-owner \
  -C "$ROOT/dist/release" -czf "$ARCHIVE" "$BUNDLE_NAME"
(
  cd "$ROOT/dist/release"
  sha256sum "$(basename "$ARCHIVE")" > "$(basename "$ARCHIVE").sha256"
)
printf 'Built %s\n' "$ARCHIVE"
