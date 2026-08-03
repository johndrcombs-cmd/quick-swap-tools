import hashlib
import json
import os
import shutil
import stat
import subprocess
import tempfile
import unittest
import zipfile
from pathlib import Path


ROOT = Path(__file__).parents[1]
INSTALLER = ROOT / "packaging" / "linux-kde" / "install.sh"
UNINSTALLER = ROOT / "packaging" / "linux-kde" / "uninstall.sh"


class ReleaseInstallerTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.home = self.root / "home"
        self.bundle = self.root / "bundle"
        self.fake_bin = self.root / "bin"
        self.home.mkdir()
        self.bundle.mkdir()
        self.fake_bin.mkdir()
        shutil.copy2(INSTALLER, self.bundle / "install.sh")
        shutil.copy2(UNINSTALLER, self.bundle / "uninstall.sh")
        (self.bundle / "README.md").write_text("Test release bundle\n")
        (self.bundle / "LICENSE").write_text("GPL test license\n")
        self._write_executable(
            self.bundle / "quick-swap-host",
            "#!/bin/sh\nprintf '%s\\n' \"$*\" >>\"$HOME/host.log\"\n"
            "if [ \"${QST_FAIL_REGISTER:-0}\" = 1 ] && [ \"${1:-}\" = --register-only ]; then exit 1; fi\n"
            "exit 0\n",
        )
        with zipfile.ZipFile(
            self.bundle / "Quick-Swap-Tools-9.9.9-firefox.xpi", "w"
        ) as archive:
            archive.writestr("manifest.json", "{}")
        self._write_executable(
            self.fake_bin / "kreadconfig6",
            "#!/bin/sh\ncase \"$*\" in\n  *'Grid View'*) printf 'Meta+G,Meta+G,Grid View\\n' ;;\n  *) printf 'Meta+A,Meta+A,Walk Through Activities\\n' ;;\nesac\n",
        )
        self._write_executable(
            self.fake_bin / "kwriteconfig6",
            "#!/bin/sh\nprintf '%s\\n' \"$*\" >>\"$HOME/kwrite.log\"\n",
        )
        checksum_names = [
            "quick-swap-host",
            "install.sh",
            "uninstall.sh",
            "README.md",
            "LICENSE",
            "Quick-Swap-Tools-9.9.9-firefox.xpi",
        ]
        checksums = []
        for name in checksum_names:
            digest = hashlib.sha256((self.bundle / name).read_bytes()).hexdigest()
            checksums.append(f"{digest}  {name}")
        (self.bundle / "SHA256SUMS").write_text("\n".join(checksums) + "\n")
        self._write_executable(self.fake_bin / "ldd", "#!/bin/sh\nexit 0\n")
        self.env = {
            **os.environ,
            "HOME": str(self.home),
            "PATH": f"{self.fake_bin}:{os.environ['PATH']}",
        }

    @staticmethod
    def _write_executable(path: Path, content: str):
        path.write_text(content)
        path.chmod(path.stat().st_mode | stat.S_IXUSR)

    def test_install_and_uninstall_preserve_shortcuts_and_native_manifest(self):
        subprocess.run([str(self.bundle / "install.sh")], env=self.env, check=True)

        install_root = self.home / ".local/lib/quick-swap-tools"
        self.assertTrue((install_root / "quick-swap-host").is_file())
        self.assertTrue((install_root / "uninstall.sh").is_file())
        self.assertTrue(
            (install_root / "Quick-Swap-Tools-9.9.9-firefox.xpi").is_file()
        )
        self.assertEqual(
            (self.home / ".local/state/quick-swap-tools").stat().st_mode & 0o777,
            0o700,
        )
        self.assertEqual(
            (self.home / "host.log").read_text().splitlines(),
            ["--check-shortcuts", "--register-only"],
        )

        manifest_path = (
            self.home
            / ".mozilla/native-messaging-hosts/com.onibyts.quickswap.json"
        )
        manifest = json.loads(manifest_path.read_text())
        self.assertEqual(
            manifest["allowed_extensions"], ["quick-swap-tools@onibyts.com"]
        )
        self.assertEqual(manifest["path"], str(install_root / "quick-swap-host"))
        self.assertEqual(
            (self.home / ".local/state/quick-swap-tools/original-grid-view")
            .read_text()
            .strip(),
            "Meta+G,Meta+G,Grid View",
        )

        subprocess.run([str(install_root / "uninstall.sh")], env=self.env, check=True)
        self.assertFalse(install_root.exists())
        self.assertFalse(manifest_path.exists())
        self.assertFalse(
            (self.home / ".local/state/quick-swap-tools").exists()
        )
        restore_log = (self.home / "kwrite.log").read_text()
        self.assertIn("Grid View Meta+G,Meta+G,Grid View", restore_log)
        self.assertIn(
            "next activity Meta+A,Meta+A,Walk Through Activities", restore_log
        )

    def test_failed_registration_rolls_back_files_manifests_and_shortcuts(self):
        env = {**self.env, "QST_FAIL_REGISTER": "1"}
        result = subprocess.run(
            [str(self.bundle / "install.sh")], env=env, capture_output=True, text=True
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("shortcut registration failed", result.stderr)
        self.assertFalse((self.home / ".local/lib/quick-swap-tools").exists())
        self.assertFalse((self.home / ".local/state/quick-swap-tools").exists())
        self.assertFalse(
            (
                self.home
                / ".mozilla/native-messaging-hosts/com.onibyts.quickswap.json"
            ).exists()
        )
        restore_log = (self.home / "kwrite.log").read_text()
        self.assertIn("Grid View Meta+G,Meta+G,Grid View", restore_log)

    def test_existing_installation_is_not_overwritten(self):
        install_root = self.home / ".local/lib/quick-swap-tools"
        install_root.mkdir(parents=True)
        marker = install_root / "keep-me"
        marker.write_text("existing installation\n")
        result = subprocess.run(
            [str(self.bundle / "install.sh")],
            env=self.env,
            capture_output=True,
            text=True,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("installation already exists", result.stderr)
        self.assertEqual(marker.read_text(), "existing installation\n")
        self.assertFalse((self.home / "host.log").exists())

    def test_tampered_bundle_is_rejected_before_changes(self):
        with (self.bundle / "LICENSE").open("a") as license_file:
            license_file.write("tampered\n")
        result = subprocess.run(
            [str(self.bundle / "install.sh")],
            env=self.env,
            capture_output=True,
            text=True,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("bundle integrity verification failed", result.stderr)
        self.assertFalse((self.home / ".local/lib/quick-swap-tools").exists())
        self.assertFalse((self.home / "host.log").exists())

    def test_existing_desktop_entry_is_not_overwritten(self):
        desktop_entry = (
            self.home / ".local/share/applications/quick-swap-tools.desktop"
        )
        desktop_entry.parent.mkdir(parents=True)
        desktop_entry.write_text("unrelated desktop entry\n")
        result = subprocess.run(
            [str(self.bundle / "install.sh")],
            env=self.env,
            capture_output=True,
            text=True,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("a desktop entry already exists", result.stderr)
        self.assertEqual(desktop_entry.read_text(), "unrelated desktop entry\n")
        self.assertFalse((self.home / "host.log").exists())


if __name__ == "__main__":
    unittest.main()
