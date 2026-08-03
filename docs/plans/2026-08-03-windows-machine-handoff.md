# Quick Swap Tools — Windows Machine Agent Handoff

> **Give this entire document to the ARMY/agent team on the Windows computer.** Treat it as the source-of-truth handoff. Inspect the live repository and GitHub Actions before relying on line numbers, because later commits may shift them.

**Goal:** Finish native Windows 10/11 testing and development setup for Quick Swap Tools without changing the established Firefox/native-messaging architecture or publishing an unverified release.

**Repository:** <https://github.com/johndrcombs-cmd/quick-swap-tools>

**Branch:** `main`

**Expected starting commit:** `af73cbed867e2e9fdcf2f375f819102c6e7fda38`

**Current status at handoff:** The source is committed and pushed. Standard CI passes. Native Windows CI builds and passes protocol/policy tests, but the real install/uninstall step currently fails while creating the Start Menu shortcut because WScript rejects `$Shortcut.IconLocation = ""`. No Windows release has been published.

---

## 1. Copy/paste prompt for the Windows ARMY

```text
You are finishing the native Windows validation and setup for Quick Swap Tools.

Work in the repository:
https://github.com/johndrcombs-cmd/quick-swap-tools.git

Start from main at or after:
af73cbed867e2e9fdcf2f375f819102c6e7fda38

Read docs/plans/2026-08-03-windows-machine-handoff.md completely before changing anything. Also read README.md, packaging/windows/README.md, docs/plans/2026-08-02-windows-distribution.md, CONTRIBUTING.md, and SECURITY.md.

Use tests-first development. Inspect the current files before editing. Do not modify or retag v0.1.0. Do not publish a Windows release. Do not claim the package is production-ready, trusted, signed, or friend-ready until every release gate in the handoff is satisfied.

Preserve the architecture and protocol:
keyboard/macro-pad -> RegisterHotKey -> resident Firefox native-messaging host -> existing extension -> semantic Whatnot DOM activation.

Preserve actions `auction` and `giveaway`, the 150 ms physical-bounce interval, the 1500 ms extension reconnect delay, command-ID duplicate suppression, per-user HKCU registration, no-admin installation, semantic DOM safety, and Firefox.

First reproduce/fix the current shortcut creation failure caused by assigning an empty WScript IconLocation. Run the real installer/uninstaller using Windows PowerShell 5.1, not only PowerShell 7 parsing. Then run all native tests and the manual Firefox/Whatnot acceptance checklist. Keep a timestamped evidence log with commands, outputs, Windows version, Firefox version, screenshots, and failures.

Use only OpenAI Codex GPT-5.6 Sol at medium reasoning for agent work. Do not use Anthropic or other models.
```

---

## 2. Non-negotiable product constraints

Do not change these unless John explicitly approves:

- License: `GPL-3.0-or-later`.
- Browser: Firefox.
- Desktop-to-extension transport: Firefox native messaging.
- Native-host name: `com.onibyts.quickswap`.
- Firefox extension ID: `quick-swap-tools@onibyts.com`.
- Commands: `auction` and `giveaway`.
- Native-message framing: 4-byte native-endian length followed by UTF-8 JSON.
- Maximum native message: 1 MiB.
- Reject zero-length, oversized, truncated, malformed, invalid-UTF-8, ambiguous, and duplicate-key JSON.
- Never click screen coordinates.
- Never hardcode a private Whatnot livestream URL.
- Never activate hidden, disabled, covered, missing, unusable, or ambiguous controls.
- Continue accepting distinct rapid presses; suppress only repeat delivery of the same command ID.
- Physical-bounce interval: 150 ms per action.
- Extension reconnect delay: 1500 ms.
- Windows defaults:
  - `Ctrl+Shift+F9` → Auction.
  - `Ctrl+Shift+F10` → Giveaway.
- Reject F12 because Windows reserves it for debugger use.
- Recommend F13–F24 for a Tartarus or keyboard-emulating macro pad.
- `RegisterHotKey` identifies the emitted key combination, not the physical keyboard.
- Raw Input/HID/device-specific behavior is deferred.
- Installation is per-user and must not require elevation.
- Registry mutation must be limited to Quick Swap Tools-owned HKCU values.
- Uninstallation must preserve unrelated settings, registry entries, shortcuts, and files.
- Native-messaging stdout must contain framed protocol messages only. Diagnostics go to stderr or an owner-only privacy-safe log.
- Both executables use the Windows GUI subsystem to prevent console flashes.
- Do not expose personal paths, credentials, private URLs, or secrets in source, logs, archives, screenshots, or releases.
- Keep immutable release `v0.1.0` unchanged. Windows support must use a later version.

---

## 3. Architecture already implemented

```text
Keyboard or keyboard-emulating macro pad
        ↓
Win32 RegisterHotKey / WM_HOTKEY
        ↓
quick-swap-tools.exe
(resident GUI-subsystem native-messaging host)
        ↓
Firefox native messaging
        ↓
quick-swap-tools@onibyts.com
        ↓
Eligible Whatnot seller livestream tab
        ↓
Semantic DOM control selection and activation
```

Windows components:

- `native/windows/quick-swap-tools.cpp`
  - Native host and configurator implementation.
  - Win32 `RegisterHotKey` and `WM_HOTKEY`.
  - Paired hotkey ownership and reload transactions.
  - Strict JSON using vendored JSON for Modern C++ v3.11.3.
  - Decoded duplicate-key rejection using SAX before DOM extraction.
  - Bounded native-messaging frames.
  - Queued writer thread so blocked Firefox stdout cannot stall the hotkey thread.
  - Per-session hotkey-owner and configurator-transaction mutexes.
- `build/windows/quick-swap-tools.exe`
  - Generated native host; ignored by Git.
- `build/windows/quick-swap-config.exe`
  - Generated graphical configurator; ignored by Git.
- `packaging/windows/install.ps1`
  - Current-user installer, checksums, XPI metadata checks, signer policy, staged publication, rollback.
- `packaging/windows/uninstall.ps1`
  - Ownership-aware current-user removal.
- `.github/workflows/windows-ci.yml`
  - Native `windows-2022` build, tests, and real install/uninstall exercise.
- `tests/test_windows_distribution.py`
  - Policy, parser, protocol, PE, hotkey, ownership, bundle, and runtime tests.

Registry and filesystem locations:

```text
Install root:
%LOCALAPPDATA%\Programs\Quick Swap Tools

Firefox native-host registration:
HKCU\Software\Mozilla\NativeMessagingHosts\com.onibyts.quickswap

Shortcut settings:
HKCU\Software\OniByts\Quick Swap Tools

Start Menu shortcut:
Current-user Programs known folder\Quick Swap Tools.lnk
```

Named synchronization objects:

```text
Local\OniByts.QuickSwapTools.HotkeyOwner.v1
```

The configurator also uses a named transaction mutex and reload synchronization so Auction and Giveaway bindings change as one logical pair.

---

## 4. Known current failure — fix this first

### Evidence

Latest failing native Windows run:

<https://github.com/johndrcombs-cmd/quick-swap-tools/actions/runs/30786258194>

The run passed:

- MinGW installation.
- Native Windows build.
- PowerShell syntax parsing.
- Native Windows protocol/policy tests.

The real install/uninstall step reached shortcut creation and failed with:

```text
Value does not fall within the expected range.
At install.ps1:277
$Shortcut.IconLocation = ""
```

### Required TDD fix

1. Inspect the current versions of:
   - `packaging/windows/install.ps1`
   - `tests/test_windows_distribution.py`
   - `.github/workflows/windows-ci.yml`
2. Add a failing regression test proving the installer does not assign an invalid empty `IconLocation`.
3. Run the focused test and confirm RED.
4. Remove the invalid assignment.
5. Adjust rollback ownership validation so it compares every property the installer actually configures, but does not demand an icon property that was never configured.
6. Do not weaken validation for target, working directory, description, arguments, hotkey, or window style without a documented reason and a native test.
7. Run the policy tests and parse both scripts with Windows PowerShell 5.1.
8. Run the actual installer and uninstaller locally before pushing.
9. Commit the minimal fix, push, and require Windows CI to pass.

Do not merely parse the PowerShell. The previous failures proved that PowerShell 7/static parsing can miss Windows PowerShell 5.1 runtime incompatibilities.

Previous compatibility fixes already landed:

- Invalid static `[Uri]::IsUnc($Path)` was replaced with a URI instance and `$PathUri.IsUnc`.
- Ambiguous `Split-Path -LiteralPath $Current -Parent` was replaced with `[IO.Directory]::GetParent($Current)`.

Do not revert either fix.

---

## 5. Windows development-machine setup

### Required software

Install on 64-bit Windows 10 or 11:

- Git for Windows.
- Firefox release channel.
- Python 3.11 or newer.
- Node.js LTS.
- pnpm through Corepack.
- MinGW-w64 GCC 16.1.0 to match CI.
- GitHub CLI (`gh`).
- Windows SDK tools later if Authenticode signing is configured (`signtool.exe`).
- Optional: Visual Studio Code.

Chocolatey can install development dependencies from an elevated PowerShell terminal:

```powershell
choco install git python nodejs-lts gh --no-progress -y
choco install mingw --version=16.1.0 --no-progress -y
```

Then open a new non-admin PowerShell terminal and enable pnpm:

```powershell
corepack enable
corepack prepare pnpm@latest --activate
```

Verify:

```powershell
git --version
python --version
node --version
pnpm --version
g++ --version
gh --version
firefox --version
```

If `g++` is not on `PATH`, add the actual MinGW binary directory. GitHub Actions currently uses:

```text
C:\ProgramData\mingw64\mingw64\bin
```

### Clone and verify the handoff point

```powershell
cd $HOME
git clone https://github.com/johndrcombs-cmd/quick-swap-tools.git
cd quick-swap-tools
git checkout main
git pull --ff-only origin main
git status --short
git rev-parse HEAD
```

Expected starting SHA at the time of this handoff:

```text
af73cbed867e2e9fdcf2f375f819102c6e7fda38
```

If `main` is newer, inspect commits and CI before proceeding. Do not reset away newer valid work.

Install JavaScript dependencies:

```powershell
pnpm install --frozen-lockfile
```

Authenticate GitHub CLI if needed:

```powershell
gh auth login
```

Never paste tokens, certificate passwords, or private keys into agent prompts or committed files.

---

## 6. Native build and automated tests

Run from a normal, non-admin PowerShell terminal in the repository root.

### Build

Git Bash command:

```bash
CXX_WINDOWS=g++ scripts/build-windows.sh
```

Or from PowerShell:

```powershell
$env:CXX_WINDOWS = "g++"
bash scripts/build-windows.sh
```

Expected generated files:

```text
build\windows\quick-swap-tools.exe
build\windows\quick-swap-config.exe
```

Verify both are x64 GUI-subsystem binaries and do not flash a console window.

### Full project checks

```powershell
pnpm run check
```

### Native Windows runtime tests

```powershell
$env:QST_WINDOWS_RUNTIME_TESTS = "1"
python tests\test_windows_distribution.py -v
```

Run focused groups if diagnosing:

```powershell
python tests\test_windows_distribution.py WindowsHotkeyValidationTests -v
python tests\test_windows_distribution.py WindowsNativeHostTests -v
python tests\test_windows_distribution.py WindowsConfiguratorTests -v
python tests\test_windows_distribution.py WindowsInstallerPolicyTests -v
```

Requirements:

- No hangs.
- No native-message output on stderr interpreted as protocol data.
- All non-interactive tests pass.
- Interactive hotkey ownership tests run in a real interactive desktop session.
- Record exact test counts rather than estimating them.

---

## 7. Build a local unsigned development bundle

The existing local ZIP/hash from Linux is stale after installer changes. Rebuild on Windows; do not reuse or report an older hash.

The bundle builder expects the immutable Mozilla-signed `v0.1.0` XPI. Its known SHA-256 is:

```text
47a718aca9ef77ea71b7cab842a44064051ccd76523ae5ab94d841cbcfe55bac
```

Download from:

<https://github.com/johndrcombs-cmd/quick-swap-tools/releases/download/v0.1.0/Quick-Swap-Tools-0.1.0-firefox.xpi>

Place it where `scripts/build-windows-bundle.sh` expects it, then run:

```powershell
bash scripts/build-windows-bundle.sh
```

Validate the resulting ZIP:

```powershell
Get-FileHash .\dist\windows\quick-swap-tools-0.2.0-dev-windows-x86_64-development.zip -Algorithm SHA256
```

Extract it and verify every line of `SHA256SUMS`. Treat it only as an unsigned development package.

---

## 8. Actual per-user installation test

Use **Windows PowerShell 5.1** (`powershell.exe`), not only PowerShell 7 (`pwsh`). Use a standard non-admin user.

Before installation:

```powershell
whoami
[Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
Test-Path "$env:LOCALAPPDATA\Programs\Quick Swap Tools"
Test-Path "HKCU:\Software\Mozilla\NativeMessagingHosts\com.onibyts.quickswap"
```

The install root and exact native-host registration must initially be absent for a clean test.

From the extracted development bundle:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\install.ps1 -AllowUnsignedDevelopment
```

Verify:

```powershell
$InstallRoot = "$env:LOCALAPPDATA\Programs\Quick Swap Tools"
$RegistryPath = "HKCU:\Software\Mozilla\NativeMessagingHosts\com.onibyts.quickswap"
Get-ChildItem -LiteralPath $InstallRoot -Force
Get-Item -LiteralPath $RegistryPath
(Get-Item -LiteralPath $RegistryPath).GetValue("")
Get-Content -LiteralPath "$InstallRoot\com.onibyts.quickswap.json"
```

Acceptance criteria:

- No UAC/elevation request.
- Files exist only under the current user installation root.
- Registry mutation is under HKCU only.
- Registry default value points to the absolute native manifest path, not directly to the EXE.
- Manifest path points to the installed `quick-swap-tools.exe`.
- Manifest includes only `quick-swap-tools@onibyts.com` in `allowed_extensions`.
- Start Menu shortcut opens the GUI configurator without a console flash.
- Unknown pre-existing registry values and files are never overwritten.

Take screenshots of the installed directory, registry value, Start Menu entry, and configurator. Redact usernames or personal paths before sharing publicly.

---

## 9. Firefox extension and native-host acceptance

1. Start Firefox normally as the same Windows user.
2. Press `Ctrl+O`.
3. Select the included `Quick-Swap-Tools-*-firefox.xpi`.
4. Confirm Firefox identifies and accepts the Mozilla-signed extension.
5. Open `about:addons` and verify the extension is enabled.
6. Open Firefox Browser Console only for debugging; do not publish logs containing private URLs.
7. Confirm Firefox discovers the HKCU native host and launches `quick-swap-tools.exe`.
8. Confirm the hello/ready handshake succeeds.
9. Confirm no console window appears.
10. Confirm native-messaging stdout contains framed JSON only.

Process evidence:

```powershell
Get-Process quick-swap-tools -ErrorAction SilentlyContinue | Format-List Id,Path,StartTime
```

Test Firefox restart and Windows sign-out/sign-in persistence.

### Multiple Firefox profiles

- Open two Firefox profiles with the extension enabled.
- Confirm only one host owns the global hotkey pair.
- Confirm secondary hosts remain connected without stealing the pair.
- Close the owner profile/process.
- Confirm a secondary host takes ownership.
- Confirm commands are not duplicated across profiles.

---

## 10. Configurator acceptance

Open **Quick Swap Tools** from the Start Menu.

Verify:

- Window title and controls render correctly at 100%, 125%, and 150% display scaling.
- Auction and Giveaway are clearly distinguishable.
- Click a binding, press a key/combo, then Apply.
- Defaults are `Ctrl+Shift+F9` and `Ctrl+Shift+F10`.
- F12 is refused.
- Duplicate Auction/Giveaway bindings are refused.
- Risky unmodified typing keys show a global-keyboard warning.
- F13–F24 can be recorded from a keyboard-emulating macro pad.
- A successful Apply updates the running Firefox host without restarting Firefox.
- Swapping the two bindings succeeds as one transaction.
- If Windows rejects either new hotkey, both prior bindings are restored.
- The UI must not claim restoration succeeded if registry rollback or host reload failed.
- Two configurator processes cannot interleave Apply transactions.
- Closing the configurator does not stop the resident Firefox host.

### Conflict test

Use a separate small process or known Windows shortcut to reserve one candidate hotkey. Confirm Quick Swap Tools reports the conflict and preserves both previous bindings without silently stealing the other registration.

---

## 11. Real Whatnot seller-stream acceptance

Only run this with John present and with a harmless test auction/giveaway setup. Never expose or commit the private stream URL.

### Positive cases

- Open an eligible live Whatnot seller tab.
- Press the Auction shortcut once.
- Confirm exactly one next-auction semantic control is activated quickly.
- Press the Giveaway shortcut once.
- Confirm the extension selects Giveaways when necessary and activates exactly one fresh giveaway control.
- Press distinct valid commands rapidly and confirm both are accepted in order.
- Confirm repeated delivery of the same command ID is suppressed.

### Mandatory negative/safety cases

Confirm no activation when:

- There is no eligible Whatnot tab.
- The relevant control is missing.
- The control is hidden.
- The control is disabled.
- The control is transparent or pointer-disabled.
- The control is covered by another element.
- Two equally valid controls are present.
- A generic Start/Run button is unrelated to the desired mode.
- A reused generic control has not yet become fresh for the newly selected mode.

Verify the implementation never uses screen coordinates.

Record latency observations, but do not claim a formal latency guarantee unless measured repeatedly with a documented method.

---

## 12. Macro-pad/Tartarus acceptance

- Configure one Tartarus key to emit an otherwise-unused keyboard key such as F13.
- Record F13 in the Quick Swap Tools configurator.
- Confirm it starts the intended action.
- Confirm an ordinary keyboard emitting F13 triggers the same action; document this as expected behavior.
- Do not claim physical-device identification. `RegisterHotKey` sees the emitted key, not its source device.
- Leave Raw Input/HID work deferred unless John explicitly authorizes a separate feature.

---

## 13. Uninstall and ownership-safety acceptance

Close Firefox first for the standard test:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$env:LOCALAPPDATA\Programs\Quick Swap Tools\uninstall.ps1"
```

Before uninstall, deliberately add unrelated settings:

```powershell
$SettingsPath = "HKCU:\Software\OniByts\Quick Swap Tools"
New-Item $SettingsPath -Force | Out-Null
New-ItemProperty $SettingsPath -Name FriendValue -PropertyType String -Value preserve-me -Force | Out-Null
```

After uninstall, verify:

- Installation directory removed only when exact ownership is proven.
- Native-host registry entry removed only when its exact manifest path is owned.
- Start Menu shortcut removed only when its configured properties match.
- Owned shortcut values removed.
- `FriendValue` remains `preserve-me`.
- Unknown values and subkeys remain.
- Firefox extension remains until removed separately through `about:addons`.

Then test hostile/collision cases in a disposable Windows user or VM:

- Foreign file added to install root before rollback.
- Owned file modified before rollback.
- Foreign registry value/subkey added.
- Shortcut target or properties altered.
- Destination native-host registration created between preflight and publication.
- Destination Start Menu shortcut created between preflight and publication.
- Installer killed at each publication stage.
- Uninstall while Firefox has the host open.
- Locked binaries/files.

Expected behavior is fail closed and preserve anything not proven to be owned. Never recursively delete an unproven published directory.

---

## 14. Windows 10/11 test matrix

At minimum record:

| Test area | Windows 10 x64 | Windows 11 x64 |
|---|---:|---:|
| MinGW build | Pending | Pending |
| Windows PowerShell 5.1 install | Pending | Pending |
| No-admin HKCU registration | Pending | Pending |
| Firefox native-host discovery | Pending | Pending |
| Mozilla-signed XPI install | Pending | Pending |
| Auction hotkey | Pending | Pending |
| Giveaway hotkey | Pending | Pending |
| Configurator capture/apply/rollback | Pending | Pending |
| Multiple Firefox profiles | Pending | Pending |
| Start Menu known-folder behavior | Pending | Pending |
| Locked-file uninstall behavior | Pending | Pending |
| Defender/SmartScreen observations | Pending | Pending |
| Ownership-preserving uninstall | Pending | Pending |

If only one physical machine is available, finish it there and use a clean VM for the other OS. Do not mark an OS passed based only on Wine or cross-compilation.

---

## 15. Evidence log template

Create an untracked/private evidence directory unless screenshots are fully sanitized:

```text
windows-test-evidence/
  YYYY-MM-DD-HHMM/
    environment.txt
    commands.txt
    results.md
    screenshots/
    logs/
```

`environment.txt` should include:

```powershell
Get-ComputerInfo | Select-Object WindowsProductName,WindowsVersion,OsBuildNumber,OsArchitecture
(Get-Item "C:\Program Files\Mozilla Firefox\firefox.exe").VersionInfo.FileVersion
g++ --version
python --version
node --version
pnpm --version
git rev-parse HEAD
```

For each test record:

```text
Test name:
Date/time:
Windows edition/build:
Windows user type: standard/admin
Firefox version/profile count:
Commit SHA:
Exact command or manual steps:
Expected result:
Actual result:
PASS/FAIL/BLOCKED:
Evidence filenames:
Notes/private data redacted:
```

Do not commit raw browser logs, private Whatnot URLs, usernames, account identifiers, certificate material, or secrets.

---

## 16. Authenticode setup — required before public Windows release

The current Windows executables are unsigned development artifacts. `-AllowUnsignedDevelopment` is only for controlled testing.

Production requires a real code-signing identity. Do not treat a local self-signed certificate as public trust.

Recommended flow:

1. Choose and acquire an approved public code-signing solution:
   - Hardware-backed OV/EV code-signing certificate, or
   - Managed signing such as Azure Trusted Signing/SignPath if appropriate.
2. Keep private keys/passwords outside the repository and outside agent prompts.
3. Record the approved signer certificate thumbprint through secure configuration.
4. Populate the production signer allowlist in `packaging/windows/install.ps1` only after John approves the identity.
5. Build clean binaries from the reviewed commit.
6. Sign both:
   - `quick-swap-tools.exe`
   - `quick-swap-config.exe`
7. Use an RFC 3161 timestamp server.
8. Verify locally:

```powershell
Get-AuthenticodeSignature .\quick-swap-tools.exe | Format-List Status,StatusMessage,SignerCertificate,TimeStamperCertificate
Get-AuthenticodeSignature .\quick-swap-config.exe | Format-List Status,StatusMessage,SignerCertificate,TimeStamperCertificate
```

9. Test trust on clean Windows 10/11 machines.
10. Rebuild bundle checksums **after signing**, because signing changes the EXE bytes.
11. Scan with Defender and observe SmartScreen reputation behavior.
12. Keep production mode fail closed: unsigned, invalid, unknown, or unapproved signatures must fail.

Never commit a PFX, certificate password, private key, signing token, or cloud-signing credential.

---

## 17. Release gates

Do not publish until all are true:

- [ ] Current Windows CI is green.
- [ ] All native Windows tests pass on a real interactive desktop.
- [ ] Actual Windows PowerShell 5.1 install/uninstall passes.
- [ ] Windows 10 x64 acceptance complete.
- [ ] Windows 11 x64 acceptance complete.
- [ ] Firefox launches the HKCU native host.
- [ ] Mozilla-signed XPI installs successfully.
- [ ] Real Whatnot Auction acceptance passes.
- [ ] Real Whatnot Giveaway acceptance passes.
- [ ] All semantic negative/safety cases pass.
- [ ] Configurator capture/apply/swap/rollback passes.
- [ ] Hotkey-conflict behavior passes.
- [ ] Multiple Firefox profiles pass without duplicate commands.
- [ ] Start Menu discovery works through the known-folder path.
- [ ] Ownership-preserving rollback/uninstall fault tests pass.
- [ ] Executables are Authenticode-signed by an approved pinned publisher.
- [ ] Defender/SmartScreen behavior is documented and acceptable.
- [ ] Final source and staged artifacts pass privacy/credential scans.
- [ ] Final archive `SHA256SUMS` passes.
- [ ] Outer ZIP SHA-256 is computed and recorded after the final build.
- [ ] Independent final review returns PASS.
- [ ] A later release version is selected; `v0.1.0` remains untouched.

The current `0.2.0-dev` name is development-only and is not a final release decision.

---

## 18. Commit and CI discipline

For each fix:

1. Inspect definition and all usages before editing.
2. Add a failing regression test.
3. Confirm RED.
4. Apply the smallest root-cause fix.
5. Confirm GREEN.
6. Run focused tests.
7. Run `pnpm run check` when practical.
8. Run the actual Windows PowerShell 5.1 path for installer changes.
9. Run `git diff --check`.
10. Independently review security-sensitive changes.
11. Stage exact files only; never stage `build/`, `dist/`, evidence, screenshots, or credentials accidentally.
12. Commit with a conventional message.
13. Push `main` only after review.
14. Monitor both standard CI and Windows CI to completion.
15. If CI fails, inspect `gh run view <run-id> --log-failed`; do not guess.

Useful commands:

```powershell
git status --short
git diff --check
git diff --cached --check
gh run list --branch main --limit 10
gh run watch <RUN_ID> --exit-status
gh run view <RUN_ID> --log-failed
```

Never rewrite history, retag `v0.1.0`, or publish a release without John’s explicit approval.

---

## 19. Known-good evidence before this handoff

Verified locally before moving to native Windows:

- Independent final merge review returned PASS before the Windows runtime findings.
- MinGW-w64 GCC 16.1.0 cross-build succeeded.
- Windows GUI-subsystem PE checks passed.
- Runtime-enabled Wine Windows suite passed 34 tests.
- Full repository check passed 57 tests with two expected interactive-runtime skips.
- Strict JSON adversarial tests passed, including malformed separators and escaped-equivalent duplicate keys.
- Installer policy tests passed locally.
- PowerShell scripts parsed successfully in PowerShell 7, but native CI proved parsing alone is insufficient.
- ShellCheck, actionlint, Python byte-compilation, and `git diff --check` passed.
- Source/documentation privacy scan found no blocked patterns.
- Vendored JSON for Modern C++ header matched upstream v3.11.3 before commit.
- Standard GitHub CI has remained green through the Windows CI fixes.

Native Windows CI progression:

1. Run `30785610650` found invalid static `[Uri]::IsUnc($Path)`.
2. Run `30786052643` found ambiguous `Split-Path -LiteralPath $Current -Parent` in Windows PowerShell 5.1.
3. Run `30786258194` passed those points and found invalid empty WScript `IconLocation` assignment.

This progression is useful evidence that the native workflow is testing real installer behavior rather than only parsing source.

---

## 20. Definition of done for the Windows ARMY

Return a concise final report containing:

- Final commit SHA.
- Link to green standard CI.
- Link to green Windows CI.
- Windows 10 and Windows 11 versions/builds tested.
- Firefox versions tested.
- Exact automated test counts.
- Installer/uninstaller result.
- Firefox native-host handshake result.
- Auction and Giveaway real-stream result.
- Configurator and conflict/rollback result.
- Multi-profile result.
- Ownership/fault-injection result.
- Authenticode signer identity and verification status, without secrets.
- Defender/SmartScreen observations.
- Final archive filename and SHA-256.
- Remaining limitations.
- Explicit confirmation that `v0.1.0` was not modified.

If any release gate remains incomplete, state `NOT READY FOR PUBLIC WINDOWS RELEASE` plainly. Never substitute plausible output for tests that were not actually run.
