# ASan / UBSan sweep — how to run it, and what the first run found

Part of the clean-room upstream-reimplementation track (T4.3 / T4.4 in
[docs/upstream-reimpl-backlog.md](upstream-reimpl-backlog.md)). Upstream ran its
suite under AddressSanitizer + UndefinedBehaviorSanitizer and fixed a class of
memory-safety defects; we adopt the *practice* (our own CI job + local recipe),
not their code. This file is the recipe and the first run's record.

## What it is

A build with `-DOES_SANITIZE=address` (optionally `address,undefined`) runs the
non-GUI test suite instrumented: every heap access is bounds- and
lifetime-checked, so a use-after-free, buffer overflow, or UB surfaces with a
full stack at the point it happens, instead of as a later mystery crash. On
Linux, LeakSanitizer rides in with ASan and reports leaks too. The MSVC build
has ASan but no leak detector.

- **CI:** job `tests-asan` in `.github/workflows/ci.yml` (Linux, `linux-asan`
  preset = RelWithDebInfo + `address,undefined`). Same target list and `-E`
  exclusions as the ordinary Linux test job, `ASAN_OPTIONS=halt_on_error=0` so a
  single run reports every distinct finding rather than stopping at the first.
- **Preset:** `linux-asan` in `CMakePresets.json`.
- **CMake:** the `OES_SANITIZE` block in `CMakeLists.txt`. On MSVC it also adds
  `/Zi` (+ `/DEBUG`) so ASan can symbolize — without debug info MSVC emits the
  fatal-under-`/WX` warning C5072 — and `/STACK:67108864`, because ASan inflates
  every stack frame and the recursive-descent compiler overflows the default
  1 MB thread stack at a nesting depth that is fine in an ordinary build (a
  stack overflow the sweep would otherwise read as a crash, e.g.
  `ExpressionDepth.ModestNestingStillCompiles`).

## Run it locally (Windows / MSVC)

```bat
REM from a VS 2022 x64 developer prompt (vcvars64) — ASan runtime DLL must be on PATH
cmake -G Ninja -B build-asan -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_TESTING=ON ^
      -DOES_SANITIZE=address -DOES_USE_FIREBIRD=ON -DOES_USE_POSTGRESQL=ON
cmake --build build-asan --target oes_tests --parallel 3

REM run via ctest — one process per test, per-test timeout, so a hang or a crash
REM is isolated and reported rather than taking the whole run down
set ASAN_OPTIONS=halt_on_error=0:abort_on_error=0
ctest --test-dir build-asan --output-on-failure --timeout 120 ^
      -E "oes_frontend_runtime_test|oes_pg_dialect_test"
```

Prefer **ctest** over running `oes_tests.exe` directly: the single-exe run buffers
stdout and loses the gtest summary if any test crashes, and one hanging test
blocks the whole binary. ctest gives one process (and timeout) per test.

Firebird + PostgreSQL must be ON or the link fails — the suite references driver
symbols (`ibFirebirdBlobCompression`, `ibDatabaseLayerPostgres::Dialect`, …) that
are only compiled with those drivers. The PG driver loads `libpq` at run time, so
the dialect tests link without a client present.

## First run — 2026-10-05 (branch feature/import-forms @ cfe0ed45)

**Memory safety: CLEAN.** 1461 of the oes_tests cases pass; ASan reported **zero**
heap-use-after-free / overflow / UB / leaks. Our code is ASan-clean on the suite.

Caveat that cost time and is worth remembering: a first ctest pass showed 4 extra
failures (`ScriptCorpus.EveryScriptRuns`, `ConfigSpec.BuildFromJson_RejectsMalformedJson`,
`QueryLifecycle.ReleasesDuringExceptionUnwind`, `WorkerPoolDrop.…AfterADeferredErase`).
They were **flakes** — a stale `oes_tests.exe` from an earlier crashed run was
spinning at 100 % CPU in the background, starving the machine; the timing-sensitive
cases (notably the threaded `WorkerPoolDrop`) failed under the contention. All four
pass deterministically in isolation on an unloaded machine, with no ASan report.
Lesson: kill stray `oes_tests` before trusting a sweep, and read threading failures
as suspect-until-reproduced.

### Pre-existing failures the sweep surfaced (NOT memory, NOT ASan-specific)

These fail **identically in the ordinary Release build** (build-perf) and under
ASan, deterministically, at HEAD — so they predate this work and are unrelated to
memory safety. Recorded here so they are not mistaken for ASan findings:

1. **`BuiltInRuntime.VariadicBuiltinTakesMoreArgumentsThanZero` — HANGS** (infinite
   loop; confirmed in both Release and ASan). A real defect in the variadic
   builtin path. Tracked separately.
2. **8 QueryL4 tests FAILED → FIXED** (commit `232f70ef`). `QueryL4Lexer.Parameter_AmpersandName`,
   `QueryL4Parser.{WherePrecedence_AndComparesWithParam, SourceCallArgs,
   InHierarchyCarriesTheWordAndOneParameter, InHierarchyOnlyIsItsOwnWord,
   NotInHierarchyKeepsBothTheNegationAndTheWord}`, `QueryRender.InHierarchyRoundTrips`,
   `QueryParameterTable.{ItIsMARKED_InTheTextAndSurvivesTheRoundTrip,
   ItGoesInToATemporaryTableAndOnlyThere}`. Root cause was NOT missing features — the
   query-L4 language surface (`&name` params, `IN HIERARCHY`, parameter temp tables)
   already exists in the fork. It was a **regression** from the `&`-directive work
   (tasks #44-47): `ibTranslateCode` made `&` a word-start char for the script's
   `&НаКлиенте` directives, and `ibQueryLexer` (which derives from it) checked
   `IsWord()` before its own `&`-param branch, so `&Warehouse` lexed as one Ident.
   Fixed by testing `&` before `IsWord()` in the query lexer. 87/87 query tests pass.

The remaining **T1.1** work is therefore NOT the query language surface (it is present)
but the upstream **server pushdown** — executing `IN (SELECT)` as a semi-join, `IN
HIERARCHY` / `TOTALS` / turnovers folded by the DB — which is the performance win.

After this fix the only pre-existing red left is the variadic-builtin **hang** (flagged
as a separate task); the `tests-asan` CI job inherits the ordinary Linux `-E` exclusions.
