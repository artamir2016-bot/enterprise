#!/usr/bin/env python3
"""OES development MCP server (stdio) for Claude Code — variant B (thin shim).

Wraps the OES CLI tools and the in-app test agent so Claude Code can DEVELOP
configurations and drive running apps:

  * oes_config_generate — greenfield JSON spec -> .mcf              (oes_config_gen)
  * oes_config_edit     — merge a JSON patch onto an existing .mcf  (oes_config_edit)
  * oes_config_load     — load an .mcf into a file base (+update DB) (designer batch)
  * oes_config_check    — compile-check the base's modules          (designer batch)
  * oes_app             — forward any command to a running app's --testagent
                          (Enterprise/Configurator live: forms, DB objects, UI,
                          debug, profiler — the whole agent surface)

Transport: newline-delimited JSON-RPC 2.0 over stdio (the MCP stdio transport).
No third-party dependencies. HTTP-in-app embedding is a later migration; the tool
names stay stable across it.

Config via env:
  OES_BIN  — dir with designer.exe / enterprise.exe / oes_config_*.exe
             (default: <repo>/build-perf/bin/Release)
"""

import json
import os
import socket
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
BIN = os.environ.get("OES_BIN", os.path.join(REPO, "build-perf", "bin", "Release"))

SERVER_INFO = {"name": "oes-dev", "version": "0.1.0"}
PROTOCOL_VERSION = "2024-11-05"


def log(*a):
    print("[oes-mcp]", *a, file=sys.stderr, flush=True)


def exe(name):
    return os.path.join(BIN, name + (".exe" if os.name == "nt" else ""))


# ---- subprocess helpers ------------------------------------------------------

def run(argv, timeout=180):
    """Run a CLI tool (no shell -> clean arg quoting). Return combined text."""
    try:
        p = subprocess.run(argv, cwd=BIN, capture_output=True, timeout=timeout)
        out = (p.stdout or b"").decode("utf-8", "replace")
        err = (p.stderr or b"").decode("utf-8", "replace")
        tail = f"\n[stderr]\n{err}" if err.strip() else ""
        return f"exit={p.returncode}\n{out}{tail}"
    except Exception as ex:
        return f"ERROR running {argv[0]}: {ex}"


def _write_temp(text, suffix):
    fd, path = tempfile.mkstemp(suffix=suffix, prefix="oes_mcp_")
    with os.fdopen(fd, "w", encoding="utf-8") as f:
        f.write(text)
    return path


def designer_batch(base, extra_args):
    """Run designer.exe batch mode; capture its /Out report (GUI app has no stdout)."""
    out_log = _write_temp("", ".txt")
    argv = [exe("designer"), f"/F{base}"] + extra_args + [f"/Out{out_log}"]
    run(argv)
    try:
        with open(out_log, encoding="utf-8", errors="replace") as f:
            report = f.read()
    except Exception:
        report = "(no report written)"
    finally:
        try: os.remove(out_log)
        except Exception: pass
    return report.strip() or "(empty report)"


# ---- test-agent client (framed JSON over TCP) --------------------------------

def agent_call(port, cmd, args):
    s = socket.create_connection(("127.0.0.1", int(port)), timeout=30)
    try:
        req = json.dumps({"id": 1, "cmd": cmd, "args": args or {}}, ensure_ascii=False).encode("utf-8")
        s.sendall(struct.pack("<I", len(req)) + req)
        hdr = _recv_exact(s, 4)
        (n,) = struct.unpack("<I", hdr)
        resp = json.loads(_recv_exact(s, n).decode("utf-8"))
        if resp.get("ok"):
            return json.dumps(resp.get("result", {}), ensure_ascii=False, indent=2)
        return f"agent error: {resp.get('error')}"
    finally:
        s.close()


def _recv_exact(s, n):
    buf = b""
    while len(buf) < n:
        chunk = s.recv(n - len(buf))
        if not chunk:
            raise ConnectionError("agent closed the connection")
        buf += chunk
    return buf


# ---- tool implementations ----------------------------------------------------

def t_generate(a):
    spec = _write_temp(a["spec"], ".json") if "spec" in a else a["spec_file"]
    return run([exe("oes_config_gen"), spec, a["out_mcf"]])

def t_edit(a):
    patch = _write_temp(a["patch"], ".json") if "patch" in a else a["patch_file"]
    argv = [exe("oes_config_edit"), a["in_mcf"], patch]
    if a.get("out_mcf"):
        argv.append(a["out_mcf"])
    return run(argv)

def t_load(a):
    extra = ["/LoadCfg", a["mcf"]]
    if a.get("update_db", True):
        extra.append("/UpdateDBCfg")
    return designer_batch(a["base"], extra)

def t_check(a):
    return designer_batch(a["base"], ["/CheckModules"])

def t_app(a):
    return agent_call(a.get("port", 1651), a["cmd"], a.get("args", {}))


TOOLS = [
    {"name": "oes_config_generate",
     "description": "Build a NEW .mcf configuration from a friendly JSON spec (greenfield, create-only). "
                    "Spec shape: {name, catalogs:[{name,attributes:[{name,type,length,precision,scale}]}], "
                    "documents, enums, constants, commonModules:[{name,code}], ...}. type: String|Number|Date|Boolean|ref.",
     "inputSchema": {"type": "object",
        "properties": {"spec": {"type": "string", "description": "JSON spec text"},
                       "spec_file": {"type": "string"},
                       "out_mcf": {"type": "string", "description": "output .mcf path"}},
        "required": ["out_mcf"]},
     "fn": t_generate},
    {"name": "oes_config_edit",
     "description": "MERGE a JSON patch onto an EXISTING .mcf: adds new objects/attributes/modules or EDITS "
                    "existing ones (idempotent by name). Same JSON shape as generate; only what you mention is touched.",
     "inputSchema": {"type": "object",
        "properties": {"in_mcf": {"type": "string"},
                       "patch": {"type": "string", "description": "JSON patch text"},
                       "patch_file": {"type": "string"},
                       "out_mcf": {"type": "string", "description": "defaults to in_mcf (in place)"}},
        "required": ["in_mcf"]},
     "fn": t_edit},
    {"name": "oes_config_load",
     "description": "Load an .mcf into a file base and (by default) apply it to the DB. Returns the batch report. "
                    "Close any app holding the base first.",
     "inputSchema": {"type": "object",
        "properties": {"base": {"type": "string", "description": "base directory (file infobase)"},
                       "mcf": {"type": "string"},
                       "update_db": {"type": "boolean", "default": True}},
        "required": ["base", "mcf"]},
     "fn": t_load},
    {"name": "oes_config_check",
     "description": "Compile-check all modules of a base's configuration; returns diagnostics (errors with module+line).",
     "inputSchema": {"type": "object",
        "properties": {"base": {"type": "string"}}, "required": ["base"]},
     "fn": t_check},
    {"name": "oes_app",
     "description": "Send a command to a RUNNING app's test agent (start it with --testagent=PORT). "
                    "Enterprise (port 1651) for DB objects + UI: openForm, listControls, getControlValue, "
                    "setControlValue, pressCommand, getForms, getDiagnostics, getMessages, listWindows. "
                    "Configurator (port 1652) for development/debug: invokeMenu, setBreakpoint, debugState, "
                    "readTreeList, openMetaEditor. Pass cmd + args (object).",
     "inputSchema": {"type": "object",
        "properties": {"port": {"type": "integer", "default": 1651},
                       "cmd": {"type": "string"},
                       "args": {"type": "object"}},
        "required": ["cmd"]},
     "fn": t_app},
]
TOOL_BY_NAME = {t["name"]: t for t in TOOLS}


# ---- JSON-RPC 2.0 over stdio -------------------------------------------------

def handle(msg):
    method = msg.get("method")
    mid = msg.get("id")
    if method == "initialize":
        return ok(mid, {"protocolVersion": PROTOCOL_VERSION,
                        "capabilities": {"tools": {"listChanged": False}},
                        "serverInfo": SERVER_INFO})
    if method == "notifications/initialized" or method == "notifications/cancelled":
        return None  # notification, no reply
    if method == "ping":
        return ok(mid, {})
    if method == "tools/list":
        return ok(mid, {"tools": [{k: t[k] for k in ("name", "description", "inputSchema")} for t in TOOLS]})
    if method == "tools/call":
        params = msg.get("params", {})
        name = params.get("name")
        args = params.get("arguments", {}) or {}
        tool = TOOL_BY_NAME.get(name)
        if tool is None:
            return err(mid, -32602, f"unknown tool: {name}")
        try:
            text = tool["fn"](args)
            return ok(mid, {"content": [{"type": "text", "text": text}], "isError": False})
        except Exception as ex:
            return ok(mid, {"content": [{"type": "text", "text": f"ERROR: {ex}"}], "isError": True})
    if mid is not None:
        return err(mid, -32601, f"method not found: {method}")
    return None


def ok(mid, result):  return {"jsonrpc": "2.0", "id": mid, "result": result}
def err(mid, code, m): return {"jsonrpc": "2.0", "id": mid, "error": {"code": code, "message": m}}


def main():
    log(f"starting; BIN={BIN}")
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            msg = json.loads(line)
        except Exception as ex:
            log("bad json:", ex); continue
        resp = handle(msg)
        if resp is not None:
            sys.stdout.write(json.dumps(resp, ensure_ascii=False) + "\n")
            sys.stdout.flush()


if __name__ == "__main__":
    main()
