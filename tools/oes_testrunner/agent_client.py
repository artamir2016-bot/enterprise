"""OES-TEST: client for the embedded test-automation agent (docs/test-automation.md).

Transport: framed JSON over TCP. Each message is a 4-byte little-endian length prefix followed by a
UTF-8 JSON payload.
    request  = {"id": <n>, "cmd": "<name>", "args": {...}}
    response = {"id": <n>, "ok": true,  "result": {...}}
             | {"id": <n>, "ok": false, "error": "<msg>"}

The agent runs inside a running OES GUI app started with --testagent=<port> (localhost only).
"""

import json
import socket
import struct
import time


class AgentError(RuntimeError):
    """The agent returned ok=false for a command."""


class TestAgentClient:
    def __init__(self, host: str = "127.0.0.1", port: int = 1651, timeout: float = 30.0):
        self.host = host
        self.port = port
        self.timeout = timeout
        self._sock: socket.socket | None = None
        self._next_id = 1

    # -- connection ------------------------------------------------------------------------------
    def connect(self, retries: int = 40, delay: float = 0.25) -> "TestAgentClient":
        """Connect, retrying while the app finishes coming up."""
        last = None
        for _ in range(retries):
            try:
                s = socket.create_connection((self.host, self.port), timeout=self.timeout)
                s.settimeout(self.timeout)
                self._sock = s
                return self
            except OSError as exc:
                last = exc
                time.sleep(delay)
        raise ConnectionError(f"cannot reach test agent at {self.host}:{self.port}: {last}")

    def close(self) -> None:
        if self._sock is not None:
            try:
                self._sock.close()
            finally:
                self._sock = None

    def __enter__(self) -> "TestAgentClient":
        return self.connect()

    def __exit__(self, *exc) -> None:
        self.close()

    # -- framing ---------------------------------------------------------------------------------
    def _send(self, obj: dict) -> None:
        payload = json.dumps(obj, ensure_ascii=False).encode("utf-8")
        self._sock.sendall(struct.pack("<I", len(payload)) + payload)

    def _recv(self) -> dict:
        header = self._recv_exact(4)
        (length,) = struct.unpack("<I", header)
        return json.loads(self._recv_exact(length).decode("utf-8"))

    def _recv_exact(self, n: int) -> bytes:
        buf = bytearray()
        while len(buf) < n:
            chunk = self._sock.recv(n - len(buf))
            if not chunk:
                raise ConnectionError("test agent closed the connection")
            buf.extend(chunk)
        return bytes(buf)

    # -- commands --------------------------------------------------------------------------------
    def call(self, cmd: str, **args) -> dict:
        """Send one command, return its result dict. Raises AgentError on ok=false."""
        if self._sock is None:
            raise ConnectionError("not connected")
        req_id = self._next_id
        self._next_id += 1
        self._send({"id": req_id, "cmd": cmd, "args": args})
        resp = self._recv()
        if not resp.get("ok"):
            raise AgentError(resp.get("error", "unknown agent error"))
        return resp.get("result", {})

    # convenience wrappers ------------------------------------------------------------------------
    def ping(self) -> bool:
        return bool(self.call("ping").get("pong"))

    def app_info(self) -> dict:
        return self.call("appInfo")

    def quit(self) -> dict:
        return self.call("quit")


if __name__ == "__main__":
    import argparse

    ap = argparse.ArgumentParser(description="Smoke-test the OES test agent")
    ap.add_argument("--port", type=int, default=1651)
    ap.add_argument("--host", default="127.0.0.1")
    args = ap.parse_args()

    with TestAgentClient(args.host, args.port) as c:
        print("ping   ->", c.ping())
        print("info   ->", c.app_info())
