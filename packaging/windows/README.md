# Quick Swap Tools for Windows — development bundle

This bundle targets **64-bit Windows 10/11** and **Firefox**. It installs only for the current Windows user and does not request administrator access.

> This is an unsigned development build. Windows SmartScreen may warn about both executables. Public releases should be Authenticode-signed before distribution to nontechnical users.

## Install

1. Extract the entire ZIP to a normal folder on a local drive.
2. Open Windows PowerShell in the extracted folder.
3. For this explicitly unsigned development build, run:

   ```powershell
   powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\install.ps1 -AllowUnsignedDevelopment
   ```

4. In Firefox, press **Ctrl+O**, select the included `Quick-Swap-Tools-*-firefox.xpi`, and approve **Add**.
5. Open **Quick Swap Tools** from the Windows Start Menu to configure controls.

The installer verifies every payload checksum, the extension ID, and expected Mozilla XPI signature metadata before writing anything. Firefox performs the authoritative cryptographic extension-signature check when you add the XPI. The installer installs under `%LOCALAPPDATA%\Programs\Quick Swap Tools`, registers the Firefox native host under the current user's Mozilla registry key, and creates one Start Menu shortcut.

## Default controls

- **Ctrl+Shift+F9** — start the next Auction
- **Ctrl+Shift+F10** — start the next Giveaway

Windows reserves many `Win+…` shortcuts, including common combinations such as `Win+A` and `Win+G`, so the Windows defaults intentionally differ from Linux.

To rebind a control, click its binding, press the desired key or key combination, then click **Apply**. The configurator updates a running Firefox host and restores the prior pair if Windows rejects either replacement.

### Tartarus, macro pads, and controllers

Quick Swap Tools records the keyboard key emitted by a device. `F13`–`F24` are recommended for a Razer Tartarus or other macro pad. If two physical keyboards emit the same key, the normal Windows global-hotkey API cannot distinguish which device produced it. Map gamepad, HID-only, or vendor-only buttons to keyboard keys before recording them.

## Uninstall

Close Firefox, then run:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$env:LOCALAPPDATA\Programs\Quick Swap Tools\uninstall.ps1"
```

The uninstaller verifies that the manifest, registry entry, and Start Menu shortcut belong to this installation before deleting them. It removes only Quick Swap Tools' four owned shortcut-setting values and preserves unrelated values or subkeys. Remove the Firefox extension separately from `about:addons` if it is no longer wanted.

## Signing status

The included immutable Firefox XPI is Mozilla-signed. The Windows executables and development ZIP are not yet Authenticode-signed. Do not relabel this development artifact as a production release.

Third-party license notices for the vendored JSON for Modern C++ parser are included in `THIRD_PARTY_NOTICES.md`.
