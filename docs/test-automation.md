# OES Test Automation — Vanessa-like functional & interactive testing (PLAN)

> **FORK FEATURE (not upstream).** A BDD/Gherkin functional-and-interactive UI test system for
> OES, analogous to 1C's **Vanessa Automation** (BDD over the UI) + **Vanessa Runner/ADD** (scenario
> orchestration) — but broader: it must drive **both the Designer GUI and the Enterprise runtime**,
> and run **end-to-end scenarios that cross from Designer into Enterprise in a single run**.
> Every code edit is marked `OES-TEST:` for upstream diffing.

## Goal & scope (confirmed with the user)

- **BDD in Russian Gherkin**: `Функционал / Сценарий / Дано / Когда / Тогда / И` (Vanessa-style).
- **Cross-application, end-to-end**: one scenario can drive `designer.exe` (edit/check a config)
  and then `enterprise.exe` (exercise the running application) — in the same run.
- **Interactive GUI driving**: real windows, menus, dialogs, tree, property grid, code editor
  (Designer) AND runtime forms/controls/commands (Enterprise).

Because a single scenario spans two separate processes, the engine **cannot** live only in-process
(as Vanessa does inside the tested 1C client). The architecture is **out-of-process orchestration +
an embedded test agent in every OES app**.

## Architecture

```
   ┌─────────────────────────┐        Russian .feature files
   │   oes_testrunner        │  ← parses Gherkin, maps steps → agent commands,
   │   (orchestrator)        │    launches apps, sequences across processes,
   └───────────┬─────────────┘    collects results + screenshots → report
               │  TCP control channel (per app instance)
        ┌──────┴───────────────────────────┐
        ▼                                   ▼
  ┌───────────────┐                  ┌───────────────┐
  │ designer.exe  │                  │ enterprise.exe│
  │  Test Agent   │                  │  Test Agent   │
  │  (frontend)   │                  │  (frontend)   │
  └───────────────┘                  └───────────────┘
   generic wx UI ops                  generic wx UI ops
   + designer helpers                 + runtime FORM API
```

### 1. Test Agent (embedded, `frontend.dll`)
A control server started when an app runs with `--testagent[=port]` (mirrors the debugger's
`--debug`). Reuses the **debugger's proven TCP pattern** (`src/engine/backend/debugger/` — server
= the app, length-prefixed messages, async replies with a request id). Lives in `frontend.dll`
(both designer and enterprise link it), so the SAME agent serves both apps.

Command set (JSON over the framed channel):
- **Generic wx UI** (works in any OES app): `listWindows`, `findControl {by:name|label|type|id}`,
  `click`, `type`, `setValue`, `getValue`, `getState {enabled,visible,text,checked}`,
  `invokeMenu {path}`, `selectTreeItem {path}`, `waitFor {selector, timeoutMs}`, `screenshot`.
  Driving is **programmatic** (walk the `wxWindow` tree, match by `GetName()`/label/type, fire the
  control's own event) — robust, not pixel-based; `wxUIActionSimulator` only where unavoidable.
- **Enterprise runtime forms** (reuses the mapped API): `openForm`, `formFindControl`
  (`ibValueFrame::FindControlByName`), `getControlValue`/`setControlValue`, `pressCommand`
  (`CallAsAction` / command resolve), `readTable {rows,cols}`, `getFormList`
  (`s_createdDocFormArray`).
- **Designer helpers**: `openConfig`, `openMetaobjectEditor`, `checkModules` (reuses the new CLI
  batch check), `saveConfig`.
- **Lifecycle**: `ping`, `appInfo`, `quit`.

### 2a. IDE / feature workbench (`tools/oes_testrunner/ide.py`)
A dependency-free Tkinter desktop UI (Vanessa-like): feature list (open/new/save), a Gherkin editor
with Russian keyword highlighting, a **step palette** (grouped by category from `steps_catalog.py`;
double-click inserts a step template), a **live inspector** tab (connects to a running app's agent by
port and lists its Окна / Меню / Контролы активной формы — double-click inserts a step referencing
that element; uses `listWindows` / `listMenus` / `listControls`), and **Run / Run with video** buttons
that drive `runner.py` as a subprocess and stream its output live (OK green / FAIL red / summary).
Launch: `python tools/oes_testrunner/ide.py`.

### 2. Runner / Orchestrator (`oes_testrunner`)
Parses Russian Gherkin, owns the **step library** (phrase → agent command mapping — extensible in
data/config without rebuilding the platform), launches/attaches app instances with `--testagent`,
routes each step to the right agent, sequences cross-app flow, and emits a report (console +
**JUnit XML** for CI + **Allure-style** artefacts + screenshots on failure).
- **MVP language: Python** (fast to build the parser/orchestrator/report; matches repo tooling in
  `tools/*.py`). A native `oes_testrunner.exe` can follow once the protocol is stable.

### 3. Gherkin dialect
Russian keywords (`Функционал`, `Сценарий`, `Структура сценария`, `Дано`, `Когда`, `Тогда`, `И`,
`Также`, `Примеры`), reusing the official Gherkin i18n Russian set for compatibility with existing
1C `.feature` files where reasonable.

## Reused vs new

| Piece | Reuse | New |
|---|---|---|
| TCP control channel | debugger transport pattern (`debugger/`, port model, framing, async id) | test command set + port (e.g. 1651) |
| Open-form registry | `s_createdDocFormArray`, `FindFormBySourceUniqueKey` | `getFormList` command |
| Control find / value | `FindControlByName`, `GetControlValue`/`SetControlValue` | generic selector layer |
| Button/command | `CallAsAction`, command resolve, `CallAsEvent` | `pressCommand` command |
| Generic wx UI (Designer) | `wxWindow` tree, `wxUIActionSimulator`, menus | window/control selector + action layer |
| Config check | the new `/CheckConfig` `/CheckModules` batch CLI | agent `checkModules` |
| Gherkin + report | — | parser, step library, JUnit/Allure report |

## Status (delivered)

- **#1 transport — DONE.** `ibTestAgent` in `frontend.dll`, `--testagent[=port]` in designer +
  enterprise, framed JSON, `ping`/`appInfo`/`quit`. Verified.
- **#4 enterprise form commands + message tap — DONE (core).** `getForms`, `activeForm`,
  `findControl`, `get/setControlValue`, `get/setAttribute`, `openForm` (catalog list/object),
  `pressCommand` (form command / form-module proc), `getMessages`/`clearMessages` via a backend
  message tap. Verified on `demo_ru_base` (open Товары, set/get Артикул + Цена round-trip).
- **#3 runner — DONE (MVP).** `tools/oes_testrunner/` — Russian Gherkin parser, launcher, step
  library, JUnit report. `features/demo_ru.feature` runs green (1/1): Designer → close → Enterprise
  → open Товары → assert controls → set/read Артикул & Цена.

### Next
- Press STANDARD actions (записать/провести) — needs the protected command set; unlocks the
  ПередЗаписью + message-tap assertion (create Товар, Цена<0, записать → «Цена не может быть
  отрицательной»).
- #2 generic wx-UI commands (menus/dialogs/tree) to drive the DESIGNER beyond ping.
- #5 designer helpers (openConfig / checkModules / saveConfig).
- Concurrency: a SERVER base lets Designer + Enterprise run truly at once (a file base is exclusive,
  so the demo closes Designer before Enterprise).

## Phased plan (subtasks · priority · branch)

Each subtask: branch from `feature/import-forms`, implement + test, merge back `--no-ff`.

| # | Prio | Branch | Scope |
|---|---|---|---|
| 1 | **P0** | `feature/test-agent-transport` | Embed the test-agent TCP server in `frontend.dll`; `--testagent[=port]` flag in designer + enterprise; `ping`/`appInfo`/`quit`; framed JSON protocol. |
| 2 | **P0** | `feature/test-agent-wxui` | Generic wx UI commands: `listWindows`, `findControl`, `click`, `type`, `getState`, `invokeMenu`, `waitFor`, `screenshot`. Programmatic driving. |
| 3 | **P0** | `feature/test-runner-mvp` | Python `oes_testrunner`: Russian Gherkin parser, step library, launch apps with `--testagent`, run steps, console + JUnit report + failure screenshots. |
| 4 | **P1** | `feature/test-agent-forms` | Enterprise runtime-form commands: `openForm`, `formFindControl`, get/set value, `pressCommand`, `readTable`, `getFormList`. |
| 5 | **P1** | `feature/test-designer-helpers` | Designer helpers: `openConfig`, `openMetaobjectEditor`, `checkModules`, `saveConfig`. |
| 6 | **P1** | `feature/test-e2e-demo` | End-to-end demo `.feature` on `demo_ru_base`: Designer opens Товары + checks config → Enterprise creates a Товар and asserts the `ПередЗаписью` guard (Цена<0 → сообщение). |
| 7 | **P2** | `feature/test-report-allure` | Allure-style artefacts, step timings, video/gif of a run, CI wiring. |
| 8 | **P2** | `feature/test-native-runner` | Optional native `oes_testrunner.exe` once the protocol is stable. |

## MVP (Phase 1 = subtasks 1–3 + a thin slice of 4/6)

Deliverable: a Russian `.feature` that launches `enterprise.exe --file=demo_ru_base --testagent`,
opens the Товары list, creates an item, sets Цена = -1, presses "записать", and asserts the
platform shows "Цена не может быть отрицательной". Plus one Designer step (open config, run
`/CheckModules`) to prove cross-app control. Green run + JUnit report + screenshot on failure.

## Security
The agent, like the debugger, binds **localhost only** and is **off unless `--testagent` is passed**.
No remote control by default; the port carries the ability to drive the app, so it stays local and
opt-in (same stance as `docs/debugger-architecture.md`).

## Open questions (non-blocking; sensible defaults chosen)
- Runner language: **Python for MVP** (revisit native exe at #8).
- Selector syntax for controls: start with `name` / `label` / `type` / `path`; extend as needed.
- Report format: **JUnit XML** first (CI-friendly), Allure later (#7).
