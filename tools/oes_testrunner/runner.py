"""OES-TEST: BDD test runner for OES (Vanessa-like). See docs/test-automation.md.

Parses a Russian .feature file, launches designer.exe / enterprise.exe with --testagent, drives them
through the embedded agent, and reports results (console + optional JUnit XML). One scenario can span
BOTH apps (Designer then Enterprise) — each launch gets its own agent on its own port.

Usage:
    python runner.py <feature-file> [--bin <dir>] [--junit <out.xml>]

Steps are matched by Russian phrases (see STEPS below); extend that table to add steps — no platform
rebuild needed.
"""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
import time
import xml.etree.ElementTree as ET

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from agent_client import TestAgentClient, AgentError          # noqa: E402
from gherkin import parse_feature, Scenario, Step             # noqa: E402


DEFAULT_BIN = r"E:\Projects\OES\build\windows-x64-release\bin\Release"


class StepError(AssertionError):
    pass


class Context:
    """Shared state across a scenario's steps."""

    def __init__(self, bin_dir: str):
        self.bin_dir = bin_dir
        self.procs: dict[str, subprocess.Popen] = {}
        self.agents: dict[str, TestAgentClient] = {}
        self.current: TestAgentClient | None = None
        self._next_port = 1651

    def launch(self, role: str, exe: str, base: str) -> TestAgentClient:
        port = self._next_port
        self._next_port += 1
        path = os.path.join(self.bin_dir, exe)
        args = [path, f'--file={base}', f'--testagent={port}']
        proc = subprocess.Popen(args)
        self.procs[role] = proc
        agent = TestAgentClient(port=port).connect()
        self.agents[role] = agent
        self.current = agent
        return agent

    def close_current(self, wait: float = 8.0):
        """Quit the current app and WAIT for its process to exit (releases a file-base lock)."""
        if self.current is None:
            return
        role = next((r for r, a in self.agents.items() if a is self.current), None)
        try:
            self.current.quit()
        except Exception:
            pass
        self.current.close()
        if role is not None:
            proc = self.procs.get(role)
            if proc is not None:
                try:
                    proc.wait(timeout=wait)
                except Exception:
                    try:
                        proc.terminate()
                    except Exception:
                        pass
                self.procs.pop(role, None)
            self.agents.pop(role, None)
        self.current = None

    def teardown(self):
        for agent in self.agents.values():
            try:
                agent.quit()
            except Exception:
                pass
            agent.close()
        time.sleep(1.0)
        for proc in self.procs.values():
            try:
                if proc.poll() is None:
                    proc.terminate()
            except Exception:
                pass
        self.agents.clear()
        self.procs.clear()
        self.current = None


def _coerce(value: str):
    if re.fullmatch(r"-?\d+", value):
        return int(value)
    if re.fullmatch(r"-?\d+\.\d+", value):
        return float(value)
    if value.lower() in ("истина", "true"):
        return True
    if value.lower() in ("ложь", "false"):
        return False
    return value


# ---- step library (Russian phrase -> handler) --------------------------------------------------
# Each entry: (compiled regex, handler(ctx, *groups)). First match wins.
STEPS: list[tuple[re.Pattern, callable]] = []


def step(pattern: str):
    def deco(fn):
        STEPS.append((re.compile(pattern, re.IGNORECASE), fn))
        return fn
    return deco


@step(r'^Я запускаю предприятие на базе "(.+)"$')
def _launch_enterprise(ctx: Context, base):
    a = ctx.launch("enterprise", "enterprise.exe", base)
    if not a.ping():
        raise StepError("enterprise agent did not respond")


@step(r'^Я запускаю дизайнер на базе "(.+)"$')
def _launch_designer(ctx: Context, base):
    a = ctx.launch("designer", "designer.exe", base)
    if not a.ping():
        raise StepError("designer agent did not respond")


@step(r'^Я переключаюсь на (предприятие|дизайнер)$')
def _switch(ctx: Context, role):
    key = "enterprise" if role.lower() == "предприятие" else "designer"
    if key not in ctx.agents:
        raise StepError(f"{role} не запущен")
    ctx.current = ctx.agents[key]


@step(r'^Я открываю форму объекта справочника "(.+)"$')
def _open_object(ctx: Context, name):
    ctx.current.call("openForm", name=name, kind="object")


@step(r'^Я открываю форму списка справочника "(.+)"$')
def _open_list(ctx: Context, name):
    ctx.current.call("openForm", name=name, kind="list")


@step(r'^Я устанавливаю значение поля "(.+)" равным "(.*)"$')
def _set_control(ctx: Context, name, value):
    ctx.current.call("setControlValue", name=name, value=_coerce(value))


@step(r'^Я устанавливаю реквизит формы "(.+)" равным "(.*)"$')
def _set_attr(ctx: Context, name, value):
    ctx.current.call("setAttribute", name=name, value=_coerce(value))


@step(r'^Я нажимаю команду "(.+)"$')
def _press(ctx: Context, name):
    ctx.current.call("pressCommand", name=name)


@step(r'^Я очищаю сообщения$')
def _clear_msgs(ctx: Context):
    ctx.current.call("clearMessages")


@step(r'^Я вижу сообщение "(.+)"$')
@step(r'^Сообщение "(.+)" присутствует$')
def _assert_message(ctx: Context, fragment):
    msgs = ctx.current.call("getMessages").get("messages", [])
    texts = [m.get("text", "") for m in msgs]
    if not any(fragment in t for t in texts):
        raise StepError(f'сообщение "{fragment}" не найдено. Получено: {texts}')


@step(r'^Значение поля "(.+)" равно "(.*)"$')
def _assert_control(ctx: Context, name, expected):
    got = ctx.current.call("getControlValue", name=name).get("value")
    if str(got) != expected:
        raise StepError(f'поле "{name}": ожидалось "{expected}", получено "{got}"')


@step(r'^Поле "(.+)" существует$')
def _assert_control_exists(ctx: Context, name):
    if not ctx.current.call("findControl", name=name).get("found"):
        raise StepError(f'контрол "{name}" не найден')


@step(r'^Я вижу форму "(.+)"$')
def _assert_form(ctx: Context, caption):
    forms = ctx.current.call("getForms").get("forms", [])
    caps = [f.get("caption", "") for f in forms]
    if not any(caption in c for c in caps):
        raise StepError(f'форма "{caption}" не открыта. Открыто: {caps}')


@step(r'^Я закрываю приложение$')
def _close(ctx: Context):
    ctx.close_current()


def run_step(ctx: Context, st: Step) -> None:
    for pattern, fn in STEPS:
        m = pattern.match(st.text)
        if m:
            fn(ctx, *m.groups())
            return
    raise StepError(f"нет обработчика для шага: {st.keyword} {st.text}")


def run(feature_path: str, bin_dir: str, junit: str | None) -> int:
    feature = parse_feature(open(feature_path, encoding="utf-8").read())
    print(f"Функционал: {feature.name}")

    suite = ET.Element("testsuite", name=feature.name or "feature")
    failures = 0

    for sc in feature.scenarios:
        print(f"  Сценарий: {sc.name}")
        case = ET.SubElement(suite, "testcase", name=sc.name)
        ctx = Context(bin_dir)
        t0 = time.time()
        try:
            for st in sc.steps:
                print(f"    {st.keyword} {st.text}", end="", flush=True)
                run_step(ctx, st)
                print("  ... OK")
        except Exception as exc:  # noqa: BLE001
            failures += 1
            print(f"  ... FAIL\n      {exc}")
            fail = ET.SubElement(case, "failure", message=str(exc)[:200])
            fail.text = str(exc)
        finally:
            case.set("time", f"{time.time() - t0:.2f}")
            ctx.teardown()

    total = len(feature.scenarios)
    print(f"\nИтого: {total - failures}/{total} сценариев пройдено")

    if junit:
        suite.set("tests", str(total))
        suite.set("failures", str(failures))
        ET.ElementTree(suite).write(junit, encoding="utf-8", xml_declaration=True)
        print(f"JUnit отчёт: {junit}")

    return 1 if failures else 0


def main() -> int:
    ap = argparse.ArgumentParser(description="OES BDD test runner")
    ap.add_argument("feature")
    ap.add_argument("--bin", default=DEFAULT_BIN, help="directory with designer.exe/enterprise.exe")
    ap.add_argument("--junit", default=None, help="write JUnit XML report to this path")
    args = ap.parse_args()
    return run(args.feature, args.bin, args.junit)


if __name__ == "__main__":
    raise SystemExit(main())
