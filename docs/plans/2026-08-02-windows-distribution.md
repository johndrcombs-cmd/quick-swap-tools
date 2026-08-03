# Windows Distribution Implementation Plan

> **For Hermes:** Use subagent-driven-development and strict RED→GREEN TDD for each implementation slice.

**Goal:** Produce a no-admin Windows 10/11 x64 development distribution of Quick Swap Tools for Firefox, with a low-latency global-hotkey native host, game-style shortcut configuration, safe per-user registration, and reproducible Linux cross-builds.

**Architecture:** Two dependency-light Win32 C++ executables provide a console-free Firefox native-messaging host and a Start Menu configurator. The host uses `RegisterHotKey`, a Win32 message loop, and a native-message reader thread; logical `auction` and `giveaway` messages remain unchanged, so the existing signed Firefox extension is reused. The configurator stores validated modifier/virtual-key pairs under an owned HKCU product key and coordinates transactional live reload through named events. A transactional PowerShell installer copies the bundle under `%LOCALAPPDATA%`, writes a closed native-host manifest, and registers only the exact HKCU Mozilla key after preflight.

**Tech Stack:** C++20/Win32, vendored MIT-licensed JSON for Modern C++, MinGW-w64 cross-compiler, Windows Registry, Firefox native messaging, PowerShell 5.1+, Python `unittest`, Wine smoke tests, GPL-3.0-or-later.

---

### Task 1: Add Windows policy and protocol tests

**Objective:** Define the externally observable Windows contract before production code.

**Files:**
- Create: `tests/test_windows_distribution.py`
- Modify: `package.json`

**Steps:**
1. Write tests for a `--validate-hotkeys` CLI contract: distinct F13–F24 keys are accepted, duplicates are rejected, and disruptive unmodified keys produce warnings.
2. Run the focused test and verify RED because `build/windows/quick-swap-tools.exe` does not exist.
3. Add a Wine native-messaging hello test with `QUICK_SWAP_NO_SHORTCUTS=1`, a four-byte little-endian frame, bounded reads, and unconditional process cleanup.
4. Add static bundle-policy tests requiring HKCU Mozilla registration, exact host/extension IDs, no elevation/download/`Invoke-Expression`, explicit unsigned-development opt-in, ownership checks, and production Authenticode enforcement.
5. Keep the tests RED until their corresponding implementation slice is added.

### Task 2: Implement the Win32 host tracer bullet

**Objective:** Build an executable that speaks the existing Firefox protocol and safely registers global hotkeys.

**Files:**
- Create: `native/windows/quick-swap-tools.cpp`
- Create: `scripts/build-windows.sh`

**Steps:**
1. Implement bounded native-message framing (1 MiB maximum), strict JSON validation, closed handling of `hello`/`result`, and keep all diagnostics off native-messaging stdout.
2. Implement a hidden Win32 window and `RegisterHotKey` for Auction/Giveaway defaults, with a 150 ms per-action bounce filter.
3. Run input reading on a bounded worker thread and marshal shutdown/messages to the Win32 message loop; serialize stdout writes.
4. Build with `x86_64-w64-mingw32-g++ -municode -static-libgcc -static-libstdc++` and warning flags.
5. Run the focused Wine hello test and validation tests until GREEN.

### Task 3: Implement configurable Windows shortcuts

**Objective:** Provide the same click-record-Apply workflow as the KDE build without introducing a runtime framework dependency.

**Files:**
- Modify: `native/windows/quick-swap-tools.cpp`
- Modify: `tests/test_windows_distribution.py`

**Steps:**
1. Add RED validation tests for modifier/virtual-key encoding, defaults, duplicates, and F13/F24 boundaries.
2. Implement a separate `quick-swap-config.exe` Win32 GUI with two recorder controls, Apply, Reset, and Close.
3. Store only DWORD modifier/key pairs under `HKCU\Software\OniByts\Quick Swap Tools`; validate values before use.
4. Warn for every unmodified global key except F13–F24 and explain that Windows cannot distinguish identical keys from different physical keyboards.
5. Coordinate Apply with a running host: release both existing hotkeys, probe both replacements, persist only when both are available, reload both, and restore the previous pair on any failure.
6. Add a pure `--self-test-hotkey-transaction` test covering swaps and rollback after a final-write failure.

### Task 4: Add secure per-user installation

**Objective:** Install and unregister the Windows bundle without elevation or collateral registry/file deletion.

**Files:**
- Create: `packaging/windows/install.ps1`
- Create: `packaging/windows/uninstall.ps1`
- Create: `packaging/windows/README.md`
- Create: `packaging/windows/native-manifest.template.json`
- Modify: `tests/test_windows_distribution.py`

**Steps:**
1. Preflight architecture, PowerShell version, bundle checksums, file locality, existing install/registry ownership, and Authenticode status before mutations.
2. Require `-AllowUnsignedDevelopment` for unsigned development binaries; production has no unsigned bypass in release instructions.
3. Copy files to `%LOCALAPPDATA%\Programs\Quick Swap Tools` through a temporary product-owned directory.
4. Generate the exact closed manifest with absolute executable path and `allowed_extensions: ["quick-swap-tools@onibyts.com"]`.
5. Write only `HKCU\Software\Mozilla\NativeMessagingHosts\com.onibyts.quickswap` default `REG_SZ` after filesystem preflight.
6. Create a Start Menu shortcut targeting `quick-swap-config.exe`.
7. On failure, remove only artifacts created by this attempt. On uninstall, prove exact registry/manifest/shortcut ownership before deletion; preserve unrelated keys and values.

### Task 5: Build the Windows development bundle

**Objective:** Produce a reproducible friend-testable ZIP without claiming a signed public release.

**Files:**
- Create: `scripts/build-windows-bundle.sh`
- Modify: `package.json`
- Modify: `.github/workflows/ci.yml`
- Modify: `README.md`
- Modify: `CHANGELOG.md`

**Steps:**
1. Stage the Windows executable, PowerShell scripts, signed Firefox XPI, README, LICENSE, and normalized `SHA256SUMS`.
2. Name the artifact `quick-swap-tools-<version>-windows-x86_64-development.zip` until Authenticode signing exists.
3. Add MinGW cross-build and portable policy tests to CI; do not claim Wine or real registry verification from Linux CI.
4. Document Windows defaults, Tartarus F13–F24 guidance, explicit unsigned-development installation, and current signing limitation.
5. Verify ZIP contents and hashes after extraction.

### Task 6: Independent review and publication groundwork

**Objective:** Make the initial Windows groundwork safe to merge without presenting it as a finished signed release.

**Steps:**
1. Run focused Windows tests, Wine protocol smoke test, Linux suite, extension lint, cross-build, shell checks, PowerShell static policy tests, and `git diff --check`.
2. Obtain specification PASS and independent quality/security APPROVED reviews.
3. Stage exact intended files, run a privacy/credential scan, commit, push, and verify GitHub CI.
4. Defer a public Windows release until a real Windows 10/11 test validates HKCU registration, Start Menu launch, Firefox connection, hotkey conflict/reload behavior, uninstall ownership, and Authenticode signing.
