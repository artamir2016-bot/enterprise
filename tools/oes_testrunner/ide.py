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
    "Контекст", "Предыстория", "Background",
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
        self.live_proc: subprocess.Popen | None = None   # app launched for live inspection
        self.out_q: "queue.Queue[str|None]" = queue.Queue()

        self._build_ui()
        self._refresh_feature_list()
        self.after(80, self._drain_output)
        self.after(300, self._inspect_refresh)   # show the launch node from the start
        self.bind_all("<Control-w>", lambda e: (self.close_tab(), "break")[1])
        self.protocol("WM_DELETE_WINDOW", self._on_close)

    # -- layout ----------------------------------------------------------------------------------
    def _build_ui(self):
        mono = tkfont.Font(family="Consolas", size=11)
        self.mono = mono
        self.tabs: list[dict] = []

        toolbar = ttk.Frame(self)
        toolbar.pack(side=tk.TOP, fill=tk.X, padx=6, pady=4)
        ttk.Button(toolbar, text="Новая", command=self.new_feature).pack(side=tk.LEFT)
        ttk.Button(toolbar, text="Открыть…", command=self.open_feature).pack(side=tk.LEFT, padx=2)
        ttk.Button(toolbar, text="Сохранить", command=self.save_feature).pack(side=tk.LEFT, padx=2)
        ttk.Button(toolbar, text="Закрыть", command=self.close_tab).pack(side=tk.LEFT, padx=2)
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

        # center: editor tabs + output
        center = ttk.Panedwindow(main, orient=tk.VERTICAL)
        edit_frame = ttk.Frame(center)
        self.editors_nb = ttk.Notebook(edit_frame)
        self.editors_nb.pack(fill=tk.BOTH, expand=True)
        self.editors_nb.bind("<<NotebookTabChanged>>", self._on_tab_changed)
        self.editors_nb.bind("<Button-2>", self._on_tab_middle_click)  # middle-click closes a tab
        self.editor: tk.Text | None = None
        self._new_editor_tab()  # start with one empty tab; sets self.editor
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

    def _enable_clipboard(self, widget: tk.Text):
        """Layout-independent copy/paste/cut/select-all + right-click menu.

        Tk's default <Control-c>/<Control-v> bindings key off the Latin keysym, so
        under a Cyrillic layout Ctrl+С/Ctrl+М never fire. We bind on the physical
        keycode instead (C=67, V=86, X=88, A=65) so it works in any layout.
        """
        def do_copy(_e=None):
            try:
                widget.event_generate("<<Copy>>")
            except tk.TclError:
                pass
            return "break"

        def do_paste(_e=None):
            try:
                if widget.tag_ranges(tk.SEL):
                    widget.delete(tk.SEL_FIRST, tk.SEL_LAST)
            except tk.TclError:
                pass
            widget.event_generate("<<Paste>>")
            self._highlight()
            return "break"

        def do_cut(_e=None):
            widget.event_generate("<<Cut>>")
            self._highlight()
            return "break"

        def do_select_all(_e=None):
            widget.tag_add(tk.SEL, "1.0", tk.END)
            widget.mark_set(tk.INSERT, "1.0")
            return "break"

        def on_ctrl_key(e):
            kc = e.keycode
            if kc == 67:      # C
                return do_copy()
            if kc == 86:      # V
                return do_paste()
            if kc == 88:      # X
                return do_cut()
            if kc == 65:      # A
                return do_select_all()
            return None

        widget.bind("<Control-KeyPress>", on_ctrl_key)
        # keep the Latin bindings too (harmless, helps on some Tk builds)
        widget.bind("<Control-c>", do_copy)
        widget.bind("<Control-v>", do_paste)
        widget.bind("<Control-x>", do_cut)
        widget.bind("<Control-a>", do_select_all)

        menu = tk.Menu(widget, tearoff=0)
        menu.add_command(label="Копировать", command=do_copy)
        menu.add_command(label="Вставить", command=do_paste)
        menu.add_command(label="Вырезать", command=do_cut)
        menu.add_separator()
        menu.add_command(label="Выделить всё", command=do_select_all)

        def popup(e):
            try:
                menu.tk_popup(e.x_root, e.y_root)
            finally:
                menu.grab_release()
        widget.bind("<Button-3>", popup)

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
        # launch bar — start an app for live inspection straight from the IDE
        launch = ttk.Frame(parent)
        launch.pack(fill=tk.X, pady=2)
        self.live_app = tk.StringVar(value="Предприятие")
        ttk.Combobox(launch, textvariable=self.live_app, width=12, state="readonly",
                     values=["Предприятие", "Дизайнер"]).pack(side=tk.LEFT)
        ttk.Button(launch, text="▶ Запустить", command=self._live_launch).pack(side=tk.LEFT, padx=2)
        ttk.Button(launch, text="■ Остановить", command=self._live_stop).pack(side=tk.LEFT)


        base_bar = ttk.Frame(parent)
        base_bar.pack(fill=tk.X, pady=2)
        ttk.Label(base_bar, text="база:").pack(side=tk.LEFT)
        self.live_base = tk.StringVar(value=r"E:\Projects\OES\testbase\demo_ru_base")
        ttk.Entry(base_bar, textvariable=self.live_base).pack(side=tk.LEFT, fill=tk.X, expand=True, padx=2)

        bar = ttk.Frame(parent)
        bar.pack(fill=tk.X, pady=2)
        ttk.Label(bar, text="порт:").pack(side=tk.LEFT)
        self.insp_port = tk.StringVar(value="1651")
        ttk.Entry(bar, textvariable=self.insp_port, width=6).pack(side=tk.LEFT, padx=2)
        ttk.Button(bar, text="Обновить", command=self._inspect_refresh).pack(side=tk.LEFT, padx=2)
        ttk.Label(bar, text="слово:").pack(side=tk.LEFT, padx=(8, 2))
        self.launch_kw = tk.StringVar(value="Дано")
        ttk.Combobox(bar, textvariable=self.launch_kw, width=6, state="readonly",
                     values=["Дано", "И", "Когда"]).pack(side=tk.LEFT)
        ttk.Label(parent, text="Двойной клик — вставить шаг по элементу").pack(anchor=tk.W)
        self.inspector = ttk.Treeview(parent, show="tree", height=28)
        self.inspector.pack(fill=tk.BOTH, expand=True)
        self.inspector.bind("<Double-1>", self._insert_from_inspector)
        self._insp_meta: dict[str, tuple[str, str]] = {}   # node -> (kind, payload)

    def _live_launch(self):
        if self.live_proc is not None and self.live_proc.poll() is None:
            messagebox.showinfo("Уже запущено", "Приложение для live-view уже запущено. Сначала остановите.")
            return
        exe = "enterprise.exe" if self.live_app.get() == "Предприятие" else "designer.exe"
        path = os.path.join(self.bin_var.get(), exe)
        if not os.path.exists(path):
            messagebox.showerror("Не найдено", f"Нет файла:\n{path}")
            return
        port = self.insp_port.get() or "1651"
        args = [path, f'--file={self.live_base.get()}', f'--testagent={port}']
        try:
            self.live_proc = subprocess.Popen(args)
        except Exception as exc:
            messagebox.showerror("Ошибка запуска", str(exc))
            return
        self.status.set(f"Запущено {exe} (--testagent={port}); обновляю инспектор…")
        self.after(6000, self._inspect_refresh)   # give the app time to come up

    def _live_stop(self):
        # try a clean quit through the agent, then terminate the process
        try:
            c = TestAgentClient(port=int(self.insp_port.get() or "1651"), timeout=3).connect(retries=1)
            c.quit()
            c.close()
        except Exception:
            pass
        if self.live_proc is not None:
            try:
                self.live_proc.wait(timeout=6)
            except Exception:
                try:
                    self.live_proc.terminate()
                except Exception:
                    pass
            self.live_proc = None
        self.status.set("Live-приложение остановлено")

    def _on_close(self):
        # never leave a launched app or a running scenario orphaned
        for p in (self.live_proc, self.proc):
            if p is not None and p.poll() is None:
                try:
                    p.terminate()
                except Exception:
                    pass
        self.destroy()

    def _inspect_refresh(self):
        port = int(self.insp_port.get() or "1651")
        for n in self.inspector.get_children(""):
            self.inspector.delete(n)
        self._insp_meta.clear()

        # launch steps live at the top of the tree — always available, even offline
        launch = self.inspector.insert("", tk.END, text="Запуск приложения", open=True)
        ent = self.inspector.insert(launch, tk.END, text="Я запускаю предприятие (база из полей выше)")
        self._insp_meta[ent] = ("launch", "предприятие")
        des = self.inspector.insert(launch, tk.END, text="Я запускаю дизайнер (база из полей выше)")
        self._insp_meta[des] = ("launch", "дизайнер")

        try:
            c = TestAgentClient(port=port, timeout=8).connect(retries=1)
        except Exception as exc:
            self.status.set(f"Инспектор: нет подключения к :{port} ({exc}) — шаги запуска доступны")
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
                self._insp_meta[mnode] = ("topmenu", m.get("menu", ""))
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
        if kind == "launch":
            kw = self.launch_kw.get() or "Дано"
            line = f'{kw} Я запускаю {payload} на базе "{self.live_base.get()}"'
        elif kind == "window":
            line = f'Тогда Я вижу окно "{payload}"'
        elif kind == "topmenu":
            line = f'Когда Я открываю меню "{payload}"'
        elif kind == "menu":
            line = f'Когда Я выбираю меню "{payload}"'
        elif kind == "control":
            line = f'Когда Я устанавливаю значение поля "{payload}" равным ""'
        else:
            return
        self._insert_at_cursor(line)

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

    # -- editor tabs -----------------------------------------------------------------------------
    def _new_editor_tab(self, path: str | None = None, text: str = "") -> dict:
        frame = ttk.Frame(self.editors_nb)
        ed = tk.Text(frame, wrap=tk.NONE, font=self.mono, undo=True)
        ys = ttk.Scrollbar(frame, command=ed.yview)
        ed.configure(yscrollcommand=ys.set)
        ys.pack(side=tk.RIGHT, fill=tk.Y)
        ed.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        ed.tag_configure("kw", foreground="#0057b7", font=(self.mono.actual("family"), 11, "bold"))
        ed.tag_configure("str", foreground="#b76e00")
        ed.tag_configure("comment", foreground="#7a7a7a")
        ed.bind("<KeyRelease>", self._on_edit_key)
        ed.bind("<Return>", self._on_return)
        self._enable_clipboard(ed)
        if text:
            ed.insert("1.0", text)
            ed.edit_reset()   # so the initial fill is not an undo step
        tab = {"frame": frame, "ed": ed, "path": path, "dirty": False}
        self.tabs.append(tab)
        self.editors_nb.add(frame, text=self._tab_label(tab))
        self.editors_nb.select(frame)
        self.editor = ed
        self.current_path = path
        self._highlight()
        return tab

    def _tab_label(self, tab: dict) -> str:
        name = os.path.basename(tab["path"]) if tab["path"] else "(без имени)"
        return ("• " + name) if tab["dirty"] else name

    def _update_tab_label(self, tab: dict):
        self.editors_nb.tab(tab["frame"], text=self._tab_label(tab))

    def _active_tab(self) -> dict | None:
        cur = self.editors_nb.select()
        for t in self.tabs:
            if str(t["frame"]) == cur:
                return t
        return None

    def _on_tab_changed(self, _evt=None):
        t = self._active_tab()
        if not t:
            return
        self.editor = t["ed"]
        self.current_path = t["path"]
        name = os.path.basename(t["path"]) if t["path"] else "(без имени)"
        self.title(f"OES — Тестирование — {name}")
        self._highlight()

    def _on_edit_key(self, _evt=None):
        self._highlight()
        t = self._active_tab()
        if t and not t["dirty"]:
            t["dirty"] = True
            self._update_tab_label(t)

    def _on_tab_middle_click(self, evt):
        try:
            idx = self.editors_nb.index(f"@{evt.x},{evt.y}")
        except tk.TclError:
            return
        if 0 <= idx < len(self.tabs):
            self.close_tab(self.tabs[idx])

    def close_tab(self, tab: dict | None = None):
        tab = tab or self._active_tab()
        if not tab:
            return
        if tab["dirty"]:
            name = os.path.basename(tab["path"]) if tab["path"] else "(без имени)"
            ans = messagebox.askyesnocancel("Закрыть вкладку", f"Сохранить изменения в «{name}»?")
            if ans is None:
                return
            if ans:
                self.editors_nb.select(tab["frame"])
                if self.save_feature() is None:
                    return
        self.editors_nb.forget(tab["frame"])
        self.tabs.remove(tab)
        if not self.tabs:
            self._new_editor_tab()

    def _load(self, path: str):
        # already open? just switch to it
        for t in self.tabs:
            if t["path"] and os.path.normcase(t["path"]) == os.path.normcase(path):
                self.editors_nb.select(t["frame"])
                return
        with open(path, encoding="utf-8") as f:
            text = f.read()
        # reuse a single pristine untitled tab instead of stacking an empty one
        cur = self._active_tab()
        if cur and cur["path"] is None and not cur["dirty"] and not cur["ed"].get("1.0", "end-1c").strip():
            cur["ed"].delete("1.0", tk.END)
            cur["ed"].insert("1.0", text)
            cur["ed"].edit_reset()
            cur["path"] = path
            self._update_tab_label(cur)
            self._on_tab_changed()
        else:
            self._new_editor_tab(path=path, text=text)

    def new_feature(self):
        self._new_editor_tab(
            path=None,
            text="# language: ru\n\nФункционал: Новый функционал\n\n  Сценарий: Новый сценарий\n    Дано \n")

    def open_feature(self):
        path = filedialog.askopenfilename(initialdir=self.features_dir,
                                          filetypes=[("Feature", "*.feature"), ("Все", "*.*")])
        if path:
            self.features_dir = os.path.dirname(path)
            self._refresh_feature_list()
            self._load(path)

    def save_feature(self) -> str | None:
        tab = self._active_tab()
        path = tab["path"] if tab else self.current_path
        if path is None:
            path = filedialog.asksaveasfilename(initialdir=self.features_dir, defaultextension=".feature",
                                                filetypes=[("Feature", "*.feature")])
            if not path:
                return None
        with open(path, "w", encoding="utf-8") as f:
            f.write(self.editor.get("1.0", "end-1c"))
        if tab:
            tab["path"] = path
            tab["dirty"] = False
            self._update_tab_label(tab)
        self.current_path = path
        self.features_dir = os.path.dirname(path)
        self._refresh_feature_list()
        self.title(f"OES — Тестирование — {os.path.basename(path)}")
        self.status.set(f"Сохранено: {path}")
        return path

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

    # -- indentation / context-aware editing -----------------------------------------------------
    _DEEPEN = ("функционал", "функциональность", "функция", "feature",
               "контекст", "предыстория", "background",
               "структура сценария", "сценарий", "scenario")

    def _indent_unit(self) -> str:
        """One nesting step, inferred from the document: a TAB, else the smallest space indent."""
        text = self.editor.get("1.0", "end-1c")
        min_sp = None
        for line in text.splitlines():
            stripped = line.lstrip(" \t")
            if not stripped:
                continue
            lead = line[:len(line) - len(stripped)]
            if lead.startswith("\t"):
                return "\t"
            if lead:
                min_sp = len(lead) if min_sp is None else min(min_sp, len(lead))
        return " " * (min_sp or 2)

    def _deepens(self, stripped: str) -> bool:
        """A line whose following line should be indented one level deeper."""
        if stripped.startswith("*"):
            return True
        low = stripped.lower()
        return any(low.startswith(k) for k in self._DEEPEN)

    def _on_return(self, _evt):
        """Enter keeps the caller's indent; after Функционал/Сценарий/* it indents one level deeper."""
        ed = self.editor
        ln = int(ed.index("insert").split(".")[0])
        line = ed.get(f"{ln}.0", f"{ln}.end")
        stripped = line.lstrip(" \t")
        lead = line[:len(line) - len(stripped)]
        indent = (lead + self._indent_unit()) if self._deepens(stripped) else lead
        ed.insert("insert", "\n" + indent)
        ed.see("insert")
        self._on_edit_key()
        return "break"

    def _insert_at_cursor(self, text: str):
        """Drop a step at the cursor: fill a blank line, else add a line below it, context-indented."""
        ed = self.editor
        ln = int(ed.index("insert").split(".")[0])
        line = ed.get(f"{ln}.0", f"{ln}.end")
        stripped = line.lstrip(" \t")
        lead = line[:len(line) - len(stripped)]
        unit = self._indent_unit()
        if not stripped:
            indent = lead if lead else unit
            ed.delete(f"{ln}.0", f"{ln}.end")
            ed.insert(f"{ln}.0", indent + text)
            ed.mark_set("insert", f"{ln}.end")
        else:
            indent = (lead + unit) if self._deepens(stripped) else lead
            ed.insert(f"{ln}.end", "\n" + indent + text)
            ed.mark_set("insert", f"{ln + 1}.end")
        ed.see("insert")
        self._on_edit_key()
        ed.focus_set()

    def _insert_step(self, _evt):
        node = self.palette.focus()
        info = getattr(self, "_step_by_node", {}).get(node)
        if not info:
            return
        self._insert_at_cursor(info[0])

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
