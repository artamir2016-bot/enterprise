# Module tests & TDD for configuration code

This is the how-to for unit-testing the **script code inside a configuration's modules** —
the code a config developer writes in common modules, object modules and managers — and the
red-green-refactor loop built on top of it. It is meant to be usable both by a human in the
Configurator and by Claude Code through the `oes-dev` MCP.

The platform's own C++ is tested with Google Test (`enterprise/tests/`, see CLAUDE.md). That is a
different thing. **This file is about testing the LANGUAGE code that ships in an `.mcf`.**

---

## The three pieces

1. **An assert library** — an ordinary **common module** written in the OES language (not a
   built-in platform API). The demo base calls it «Проверки». You copy it into your configuration
   and extend it; it is yours.
2. **A naming convention** — a test is an **exported, parameterless procedure** whose name starts
   with `Тест` (or `Test`), living in any common module. No registration, no attributes: the
   runner discovers tests by parsing module text.
3. **A headless runner** — `enterprise.exe --runtests`. It opens the base with no window, finds
   every test, runs each one, prints a red/green report and (optionally) writes JUnit XML. Exit
   code is `0` iff all tests pass.

---

## Why asserts report by message, not by exception

The obvious design — an assert `Raise`s on failure — does **not** work headless. In a windowless
run there is no interactive frame, and the interpreter's throw/unwind + error-reporting path
assumes one; on Windows that surfaces as an access violation the process cannot catch. So the
assert library **does not throw**. It reports a failure by emitting a **marked user message**:

```
Сообщить("##OESFAIL## <human-readable reason>");
```

The runner installs a message tap, clears it before each test, runs the test, and if any captured
message starts with `##OESFAIL##` the test is a failure with that text as the reason. A genuine
runtime error that still throws is caught as a fallback and also fails the test (it will not,
however, produce as clean a message — prefer asserts).

Consequence for the assert library: **an assert only records the first-failing fact you give it**;
because it does not throw, code after a failed assert in the same test keeps running. Keep one
logical assertion per test, or expect "soft assertion" semantics.

---

## The assert library («Проверки»)

Minimal, in VES. `Равно` = "equals", `Верно` = "true". Copy and grow it (`НеРавно`, `Пусто`,
`Содержит`, …) — it is just configuration code.

```
Процедура Равно(Факт, Ожидание, Сообщение) Экспорт
	Если Факт <> Ожидание Тогда
		Сообщить("##OESFAIL## Равно [" + Сообщение + "]: ожидалось ["
			+ Строка(Ожидание) + "], получено [" + Строка(Факт) + "]");
	КонецЕсли;
КонецПроцедуры

Процедура Верно(Условие, Сообщение) Экспорт
	Если Не Условие Тогда
		Сообщить("##OESFAIL## Верно [" + Сообщение + "]: условие ложно");
	КонецЕсли;
КонецПроцедуры
```

> VES pitfalls that bite here: identifiers must not collide with keywords — `И`/`Истина`/`Ложь`
> are AND/TRUE/FALSE, `Не` is NOT. `Строка(...)` is the String() cast. `<>` is not-equal.

---

## Writing tests

A test is an exported no-arg procedure named `Тест…`/`Test…` in any common module:

```
Процедура ТестСложение() Экспорт
	Проверки.Равно(2 + 2, 4, "арифметика");
КонецПроцедуры

Процедура ТестБулево() Экспорт
	Проверки.Верно(2 > 1, "два больше одного");
КонецПроцедуры
```

Optional per-test fixtures: if a module exports `BeforeEach` / `AfterEach`, the runner calls them
around every test in the run (they are optional — absence is not a failure).

Tests run **without eval mode**, so `Сообщить` works (that is the failure channel) — but note DB
transactions are live. A test that mutates the database mutates the real base; for data-touching
tests prefer building throwaway objects and not writing them, or run against a scratch base.

---

## Running

Headless CLI (the base must already have the configuration loaded):

```
enterprise.exe --file=<base> --runtests --junit=<report.xml> --minimized
```

Output:

```
  PASS Тесты.ТестСложение
  PASS Тесты.ТестБулево
  FAIL Тесты.ТестНамеренныйПровал: Равно [должен упасть]: ожидалось [2], получено [1]
Tests: 3, passed 2, failed 1
```

Exit code = number of failed tests (0 = green). JUnit XML is standard `<testsuite>/<testcase>`
with `<failure message="…">`, consumable by CI.

Through the `oes-dev` MCP, the same run is the `oes_run_tests` tool: `{base, junit?, timeout?}`.

---

## The TDD loop (red → green → refactor)

For a human in the Configurator, or for Claude Code via MCP, the cycle is the same:

1. **Red** — write a failing `Тест…` for the behaviour you want. Add/patch the module with
   `oes_config_edit`, load it with `oes_config_load`, run `oes_run_tests`. See it FAIL.
2. **Green** — write the smallest module code that makes it pass. Edit → load → run. See it PASS.
3. **Refactor** — clean the code up; keep running the tests; they stay green.

Claude-Code-friendly recipe (MCP tool names):

```
oes_config_edit   in_mcf=demo.mcf  patch={commonModules:[{name:"Тесты", code:"<new test>"}]}
oes_config_load   base=<base>      mcf=demo.mcf
oes_run_tests     base=<base>      junit=<out.xml>     # -> RED
oes_config_edit   in_mcf=demo.mcf  patch={commonModules:[{name:"ОбщийМодуль", code:"<impl>"}]}
oes_config_load   base=<base>      mcf=demo.mcf
oes_run_tests     base=<base>      junit=<out.xml>     # -> GREEN
```

`oes_config_edit` merges by name (idempotent), so re-patching a module replaces its code.

---

## Where the pieces live (for maintainers)

| Piece | Location |
|---|---|
| Runner (shared free function) | `src/engine/frontend/testAgent/moduleTestRunner.{h,cpp}` — `ibRunModuleTests(session, junitPath, report)` |
| Enterprise entry | `enterprise.exe --runtests [--junit=<file>]` (`src/engine/enterprise/mainApp.{h,cpp}`) |
| Message tap (failure channel) | `ibValueSystemFunction::SetMessageTap` (`backend/system/systemManager.h`) |
| Test discovery | `ibParserModule` (`frontend/.../codeEditorParser.h`) — export procs, name filter |
| MCP tool | `oes_run_tests` in `tools/oes_mcp/server.py` |
| Demo | `testbase/demo_ru.json` — common modules «Проверки» + «Тесты» |

**Runtime constraint that shaped this:** module code executes only in an **Enterprise/Service**
session — `AttachRuntime` short-circuits for the Designer, so per-module procUnits are never wired
there. That is why the runner is hosted by `enterprise.exe`, not `designer.exe`.
