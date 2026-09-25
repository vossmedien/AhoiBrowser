"""Minimal Chrome DevTools Protocol client over a stdlib WebSocket.

Only what the performance harness needs: connect to a target's
webSocketDebuggerUrl, send commands, and collect events. Text frames only,
client-masked as RFC 6455 requires; no extensions, no compression.
"""

from __future__ import annotations

import base64
import json
import os
import socket
import struct
import time
import urllib.parse
import urllib.request
from typing import Any, Optional


class CDPError(RuntimeError):
    pass


def http_json(port: int, path: str, method: str = "GET", timeout: float = 5.0) -> Any:
    request = urllib.request.Request(f"http://127.0.0.1:{port}{path}", method=method)
    with urllib.request.urlopen(request, timeout=timeout) as response:
        return json.loads(response.read().decode("utf-8"))


def wait_for_endpoint(port: int, timeout: float) -> float:
    """Poll /json/version until DevTools answers; return the monotonic time it did."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            http_json(port, "/json/version", timeout=1.0)
            return time.monotonic()
        except OSError:
            time.sleep(0.05)
    raise CDPError(f"DevTools endpoint on port {port} did not answer in {timeout}s")


def encode_frame(payload: bytes, opcode: int = 0x1) -> bytes:
    header = bytearray([0x80 | opcode])
    length = len(payload)
    if length < 126:
        header.append(0x80 | length)
    elif length < 1 << 16:
        header.append(0x80 | 126)
        header += struct.pack("!H", length)
    else:
        header.append(0x80 | 127)
        header += struct.pack("!Q", length)
    mask = os.urandom(4)
    header += mask
    return bytes(header) + bytes(b ^ mask[i % 4] for i, b in enumerate(payload))


def decode_frame(read) -> tuple[int, bytes, bool]:
    first, second = read(2)
    fin = bool(first & 0x80)
    opcode = first & 0x0F
    length = second & 0x7F
    if length == 126:
        (length,) = struct.unpack("!H", read(2))
    elif length == 127:
        (length,) = struct.unpack("!Q", read(8))
    mask = read(4) if second & 0x80 else None
    payload = read(length)
    if mask:
        payload = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
    return opcode, payload, fin


class CDPSession:
    def __init__(self, ws_url: str, timeout: float = 30.0):
        parsed = urllib.parse.urlparse(ws_url)
        self.sock = socket.create_connection((parsed.hostname, parsed.port), timeout=timeout)
        key = base64.b64encode(os.urandom(16)).decode()
        self.sock.sendall((
            f"GET {parsed.path} HTTP/1.1\r\nHost: {parsed.hostname}:{parsed.port}\r\n"
            "Upgrade: websocket\r\nConnection: Upgrade\r\n"
            f"Sec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n").encode())
        response = b""
        while b"\r\n\r\n" not in response:
            chunk = self.sock.recv(4096)
            if not chunk:
                raise CDPError("DevTools closed the WebSocket handshake")
            response += chunk
        if b" 101 " not in response.split(b"\r\n", 1)[0]:
            raise CDPError(f"WebSocket upgrade refused: {response[:80]!r}")
        self.buffer = response.split(b"\r\n\r\n", 1)[1]
        self.next_id = 0
        self.events: list[dict] = []

    def _read(self, count: int) -> bytes:
        while len(self.buffer) < count:
            chunk = self.sock.recv(max(65536, count - len(self.buffer)))
            if not chunk:
                raise CDPError("DevTools closed the WebSocket")
            self.buffer += chunk
        data, self.buffer = self.buffer[:count], self.buffer[count:]
        return data

    def _message(self) -> dict:
        parts = []
        while True:
            opcode, payload, fin = decode_frame(self._read)
            if opcode == 0x8:
                raise CDPError("DevTools closed the WebSocket")
            if opcode == 0x9:
                self.sock.sendall(encode_frame(payload, 0xA))
                continue
            if opcode in (0x1, 0x0):
                parts.append(payload)
                if fin:
                    return json.loads(b"".join(parts).decode("utf-8"))

    def send(self, method: str, params: Optional[dict] = None,
             session_id: Optional[str] = None) -> dict:
        self.next_id += 1
        command = {"id": self.next_id, "method": method, "params": params or {}}
        if session_id:
            command["sessionId"] = session_id
        self.sock.sendall(encode_frame(json.dumps(command).encode("utf-8")))
        while True:
            message = self._message()
            if message.get("id") == self.next_id:
                if "error" in message:
                    raise CDPError(f"{method}: {message['error']}")
                return message.get("result", {})
            self.events.append(message)

    def wait_event(self, method: str, timeout: float) -> dict:
        return self.wait_any((method,), timeout)

    def wait_any(self, methods: tuple[str, ...], timeout: float) -> dict:
        deadline = time.monotonic() + timeout
        for index, event in enumerate(self.events):
            if event.get("method") in methods:
                return self.events.pop(index)
        while time.monotonic() < deadline:
            self.sock.settimeout(max(0.05, deadline - time.monotonic()))
            try:
                message = self._message()
            except socket.timeout:
                break
            if message.get("method") in methods:
                return message
            self.events.append(message)
        raise CDPError(f"none of {', '.join(methods)} within {timeout}s")

    def evaluate(self, expression: str, session_id: Optional[str] = None) -> Any:
        result = self.send("Runtime.evaluate", {"expression": expression,
                                                "returnByValue": True,
                                                "awaitPromise": True}, session_id)
        if "exceptionDetails" in result:
            raise CDPError(f"evaluation failed: {result['exceptionDetails']}")
        return result["result"].get("value")

    def close(self) -> None:
        try:
            self.sock.sendall(encode_frame(b"", 0x8))
        except OSError:
            pass
        self.sock.close()


def browser_session(port: int) -> CDPSession:
    return CDPSession(http_json(port, "/json/version")["webSocketDebuggerUrl"])
