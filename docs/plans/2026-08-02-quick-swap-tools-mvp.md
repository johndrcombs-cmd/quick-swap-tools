# Quick Swap Tools MVP Implementation Plan

> **For Hermes:** Use subagent-driven development and verify every stage locally.

**Goal:** Trigger Whatnot's next auction or giveaway from global KDE shortcuts without focusing Firefox.

**Architecture:** Firefox keeps a 50 KB C++ native-messaging host alive. That host registers `Super+A` and `Super+G` directly through KDE's native `KGlobalAccel` API and emits commands over the already-open Firefox pipe. The extension locates the live Whatnot tab and clicks a narrowly matched, enabled control. This uses no polling, per-key process launch, browser switching, Chromium, coordinate automation, or third-party runtime service.

**Tech stack:** Firefox WebExtension (Manifest V2), C++20/Qt 6/KF6 KGlobalAccel native host, Node/Python standard-library tests.

---

### Task 1: Specify command protocol with failing tests
- Create `tests/test_native.py` for command validation, Firefox native-message framing, and socket-path behavior.
- Run the Python test and confirm RED because `native/quick_swap.py` does not exist.
- Implement only enough protocol code to pass.

### Task 2: Specify safe control matching with failing tests
- Create `tests/button_finder.test.js` covering exact auction/giveaway labels, disabled controls, unrelated controls, and shadow DOM traversal.
- Run the Node test and confirm RED because `extension/button_finder.js` does not exist.
- Implement a deterministic scorer that rejects ambiguous or unrelated controls.

### Task 3: Add the bridge and Firefox routing
- Add `native/quick_swap.py` host/send/status modes.
- Add `extension/manifest.json`, `background.js`, and `content.js`.
- Correlate every command with an ID and return success/failure to the CLI.
- Avoid retries that could accidentally start two items.

### Task 4: Install reversibly
- Add `scripts/install.sh` and `scripts/uninstall.sh`.
- Install project files under the user's home, native-host manifests in Firefox's supported locations, and two KDE service launchers.
- Back up shortcut configuration before replacing existing `Meta+A` and `Meta+G` bindings.
- Restore prior bindings on uninstall.

### Task 5: Verify
- Run all automated tests and syntax checks.
- Package the extension and validate its manifest.
- Verify native host/CLI round-trip with a test client.
- Load the extension in the user's actual Firefox profile and confirm connection status.
- On a real Whatnot seller page, use dry-run inspection before allowing clicks.

### Task 6: Document operation and ring upgrade
- Document install/load steps, shortcut conflicts, status diagnostics, and safe selector calibration.
- Explain that a Bluetooth ring can map two HID buttons to the same CLI actions later, with no browser changes.
