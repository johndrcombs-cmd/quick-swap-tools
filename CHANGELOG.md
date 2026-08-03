# Changelog

All notable changes to Quick Swap Tools are documented here.

## Unreleased

- Add device-filtered Logitech R400 support on Windows: Previous starts the next
  auction and Next starts the next giveaway without treating Page Up/Page Down
  from other keyboards as Quick Swap controls.
- Add a game-style KDE control configurator for keyboards, macro pads, and
  controller buttons mapped to keyboard keys.
- Preserve user-selected global shortcuts through KDE `KGlobalAccel` and warn
  when an unmodified global key could also trigger from the main keyboard.
- Make the source-development installer refuse existing installations and
  unrelated desktop entries, and roll back files, manifests, state, and KDE
  shortcuts if installation fails.
- Exclude local Mozilla upload-tracking files from development packages.
- Add a dependency-light Windows 10/11 x64 native-messaging host using
  `RegisterHotKey`, a game-style Win32 shortcut configurator, and safe
  transactional live rebinding.
- Add a no-admin Windows development ZIP with checksum and Authenticode policy,
  closed per-user Firefox registration, ownership-checking uninstall, and
  native Windows CI. Public Windows release remains pending code signing and
  real Windows/Firefox acceptance testing.

## 0.1.0 — 2026-08-02

- Add persistent KDE Plasma global shortcuts for auctions and giveaways.
- Add safe semantic Whatnot control discovery and cross-mode transitions.
- Add Firefox native messaging and Mozilla-signed extension distribution.
- Add a rootless Linux KDE release installer and uninstaller.
- Add shortcut-conflict protection and restoration of Plasma defaults.