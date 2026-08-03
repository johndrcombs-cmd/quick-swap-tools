# Quick Swap Tools

Quick Swap Tools is a separate, low-latency control path for Whatnot seller streams on KDE Plasma/Wayland and Firefox.

## Controls

| Shortcut | Action |
|---|---|
| `Super+A` | Start the next auction |
| `Super+G` | Start the next giveaway |

The host applies only a 150 ms hardware key-bounce filter. Distinct presses are queued, while duplicate delivery of the same command ID is suppressed. The page script refuses to click when the matching control is hidden, disabled, covered, missing, or ambiguous.

## Configure controls

Open **Quick Swap Tools** from KDE's application launcher. Click the auction or
giveaway binding, press the desired keyboard, macro-pad, or controller button,
then click **Apply**. The configurator checks for duplicate and system-wide
conflicts, warns before accepting potentially disruptive unmodified keys, and
can reset the defaults.

The Razer Tartarus Pro appears to Linux as a keyboard, so its keys can be
recorded directly. KDE sees the key emitted by the device rather than the
physical label such as “Key 20.” To keep a binding specific in practice, map
the Tartarus or controller button to an unused key such as `F13`–`F24`, then
record that key in Quick Swap Tools. A gamepad that emits only joystick-button
events needs Input Remapper, Steam Input, or a similar keyboard mapper first.

## Why this architecture

The installed native host is a 50 KB dynamically linked C++ executable. Firefox starts it once and keeps it connected through native messaging. It registers directly with KDE's `KGlobalAccel`, so a hotkey does not launch a shell, Python process, browser, or desktop automation tool:

```text
KGlobalAccel → native host → Firefox native messaging → Whatnot content script
```

This is faster and more reliable on Wayland than coordinate clicking, `ydotool`, Firefox remote debugging, or launching a command for every keypress. Chrome would not make this path faster, so Firefox remains the target.

## Install a signed release

Download the `quick-swap-tools-*-linux-kde-x86_64.tar.gz` bundle and matching
`.sha256` sidecar from the [latest GitHub release](../../releases/latest), then
verify and extract it:

```bash
sha256sum -c quick-swap-tools-0.1.0-linux-kde-x86_64.tar.gz.sha256
tar -xzf quick-swap-tools-0.1.0-linux-kde-x86_64.tar.gz
cd quick-swap-tools-0.1.0-linux-kde-x86_64
./install.sh
```

The release installer does not need root. It installs the native host, safely
claims the two KDE shortcuts, and prints the local path to the included
Mozilla-signed XPI. Open that XPI in Firefox and click **Add**.

Requirements:

- x86-64 Linux;
- KDE Plasma 6 with glibc 2.34+, Qt 6.11+, and KDE Frameworks 6.12+
  GlobalAccel and XmlGui runtime libraries;
- Firefox 140 or newer;
- Python 3 for the one-time native-messaging manifest setup.

### Install from source for development

```bash
cd ~/quick-swap-tools
./scripts/install.sh
```

The source installer:

- refuses to overwrite existing installations, manifests, or desktop entries;
- rolls back files and shortcut changes if installation fails;
- builds and installs the native host at `~/.local/lib/quick-swap-tools/`;
- installs the Firefox native-host manifest;
- packages the extension;
- backs up the prior `Super+A` and `Super+G` KDE assignments;
- assigns the shortcuts to Quick Swap Tools.

Before installation, `Super+A` walked through Plasma Activities and `Super+G` opened Grid View. Those bindings are saved and the uninstall script restores them.

### Install a locally signed Firefox release

After installing the native host, open the Mozilla-signed XPI in Firefox and approve installation:

```text
dist/release/Quick-Swap-Tools-0.1.0-firefox.xpi
```

The signed release stays installed across Firefox restarts. It has the permanent extension ID `quick-swap-tools@onibyts.com`.

### Temporary development builds only

When testing unpublished source changes, Firefox can load the local source temporarily:

1. Open `about:debugging#/runtime/this-firefox`.
2. Click **Load Temporary Add-on…**.
3. Select:
   `~/.local/lib/quick-swap-tools/extension/manifest.json`

When reloading a changed development build, refresh any already-open Whatnot tab once so Firefox replaces its old content script.

Release signing is configured through `pnpm run sign:unlisted`. See [`docs/FIREFOX_SIGNING.md`](docs/FIREFOX_SIGNING.md) for Mozilla account setup, secure credential handling, signed installation, updates, and friend distribution.

## Safe verification

Never test `Super+A` or `Super+G` against an active show unless you intend to start the queued item.

After loading the extension, verify the native connection:

```bash
pgrep -af quick-swap-host
qdbus6 org.kde.kglobalaccel /component/quick_swap_tools \
  org.kde.kglobalaccel.Component.isActive
```

Inspect current Whatnot control labels **without clicking**:

```bash
qdbus6 org.kde.kglobalaccel /component/quick_swap_tools \
  org.kde.kglobalaccel.Component.invokeShortcut inspect-controls
cat ~/.local/state/quick-swap-tools/last-result.json
```

## Whatnot selector safety

Quick Swap Tools re-queries the live React DOM for every action. It uses visible, enabled controls and exact normalized labels rather than generated CSS classes. The script first selects the requested `Auction`/`Auctions` or `Giveaway`/`Giveaways` tab. It waits for Whatnot's React UI to finish changing modes, then uses one unique visible action control. Current accepted action labels are:

- `Start next auction`
- `Run next auction`
- `Start auction`
- `Start next giveaway`
- `Run next giveaway`
- `Start giveaway`
- `Run next`

If Whatnot uses a different label, run the safe inspection command and add that exact label with a regression test before changing production matching.

## Bluetooth scroll ring later

Most TikTok-style Bluetooth rings identify as HID keyboards or media remotes. Map two physical ring inputs to `Super+A` and `Super+G`; no browser or Whatnot code changes are required. If the ring emits only wheel events, add a device-specific input adapter that forwards deliberate ring gestures to the same two native actions. The native host's 150 ms key-bounce filter protects against switch chatter without blocking normal rapid input.

## Uninstall

```bash
cd ~/quick-swap-tools
./scripts/uninstall.sh
```

This removes the host, extension files, native manifest, and launcher, then restores the previous KDE shortcut values.

## Development checks

```bash
npm run check
```

This builds the native host and runs the JavaScript selector/routing tests, Python protocol tests, native-process integration test, and syntax checks.

Build the distributable Linux KDE bundle from an already signed XPI. Release
builds use a clean standard Arch Linux Docker image so the native binary does
not inherit CachyOS x86-64-v3 compiler defaults:

```bash
pnpm run build:release
```

## License

Copyright © 2026 OniByts.

Quick Swap Tools is free software licensed under the GNU General Public License,
version 3 or any later version (`GPL-3.0-or-later`). See [`LICENSE`](LICENSE).
