#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
INSTALL_ROOT="$HOME/.local/lib/quick-swap-tools"
STATE_ROOT="$HOME/.local/state/quick-swap-tools"
HOST_NAME="com.onibyts.quickswap"
EXTENSION_ID="quick-swap-tools@onibyts.com"

"$ROOT/scripts/build-native.sh"
"$ROOT/build/quick-swap-host" --check-shortcuts

mkdir -p "$INSTALL_ROOT" \
  "$HOME/.local/share/applications" \
  "$HOME/.mozilla/native-messaging-hosts" \
  "$HOME/.config/mozilla/native-messaging-hosts"
install -d -m 0700 "$STATE_ROOT"
install -m 0755 "$ROOT/build/quick-swap-host" "$INSTALL_ROOT/quick-swap-host"
rm -rf "$INSTALL_ROOT/extension"
cp -a "$ROOT/extension" "$INSTALL_ROOT/extension"

python3 - "$INSTALL_ROOT/extension" "$INSTALL_ROOT/quick-swap-tools.xpi" <<'PY'
import sys
import zipfile
from pathlib import Path
source = Path(sys.argv[1])
target = Path(sys.argv[2])
with zipfile.ZipFile(target, "w", zipfile.ZIP_DEFLATED) as archive:
    for path in sorted(source.rglob("*")):
        if path.is_file():
            archive.write(path, path.relative_to(source))
PY

python3 - "$INSTALL_ROOT/quick-swap-host" "$EXTENSION_ID" \
  "$HOME/.mozilla/native-messaging-hosts/$HOST_NAME.json" \
  "$HOME/.config/mozilla/native-messaging-hosts/$HOST_NAME.json" <<'PY'
import json
import sys
from pathlib import Path
host, extension_id, *destinations = sys.argv[1:]
manifest = {
    "name": "com.onibyts.quickswap",
    "description": "Quick Swap Tools KDE hotkey bridge",
    "path": host,
    "type": "stdio",
    "allowed_extensions": [extension_id],
}
for destination in destinations:
    path = Path(destination)
    path.write_text(json.dumps(manifest, indent=2) + "\n")
    path.chmod(0o600)
PY

if command -v kreadconfig6 >/dev/null && [[ ! -f "$STATE_ROOT/original-grid-view" ]]; then
  kreadconfig6 --file kglobalshortcutsrc --group kwin --key "Grid View" \
    >"$STATE_ROOT/original-grid-view"
  kreadconfig6 --file kglobalshortcutsrc --group plasmashell --key "next activity" \
    >"$STATE_ROOT/original-next-activity"
fi
if [[ ! -f "$STATE_ROOT/kglobalshortcutsrc.before-install" ]]; then
  cp "$HOME/.config/kglobalshortcutsrc" "$STATE_ROOT/kglobalshortcutsrc.before-install"
fi

"$INSTALL_ROOT/quick-swap-host" --register-only

cat >"$HOME/.local/share/applications/quick-swap-tools.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=Quick Swap Tools
Comment=Whatnot auction and giveaway hotkeys
Exec=firefox about:debugging#/runtime/this-firefox
Icon=preferences-desktop-keyboard-shortcuts
Terminal=false
Categories=Utility;
EOF

printf '\nQuick Swap Tools installed.\n'
printf 'Firefox extension manifest: %s\n' "$INSTALL_ROOT/extension/manifest.json"
printf 'Super+A: next auction | Super+G: next giveaway\n'
printf 'Development build: load the manifest temporarily from about:debugging.\n'
printf 'For permanent installation, use the Mozilla-signed release bundle.\n'
