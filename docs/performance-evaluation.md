# Performance evaluation — system and findings

This is the project-level companion to `tools/perf/README.md`. It explains *what*
the platform's performance is measured on, *how* the evaluation is produced, and
records the *findings* of a run so the numbers are read the same way twice.

## Tail optimisation — what the measurement actually showed (2026-08-31)

Asked to optimise the two tail items (frame-cost, join index), the harness earned
its keep by settling the question with numbers instead of intuition:

- **Both named targets were already optimised in prior work.** The join index is a
  `std::unordered_map` (not the red-black tree its old bench comment described),
  and its key hash already short-circuits integers with no string conversion. The
  call frame already uses raw inline storage sized to the function's *real* local
  count, with the former per-frame `std::map` replaced by a vector. There was no
  red-black-tree-to-hash or 25-slot-to-N win left to take — those were spent.
- **One real remaining inefficiency in the join index: a heap allocation per
  distinct key.** Each bucket was a `std::vector<ibValue>`, which allocates its
  buffer on the first `push_back` — so a 1:1 join over N keys did N heap
  allocations for buckets holding one element. Replaced with a small-buffer bucket
  (`ibJoinBucket`: first match inline, vector only on collision) → **zero per-key
  allocation on the common 1:1 join**, multi-match preserved (guarded by the new
  `JoinIndex.OneToOneAndMultiMatchCounts` test).
- **But it is wall-time-neutral (median of 9): the join is dominated by lambda
  dispatch, not allocation.** Three lambda invocations per row (leftKey, rightKey,
  projection) at interpreter-call cost swamp the saved `malloc`s. The gain is in
  **allocator pressure / resident set** (the axis `MillionRowScale` flagged), not
  in nanoseconds. Honest and worth keeping — just not a speedup.
- **The true tail is the interpreter CALL itself** (`recursion` x114,
  `host→script` x157), shared by every lambda-heavy path. That path is already a
  tight stack-frame fast-path; the bench comments record that two attempts to
  rewrite it crashed the corpus. It is a deliberate, higher-risk piece of work,
  not a quick win, and is left as the next frontier rather than forced here.
- **Harness lesson: single runs are too noisy for small deltas.** Metrics my change
  cannot touch (`FromString`, `str concat`) swung ±20% run to run; a median of 9
  was needed to see the join was flat. Use `--repeat 7+` before trusting a compare.

## Interpreter call cost — the session-resolution tax (2026-08-31)

The next tail item was the interpreter CALL itself (`recursion` x114, `host→script`
x157). Reading the path settled where the cost is — and, importantly, where the
micro-bench can and cannot see it:

- **`ibProcUnit::Execute` resolves `ibSession::Current()` on every invocation** —
  i.e. on every function call — and the old `Current()` took a `shared_lock` on a
  process `shared_mutex`, an `unordered_map` find by thread id, and a `weak_ptr`
  lock. In a live session that is three synchronised operations per call; under
  **concurrent** sessions the shared_mutex is read-contended by every executing
  thread on every call — a scalability tax, not just a constant one.
- **Fix: a thread-local memo validated by a generation counter.** `Current()` now
  returns a cached `(generation, session)` with a single acquire-load of one
  atomic while nothing changed; the full resolve runs only when a binding actually
  moved. Every mutation that can change the answer bumps the generation — the five
  thread→session binding sites, the process fallback, the debug-thread set and the
  parked-target queue, and `~ibSession` (belt-and-braces so no memoised pointer
  outlives its session). Only the non-debug result is memoised; a debug worker,
  whose answer is the global parked queue, always re-resolves.
- **Correctness is the whole game here, and it is validated:** the full suite —
  1404 tests including every session / scope / worker-pool / job / debug path —
  passes with the memo in place.
- **The micro-benches cannot show the win, and that is a property of the bench, not
  the fix.** `oes_bench` runs with no session registry, so `Current()` returns null
  at the first line and never reached the lock even before the change; the
  `recursion` / `host→script` numbers are therefore flat. The tax is paid by the
  *live* runtime (enterprise / designer / daemon / server), where a session is
  bound and every call took the lock. The reasoned win is real; a session-less
  loop is simply the wrong instrument to read it.
- **The remaining bench-visible call cost is inherent VM dispatch.** With session
  resolution removed, what a session-less call still spends is frame
  construction + stack-guard + opcode dispatch — all already tight (raw sized
  frames, force-inlined operand resolve, hoisted prologue, amortised context
  stack). There is no single fat left to trim there; ~114x native for call-heavy
  recursion is the honest cost of a bytecode interpreter.

## Script profiler (GitHub #2) — backend core landed

The first slice of the configuration-code profiler is in: a per-session
`ibScriptProfiler` (`compiler/scriptProfiler.{h,cpp}`) driven from
`ibProcStackGuard`, which brackets every interpreter `Execute` (named call,
lambda, module body).

- **Aggregate** — per function/procedure (or module body): call count, inclusive
  time (with nested calls) and self time (inclusive minus children), the last two
  attributed via a child-time stack that the guard pushes on entry and pops on
  exit. Sorted by self time (hot spots first).
- **Trace** — one record per completed call carrying entry time, depth and
  inclusive duration; sort by entry time for the call-sequence view. Bounded ring
  (default 200k) that names its truncation (`GetTraceDropped()`).
- **Identity resolved at EXIT**, not entry: a named function's
  `ibRunContext::m_currentFunction` is stamped by its own `OPER_FUNC` opcode, so
  it is unknown when the guard is built but known when it is destroyed.
- **Zero overhead when off, and it is measured** (not assumed): an A/B on the
  same machine state — profiler build vs the same commit with the profiler
  stashed out — put `recursion` / `host→script` / `add immediate` within run
  noise of each other. (Lesson re-learned: `host→script` is a single-pass
  `TimeNsPerOp` and swings ±2x across machine states; only a same-state A/B, or
  the best-of-N metrics, is trustworthy for a call-path change.)
- **The exit hook is `IB_NOINLINE`** on purpose: `ibProcStackGuard` is inlined
  into `Execute`, and inlining the identity + `wxString` work bloated `Execute`
  and hurt the hot call path, so it lives behind one out-of-line call.

Guarded by `JoinIndex` (unchanged) and a new `ScriptProfiler` correctness test
(counts, self≤inclusive, parent/child attribution, trace order, off-by-default).
**Script API landed** — three globals in `ibSystemManager`, reached through the
bound `System` scope like any built-in:
`StartPerformanceMeasurement()` / `StopPerformanceMeasurement()` (procedures) and
`PerformanceMeasurementResult()` (function → a sorted text table: module,
procedure, calls, self ms, total ms). Russian aliases
`НачатьЗамерПроизводительности` / `ОстановитьЗамерПроизводительности` /
`РезультатЗамераПроизводительности` (OES-RU). So a configuration can bracket a
section and `Message(PerformanceMeasurementResult())` its own hot spots. Covered
by `BuiltInRuntime.ScriptProfilerStartStopResultNamesTheFunctionsRun`.

**Structured result landed** — `PerformanceMeasurementData()`
(RU `ДанныеЗамераПроизводительности`) returns an Array of
Structure{Module, Procedure, Count, SelfMs, TotalMs}, sorted by self time, that a
script can iterate (`data.Get(i).Count`, etc.). This is the programmatic face of
the text report and the data source the Designer panel will bind to. Covered by
`BuiltInRuntime.ScriptProfilerDataIsAnIterableArrayOfRows`.

**Structured trace landed** — `PerformanceMeasurementTrace()`
(RU `ТрассаЗамераПроизводительности`) returns an Array of
Structure{Module, Procedure, Depth, EnterMs, DurationMs}, one row per invocation,
sorted by entry time — the call SEQUENCE ("who called whom, when, for how long"),
not the aggregate. `EnterMs` is relative to `Start`; `Depth` is the call-nesting
level (0 = outermost profiled). The record set is bounded (`m_traceCap`, 200k) —
a long run truncates and the text `Result` notes the drop. Trace records carry
only the aggregate key; the name/module is resolved at readout via
`ibScriptProfiler::ResolveKey`. Covered by
`BuiltInRuntime.ScriptProfilerTraceIsTheCallSequenceInOrder`.

**Designer panel landed** — `ibProfilerWindow`
(`src/engine/designer/mainFrame/profiler/`), a lazy AUI pane toggled from
Debug ▸ *Performance profiler* (`wxID_DESIGNER_VIEW_PROFILER`), modelled on the
syntax-helper pane's lifecycle. Two `ibTreeListCtrl` views in a notebook:
*Hot spots* (the aggregate — Procedure, Module, Calls, Self ms, Total ms, sorted
by self) and *Call sequence* (the trace, rebuilt into the call TREE from each
record's depth, in entry order — Procedure, Module, Enter ms, Duration ms). A
*Refresh* button re-reads and a *Clear* button empties the view; the status line
reports "measuring / ready / trace truncated / no measurement". Data is read
**in-process** from `ibSession::GetPUState()->Profiler()` — populated when
configuration code runs in THIS process (codeRunner / an in-process run) — see the
transport slice below for the F5-debug case. The pane is GUI-only; no unit test
(frontend), verified by building the `designer` target.

**Remote debuggee over the transport landed** — during normal F5 debugging the
configuration runs in the *debuggee* (enterprise / daemon / wes), not the Designer,
so `RefreshData()` detects a parked session (`ibDebuggerClient::IsEnterLoop()`) and
asks the debuggee for its profiler instead of reading the empty in-process one.
The round-trip mirrors the stack/locals path:
- Wire: two new command ids in `debugDefs.h` — `CommandId_GetProfilerData`
  (Designer → debuggee request) and `CommandId_SetProfilerData` (reply), plus a
  wire-friendly `ibProfilerReportData` (aggregate rows + trace rows + dropped
  count + a `m_hasProfiler` flag).
- Server (`debugServer.cpp`): `SendProfilerData()` reads the parked session's
  `ibSession::GetPUState()->Profiler()` (safe — the interpreter is stopped, so
  `Aggregate()`/`Trace()` snapshot a stable state), gated on `IsDebugLooped()`.
- Client (`debugClient.cpp`): `RequestProfilerData()` sends the request only while
  parked; the reply deserialises into `ibProfilerReportData` and rides the existing
  adapter → bridge path (`OnSetProfilerData`, a non-pure bridge method so other
  bridges need not override it).
- Designer bridge → `ibFrontendMainFrameDesigner::Debugger_OnProfilerData` reveals
  the pane and calls `ibProfilerWindow::LoadReport(data)`, which renders exactly the
  same two views from the wire struct (status line shows "From debuggee" /
  "trace truncated" / "no measurement in the debuggee"). Verified by building both
  `oes_tests` (backend) and `designer` (frontend); not unit-testable (needs a live
  debug session).

**XLSX export landed** — an *Export XLSX…* button on the panel toolbar writes the
current report to a real `.xlsx` via the in-tree `ibXlsxExporter` (independent
ECMA-376 writer, `backend/export/`). One worksheet ("Profiler"): the aggregate
table (Procedure, Module, Calls, Self ms, Total ms), a blank separator, then the
call sequence sorted by entry time and indented by depth (Depth, Procedure,
Module, Enter ms, Duration ms). Numeric cells are written as text; the exporter
promotes unambiguously-numeric strings to real XLSX numbers, so Excel sums the
Calls / ms columns. To make one source serve the trees, the wire and the
spreadsheet, the panel now holds the last report as a single `ibProfilerReportData
m_report` — `CaptureInProcess()` (in-process) and `LoadReport()` (from the
debuggee) both fill it, `RenderFromReport()` draws the trees, `ExportToXlsx()`
builds the sheet. Export refuses an empty report with a prompt. Verified by
building the `designer` target.

**Issue #2 is complete** — measurement core, script API (start/stop + text/struct/
trace readouts), the Designer panel, the remote-debuggee transport, and XLSX
export all landed. No open follow-ups.

## The system

Three layers, each doing one job:

1. **Measurement** — `tests/bench_runtime.cpp` and `tests/bench_string.cpp`. Every
   hot path of the runtime has a scenario: the bytecode interpreter (arithmetic
   loop, recursion, host→script call, string concat, LINQ pipelines, joins,
   group-by, structure build/read, method resolve, call frame), `ibNumber`
   (immediate and heap-decimal arithmetic, ToString/FromString), the parser
   (compile throughput), and `ibString` vs `wxString`. Each prints its figure
   next to a **native-C++ baseline**, so the number reads as an *overhead factor*
   rather than a raw nanosecond count that means nothing off this CPU.

2. **Capture + evaluation** — `tools/perf/oes_perf.py`. Runs the benches, parses
   the `@PERF{…}` lines, stores a structured run, tracks a baseline, and:
   - `compare` / `gate` — per-metric % delta vs baseline, regressions flagged;
     `gate` exits non-zero past a threshold (the CI hook).
   - `grade` — letter grades per native-comparable metric + a geo-mean overall.

3. **Record** — this document. A run's headline grade and the standing structural
   findings live here so a later reader compares against a stated anchor.

## Why ratios, not nanoseconds

The benches are noisy and machine-dependent (they say so in their own header).
The defensible cross-machine statement is *"the interpreter is Nx native C++ on
this operation"*, and that ratio is what `grade` bands. Raw ns and wall times are
kept in the run JSON for local before/after work, not for cross-machine claims.

## Standing structural findings (from the bench comments, kept here so they are findable)

These are the performance characteristics the bench arc has already established in
code. They are the "assessment" that does not change run to run:

- **Immediate-integer arithmetic is near-native.** `ibNumber` short-circuits two
  immediate integer operands through a single `int64` op (`+ - * / <`), so common
  arithmetic and comparison are a few ns. Non-exact decimal division pays the full
  exact long-division cost by design (exactness, not a regression).
- **The call is the interpreter's main tax.** A script call costs ~20 opcodes'
  worth of work (frame setup, name lookup, arg/return marshalling). Sizing the
  frame to the function's real local count and removing a per-frame `std::map`
  bought measurable wins (`recursion` −20%, `host→script` −15% on 2026-08-10).
- **`std::map`-keyed-by-`ibValue` surfaces are the recurring cost.** Structure
  fields and the join/group index are red-black trees whose comparator historically
  uppercased both sides into fresh strings per compare. This is the through-line
  behind struct field read, join, and group-by costs.
- **Per-row work must stay flat with N.** The scale benches (250 → 1e6 rows) exist
  to catch a per-row cost that grows with the data — the join once rebuilt its
  index per outer row (O(N²)); the scale row is the guard that it stays flat.
- **`ibString` vs `wxString`** — the pooled small-string type is measured against
  `wxString` on ctor/copy/concat/compare so a string-layer change is visible.

## Latest measured run

Measured on commit `085c4e7b` (Release, MSVC x64, `oes_bench`, median of 3),
80 metrics captured. Headline:

- **Overall interpreter overhead (geo-mean of native-comparable metrics): x4.95 → grade A.**
- The distribution, not the headline, is the reading:

| band | metrics | reading |
|---|---|---|
| **A (≤5x)** | string ops (0.28–0.43x — `ibString` *beats* `wxString`), `ToString`/`FromString`, immediate `+ - < /-exact`, `compare` | near-native; the number/string layers are excellent |
| **B (≤15x)** | `mul immediate` (5.1x), type-check id-vs-cast (6.5x), 25-vs-3 slot cost (7.2x) | small fixed taxes, understood |
| **C–D** | per-append string concat (36x), tight arith loop (80x) | interpreter dispatch overhead — expected for a bytecode VM |
| **E** | `recursion` (114x), `host→script` call (157x), `div non-exact` (2929x) | the call is the interpreter's main tax; non-exact decimal division pays exact long-division **by design** (correctness, not a defect) |

**How to read x4.95-grade-A honestly:** the geo-mean is pulled toward A by the many
fast primitive ops; the *tail* (call overhead, non-exact division) is where the cost
lives and is already the documented optimisation frontier (frame sizing, `std::map`
-keyed-by-`ibValue` surfaces). The system's value is that both the headline and the
tail are now a **tracked, gated** measurement rather than a memory.

Notable OES-only figures (no native equivalent): LINQ `no-lambda` floor 13.7 ns/el,
one-lambda 128 ns/el, two-lambda 287 ns/el (each lambda ≈ 160 ns); join ~3.4 µs/row;
group-by ~6 µs/row; record build+walk ~9 µs/row — all **flat across 250 → 1e6 rows**
(no per-row cost growth, the quadratic-join guard holds).

Reproduce:

```bat
cmake -S . -B build-perf -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build-perf --target oes_tests
python tools/perf/oes_perf.py run --repeat 3 --baseline
python tools/perf/oes_perf.py grade
```
