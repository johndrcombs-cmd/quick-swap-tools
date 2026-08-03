#!/usr/bin/env python3
"""Quick Swap Tools native-messaging bridge and command client."""

from __future__ import annotations

import json
import os
import selectors
import socket
import struct
import subprocess
import sys
import uuid
from pathlib import Path
from typing import Any, BinaryIO

SUPPORTED_ACTIONS = frozenset({"auction", "giveaway", "status", "inspect"})


def validate_action(action: str) -> str:
    normalized = action.strip().lower()
    if normalized not in SUPPORTED_ACTIONS:
        supported = ", ".join(sorted(SUPPORTED_ACTIONS))
        raise ValueError(f"unsupported action {action!r}; choose one of: {supported}")
    return normalized


def encode_native_message(message: dict[str, Any]) -> bytes:
    body = json.dumps(message, separators=(",", ":")).encode("utf-8")
    return struct.pack("<I", len(body)) + body


def _read_exact(stream: BinaryIO, size: int) -> bytes:
    chunks = bytearray()
    while len(chunks) < size:
        chunk = stream.read(size - len(chunks))
        if not chunk:
            if not chunks:
                return b""
            raise EOFError("native message ended before its declared length")
        chunks.extend(chunk)
    return bytes(chunks)


def decode_native_message(stream: BinaryIO) -> dict[str, Any] | None:
    header = _read_exact(stream, 4)
    if not header:
        return None
    (length,) = struct.unpack("<I", header)
    if length > 1_048_576:
        raise ValueError("native message exceeds 1 MiB safety limit")
    body = _read_exact(stream, length)
    message = json.loads(body)
    if not isinstance(message, dict):
        raise ValueError("native message must be a JSON object")
    return message


def socket_path() -> Path:
    runtime_dir = os.environ.get("XDG_RUNTIME_DIR")
    if runtime_dir:
        return Path(runtime_dir) / "quick-swap-tools.sock"
    return Path.home() / ".cache" / "quick-swap-tools" / "bridge.sock"


def send_action(
    action: str,
    *,
    path: Path | None = None,
    timeout: float = 2.0,
) -> dict[str, Any]:
    request = {
        "type": "command",
        "id": uuid.uuid4().hex,
        "action": validate_action(action),
    }
    target = path or socket_path()
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as client:
        client.settimeout(timeout)
        try:
            client.connect(str(target))
        except FileNotFoundError as exc:
            raise ConnectionError(
                "Quick Swap bridge is offline; load the Firefox extension first"
            ) from exc
        client.sendall(json.dumps(request, separators=(",", ":")).encode() + b"\n")
        received = bytearray()
        while b"\n" not in received:
            chunk = client.recv(4096)
            if not chunk:
                break
            received.extend(chunk)
            if len(received) > 65_536:
                raise ValueError("bridge response exceeds 64 KiB safety limit")
    if not received:
        raise ConnectionError("Quick Swap bridge closed without a response")
    response = json.loads(received.split(b"\n", 1)[0])
    if not isinstance(response, dict):
        raise ValueError("bridge response must be a JSON object")
    return response


def _write_native(stream: BinaryIO, message: dict[str, Any]) -> None:
    stream.write(encode_native_message(message))
    stream.flush()


def _read_socket_request(connection: socket.socket) -> dict[str, Any]:
    connection.settimeout(0.5)
    received = bytearray()
    while b"\n" not in received:
        chunk = connection.recv(4096)
        if not chunk:
            break
        received.extend(chunk)
        if len(received) > 8192:
            raise ValueError("command exceeds 8 KiB safety limit")
    message = json.loads(received.split(b"\n", 1)[0])
    if not isinstance(message, dict):
        raise ValueError("command must be a JSON object")
    message["action"] = validate_action(str(message.get("action", "")))
    if not isinstance(message.get("id"), str) or not message["id"]:
        raise ValueError("command requires a non-empty id")
    message["type"] = "command"
    return message


def serve_host() -> int:
    path = socket_path()
    path.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
    path.unlink(missing_ok=True)
    pending: dict[str, socket.socket] = {}

    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as server:
        server.bind(str(path))
        os.chmod(path, 0o600)
        server.listen(8)
        with selectors.DefaultSelector() as selector:
            selector.register(server, selectors.EVENT_READ, "socket")
            selector.register(sys.stdin.buffer, selectors.EVENT_READ, "browser")
            try:
                while True:
                    for key, _ in selector.select():
                        if key.data == "socket":
                            connection, _ = server.accept()
                            try:
                                request = _read_socket_request(connection)
                                pending[request["id"]] = connection
                                _write_native(sys.stdout.buffer, request)
                            except Exception as exc:
                                response = {"ok": False, "error": str(exc)}
                                connection.sendall(json.dumps(response).encode() + b"\n")
                                connection.close()
                        else:
                            message = decode_native_message(sys.stdin.buffer)
                            if message is None:
                                return 0
                            if message.get("type") == "hello":
                                _write_native(
                                    sys.stdout.buffer,
                                    {"type": "ready", "socket": str(path)},
                                )
                                continue
                            request_id = message.get("id")
                            connection = pending.pop(str(request_id), None)
                            if connection is not None:
                                try:
                                    connection.sendall(
                                        json.dumps(message, separators=(",", ":")).encode()
                                        + b"\n"
                                    )
                                finally:
                                    connection.close()
            finally:
                for connection in pending.values():
                    connection.close()
                path.unlink(missing_ok=True)


def _notify(summary: str, body: str, *, urgency: str = "normal") -> None:
    try:
        subprocess.run(
            ["notify-send", "--urgency", urgency, summary, body],
            check=False,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
    except FileNotFoundError:
        pass


def main(argv: list[str] | None = None) -> int:
    import argparse

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=["host", *sorted(SUPPORTED_ACTIONS)])
    parser.add_argument("--notify", action="store_true", help="show desktop feedback")
    parser.add_argument("--quiet", action="store_true", help="suppress JSON output")
    args = parser.parse_args(argv)

    if args.action == "host":
        return serve_host()

    try:
        result = send_action(args.action)
    except Exception as exc:
        if args.notify:
            _notify("Quick Swap failed", str(exc), urgency="critical")
        if not args.quiet:
            print(json.dumps({"ok": False, "error": str(exc)}))
        return 1

    if args.notify:
        if result.get("ok"):
            _notify("Quick Swap", str(result.get("message", "Action sent")))
        else:
            _notify("Quick Swap failed", str(result.get("error", "Unknown error")), urgency="critical")
    if not args.quiet:
        print(json.dumps(result, separators=(",", ":")))
    return 0 if result.get("ok") else 1


if __name__ == "__main__":
    raise SystemExit(main())
