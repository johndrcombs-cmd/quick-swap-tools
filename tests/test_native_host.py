import json
import os
import struct
import subprocess
import tempfile
import time
import unittest
from pathlib import Path


ROOT = Path(__file__).parents[1]
BINARY = ROOT / "build" / "quick-swap-host"


class NativeHostIntegrationTests(unittest.TestCase):
    def test_hello_returns_ready_frame(self):
        env = {
            **os.environ,
            "QT_QPA_PLATFORM": "offscreen",
            "QUICK_SWAP_NO_SHORTCUTS": "1",
        }
        process = subprocess.Popen(
            [str(BINARY)],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            env=env,
        )
        def stop_process():
            if process.poll() is None:
                process.terminate()
                process.wait(timeout=1)
            for stream in (process.stdin, process.stdout, process.stderr):
                if stream is not None:
                    stream.close()

        self.addCleanup(stop_process)
        assert process.stdin is not None
        assert process.stdout is not None
        payload = json.dumps({"type": "hello", "version": "test"}).encode()
        process.stdin.write(struct.pack("<I", len(payload)) + payload)
        process.stdin.flush()

        header = process.stdout.read(4)
        self.assertEqual(len(header), 4)
        (length,) = struct.unpack("<I", header)
        message = json.loads(process.stdout.read(length))
        self.assertEqual(message["type"], "ready")
        self.assertFalse(message["auctionShortcut"])
        self.assertFalse(message["giveawayShortcut"])

    def test_persisted_result_omits_private_url_and_is_owner_only(self):
        with tempfile.TemporaryDirectory() as state_home:
            env = {
                **os.environ,
                "QT_QPA_PLATFORM": "offscreen",
                "QUICK_SWAP_NO_SHORTCUTS": "1",
                "XDG_STATE_HOME": state_home,
            }
            process = subprocess.Popen(
                [str(BINARY)],
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                env=env,
            )
            try:
                assert process.stdin is not None
                payload = json.dumps(
                    {
                        "type": "result",
                        "ok": True,
                        "action": "inspect",
                        "url": "https://www.whatnot.com/dashboard/live/private-id",
                        "message": "private-id must not persist",
                        "error": "failed at https://www.whatnot.com/dashboard/live/private-id",
                    }
                ).encode()
                process.stdin.write(struct.pack("<I", len(payload)) + payload)
                process.stdin.flush()
                result_path = (
                    Path(state_home) / "quick-swap-tools" / "last-result.json"
                )
                for _ in range(50):
                    if result_path.exists() and result_path.stat().st_size:
                        break
                    time.sleep(0.02)
                persisted = json.loads(result_path.read_text())
                self.assertNotIn("url", persisted)
                self.assertNotIn("message", persisted)
                self.assertNotIn("private-id", json.dumps(persisted))
                self.assertEqual(persisted["error"], "failed at [redacted-url]")
                self.assertEqual(result_path.stat().st_mode & 0o777, 0o600)
            finally:
                process.terminate()
                process.wait(timeout=1)
                for stream in (process.stdin, process.stdout, process.stderr):
                    if stream is not None:
                        stream.close()


if __name__ == "__main__":
    unittest.main()
