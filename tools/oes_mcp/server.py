#!/usr/bin/env python
r"""OES data MCP server — gives a model read access to an OES infobase's DATA.

It drives the headless `oes_query` engine (which opens a FILE infobase + a live
session and answers OES queries through the real engine — metadata-aware, not raw
SQL). One persistent oes_query subprocess per base; requests are one JSON line in,
one JSON line out.

Tools exposed to the model:
  * oes_run_query(query, limit)  — run an OES query, get {columns, rows, count}
  * oes_list_objects()           — list the configuration's catalogs & documents

Config via env (or defaults):
  OES_BASE  infobase directory      (default: E:\Projects\OES\testbase\devtracker_base)
  OES_BIN   dir with oes_query.exe  (default: E:\Projects\OES\build-perf\bin\Release)

Register (e.g. in a Claude MCP config):
  {"mcpServers": {"oes-data": {
      "command": "C:\\Users\\User\\.pyenv\\pyenv-win\\versions\\3.12.10\\python.exe",
      "args": ["E:\\Projects\\OES\\tools\\oes_mcp\\server.py"],
      "env": {"OES_BASE": "E:\\Projects\\OES\\testbase\\devtracker_base"}}}}
"""

from __future__ import annotations

import json
import os
import subprocess
import threading

from mcp.server.fastmcp import FastMCP

BASE = os.environ.get("OES_BASE", r"E:\Projects\OES\testbase\devtracker_base")
BIN = os.environ.get("OES_BIN", r"E:\Projects\OES\build-perf\bin\Release")

mcp = FastMCP("oes-data")

_proc: subprocess.Popen | None = None
_lock = threading.Lock()


def _ensure() -> subprocess.Popen:
    """Start (or reuse) the oes_query subprocess and wait for its READY banner."""
    global _proc
    if _proc is not None and _proc.poll() is None:
        return _proc
    exe = os.path.join(BIN, "oes_query.exe")
    if not os.path.exists(exe):
        raise RuntimeError(f"oes_query.exe not found at {exe} (set OES_BIN)")
    _proc = subprocess.Popen(
        [exe, "--file", BASE],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        text=True, encoding="utf-8", bufsize=1,
    )
    # Wait for READY (printed to stderr once the base + session are up).
    while True:
        line = _proc.stderr.readline()
        if not line:
            raise RuntimeError("oes_query exited before READY")
        if "READY" in line:
            return _proc


def _request(payload: dict) -> dict:
    """Send one JSON request line, read one JSON response line."""
    line = json.dumps(payload, ensure_ascii=False)
    with _lock:
        p = _ensure()
        assert p.stdin and p.stdout
        p.stdin.write(line + "\n")
        p.stdin.flush()
        out = p.stdout.readline().strip()
    try:
        return json.loads(out)
    except Exception:
        return {"error": f"bad response: {out[:500]}"}


@mcp.tool()
def oes_run_query(query: str, limit: int = 100) -> dict:
    """Run an OES query against the infobase and return its result.

    `query` is OES query-language text (metadata-aware: Catalog.<Name>,
    Document.<Name>, reference dot-walk, &parameters are not bound here). Returns
    {"columns": [...], "rows": [[...], ...], "count": N} or {"error": "..."}.

    Example: SELECT Ref, Description FROM Catalog.Компоненты
    """
    return _request({"q": query, "limit": int(limit)})


@mcp.tool()
def oes_list_objects() -> dict:
    """List the configuration's queryable objects (catalogs & documents).

    Returns {"objects": [{"name": "...", "kind": "Catalog"|"Document"}, ...]}.
    Use this to discover what can be queried before writing a query.
    """
    return _request({"cmd": "meta"})


if __name__ == "__main__":
    mcp.run()
