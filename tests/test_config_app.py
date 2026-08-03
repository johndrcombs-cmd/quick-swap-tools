import json
import subprocess
import unittest
from pathlib import Path


ROOT = Path(__file__).parents[1]
CONFIG_BINARY = ROOT / "build" / "quick-swap-config"


class ShortcutConfigCliTests(unittest.TestCase):
    def run_validate(self, auction: str, giveaway: str):
        return subprocess.run(
            [
                str(CONFIG_BINARY),
                "--validate-shortcuts",
                auction,
                giveaway,
            ],
            capture_output=True,
            text=True,
        )

    def test_distinct_function_keys_are_valid(self):
        result = self.run_validate("F20", "F21")
        self.assertEqual(result.returncode, 0, result.stderr)
        payload = json.loads(result.stdout)
        self.assertTrue(payload["valid"])
        self.assertEqual(payload["warnings"], [])

    def test_duplicate_shortcuts_are_rejected(self):
        result = self.run_validate("F20", "F20")
        self.assertNotEqual(result.returncode, 0)
        payload = json.loads(result.stdout)
        self.assertFalse(payload["valid"])
        self.assertEqual(payload["error"], "auction and giveaway shortcuts must differ")

    def test_plain_letter_is_valid_with_global_keyboard_warning(self):
        result = self.run_validate("A", "F21")
        self.assertEqual(result.returncode, 0, result.stderr)
        payload = json.loads(result.stdout)
        self.assertTrue(payload["valid"])
        self.assertEqual(
            payload["warnings"], ["auction shortcut is an unmodified global key"]
        )

    def test_plain_symbol_is_valid_with_global_keyboard_warning(self):
        result = self.run_validate(";", "F21")
        self.assertEqual(result.returncode, 0, result.stderr)
        payload = json.loads(result.stdout)
        self.assertTrue(payload["valid"])
        self.assertEqual(
            payload["warnings"], ["auction shortcut is an unmodified global key"]
        )

    def test_unmodified_navigation_key_has_global_warning(self):
        result = self.run_validate("Escape", "F21")
        self.assertEqual(result.returncode, 0, result.stderr)
        payload = json.loads(result.stdout)
        self.assertTrue(payload["valid"])
        self.assertEqual(
            payload["warnings"], ["auction shortcut is an unmodified global key"]
        )

    def test_empty_shortcut_is_rejected(self):
        result = self.run_validate("none", "F21")
        self.assertNotEqual(result.returncode, 0)
        payload = json.loads(result.stdout)
        self.assertFalse(payload["valid"])
        self.assertEqual(payload["error"], "both shortcuts are required")

    def test_transaction_logic_supports_swaps_and_verified_rollback(self):
        result = subprocess.run(
            [str(CONFIG_BINARY), "--self-test-transaction"],
            capture_output=True,
            text=True,
            timeout=5,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        payload = json.loads(result.stdout)
        self.assertTrue(payload["swapSucceeded"])
        self.assertTrue(payload["rollbackRestored"])


if __name__ == "__main__":
    unittest.main()
