import json
import os
import shutil
import stat
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).parents[1]
INSTALLER = ROOT / "scripts" / "install.sh"
UNINSTALLER = ROOT / "scripts" / "uninstall.sh"


class SourceInstallerTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.project = self.root / "project"
        self.home = self.root / "home"
        self.fake_bin = self.root / "bin"
        (self.project / "scripts").mkdir(parents=True)
        (self.project / "extension").mkdir()
        self.home.mkdir()
        self.fake_bin.mkdir()

        shutil.copy2(INSTALLER, self.project / "scripts/install.sh")
        shutil.copy2(UNINSTALLER, self.project / "scripts/uninstall.sh")
        (self.project / "extension/manifest.json").write_text("{}\n")
        (self.project / "extension/.amo-upload-uuid").write_text("local-only\n")
        self._write_executable(
            self.project / "host-stub",
            "#!/bin/sh\nprintf '%s\\n' \"$*\" >>\"$HOME/host.log\"\n"
            "if [ \"${QST_FAIL_REGISTER:-0}\" = 1 ] && [ \"${1:-}\" = --register-only ]; then exit 1; fi\n"
            "exit 0\n",
        )
        self._write_executable(
            self.project / "scripts/build-native.sh",
            "#!/bin/sh\nset -eu\nROOT=$(cd \"$(dirname \"$0\")/..\" && pwd)\n"
            "mkdir -p \"$ROOT/build\"\n"
            "cp \"$ROOT/host-stub\" \"$ROOT/build/quick-swap-host\"\n"
            "printf 'built\\n' >>\"$HOME/build.log\"\n",
        )
        self._write_executable(
            self.fake_bin / "kreadconfig6",
            "#!/bin/sh\ncase \"$*\" in\n"
            "  *'Grid View'*) printf 'Meta+G,Meta+G,Grid View\\n' ;;\n"
            "  *) printf 'Meta+A,Meta+A,Walk Through Activities\\n' ;;\n"
            "esac\n",
        )
        self._write_executable(
            self.fake_bin / "kwriteconfig6",
            "#!/bin/sh\nprintf '%s\\n' \"$*\" >>\"$HOME/kwrite.log\"\n",
        )
        self.env = {
            **os.environ,
            "HOME": str(self.home),
            "PATH": f"{self.fake_bin}:{os.environ['PATH']}",
        }

    @staticmethod
    def _write_executable(path: Path, content: str):
        path.write_text(content)
        path.chmod(path.stat().st_mode | stat.S_IXUSR)

    def test_successful_install_is_removable_and_excludes_local_tracker(self):
        subprocess.run(
            [str(self.project / "scripts/install.sh")], env=self.env, check=True
        )
        install_root = self.home / ".local/lib/quick-swap-tools"
        self.assertTrue((install_root / "quick-swap-host").is_file())
        self.assertTrue((install_root / "uninstall.sh").is_file())
        self.assertFalse((install_root / "extension/.amo-upload-uuid").exists())
        self.assertEqual(
            (self.home / ".local/state/quick-swap-tools").stat().st_mode & 0o777,
            0o700,
        )
        manifest_path = (
            self.home
            / ".mozilla/native-messaging-hosts/com.onibyts.quickswap.json"
        )
        manifest = json.loads(manifest_path.read_text())
        self.assertEqual(manifest["path"], str(install_root / "quick-swap-host"))

        subprocess.run([str(install_root / "uninstall.sh")], env=self.env, check=True)
        self.assertFalse(install_root.exists())
        self.assertFalse(manifest_path.exists())
        self.assertFalse(
            (self.home / ".local/state/quick-swap-tools").exists()
        )

    def test_failed_registration_rolls_back_source_install(self):
        result = subprocess.run(
            [str(self.project / "scripts/install.sh")],
            env={**self.env, "QST_FAIL_REGISTER": "1"},
            capture_output=True,
            text=True,
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
        self.assertIn(
            "Grid View Meta+G,Meta+G,Grid View",
            (self.home / "kwrite.log").read_text(),
        )

    def test_existing_desktop_entry_is_not_overwritten(self):
        desktop_entry = (
            self.home / ".local/share/applications/quick-swap-tools.desktop"
        )
        desktop_entry.parent.mkdir(parents=True)
        desktop_entry.write_text("unrelated desktop entry\n")
        result = subprocess.run(
            [str(self.project / "scripts/install.sh")],
            env=self.env,
            capture_output=True,
            text=True,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("a desktop entry already exists", result.stderr)
        self.assertEqual(desktop_entry.read_text(), "unrelated desktop entry\n")
        self.assertFalse((self.home / "build.log").exists())


if __name__ == "__main__":
    unittest.main()
