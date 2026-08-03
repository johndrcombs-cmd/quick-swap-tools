#!/usr/bin/env bash
set -euo pipefail

INSTALL_ROOT="$HOME/.local/lib/quick-swap-tools"
STATE_ROOT="$HOME/.local/state/quick-swap-tools"
HOST_NAME="com.onibyts.quickswap"

pkill -u "$(id -u)" -x quick-swap-host 2>/dev/null || true
if [[ -f "$STATE_ROOT/original-grid-view" ]]; then
  kwriteconfig6 --file kglobalshortcutsrc --group kwin --key "Grid View" \
    "$(<"$STATE_ROOT/original-grid-view")"
fi
if [[ -f "$STATE_ROOT/original-next-activity" ]]; then
  kwriteconfig6 --file kglobalshortcutsrc --group plasmashell --key "next activity" \
    "$(<"$STATE_ROOT/original-next-activity")"
fi
kwriteconfig6 --file kglobalshortcutsrc --delete-group quick-swap-tools || true

rm -rf "$INSTALL_ROOT"
rm -f \
  "$HOME/.mozilla/native-messaging-hosts/$HOST_NAME.json" \
  "$HOME/.config/mozilla/native-messaging-hosts/$HOST_NAME.json" \
  "$HOME/.local/share/applications/quick-swap-tools.desktop"
rm -rf "$STATE_ROOT"

printf 'Quick Swap Tools removed. Original Meta+A/Meta+G settings were restored in KDE config.\n'
printf 'Log out and back in if KDE still shows a stale shortcut registration.\n'
