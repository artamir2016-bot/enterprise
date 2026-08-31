# OES performance evaluation system

A reproducible, machine-readable, graded harness over the C++ micro-benchmarks
in `tests/bench_runtime.cpp` (interpreter / `ibNumber` / parser) and
`tests/bench_string.cpp` (`ibString` vs `wxString`).

The benches themselves already *measure* — they print one figure per scenario next
to a native-C++ baseline. This tool makes those figures **usable as an evaluation**:
it captures them as structured JSON, tracks a baseline, flags regressions past a
threshold (a CI gate), and turns a run into letter grades and a one-line verdict.

## How it fits together

```
tests/bench_runtime.cpp   --OES_PERF_JSON=1-->  @PERF{...} lines on stdout
tests/bench_string.cpp
        |                                              |
        |  (gtest --gtest_also_run_disabled_tests      |
        |         --gtest_filter=*Bench*)              v
        +----------------------------->  tools/perf/oes_perf.py
                                             run / baseline / compare / gate / grade
                                             |            |
                                        runs/*.json   baseline.json
```

The C++ side prints, only when `OES_PERF_JSON` is set in the environment, one
self-describing line per figure:

```
@PERF{"name":"arith loop (ns/iter)","oes":12.3,"native":4.5,"unit":"ns","oes_wall_ns":...}
```

Everything else on stdout (the pretty human rows) is ignored by the parser, so the
two coexist. **Convention: every metric is lower-is-better** (ns/op, ns/row, or the
`oes/native` overhead ratio), so a positive delta vs baseline is a slowdown.

## Build the bench binary (Release — required for meaningful numbers)

Windows (from the repo root, in a shell that can reach CMake/Ninja/MSVC):

```bat
cmake -S . -B build-perf -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build-perf --target oes_tests
```

`tools/perf/_build_tests.bat` does exactly this via the VS 2022 environment.

## Use

```bash
PY=C:\Users\User\.pyenv\pyenv-win\versions\3.12.10\python.exe

# run the benches, median of 3, print a table, store a run JSON
%PY% tools/perf/oes_perf.py run --repeat 3

# set today's numbers as the tracked baseline
%PY% tools/perf/oes_perf.py baseline --repeat 3

# after a change: did anything regress? (uses the latest run, or --run PATH)
%PY% tools/perf/oes_perf.py compare

# CI gate — exits 1 if any metric is >15% slower than baseline
%PY% tools/perf/oes_perf.py gate --fail 15 --warn 7

# assessment view — letter grades + geo-mean overhead
%PY% tools/perf/oes_perf.py grade
```

Narrow the scope with `--filter` (a gtest filter), e.g.
`--filter=*RuntimeBench*` or `--filter=*NumberBench*`.

## Grading bands (interpreter overhead, `oes/native` ratio)

Raw nanoseconds depend on the CPU; the **ratio to native C++** is the fair
cross-machine yardstick. An AST/bytecode interpreter within ~15x of native on
tight arithmetic is in CPython's league.

| ratio (oes/native) | grade | reading |
|---|---|---|
| ≤ 5   | A | near-native |
| ≤ 15  | B | competitive interpreter |
| ≤ 40  | C | typical dynamic-language cost |
| ≤ 100 | D | heavy per-op overhead |
| > 100 | E | investigate |

The overall grade is the geometric mean of every native-comparable metric.
OES-only figures (LINQ per-element, big-decimal arithmetic — no native
equivalent) are reported but not graded.

## Files

- `oes_perf.py` — the harness (stdlib only, no deps).
- `baseline.json` — the tracked baseline (committed so `gate` works on a fresh checkout).
- `runs/` — per-run JSON snapshots (git-ignored; keep locally for history).
- `_build_tests.bat` — configure + build `oes_tests` Release via VS 2022.

## Regression gate in CI

`gate` is the one-liner for a pipeline: build Release, run it, exit non-zero if
any tracked metric slid past `--fail`. Because the baseline is committed, the gate
compares against a known-good point rather than run-to-run noise.
