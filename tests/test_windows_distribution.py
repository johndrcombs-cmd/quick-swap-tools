import json
import hashlib
import io
import os
import select
import shutil
import struct
import subprocess
import tempfile
import time
import unittest
import zipfile
from pathlib import Path


ROOT = Path(__file__).parents[1]
WINDOWS_BINARY = ROOT / "build" / "windows" / "quick-swap-tools.exe"
WINDOWS_CONFIG = ROOT / "build" / "windows" / "quick-swap-config.exe"
WINDOWS_RUNNER = [] if os.name == "nt" else ["wine"]


@unittest.skipUnless(WINDOWS_BINARY.is_file(), "Windows host has not been built")
class WindowsHotkeyValidationTests(unittest.TestCase):
    def run_validate(self, auction: str, giveaway: str):
        return subprocess.run(
            [
                *WINDOWS_RUNNER,
                str(WINDOWS_BINARY),
                "--validate-hotkeys",
                auction,
                giveaway,
            ],
            capture_output=True,
            text=True,
            timeout=10,
            env={**os.environ, "WINEDEBUG": "-all"},
        )

    def test_distinct_function_keys_are_valid(self):
        result = self.run_validate("F20", "F21")
        self.assertEqual(result.returncode, 0, result.stderr)
        payload = json.loads(result.stdout)
        self.assertTrue(payload["valid"])
        self.assertEqual(payload["warnings"], [])

    def test_duplicate_hotkeys_are_rejected(self):
        result = self.run_validate("F20", "F20")
        self.assertNotEqual(result.returncode, 0)
        payload = json.loads(result.stdout)
        self.assertFalse(payload["valid"])
        self.assertEqual(
            payload["error"], "auction and giveaway hotkeys must differ"
        )

    def test_unmodified_typing_key_has_global_warning(self):
        result = self.run_validate("A", "F21")
        self.assertEqual(result.returncode, 0, result.stderr)
        payload = json.loads(result.stdout)
        self.assertEqual(
            payload["warnings"], ["auction hotkey is an unmodified global key"]
        )

    def test_f12_is_rejected_because_windows_reserves_it(self):
        result = self.run_validate("F12", "F21")
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(json.loads(result.stdout)["error"], "invalid hotkey")

    def test_hotkey_transaction_swaps_and_rolls_back_final_write_failure(self):
        result = subprocess.run(
            [*WINDOWS_RUNNER, str(WINDOWS_BINARY), "--self-test-hotkey-transaction"],
            capture_output=True,
            text=True,
            timeout=10,
            env={**os.environ, "WINEDEBUG": "-all"},
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            json.loads(result.stdout),
            {"swapSucceeded": True, "rollbackRestored": True},
        )

    def test_configurator_transaction_mutex_is_exclusive(self):
        result = subprocess.run(
            [
                *WINDOWS_RUNNER,
                str(WINDOWS_BINARY),
                "--self-test-configurator-mutex",
            ],
            capture_output=True,
            text=True,
            timeout=10,
            env={**os.environ, "WINEDEBUG": "-all"},
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout), {"exclusive": True})

    def test_effective_windows_defaults_avoid_reserved_windows_key_combinations(self):
        result = subprocess.run(
            [*WINDOWS_RUNNER, str(WINDOWS_BINARY), "--dump-effective-hotkeys"],
            capture_output=True,
            text=True,
            timeout=10,
            env={**os.environ, "WINEDEBUG": "-all"},
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            json.loads(result.stdout),
            {"auction": "Ctrl+Shift+F9", "giveaway": "Ctrl+Shift+F10"},
        )


@unittest.skipUnless(WINDOWS_BINARY.is_file(), "Windows host has not been built")
class WindowsNativeHostTests(unittest.TestCase):
    @staticmethod
    def read_exact(stream, size: int, timeout: float = 5.0) -> bytes:
        received = bytearray()
        deadline = time.monotonic() + timeout
        while len(received) < size:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError("Windows native host response timed out")
            if os.name == "nt":
                import ctypes
                import msvcrt

                available = ctypes.c_ulong(0)
                handle = msvcrt.get_osfhandle(stream.fileno())
                while True:
                    succeeded = ctypes.windll.kernel32.PeekNamedPipe(
                        handle,
                        None,
                        0,
                        None,
                        ctypes.byref(available),
                        None,
                    )
                    if not succeeded:
                        return bytes(received)
                    if available.value:
                        break
                    if time.monotonic() >= deadline:
                        raise TimeoutError("Windows native host response timed out")
                    time.sleep(0.01)
                chunk = os.read(
                    stream.fileno(),
                    min(size - len(received), available.value),
                )
            else:
                readable, _, _ = select.select([stream], [], [], remaining)
                if not readable:
                    raise TimeoutError("Windows native host response timed out")
                chunk = os.read(stream.fileno(), size - len(received))
            if not chunk:
                break
            received.extend(chunk)
        return bytes(received)

    def test_hello_returns_ready_frame_without_registering_hotkeys(self):
        process = subprocess.Popen(
            [*WINDOWS_RUNNER, str(WINDOWS_BINARY)],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            env={
                **os.environ,
                "WINEDEBUG": "-all",
                "QUICK_SWAP_NO_SHORTCUTS": "1",
            },
        )
        try:
            assert process.stdin is not None
            assert process.stdout is not None
            payload = json.dumps({"type": "hello", "version": "test"}).encode()
            process.stdin.write(struct.pack("<I", len(payload)) + payload)
            process.stdin.flush()

            header = self.read_exact(process.stdout, 4)
            self.assertEqual(len(header), 4)
            (length,) = struct.unpack("<I", header)
            message = json.loads(self.read_exact(process.stdout, length))
            self.assertEqual(message["type"], "ready")
            self.assertFalse(message["auctionShortcut"])
            self.assertFalse(message["giveawayShortcut"])
        finally:
            if process.poll() is None:
                process.terminate()
                process.wait(timeout=3)
            for stream in (process.stdin, process.stdout, process.stderr):
                if stream is not None:
                    stream.close()

    def test_host_uses_windows_subsystem_without_console_flash(self):
        headers = subprocess.run(
            ["x86_64-w64-mingw32-objdump", "-p", str(WINDOWS_BINARY)],
            check=True,
            capture_output=True,
            text=True,
        ).stdout
        self.assertRegex(headers, r"Subsystem\s+00000002\s+\(Windows GUI\)")

    def test_hello_accepts_legal_json_whitespace_and_field_order(self):
        process = subprocess.Popen(
            [*WINDOWS_RUNNER, str(WINDOWS_BINARY)],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            env={
                **os.environ,
                "WINEDEBUG": "-all",
                "QUICK_SWAP_NO_SHORTCUTS": "1",
            },
        )
        try:
            assert process.stdin is not None
            assert process.stdout is not None
            payload = b'{"version":"test","type"\t:\n\t"hello"}'
            process.stdin.write(struct.pack("<I", len(payload)) + payload)
            process.stdin.flush()
            header = self.read_exact(process.stdout, 4, timeout=1.0)
            self.assertEqual(len(header), 4)
        finally:
            if process.poll() is None:
                process.terminate()
                process.wait(timeout=3)
            for stream in (process.stdin, process.stdout, process.stderr):
                if stream is not None:
                    stream.close()

    def test_zero_length_native_frame_is_rejected(self):
        process = subprocess.run(
            [*WINDOWS_RUNNER, str(WINDOWS_BINARY)],
            input=struct.pack("<I", 0),
            capture_output=True,
            timeout=10,
            env={
                **os.environ,
                "WINEDEBUG": "-all",
                "QUICK_SWAP_NO_SHORTCUTS": "1",
            },
        )
        self.assertNotEqual(process.returncode, 0)
        self.assertEqual(process.stdout, b"")

    def test_truncated_native_frame_is_rejected(self):
        process = subprocess.run(
            [*WINDOWS_RUNNER, str(WINDOWS_BINARY)],
            input=struct.pack("<I", 20) + b"short",
            capture_output=True,
            timeout=10,
            env={
                **os.environ,
                "WINEDEBUG": "-all",
                "QUICK_SWAP_NO_SHORTCUTS": "1",
            },
        )
        self.assertNotEqual(process.returncode, 0)
        self.assertEqual(process.stdout, b"")

    def run_closed_input_frame(self, payload: bytes):
        return subprocess.run(
            [*WINDOWS_RUNNER, str(WINDOWS_BINARY)],
            input=struct.pack("<I", len(payload)) + payload,
            capture_output=True,
            timeout=10,
            env={
                **os.environ,
                "WINEDEBUG": "-all",
                "QUICK_SWAP_NO_SHORTCUTS": "1",
            },
        )

    def test_malformed_json_is_rejected_without_a_response(self):
        process = self.run_closed_input_frame(b'{"type":"hello"')
        self.assertNotEqual(process.returncode, 0)
        self.assertEqual(process.stdout, b"")

    def test_native_message_requires_an_object_root(self):
        process = self.run_closed_input_frame(b'[{"type":"hello"}]')
        self.assertNotEqual(process.returncode, 0)
        self.assertEqual(process.stdout, b"")

    def test_nested_type_is_not_treated_as_top_level_hello(self):
        process = self.run_closed_input_frame(b'{"outer":{"type":"hello"}}')
        self.assertEqual(process.returncode, 0)
        self.assertEqual(process.stdout, b"")

    def test_duplicate_type_field_is_rejected(self):
        process = self.run_closed_input_frame(
            b'{"type":"hello","type":"result"}'
        )
        self.assertNotEqual(process.returncode, 0)
        self.assertEqual(process.stdout, b"")

    def test_escaped_duplicate_type_field_is_rejected(self):
        process = self.run_closed_input_frame(
            b'{"type":"hello","t\\u0079pe":"result"}'
        )
        self.assertNotEqual(process.returncode, 0)
        self.assertEqual(process.stdout, b"")

    def test_trailing_object_comma_is_rejected(self):
        process = self.run_closed_input_frame(b'{"type":"hello",}')
        self.assertNotEqual(process.returncode, 0)
        self.assertEqual(process.stdout, b"")

    def test_malformed_json_separators_are_rejected(self):
        payloads = (
            b'{,"type":"hello"}',
            b'{"type"::"hello"}',
            b'{"type":"hello",,"x":1}',
            b'{"type":"hello","x":[1 2]}',
            b'{"type":"hello","x":{"a":1 "b":2}}',
        )
        for payload in payloads:
            with self.subTest(payload=payload):
                process = self.run_closed_input_frame(payload)
                self.assertNotEqual(process.returncode, 0)
                self.assertEqual(process.stdout, b"")

    def test_invalid_json_primitive_is_rejected(self):
        process = self.run_closed_input_frame(b'{"type":"result","ok":tru}')
        self.assertNotEqual(process.returncode, 0)
        self.assertEqual(process.stdout, b"")

    def test_invalid_utf8_and_unescaped_control_character_are_rejected(self):
        for payload in (b'{"type":"\xff"}', b'{"type":"hello\nworld"}'):
            with self.subTest(payload=payload):
                process = self.run_closed_input_frame(payload)
                self.assertNotEqual(process.returncode, 0)
                self.assertEqual(process.stdout, b"")

    def test_multiple_valid_frames_are_processed_in_order(self):
        hello = b'{"type":"hello"}'
        result = b'{"type":"result","ok":true}'
        process = subprocess.run(
            [*WINDOWS_RUNNER, str(WINDOWS_BINARY)],
            input=(
                struct.pack("<I", len(hello))
                + hello
                + struct.pack("<I", len(result))
                + result
            ),
            capture_output=True,
            timeout=10,
            env={
                **os.environ,
                "WINEDEBUG": "-all",
                "QUICK_SWAP_NO_SHORTCUTS": "1",
            },
        )
        self.assertEqual(process.returncode, 0)
        self.assertGreaterEqual(len(process.stdout), 4)
        (length,) = struct.unpack("<I", process.stdout[:4])
        message = json.loads(process.stdout[4 : 4 + length])
        self.assertEqual(message["type"], "ready")
        self.assertEqual(len(process.stdout), 4 + length)

    def test_oversized_native_frame_is_rejected(self):
        process = subprocess.run(
            [*WINDOWS_RUNNER, str(WINDOWS_BINARY)],
            input=struct.pack("<I", 1024 * 1024 + 1),
            capture_output=True,
            timeout=10,
            env={
                **os.environ,
                "WINEDEBUG": "-all",
                "QUICK_SWAP_NO_SHORTCUTS": "1",
            },
        )
        self.assertNotEqual(process.returncode, 0)
        self.assertEqual(process.stdout, b"")

    @unittest.skipUnless(
        os.environ.get("QST_WINDOWS_RUNTIME_TESTS") == "1",
        "requires an interactive Wine desktop",
    )
    def test_default_hotkeys_register_when_available(self):
        process = subprocess.Popen(
            [*WINDOWS_RUNNER, str(WINDOWS_BINARY)],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            env={**os.environ, "WINEDEBUG": "-all"},
        )
        try:
            assert process.stdin is not None
            assert process.stdout is not None
            payload = b'{"type":"hello"}'
            process.stdin.write(struct.pack("<I", len(payload)) + payload)
            process.stdin.flush()
            header = self.read_exact(process.stdout, 4)
            (length,) = struct.unpack("<I", header)
            message = json.loads(self.read_exact(process.stdout, length))
            self.assertTrue(message["auctionShortcut"])
            self.assertTrue(message["giveawayShortcut"])
        finally:
            if process.poll() is None:
                process.terminate()
                process.wait(timeout=3)
            for stream in (process.stdin, process.stdout, process.stderr):
                if stream is not None:
                    stream.close()

    @unittest.skipUnless(
        os.environ.get("QST_WINDOWS_RUNTIME_TESTS") == "1",
        "requires an interactive Windows or Wine desktop",
    )
    def test_second_host_takes_hotkey_ownership_after_first_exits(self):
        environment = {**os.environ, "WINEDEBUG": "-all"}
        processes = [
            subprocess.Popen(
                [*WINDOWS_RUNNER, str(WINDOWS_BINARY)],
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                env=environment,
            )
            for _ in range(2)
        ]

        def hello(process):
            assert process.stdin is not None
            assert process.stdout is not None
            payload = b'{"type":"hello"}'
            process.stdin.write(struct.pack("<I", len(payload)) + payload)
            process.stdin.flush()
            (length,) = struct.unpack("<I", self.read_exact(process.stdout, 4))
            return json.loads(self.read_exact(process.stdout, length))

        try:
            states = [hello(process)["auctionShortcut"] for process in processes]
            self.assertEqual(states.count(True), 1)
            owner = processes[states.index(True)]
            waiter = processes[states.index(False)]
            assert owner.stdin is not None
            owner.stdin.close()
            owner.wait(timeout=3)
            time.sleep(1.5)
            self.assertTrue(hello(waiter)["auctionShortcut"])
        finally:
            for process in processes:
                if process.poll() is None:
                    process.terminate()
                    process.wait(timeout=3)
                for stream in (process.stdin, process.stdout, process.stderr):
                    if stream is not None and not stream.closed:
                        stream.close()


@unittest.skipUnless(WINDOWS_CONFIG.is_file(), "Windows configurator has not been built")
class WindowsConfiguratorTests(unittest.TestCase):
    def test_configurator_is_graphical_and_discoverably_labels_both_actions(self):
        self.assertTrue(WINDOWS_CONFIG.is_file())
        strings = subprocess.run(
            ["x86_64-w64-mingw32-strings", "-el", str(WINDOWS_CONFIG)],
            check=True,
            capture_output=True,
            text=True,
        ).stdout
        self.assertIn("Next auction", strings)
        self.assertIn("Next giveaway", strings)
        self.assertIn("Press a key", strings)
        self.assertIn("Apply", strings)
        self.assertIn("Reset", strings)


class WindowsInstallerPolicyTests(unittest.TestCase):
    def test_per_user_installer_has_closed_registration_and_signing_policy(self):
        installer = (ROOT / "packaging" / "windows" / "install.ps1").read_text()
        self.assertIn("HKCU:\\Software\\Mozilla\\NativeMessagingHosts", installer)
        self.assertIn("com.onibyts.quickswap", installer)
        self.assertIn("quick-swap-tools@onibyts.com", installer)
        self.assertIn("Get-AuthenticodeSignature", installer)
        self.assertIn("AllowUnsignedDevelopment", installer)
        self.assertIn("Is64BitProcess", installer)
        self.assertIn("System.Text.UTF8Encoding($false)", installer)
        self.assertIn("staged payload integrity verification", installer)
        self.assertLess(
            installer.index("$Shortcut.Save()"),
            installer.index("New-Item -Path $RegistryParentPath -Force"),
        )
        self.assertNotIn("Invoke-Expression", installer)
        self.assertNotIn("-Verb RunAs", installer)
        self.assertNotRegex(installer, r"https?://")

    def test_installer_uses_collision_safe_registry_and_shortcut_publication(self):
        installer = (ROOT / "packaging" / "windows" / "install.ps1").read_text()
        self.assertIn("RegRenameKey", installer)
        self.assertIn("$RegistryStagingName", installer)
        self.assertNotIn("New-Item -Path $RegistryPath -Force", installer)

    def test_installer_rollback_proves_published_files_and_full_shortcut(self):
        installer = (ROOT / "packaging" / "windows" / "install.ps1").read_text()
        self.assertIn("$PublishedHashes", installer)
        self.assertIn("Get-FileHash", installer)
        self.assertIn("$RollbackShortcut.WorkingDirectory", installer)
        self.assertIn("$RollbackShortcut.Description", installer)
        self.assertIn("$RollbackShortcut.Arguments", installer)
        self.assertNotIn(
            "Remove-Item -LiteralPath $InstallRoot -Recurse", installer
        )
        self.assertIn("$TemporaryShortcutPath", installer)
        self.assertIn("[IO.File]::Move($TemporaryShortcutPath, $ShortcutPath)", installer)

    def test_unsigned_bypass_rejects_broken_signatures_and_production_is_pinned(self):
        installer = (ROOT / "packaging" / "windows" / "install.ps1").read_text()
        self.assertIn("SignatureStatus]::NotSigned", installer)
        self.assertIn("$ExpectedSignerThumbprints", installer)
        self.assertIn("SignerCertificate.Thumbprint", installer)

    def test_installer_validates_xpi_identity_and_signature_metadata(self):
        installer = (ROOT / "packaging" / "windows" / "install.ps1").read_text()
        self.assertIn("System.IO.Compression", installer)
        self.assertIn("META-INF/mozilla.rsa", installer)
        self.assertIn("META-INF/cose.sig", installer)
        self.assertIn("quick-swap-tools@onibyts.com", installer)

    def test_installer_and_uninstaller_reject_unsafe_roots(self):
        installer = (ROOT / "packaging" / "windows" / "install.ps1").read_text()
        uninstaller = (ROOT / "packaging" / "windows" / "uninstall.ps1").read_text()
        for script in (installer, uninstaller):
            self.assertIn("IsUnc", script)
            self.assertNotIn("[Uri]::IsUnc(", script)
            self.assertNotIn("Split-Path -LiteralPath $Current -Parent", script)
            self.assertIn("ReparsePoint", script)
            self.assertIn("DriveType]::Fixed", script)

    def test_uninstaller_proves_registry_and_manifest_ownership(self):
        uninstaller = (ROOT / "packaging" / "windows" / "uninstall.ps1").read_text()
        self.assertIn("com.onibyts.quickswap", uninstaller)
        self.assertIn("quick-swap-tools@onibyts.com", uninstaller)
        self.assertIn("Refusing", uninstaller)
        self.assertIn("manifest.path", uninstaller)
        self.assertIn("unknown files or directories", uninstaller)
        self.assertIn("$OwnedFiles", uninstaller)
        self.assertNotIn("Remove-Item -LiteralPath $SettingsPath -Recurse", uninstaller)
        self.assertIn("AuctionModifiers", uninstaller)
        self.assertIn("GiveawayVirtualKey", uninstaller)

    def test_development_bundle_contains_friend_installation_payload(self):
        if shutil.which("x86_64-w64-mingw32-g++") is None or shutil.which("zip") is None:
            self.skipTest("Windows cross-compiler and zip are required")
        with tempfile.TemporaryDirectory() as temporary:
            result = subprocess.run(
                [
                    "bash",
                    str(ROOT / "scripts" / "build-windows-bundle.sh"),
                    "9.9.9-test",
                    temporary,
                ],
                check=True,
                capture_output=True,
                text=True,
            )
            archive = Path(result.stdout.strip().splitlines()[-1])
            self.assertTrue(archive.is_file())
            with zipfile.ZipFile(archive) as bundle:
                names = set(bundle.namelist())
                xpis = [name for name in names if name.endswith("-firefox.xpi")]
                self.assertEqual(len(xpis), 1)
                self.assertEqual(
                    names,
                    {
                        "LICENSE",
                        "README.md",
                        "SHA256SUMS",
                        "THIRD_PARTY_NOTICES.md",
                        "install.ps1",
                        "quick-swap-config.exe",
                        "quick-swap-tools.exe",
                        "uninstall.ps1",
                        xpis[0],
                    },
                )
                checksums = bundle.read("SHA256SUMS").decode().splitlines()
                self.assertEqual(len(checksums), 8)
                for line in checksums:
                    digest, name = line.split("  ", 1)
                    self.assertEqual(len(digest), 64)
                    self.assertEqual(
                        hashlib.sha256(bundle.read(name)).hexdigest(),
                        digest,
                    )
                with zipfile.ZipFile(
                    io.BytesIO(bundle.read(xpis[0]))
                ) as xpi:
                    self.assertIn("META-INF/mozilla.rsa", xpi.namelist())


if __name__ == "__main__":
    unittest.main()
