# Quick Swap Tools 0.1.0 — Linux KDE bundle

This bundle installs the native KDE shortcut host and includes the Mozilla-signed Firefox extension.

## Requirements

- x86-64 Linux
- KDE Plasma 6 on Wayland or X11
- Qt 6 and KDE Frameworks 6 GlobalAccel runtime libraries
- Firefox 140 or newer
- Python 3 (used only during installation to write the native-messaging manifest)

On Arch/CachyOS, the runtime dependencies are provided by `qt6-base` and `kglobalaccel`.

## Install

Download both the `.tar.gz` bundle and its `.sha256` sidecar from the GitHub
release, then verify the download before extracting it:

```bash
sha256sum -c quick-swap-tools-0.1.0-linux-kde-x86_64.tar.gz.sha256
tar -xzf quick-swap-tools-0.1.0-linux-kde-x86_64.tar.gz
cd quick-swap-tools-0.1.0-linux-kde-x86_64
```

The installer also verifies every bundled executable and payload against the
archive's internal `SHA256SUMS` file before making changes.

```bash
./install.sh
```

The installer never requires root. It checks for unexpected shortcut conflicts before changing them, preserves the normal Plasma `Super+A` and `Super+G` assignments, and prints the path to the signed Firefox XPI.

Open that XPI in Firefox and click **Add**. Quick Swap Tools then remains installed across Firefox restarts.

## Controls

- `Super+A`: switch to Auctions and start the next auction.
- `Super+G`: switch to Giveaways and start the next giveaway.

## Uninstall

```bash
~/.local/lib/quick-swap-tools/uninstall.sh
```

Then remove Quick Swap Tools from `about:addons` in Firefox.

## Source and license

Corresponding source for this release:
https://github.com/johndrcombs-cmd/quick-swap-tools/tree/v0.1.0

Quick Swap Tools is licensed under GPL-3.0-or-later. See `LICENSE`.
