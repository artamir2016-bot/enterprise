"""OES-TEST: desktop IDE for the OES BDD test system (Vanessa-like feature workbench).

A dependency-free Tkinter app to author and run Russian .feature files:
  * feature list (open / new / save)
  * Gherkin editor with keyword highlighting
  * step palette (double-click inserts a step template)
  * Run / Run with video — streams the runner's output live, colours OK/FAIL, shows a summary

Launch:  python tools/oes_testrunner/ide.py
Requires only the standard library (Tkinter). Drives runner.py as a subprocess.
"""

from __future__ import annotations

import os
import queue
import re
import subprocess
import sys
import threading
import tkinter as tk
from tkinter import ttk, filedialog, messagebox, font as tkfont

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from steps_catalog import STEP_CATALOG  # noqa: E402
from agent_client import TestAgentClient  # noqa: E402

DEFAULT_FEATURES = os.path.join(HERE, "features")
DEFAULT_BIN = r"E:\Projects\OES\build\windows-x64-release\bin\Release"
RUNNER = os.path.join(HERE, "runner.py")

GHERKIN_KW = [
    "Функционал", "Функциональность", "Feature",
    "Структура сценария", "Сценарий", "Scenario",
    "Дано", "Когда", "Тогда", "Также", "Затем", "И", "Но",
    "Given", "When", "Then", "And", "But",
    "Примеры", "Examples",
]
# longest-first so multi-word keywords match before their prefixes
GHERKIN_KW.sort(key=len, reverse=True)
_KW_RE = re.compile(r"^\s*(" + "|".join(re.escape(k) for k in GHERKIN_KW) + r")\b")


class TestIDE(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("OES — Тестирование (редактор фич)")
        self.geometry("1200x760")
        self.features_dir = DEFAULT_FEATURES
        self.current_path: str | None = None
        self.proc: subprocess.Popen | None = None
        self.out_q: "queue.Queue[str|None]" = queue.Queue()

        self._build_ui()
        self._refresh_feature_list()
        self.after(80, self._drain_output)

    # -- layout ----------------------------------------------------------------------------------
    def _build_ui(self):
        mono = tkfont.Font(family="Consolas", size=11)

        toolbar = ttk.Frame(self)
        toolbar.pack(side=tk.TOP, fill=tk.X, padx=6, pady=4)
        ttk.Button(toolbar, text="Новая", command=self.new_feature).pack(side=tk.LEFT)
        ttk.Button(toolbar, text="Открыть…", command=self.open_feature).pack(side=tk.LEFT, padx=2)
        ttk.Button(toolbar, text="Сохранить", command=self.save_feature).pack(side=tk.LEFT, padx=2)
        ttk.Separator(toolbar, orient=tk.VERTICAL).pack(side=tk.LEFT, fill=tk.Y, padx=8)
        self.run_btn = ttk.Button(toolbar, text="▶ Запустить", command=lambda: self.run(video=False))
        self.run_btn.pack(side=tk.LEFT)
        self.video_btn = ttk.Button(toolbar, text="🎥 Запустить с видео", command=lambda: self.run(video=True))
        self.video_btn.pack(side=tk.LEFT, padx=2)
        self.stop_btn = ttk.Button(toolbar, text="■ Стоп", command=self.stop, state=tk.DISABLED)
        self.stop_btn.pack(side=tk.LEFT, padx=2)
        ttk.Label(toolbar, text="bin:").pack(side=tk.LEFT, padx=(12, 2))
        self.bin_var = tk.StringVar(value=DEFAULT_BIN)
        ttk.Entry(toolbar, textvariable=self.bin_var, width=48).pack(side=tk.LEFT)

        main = ttk.Panedwindow(self, orient=tk.HORIZONTAL)
        main.pack(fill=tk.BOTH, expand=True, padx=6, pady=4)

        # left: feature list
        left = ttk.Frame(main)
        ttk.Label(left, text="Фичи").pack(anchor=tk.W)
        self.feat_list = tk.Listbox(left, width=26)
        self.feat_list.pack(fill=tk.BOTH, expand=True)
        self.feat_list.bind("<<ListboxSelect>>", self._on_pick_feature)
        main.add(left, weight=1)

        # center: editor + output
        center = ttk.Panedwindow(main, orient=tk.VERTICAL)
        edit_frame = ttk.Frame(center)
        self.editor = tk.Text(edit_frame, wrap=tk.NONE, font=mono, undo=True)
        yscroll = ttk.Scrollbar(edit_frame, command=self.editor.yview)
        self.editor.configure(yscrollcommand=yscroll.set)
        yscroll.pack(side=tk.RIGHT, fill=tk.Y)
        self.editor.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        self.editor.bind("<KeyRelease>", lambda e: self._highlight())
        self.editor.tag_configure("kw", foreground="#0057b7", font=(mono.actual("family"), 11, "bold"))
        self.editor.tag_configure("str", foreground="#b76e00")
        self.editor.tag_configure("comment", foreground="#7a7a7a")
        center.add(edit_frame, weight=3)

        out_frame = ttk.Frame(center)
        ttk.Label(out_frame, text="Результат прогона").pack(anchor=tk.W)
        self.output = tk.Text(out_frame, height=12, font=mono, background="#111", foreground="#ddd")
        oscroll = ttk.Scrollbar(out_frame, command=self.output.yview)
        self.output.configure(yscrollcommand=oscroll.set, state=tk.DISABLED)
        oscroll.pack(side=tk.RIGHT, fill=tk.Y)
        self.output.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        self.output.tag_configure("ok", foreground="#5fd75f")
        self.output.tag_configure("fail", foreground="#ff6b6b")
        self.output.tag_configure("head", foreground="#8ab4ff")
        self.output.tag_configure("sum", foreground="#ffd75f")
        center.add(out_frame, weight=2)
        main.add(center, weight=5)

        # right: notebook with step palette + live inspector
        right = ttk.Notebook(main)

        pal_tab = ttk.Frame(right)
        ttk.Label(pal_tab, text="Двойной клик — вставить шаг").pack(anchor=tk.W)
        self.palette = ttk.Treeview(pal_tab, show="tree", height=30)
        self.palette.pack(fill=tk.BOTH, expand=True)
        self.palette.bind("<Double-1>", self._insert_step)
        self._fill_palette()
        right.add(pal_tab, text="Шаги")

        insp_tab = ttk.Frame(right)
        self._build_inspector(insp_tab)
        right.add(insp_tab, text="Инспектор")

        main.add(right, weight=2)

        self.status = tk.StringVar(value="Готово")
        ttk.Label(self, textvariable=self.status, anchor=tk.W, relief=tk.SUNKEN).pack(side=tk.BOTTOM, fill=tk.X)

    def _fill_palette(self):
        cats: dict[str, str] = {}
        for cat, template, desc in STEP_CATALOG:
            if cat not in cats:
                cats[cat] = self.palette.insert("", tk.END, text=cat, open=True)
            node = self.palette.insert(cats[cat], tk.END, text=template)
            self.palette.item(node, tags=("step",))
            self._step_by_node = getattr(self, "_step_by_node", {})
            self._step_by_node[node] = (template, desc)

    # -- live inspector --------------------------------------------------------------------------
    def _build_inspector(self, parent):
        bar = ttk.Frame(parent)
        bar.pack(fill=tk.X, pady=2)
        ttk.Label(bar, text="порт:").pack(side=tk.LEFT)
        self.insp_port = tk.StringVar(value="1651")
        ttk.Entry(bar, textvariable=self.insp_port, width=6).pack(side=tk.LEFT, padx=2)
        ttk.Button(bar, text="Обновить", command=self._inspect_refresh).pack(side=tk.LEFT, padx=2)
        ttk.Label(parent, text="Двойной клик — вставить шаг по элементу").pack(anchor=tk.W)
        self.inspector = ttk.Treeview(parent, show="tree", height=28)
        self.inspector.pack(fill=tk.BOTH, expand=True)
        self.inspector.bind("<Double-1>", self._insert_from_inspector)
        self._insp_meta: dict[str, tuple[str, str]] = {}   # node -> (kind, payload)

    def _inspect_refresh(self):
        port = int(self.insp_port.get() or "1651")
        for n in self.inspector.get_children(""):
            self.inspector.delete(n)
        self._insp_meta.clear()
        try:
            c = TestAgentClient(port=port, timeout=8).connect(retries=1)
        except Exception as exc:
            self.status.set(f"Инспектор: нет подключения к :{port} ({exc})")
            return
        try:
            info = c.app_info()
            self.status.set(f"Инспектор: {info.get('app','?')} :{port}")

            wins = self.inspector.insert("", tk.END, text="Окна", open=True)
            for w in c.call("listWindows").get("windows", []):
                node = self.inspector.insert(wins, tk.END, text=w.get("title") or w.get("class"))
                self._insp_meta[node] = ("window", w.get("title", ""))

            menus = self.inspector.insert("", tk.END, text="Меню", open=False)
            for m in c.call("listMenus").get("menus", []):
                mnode = self.inspector.insert(menus, tk.END, text=m.get("menu", ""), open=False)
                for item in m.get("items", []):
                    inode = self.inspector.insert(mnode, tk.END, text=item)
                    self._insp_meta[inode] = ("menu", f"{m.get('menu','')} -> {item}")

            ctrls = self.inspector.insert("", tk.END, text="Контролы активной формы", open=True)
            try:
                for ctl in c.call("listControls").get("controls", []):
                    label = f"{ctl.get('name','')}  ({ctl.get('class','')})"
                    node = self.inspector.insert(ctrls, tk.END, text=label)
                    self._insp_meta[node] = ("control", ctl.get("name", ""))
            except Exception:
                self.inspector.insert(ctrls, tk.END, text="(нет активной формы)")
        finally:
            c.close()

    def _insert_from_inspector(self, _evt):
        node = self.inspector.focus()
        meta = self._insp_meta.get(node)
        if not meta:
            return
        kind, payload = meta
        if kind == "window":
            line = f'Тогда Я вижу окно "{payload}"'
        elif kind == "menu":
            line = f'Когда Я выбираю меню "{payload}"'
        elif kind == "control":
            line = f'Когда Я устанавливаю значение поля "{payload}" равным ""'
        else:
            return
        self.editor.insert(tk.INSERT, "\n    " + line)
        self._highlight()
        self.editor.focus_set()

    # -- feature files ---------------------------------------------------------------------------
    def _refresh_feature_list(self):
        self.feat_list.delete(0, tk.END)
        if os.path.isdir(self.features_dir):
            for name in sorted(os.listdir(self.features_dir)):
                if name.endswith(".feature"):
                    self.feat_list.insert(tk.END, name)

    def _on_pick_feature(self, _evt):
        sel = self.feat_list.curselection()
        if not sel:
            return
        path = os.path.join(self.features_dir, self.feat_list.get(sel[0]))
        self._load(path)

    def _load(self, path: str):
        with open(path, encoding="utf-8") as f:
            text = f.read()
        self.editor.delete("1.0", tk.END)
        self.editor.insert("1.0", text)
        self.current_path = path
        self.title(f"OES — Тестирование — {os.path.basename(path)}")
        self._highlight()

    def new_feature(self):
        self.editor.delete("1.0", tk.END)
        self.editor.insert("1.0",
            "# language: ru\n\nФункционал: Новый функционал\n\n  Сценарий: Новый сценарий\n    Дано \n")
        self.current_path = None
        self.title("OES — Тестирование — (без имени)")
        self._highlight()

    def open_feature(self):
        path = filedialog.askopenfilename(initialdir=self.features_dir,
                                          filetypes=[("Feature", "*.feature"), ("Все", "*.*")])
        if path:
            self.features_dir = os.path.dirname(path)
            self._refresh_feature_list()
            self._load(path)

    def save_feature(self) -> str | None:
        if self.current_path is None:
            path = filedialog.asksaveasfilename(initialdir=self.features_dir, defaultextension=".feature",
                                                filetypes=[("Feature", "*.feature")])
            if not path:
                return None
            self.current_path = path
        with open(self.current_path, "w", encoding="utf-8") as f:
            f.write(self.editor.get("1.0", "end-1c"))
        self.features_dir = os.path.dirname(self.current_path)
        self._refresh_feature_list()
        self.title(f"OES — Тестирование — {os.path.basename(self.current_path)}")
        self.status.set(f"Сохранено: {self.current_path}")
        return self.current_path

    # -- editor helpers --------------------------------------------------------------------------
    def _highlight(self):
        for tag in ("kw", "str", "comment"):
            self.editor.tag_remove(tag, "1.0", tk.END)
        line_count = int(self.editor.index("end-1c").split(".")[0])
        for ln in range(1, line_count + 1):
            start = f"{ln}.0"
            text = self.editor.get(start, f"{ln}.end")
            if text.lstrip().startswith("#"):
                self.editor.tag_add("comment", start, f"{ln}.end")
                continue
            m = _KW_RE.match(text)
            if m:
                a = text.index(m.group(1))
                self.editor.tag_add("kw", f"{ln}.{a}", f"{ln}.{a + len(m.group(1))}")
            for sm in re.finditer(r'"[^"]*"', text):
                self.editor.tag_add("str", f"{ln}.{sm.start()}", f"{ln}.{sm.end()}")

    def _insert_step(self, _evt):
        node = self.palette.focus()
        info = getattr(self, "_step_by_node", {}).get(node)
        if not info:
            return
        template = info[0]
        # insert on a fresh, 4-space-indented line
        self.editor.insert(tk.INSERT, "\n    " + template)
        self._highlight()
        self.editor.focus_set()

    # -- run -------------------------------------------------------------------------------------
    def run(self, video: bool):
        if self.proc is not None:
            messagebox.showinfo("Занято", "Прогон уже идёт.")
            return
        path = self.save_feature()
        if not path:
            return
        self._clear_output()
        junit = os.path.splitext(path)[0] + ".junit.xml"
        cmd = [sys.executable, RUNNER, path, "--junit", junit, "--bin", self.bin_var.get()]
        if video:
            vdir = os.path.join(os.path.dirname(path), "video")
            cmd += ["--video", vdir]
            self._append(f"→ видео в {vdir}\n", "head")
        self._append("$ " + " ".join(cmd) + "\n", "head")
        env = dict(os.environ, PYTHONIOENCODING="utf-8", PYTHONUNBUFFERED="1")
        self.proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                     env=env, text=True, encoding="utf-8", bufsize=1)
        self.run_btn.config(state=tk.DISABLED)
        self.video_btn.config(state=tk.DISABLED)
        self.stop_btn.config(state=tk.NORMAL)
        self.status.set("Прогон…")
        threading.Thread(target=self._reader, args=(self.proc,), daemon=True).start()

    def _reader(self, proc: subprocess.Popen):
        for line in proc.stdout:
            self.out_q.put(line)
        proc.wait()
        self.out_q.put(None)   # sentinel: finished

    def _drain_output(self):
        try:
            while True:
                line = self.out_q.get_nowait()
                if line is None:
                    self._on_run_done()
                else:
                    self._append_run_line(line)
        except queue.Empty:
            pass
        self.after(80, self._drain_output)

    def _append_run_line(self, line: str):
        tag = None
        if "... OK" in line:
            tag = "ok"
        elif "FAIL" in line or "error" in line.lower() or "Traceback" in line:
            tag = "fail"
        elif line.startswith("Функционал") or line.strip().startswith("Сценарий"):
            tag = "head"
        elif line.startswith("Итого") or "JUnit" in line:
            tag = "sum"
        self._append(line, tag)

    def _on_run_done(self):
        rc = self.proc.returncode if self.proc else -1
        self.proc = None
        self.run_btn.config(state=tk.NORMAL)
        self.video_btn.config(state=tk.NORMAL)
        self.stop_btn.config(state=tk.DISABLED)
        self.status.set("Прогон завершён — успех" if rc == 0 else f"Прогон завершён — есть падения (код {rc})")

    def stop(self):
        if self.proc is not None:
            try:
                self.proc.terminate()
            except Exception:
                pass

    def _append(self, text: str, tag: str | None = None):
        self.output.config(state=tk.NORMAL)
        self.output.insert(tk.END, text, (tag,) if tag else ())
        self.output.see(tk.END)
        self.output.config(state=tk.DISABLED)

    def _clear_output(self):
        self.output.config(state=tk.NORMAL)
        self.output.delete("1.0", tk.END)
        self.output.config(state=tk.DISABLED)


if __name__ == "__main__":
    TestIDE().mainloop()
