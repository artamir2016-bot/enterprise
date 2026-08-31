# Performance evaluation — system and findings

This is the project-level companion to `tools/perf/README.md`. It explains *what*
the platform's performance is measured on, *how* the evaluation is produced, and
records the *findings* of a run so the numbers are read the same way twice.

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
