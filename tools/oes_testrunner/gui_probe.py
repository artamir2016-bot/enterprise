"""OES-TEST: one-shot GUI probe — drive an OES app's test agent WITHOUT computer-use.

The point of this tool: verify / exercise a GUI (designer.exe or enterprise.exe) from a SINGLE
command, with no Gherkin feature file and no desktop driver. It launches the app as its OWN
foreground child process (so the wxApp initialises normally — the reason an app launched inside an
automation shell exits before showing UI does not apply here), connects to the embedded test agent
(--testagent), runs a scripted sequence of agent commands, captures screenshots + user messages +
runtime/compile diagnostics, and quits. The whole run is reported as one JSON blob on stdout.

Why it exists: when computer-use is unavailable there was no way to confirm a GUI change actually
works at runtime (a form opens, an editor populates, an inspector shows the right properties). This
closes that gap using the in-process agent that already drives the UI on the GUI thread.

--------------------------------------------------------------------------------------------------
USAGE
--------------------------------------------------------------------------------------------------
    set PYTHONIOENCODING=utf-8    &:: (Windows) keep Cyrillic intact through PS -> python

    # inline commands (repeatable; "cmd key=val key=val", values coerced like the runner)
    python gui_probe.py --app designer --base E:\Projects\OES\testbase\grow\mformbase ^
        --cmd "listObjects" ^
        --cmd "openMetaEditor name=ItemForm" ^
        --sleep 1.0 ^
        --cmd "listWindows" ^
        --shot E:\Projects\OES\build-perf\mform_editor.png

    # a JSON script (array of steps) — richer control, see STEP FORMAT below
    python gui_probe.py --app designer --base <base> --script probe_mform.json --out report.json

STEP FORMAT (JSON script: a list of steps). Each step is one of:
    {"cmd": "openMetaEditor", "args": {"name": "ItemForm"}}   # an agent command
    {"sleep": 0.8}                                            # pause (lets deferred UI settle)
    {"shot": "C:\\path\\shot.png"}                            # screenshot (sugar for cmd screenshot)
    {"cmd": "...", "args": {...}, "expect": {"opened": true}} # assert keys of the result
    {"cmd": "...", "optional": true}                          # a failure here is recorded, not fatal
A bare string "listWindows" is shorthand for {"cmd": "listWindows"}.

EXIT CODE: 0 if every non-optional step succeeded (and expectations held), 1 otherwise.
The JSON report always prints, pass or fail, so a failing probe is still fully inspectable.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from agent_client import TestAgentClient, AgentError          # noqa: E402

DEFAULT_BIN = r"E:\Projects\OES\build-perf\bin\Release"
EXE = {"designer": "designer.exe", "enterprise": "enterprise.exe"}


def _coerce(value: str):
    """Match the runner's scalar coercion so inline args behave the same as feature steps."""
    if re.fullmatch(r"-?\d+", value):
        return int(value)
    if re.fullmatch(r"-?\d+\.\d+", value):
        return float(value)
    low = value.lower()
    if low in ("истина", "true"):
        return True
    if low in ("ложь", "false"):
        return False
    return value


def parse_inline(spec: str) -> dict:
    """'cmd key=val key=val' -> {"cmd": cmd, "args": {...}}. Values are coerced."""
    parts = spec.split()
    if not parts:
        raise ValueError("empty --cmd")
    cmd, args = parts[0], {}
    for tok in parts[1:]:
        if "=" not in tok:
            raise ValueError(f"bad arg (expected key=value): {tok!r}")
        k, v = tok.split("=", 1)
        args[k] = _coerce(v)
    return {"cmd": cmd, "args": args}


def normalize_step(step) -> dict:
    if isinstance(step, str):
        return {"cmd": step, "args": {}}
    if not isinstance(step, dict):
        raise ValueError(f"bad step (want str or object): {step!r}")
    if "sleep" in step or "shot" in step or "cmd" in step:
        return step
    raise ValueError(f"step has no cmd/sleep/shot: {step!r}")


def run_step(agent: TestAgentClient, step: dict) -> dict:
    """Execute one normalized step; return a record of what happened."""
    if "sleep" in step:
        time.sleep(float(step["sleep"]))
        return {"kind": "sleep", "secs": float(step["sleep"]), "ok": True}

    if "shot" in step:
        res = agent.call("screenshot", path=step["shot"])
        return {"kind": "shot", "path": step["shot"], "ok": True, "result": res}

    cmd = step["cmd"]
    args = step.get("args", {})
    rec = {"kind": "cmd", "cmd": cmd, "args": args}
    try:
        res = agent.call(cmd, **args)
        rec["result"] = res
        rec["ok"] = True
        expect = step.get("expect")
        if isinstance(expect, dict):
            mismatches = {k: {"want": v, "got": res.get(k)}
                          for k, v in expect.items() if res.get(k) != v}
            if mismatches:
                rec["ok"] = False
                rec["expect_failed"] = mismatches
    except (AgentError, ConnectionError) as exc:
        rec["ok"] = False
        rec["error"] = str(exc)
    return rec


def build_script(args) -> list:
    if args.script:
        with open(args.script, encoding="utf-8") as f:
            raw = json.load(f)
        if not isinstance(raw, list):
            raise ValueError("--script must be a JSON array of steps")
        return [normalize_step(s) for s in raw]
    # inline steps, in the order given on the command line
    return [normalize_step(parse_inline(s)) for s in (args.cmd or [])]


def main() -> int:
    ap = argparse.ArgumentParser(
        description="Drive an OES app's test agent in one shot (no computer-use, no feature file).")
    ap.add_argument("--app", choices=sorted(EXE), default="designer")
    ap.add_argument("--base", required=True, help="path to the information base (--file=)")
    ap.add_argument("--bin", default=DEFAULT_BIN, help="dir with designer.exe/enterprise.exe")
    ap.add_argument("--port", type=int, default=1661, help="test-agent port (default 1661)")
    ap.add_argument("--cmd", action="append",
                    help="inline step 'cmd key=val ...' (repeatable, ordered)")
    ap.add_argument("--script", help="JSON file: an array of steps (see module docstring)")
    ap.add_argument("--sleep", type=float, default=None,
                    help="convenience: a trailing pause (s) after the last --cmd before teardown")
    ap.add_argument("--shot", help="convenience: a final screenshot to this PNG before teardown")
    ap.add_argument("--out", help="also write the JSON report to this file")
    ap.add_argument("--keep", action="store_true",
                    help="leave the app running (do not quit) — for manual follow-up")
    ap.add_argument("--launch-timeout", type=float, default=40.0)
    args = ap.parse_args()

    try:
        steps = build_script(args)
    except Exception as exc:  # noqa: BLE001
        print(json.dumps({"ok": False, "error": f"bad script: {exc}"}, ensure_ascii=False))
        return 1
    if args.sleep is not None:
        steps.append({"sleep": args.sleep})
    if args.shot:
        steps.append({"shot": args.shot})

    exe_path = os.path.join(args.bin, EXE[args.app])
    report = {"app": args.app, "base": args.base, "exe": exe_path, "port": args.port,
              "launched": False, "steps": [], "messages": [], "diagnostics": [], "ok": False}

    if not os.path.isfile(exe_path):
        report["error"] = f"exe not found: {exe_path}"
        print(json.dumps(report, ensure_ascii=False, indent=2))
        return 1

    cmd = [exe_path, f"--file={args.base}", f"--testagent={args.port}"]
    proc = subprocess.Popen(cmd, cwd=args.bin)
    report["pid"] = proc.pid

    agent = None
    try:
        agent = TestAgentClient(port=args.port, timeout=args.launch_timeout).connect(retries=60)
        if not agent.ping():
            raise ConnectionError("agent connected but ping failed")
        report["launched"] = True
        report["appInfo"] = agent.app_info()
        # Fresh capture buffers for this run (the agent installs taps lazily on first form op).
        try:
            agent.call("clearMessages")
        except Exception:  # noqa: BLE001
            pass

        all_ok = True
        for step in steps:
            rec = run_step(agent, step)
            report["steps"].append(rec)
            if not rec.get("ok") and not step.get("optional"):
                all_ok = False

        # Always harvest what the UI said / failed with — the real signal of a GUI run.
        try:
            report["messages"] = agent.call("getMessages").get("messages", [])
        except Exception:  # noqa: BLE001
            pass
        try:
            report["diagnostics"] = agent.call("getDiagnostics").get("diagnostics", [])
        except Exception:  # noqa: BLE001
            pass
        # A probe fails if a step failed OR the app emitted an error-level diagnostic.
        had_error_diag = any(d.get("kind") in (0, 1) for d in report["diagnostics"])
        report["ok"] = all_ok and not had_error_diag
        report["had_error_diagnostic"] = had_error_diag

    except Exception as exc:  # noqa: BLE001
        report["error"] = str(exc)
        report["ok"] = False
    finally:
        if agent is not None and not args.keep:
            try:
                agent.quit()
            except Exception:  # noqa: BLE001
                pass
            agent.close()
            try:
                proc.wait(timeout=10)
            except Exception:  # noqa: BLE001
                try:
                    proc.terminate()
                except Exception:  # noqa: BLE001
                    pass
        elif args.keep and agent is not None:
            agent.close()

    text = json.dumps(report, ensure_ascii=False, indent=2)
    if args.out:
        with open(args.out, "w", encoding="utf-8") as f:
            f.write(text)
    print(text)
    return 0 if report["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
