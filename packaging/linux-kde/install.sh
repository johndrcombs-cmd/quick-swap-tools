#!/usr/bin/env bash
set -euo pipefail

BUNDLE_ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
INSTALL_ROOT="$HOME/.local/lib/quick-swap-tools"
STATE_ROOT="$HOME/.local/state/quick-swap-tools"
HOST_NAME="com.onibyts.quickswap"
EXTENSION_ID="quick-swap-tools@onibyts.com"
DESKTOP_ENTRY="$HOME/.local/share/applications/quick-swap-tools.desktop"
shopt -s nullglob
XPI_FILES=("$BUNDLE_ROOT"/Quick-Swap-Tools-*-firefox.xpi)
shopt -u nullglob
[[ ${#XPI_FILES[@]} -eq 1 ]] || {
  printf 'Quick Swap Tools: expected exactly one signed Firefox XPI in the bundle\n' >&2
  exit 1
}
XPI_PATH=${XPI_FILES[0]}
XPI_NAME=${XPI_PATH##*/}

fail() {
  printf 'Quick Swap Tools: %s\n' "$*" >&2
  exit 1
}

[[ "$(uname -m)" == "x86_64" ]] || fail "this bundle supports x86-64 Linux only"
[[ -x "$BUNDLE_ROOT/quick-swap-host" ]] || fail "quick-swap-host is missing"
[[ -x "$BUNDLE_ROOT/quick-swap-config" ]] || fail "quick-swap-config is missing"
[[ -f "$XPI_PATH" ]] || fail "$XPI_NAME is missing"
command -v kreadconfig6 >/dev/null || fail "KDE Plasma 6 command kreadconfig6 is required"
command -v kwriteconfig6 >/dev/null || fail "KDE Plasma 6 command kwriteconfig6 is required"
command -v python3 >/dev/null || fail "Python 3 is required for installation"
command -v sha256sum >/dev/null || fail "sha256sum is required for integrity verification"

(
  cd "$BUNDLE_ROOT"
  sha256sum -c --quiet SHA256SUMS
) || fail "bundle integrity verification failed"

[[ ! -e "$INSTALL_ROOT" ]] || fail "an installation already exists; run its uninstall.sh first"
[[ ! -e "$STATE_ROOT" ]] || fail "old installer state exists at $STATE_ROOT; remove it after confirming Quick Swap Tools is uninstalled"
[[ ! -e "$HOME/.mozilla/native-messaging-hosts/$HOST_NAME.json" ]] || \
  fail "a Firefox native-host manifest already exists; uninstall the prior version first"
[[ ! -e "$HOME/.config/mozilla/native-messaging-hosts/$HOST_NAME.json" ]] || \
  fail "a Firefox native-host manifest already exists; uninstall the prior version first"
[[ ! -e "$DESKTOP_ENTRY" ]] || \
  fail "a desktop entry already exists at $DESKTOP_ENTRY; uninstall the prior version first"

if command -v ldd >/dev/null; then
  for binary in quick-swap-host quick-swap-config; do
    if ldd "$BUNDLE_ROOT/$binary" 2>&1 | grep -q 'not found'; then
      ldd "$BUNDLE_ROOT/$binary" >&2 || true
      fail "required Qt 6 or KDE Frameworks 6 libraries are missing"
    fi
  done
fi

"$BUNDLE_ROOT/quick-swap-host" --check-shortcuts || \
  fail "resolve the reported shortcut conflict and retry"

ROLLBACK_NEEDED=false
rollback() {
  local status=$?
  if [[ "$ROLLBACK_NEEDED" == true ]]; then
    if [[ -f "$STATE_ROOT/original-grid-view" ]]; then
      kwriteconfig6 --file kglobalshortcutsrc --group kwin --key "Grid View" \
        "$(<"$STATE_ROOT/original-grid-view")" || true
    fi
    if [[ -f "$STATE_ROOT/original-next-activity" ]]; then
      kwriteconfig6 --file kglobalshortcutsrc --group plasmashell --key "next activity" \
        "$(<"$STATE_ROOT/original-next-activity")" || true
    fi
    kwriteconfig6 --file kglobalshortcutsrc --delete-group quick-swap-tools || true
    rm -rf "$INSTALL_ROOT" "$STATE_ROOT"
    rm -f \
      "$HOME/.mozilla/native-messaging-hosts/$HOST_NAME.json" \
      "$HOME/.config/mozilla/native-messaging-hosts/$HOST_NAME.json" \
      "$DESKTOP_ENTRY"
  fi
  exit "$status"
}
trap rollback EXIT

mkdir -p "$INSTALL_ROOT" \
  "$HOME/.local/share/applications" \
  "$HOME/.mozilla/native-messaging-hosts" \
  "$HOME/.config/mozilla/native-messaging-hosts"
install -d -m 0700 "$STATE_ROOT"
ROLLBACK_NEEDED=true

if [[ ! -f "$STATE_ROOT/original-grid-view" ]]; then
  kreadconfig6 --file kglobalshortcutsrc --group kwin --key "Grid View" \
    >"$STATE_ROOT/original-grid-view"
fi
if [[ ! -f "$STATE_ROOT/original-next-activity" ]]; then
  kreadconfig6 --file kglobalshortcutsrc --group plasmashell --key "next activity" \
    >"$STATE_ROOT/original-next-activity"
fi
if [[ -f "$HOME/.config/kglobalshortcutsrc" && ! -f "$STATE_ROOT/kglobalshortcutsrc.before-install" ]]; then
  cp "$HOME/.config/kglobalshortcutsrc" "$STATE_ROOT/kglobalshortcutsrc.before-install"
fi

install -m 0755 "$BUNDLE_ROOT/quick-swap-host" "$INSTALL_ROOT/quick-swap-host"
install -m 0755 "$BUNDLE_ROOT/quick-swap-config" "$INSTALL_ROOT/quick-swap-config"
install -m 0755 "$BUNDLE_ROOT/uninstall.sh" "$INSTALL_ROOT/uninstall.sh"
install -m 0644 "$XPI_PATH" "$INSTALL_ROOT/$XPI_NAME"
install -m 0644 "$BUNDLE_ROOT/LICENSE" "$INSTALL_ROOT/LICENSE"

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

"$INSTALL_ROOT/quick-swap-host" --register-only || \
  fail "shortcut registration failed; resolve the reported shortcut conflict and retry"

cat >"$DESKTOP_ENTRY" <<EOF
[Desktop Entry]
Type=Application
Name=Quick Swap Tools
Comment=Configure Whatnot auction and giveaway controls
Exec="$INSTALL_ROOT/quick-swap-config"
Icon=preferences-desktop-keyboard-shortcuts
Terminal=false
Categories=Utility;
EOF

ROLLBACK_NEEDED=false
trap - EXIT

printf '\nQuick Swap Tools native host installed.\n'
printf 'Super+A: next auction | Super+G: next giveaway\n'
printf 'Configure controls with: %s\n' "$INSTALL_ROOT/quick-swap-config"
printf 'Now open this signed Firefox extension and click Add:\n  %s\n' \
  "$INSTALL_ROOT/$XPI_NAME"
printf 'Uninstall with: %s\n' "$INSTALL_ROOT/uninstall.sh"
