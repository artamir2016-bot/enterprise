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
import shutil
import subprocess
import sys
import time
import xml.etree.ElementTree as ET


class VideoRecorder:
    """Screen-record a scenario to MP4 via ffmpeg (gdigrab on Windows). No-op if ffmpeg is absent."""

    def __init__(self, out_dir: str | None):
        self.out_dir = out_dir
        self.ffmpeg = shutil.which("ffmpeg") if out_dir else None
        self.proc: subprocess.Popen | None = None

    def start(self, name: str) -> None:
        if not self.ffmpeg:
            return
        os.makedirs(self.out_dir, exist_ok=True)
        safe = re.sub(r"[^\w.-]+", "_", name).strip("_") or "scenario"
        out = os.path.join(self.out_dir, safe + ".mp4")
        cmd = [self.ffmpeg, "-y", "-f", "gdigrab", "-framerate", "15", "-i", "desktop",
               "-pix_fmt", "yuv420p", out]
        self.proc = subprocess.Popen(cmd, stdin=subprocess.PIPE,
                                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        time.sleep(1.0)  # let capture spin up before the scenario acts

    def stop(self) -> None:
        if self.proc is None:
            return
        try:
            self.proc.communicate(b"q", timeout=10)   # graceful finalize (writes moov atom)
        except Exception:
            try:
                self.proc.terminate()
            except Exception:
                pass
        self.proc = None

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
        # Wait for each process to ACTUALLY exit before the next scenario reopens the same base —
        # a Firebird file base is exclusive, so a lingering process makes the next open race/fail.
        for proc in self.procs.values():
            try:
                proc.wait(timeout=10)
            except Exception:
                try:
                    proc.terminate()
                    proc.wait(timeout=5)
                except Exception:
                    pass
        time.sleep(1.5)   # settle the file-base lock release
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


@step(r'^Я жду (\d+(?:[.,]\d+)?) секунд[ы]?$')
def _wait(ctx: Context, secs):
    time.sleep(float(secs.replace(",", ".")))


@step(r'^Я очищаю сообщения$')
def _clear_messages(ctx: Context):
    ctx.current.call("clearMessages")


@step(r'^Я вижу сообщение "(.+)"$')
@step(r'^Сообщение "(.+)" присутствует$')
def _assert_message(ctx: Context, fragment):
    msgs = ctx.current.call("getMessages").get("messages", [])
    texts = [m.get("text", "") for m in msgs]
    if not any(fragment in t for t in texts):
        raise StepError(f'сообщение "{fragment}" не найдено. Получено: {texts}')


# ---- REAL input steps (video-able: real cursor / keystrokes) -----------------------------------
@step(r'^Я навожу мышь на поле "(.+)"$')
def _move_to(ctx: Context, name):
    ctx.current.call("moveMouse", name=name)


@step(r'^Я кликаю по полю "(.+)"$')
def _click_ctrl(ctx: Context, name):
    ctx.current.call("clickControl", name=name)


@step(r'^Я дважды кликаю по полю "(.+)"$')
def _dclick_ctrl(ctx: Context, name):
    ctx.current.call("clickControl", name=name, double=True)


@step(r'^Я ввожу в поле "(.+)" текст "(.*)"$')
def _type_into(ctx: Context, name, value):
    ctx.current.call("setControlValueReal", name=name, value=value)


@step(r'^Я печатаю "(.*)"$')
def _type_text(ctx: Context, text):
    ctx.current.call("typeText", text=text)


@step(r'^Я нажимаю клавишу "(.+)"$')
def _press_key(ctx: Context, combo):
    parts = [p.strip() for p in combo.split("+")]
    key = parts[-1]
    mods = {p.lower(): True for p in parts[:-1]}
    key = {"ctrl": "ctrl", "shift": "shift", "alt": "alt"}.get(key.lower(), key)
    ctx.current.call("pressKey", key=key,
                     ctrl=mods.get("ctrl", False), shift=mods.get("shift", False),
                     alt=mods.get("alt", False))


@step(r'^Я делаю скриншот "(.+)"$')
def _screenshot(ctx: Context, path):
    ctx.current.call("screenshot", path=path)


# ---- generic UI (designer): menus / widgets / windows ------------------------------------------
@step(r'^Я выбираю меню "(.+)"$')
def _invoke_menu(ctx: Context, path):
    # "Конфигурация -> Обновите конфигурацию базы данных" or "Конфигурация | ..."
    parts = [p.strip() for p in re.split(r'->|\||/|→', path)]
    ctx.current.call("invokeMenu", path=parts)


@step(r'^Я кликаю по виджету "(.+)"$')
def _click_widget(ctx: Context, label):
    ctx.current.call("clickWidget", by="label", value=label)


@step(r'^Я вижу окно "(.+)"$')
def _assert_window(ctx: Context, title):
    wins = ctx.current.call("listWindows").get("windows", [])
    titles = [w.get("title", "") for w in wins]
    if not any(title in t for t in titles):
        raise StepError(f'окно "{title}" не найдено. Открыто: {titles}')


@step(r'^Значение поля "(.+)" равно "(.*)"$')
def _assert_control(ctx: Context, name, expected):
    got = ctx.current.call("getControlValue", name=name).get("value")
    if str(got) != expected:
        raise StepError(f'поле "{name}": ожидалось "{expected}", получено "{got}"')


@step(r'^Поле "(.+)" существует$')
def _assert_control_exists(ctx: Context, name):
    if not ctx.current.call("findControl", name=name).get("found"):
        raise StepError(f'контрол "{name}" не найден')


@step(r'^Я вижу ошибку "(.+)"$')
def _assert_diag(ctx: Context, fragment):
    diags = ctx.current.call("getDiagnostics").get("diagnostics", [])
    texts = [d.get("message", "") for d in diags]
    if not any(fragment in t for t in texts):
        raise StepError(f'ошибка "{fragment}" не найдена. Получено: {texts}')


@step(r'^Я вижу форму "(.+)"$')
def _assert_form(ctx: Context, caption):
    forms = ctx.current.call("getForms").get("forms", [])
    caps = [f.get("caption", "") for f in forms]
    if not any(caption in c for c in caps):
        raise StepError(f'форма "{caption}" не открыта. Открыто: {caps}')


@step(r'^Я закрываю приложение$')
def _close(ctx: Context):
    ctx.close_current()


# ---- narration groups (video sync) -------------------------------------------------------------
# A step line starting with '*' is a NARRATION marker: it names a group and carries the voice-over
# text. The steps that follow (until the next '*') belong to that group. When recording video, the
# group's steps are paced so the on-screen segment lasts as long as the narration would take to
# speak — and an .srt subtitle track is written next to the .mp4 with each narration timed to its
# segment. Duration = explicit "(5s)"/"[5]" prefix if present, else word-count / words-per-minute.

_DUR_RE = re.compile(r'^\s*[\(\[\{]\s*(\d+(?:[.,]\d+)?)\s*(?:s|с|сек)?\s*[\)\]\}]\s*')


def parse_narration(text: str, wpm: float) -> tuple[float, str]:
    """Return (seconds, clean_text) for a '*' narration line."""
    m = _DUR_RE.match(text)
    if m:
        return float(m.group(1).replace(",", ".")), text[m.end():].strip()
    words = len(text.split())
    secs = words / (wpm / 60.0) if words else 0.0
    return secs, text.strip()


def group_steps(steps: list[Step], wpm: float) -> list[dict]:
    """Split a scenario's steps into narration groups. First group may have no narration (secs=0)."""
    groups: list[dict] = []
    cur = {"narr": "", "secs": 0.0, "steps": []}
    for st in steps:
        if st.keyword == "*":
            if cur["steps"] or cur["narr"]:
                groups.append(cur)
            secs, narr = parse_narration(st.text, wpm)
            cur = {"narr": narr, "secs": secs, "steps": []}
        else:
            cur["steps"].append(st)
    groups.append(cur)
    return groups


def _fmt_srt_time(sec: float) -> str:
    if sec < 0:
        sec = 0.0
    h = int(sec // 3600)
    m = int((sec % 3600) // 60)
    s = int(sec % 60)
    ms = int(round((sec - int(sec)) * 1000))
    if ms == 1000:
        ms = 0
        s += 1
    return f"{h:02d}:{m:02d}:{s:02d},{ms:03d}"


def write_srt(path: str, entries: list[tuple[float, float, str]]) -> None:
    lines = []
    for i, (start, end, text) in enumerate(entries, 1):
        lines.append(str(i))
        lines.append(f"{_fmt_srt_time(start)} --> {_fmt_srt_time(end)}")
        lines.append(text)
        lines.append("")
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))


def run_step(ctx: Context, st: Step) -> None:
    for pattern, fn in STEPS:
        m = pattern.match(st.text)
        if m:
            fn(ctx, *m.groups())
            return
    raise StepError(f"нет обработчика для шага: {st.keyword} {st.text}")


def run(feature_path: str, bin_dir: str, junit: str | None, video_dir: str | None = None,
        wpm: float = 150.0) -> int:
    feature = parse_feature(open(feature_path, encoding="utf-8").read())
    print(f"Функционал: {feature.name}")

    suite = ET.Element("testsuite", name=feature.name or "feature")
    failures = 0

    for sc in feature.scenarios:
        print(f"  Сценарий: {sc.name}")
        case = ET.SubElement(suite, "testcase", name=sc.name)
        ctx = Context(bin_dir)
        recorder = VideoRecorder(video_dir)
        recorder.start(sc.name)
        rec0 = time.time()          # reference for subtitle timings (after capture spun up)
        t0 = time.time()
        groups = group_steps(sc.steps, wpm)
        srt: list[tuple[float, float, str]] = []
        try:
            for g in groups:
                if g["narr"]:
                    print(f"    * {g['narr']}  [~{g['secs']:.1f}s]")
                g_start = time.time()
                n = len(g["steps"])
                for idx, st in enumerate(g["steps"], 1):
                    print(f"    {st.keyword} {st.text}", end="", flush=True)
                    run_step(ctx, st)
                    print("  ... OK")
                    # pace to the narration timeline (video only): hold each step to its slot
                    if video_dir and g["secs"] > 0 and n:
                        target = g["secs"] * idx / n
                        drift = target - (time.time() - g_start)
                        if drift > 0:
                            time.sleep(drift)
                # a narration group with no steps (or steps finished early) still holds the segment
                if video_dir and g["secs"] > 0:
                    drift = g["secs"] - (time.time() - g_start)
                    if drift > 0:
                        time.sleep(drift)
                if g["narr"] and video_dir:
                    srt.append((g_start - rec0, time.time() - rec0, g["narr"]))
        except Exception as exc:  # noqa: BLE001
            failures += 1
            print(f"  ... FAIL\n      {exc}")
            fail = ET.SubElement(case, "failure", message=str(exc)[:200])
            fail.text = str(exc)
        finally:
            case.set("time", f"{time.time() - t0:.2f}")
            recorder.stop()
            if video_dir and srt:
                safe = re.sub(r"[^\w.-]+", "_", sc.name).strip("_") or "scenario"
                srt_path = os.path.join(video_dir, safe + ".srt")
                write_srt(srt_path, srt)
                print(f"    субтитры: {srt_path}")
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
    ap.add_argument("--video", default=None, help="record each scenario to MP4 in this dir (needs ffmpeg)")
    ap.add_argument("--narration-wpm", type=float, default=150.0,
                    help="speaking rate for '*' narration groups (words/min; sets video pacing)")
    args = ap.parse_args()
    return run(args.feature, args.bin, args.junit, args.video, args.narration_wpm)


if __name__ == "__main__":
    raise SystemExit(main())
