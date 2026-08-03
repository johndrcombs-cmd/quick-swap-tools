import importlib.util
import io
import json
import os
import socket
import struct
import tempfile
import threading
import unittest
from unittest import mock
from pathlib import Path


MODULE_PATH = Path(__file__).parents[1] / "native" / "quick_swap.py"


def load_module():
    spec = importlib.util.spec_from_file_location("quick_swap", MODULE_PATH)
    assert spec is not None
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


class ProtocolTests(unittest.TestCase):
    def test_validate_accepts_supported_actions(self):
        module = load_module()
        for action in ("auction", "giveaway", "status", "inspect"):
            self.assertEqual(module.validate_action(action), action)

    def test_validate_rejects_unknown_action(self):
        module = load_module()
        with self.assertRaisesRegex(ValueError, "unsupported action"):
            module.validate_action("delete-everything")

    def test_native_message_uses_little_endian_length_prefix(self):
        module = load_module()
        payload = {"type": "command", "action": "auction", "id": "abc"}
        framed = module.encode_native_message(payload)
        (length,) = struct.unpack("<I", framed[:4])
        self.assertEqual(length, len(framed[4:]))
        self.assertEqual(json.loads(framed[4:]), payload)

    def test_decode_native_message_round_trips_a_frame(self):
        module = load_module()
        payload = {"type": "result", "id": "abc", "ok": True}
        stream = io.BytesIO(module.encode_native_message(payload))
        self.assertEqual(module.decode_native_message(stream), payload)

    def test_socket_path_prefers_xdg_runtime_dir(self):
        module = load_module()
        with tempfile.TemporaryDirectory() as directory:
            with mock.patch.dict(os.environ, {"XDG_RUNTIME_DIR": directory}):
                self.assertEqual(
                    module.socket_path(),
                    Path(directory) / "quick-swap-tools.sock",
                )

    def test_send_action_round_trips_over_unix_socket(self):
        module = load_module()
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "bridge.sock"
            ready = threading.Event()

            def fake_host():
                with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as server:
                    server.bind(str(path))
                    server.listen(1)
                    ready.set()
                    connection, _ = server.accept()
                    with connection:
                        request = json.loads(connection.recv(4096))
                        response = {
                            "id": request["id"],
                            "ok": True,
                            "action": request["action"],
                        }
                        connection.sendall(json.dumps(response).encode() + b"\n")

            thread = threading.Thread(target=fake_host, daemon=True)
            thread.start()
            self.assertTrue(ready.wait(timeout=1))
            result = module.send_action("auction", path=path, timeout=1)
            thread.join(timeout=1)
            self.assertEqual(result["action"], "auction")
            self.assertTrue(result["ok"])


if __name__ == "__main__":
    unittest.main()
