#!/usr/bin/env python3
# =============================================================================
# OES Enterprise — performance evaluation system (harness over the micro-benches)
#
# The C++ side (tests/bench_runtime.cpp) already measures the interpreter, ibNumber
# and the parser and prints one figure per scenario. This tool turns those figures
# into a REPRODUCIBLE, MACHINE-READABLE, GRADED evaluation:
#
#   run       run the *Bench* tests, parse @PERF lines, write a run JSON, print a table
#   baseline  promote a run (or a fresh run) to the tracked baseline
#   compare   diff a run against the baseline: per-metric % delta, regressions flagged
#   gate      compare + exit non-zero if any metric regressed past the threshold (CI)
#   grade     turn a run into letter grades + a one-line verdict (the "assess" view)
#
# The bench binary emits, under OES_PERF_JSON=1, one line per figure:
#   @PERF{"name":"arith loop (ns/iter)","oes":12.3,"native":4.5,"unit":"ns",...}
# We grep those out; everything else on stdout is ignored, so the human rows and
# the machine contract coexist.
#
# Convention: every metric is LOWER-IS-BETTER (ns/op, ns/row, overhead ratio). A
# positive % delta vs baseline is therefore a SLOWDOWN.
#
# Zero third-party deps (stdlib only) so it runs anywhere the repo is checked out.
# =============================================================================

import argparse
import json
import os
import re
import statistics
import subprocess
import sys
import time
from datetime import datetime, timezone

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
BASELINE = os.path.join(HERE, "baseline.json")
RUNS_DIR = os.path.join(HERE, "runs")

PERF_RE = re.compile(r"@PERF(\{.*\})\s*$")

# Regression thresholds (percent slower than baseline). A metric over WARN is
# reported; a metric over FAIL makes `gate` exit non-zero. Noise floor keeps a
# tiny absolute wobble on a sub-nanosecond figure from reading as a regression.
DEFAULT_WARN_PCT = 7.0
DEFAULT_FAIL_PCT = 15.0
NOISE_FLOOR_NS = 0.5


# --------------------------------------------------------------------------- #
# discovery
# --------------------------------------------------------------------------- #
def find_exe(explicit):
    if explicit:
        if os.path.isfile(explicit):
            return explicit
        sys.exit("perf: --exe not found: %s" % explicit)
    names = ("oes_bench.exe", "oes_bench", "oes_tests.exe", "oes_tests")
    roots = [os.path.join(REPO, d) for d in
             ("build-perf", "build", "build/bin", "out")]
    best = None
    for root in roots:
        if not os.path.isdir(root):
            continue
        for dirpath, _dirs, files in os.walk(root):
            for n in names:
                if n in files:
                    p = os.path.join(dirpath, n)
                    if best is None or os.path.getmtime(p) > os.path.getmtime(best):
                        best = p
    if best is None:
        sys.exit("perf: could not find oes_tests(.exe); pass --exe PATH "
                 "(build it Release first — see tools/perf/README.md)")
    return best


def git_commit():
    try:
        out = subprocess.check_output(["git", "rev-parse", "--short", "HEAD"],
                                      cwd=REPO, stderr=subprocess.DEVNULL)
        return out.decode().strip()
    except Exception:
        return "unknown"


# --------------------------------------------------------------------------- #
# run + parse
# --------------------------------------------------------------------------- #
def run_once(exe, gtest_filter):
    env = dict(os.environ)
    env["OES_PERF_JSON"] = "1"
    cmd = [exe, "--gtest_also_run_disabled_tests",
           "--gtest_filter=%s" % gtest_filter]
    proc = subprocess.run(cmd, cwd=os.path.dirname(exe), env=env,
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    text = proc.stdout.decode("utf-8", "replace")
    records = {}
    for line in text.splitlines():
        m = PERF_RE.search(line.strip())
        if not m:
            continue
        try:
            obj = json.loads(m.group(1))
        except json.JSONDecodeError:
            continue
        records[obj["name"]] = obj
    return records, text


def cmd_run(args):
    exe = find_exe(args.exe)
    reps = max(1, args.repeat)
    print("perf: %s  x%d  filter=%s" % (os.path.relpath(exe, REPO), reps, args.filter))
    per_metric = {}          # name -> list of oes readings
    meta = {}                # name -> last full record (native/unit/wall)
    last_text = ""
    for r in range(reps):
        recs, last_text = run_once(exe, args.filter)
        if not recs:
            print("  run %d: no @PERF lines parsed" % (r + 1))
        for name, obj in recs.items():
            per_metric.setdefault(name, []).append(obj["oes"])
            meta[name] = obj
        print("  run %d: %d metrics" % (r + 1, len(recs)))

    if not per_metric:
        sys.stderr.write(last_text[-2000:])
        sys.exit("perf: no metrics captured — is the bench built with the "
                 "@PERF emitter (rebuild oes_tests)?")

    results = {}
    for name, vals in per_metric.items():
        obj = dict(meta[name])
        obj["oes"] = statistics.median(vals)
        obj["oes_min"] = min(vals)
        obj["samples"] = len(vals)
        if obj.get("native"):
            obj["ratio"] = obj["oes"] / obj["native"]
        results[name] = obj

    run = {
        "timestamp": datetime.now(timezone.utc).isoformat(),
        "commit": git_commit(),
        "exe": os.path.relpath(exe, REPO),
        "repeat": reps,
        "filter": args.filter,
        "results": results,
    }
    os.makedirs(RUNS_DIR, exist_ok=True)
    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    out = args.out or os.path.join(RUNS_DIR, "run_%s.json" % stamp)
    with open(out, "w", encoding="utf-8") as f:
        json.dump(run, f, indent=2)
    print_table(results)
    print("\nperf: wrote %s" % os.path.relpath(out, REPO))
    if args.baseline:
        with open(BASELINE, "w", encoding="utf-8") as f:
            json.dump(run, f, indent=2)
        print("perf: promoted to baseline")
    return run


def print_table(results):
    print("\n%-34s %12s %12s %8s" % ("metric", "oes", "native", "ratio"))
    print("-" * 70)
    for name in sorted(results):
        o = results[name]
        nat = ("%.1f" % o["native"]) if o.get("native") else "-"
        rat = ("x%.2f" % o["ratio"]) if o.get("ratio") else "-"
        print("%-34s %12.2f %12s %8s" % (name[:34], o["oes"], nat, rat))


# --------------------------------------------------------------------------- #
# baseline / compare / gate
# --------------------------------------------------------------------------- #
def load_run(path):
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def latest_run():
    if not os.path.isdir(RUNS_DIR):
        return None
    runs = [os.path.join(RUNS_DIR, f) for f in os.listdir(RUNS_DIR)
            if f.endswith(".json")]
    return max(runs, key=os.path.getmtime) if runs else None


def cmd_baseline(args):
    if args.run:
        run = load_run(args.run)
    else:
        run = cmd_run(args)
    with open(BASELINE, "w", encoding="utf-8") as f:
        json.dump(run, f, indent=2)
    print("perf: baseline set from commit %s (%d metrics)"
          % (run.get("commit"), len(run["results"])))


def diff(base, cur, warn, fail):
    """Return list of (name, base_oes, cur_oes, pct, status)."""
    rows = []
    b = base["results"]
    c = cur["results"]
    for name in sorted(set(b) & set(c)):
        bo = b[name]["oes"]
        co = c[name]["oes"]
        if bo <= 0:
            continue
        pct = (co - bo) / bo * 100.0
        if abs(co - bo) < NOISE_FLOOR_NS:
            status = "ok"
        elif pct >= fail:
            status = "FAIL"
        elif pct >= warn:
            status = "warn"
        elif pct <= -warn:
            status = "faster"
        else:
            status = "ok"
        rows.append((name, bo, co, pct, status))
    return rows


def cmd_compare(args, exit_on_fail=False):
    if not os.path.isfile(BASELINE):
        sys.exit("perf: no baseline — run `oes_perf.py baseline` first")
    base = load_run(BASELINE)
    cur = load_run(args.run) if args.run else load_run(
        latest_run() or sys.exit("perf: no run to compare (run first)"))
    rows = diff(base, cur, args.warn, args.fail)

    print("compare  base=%s -> cur=%s   (warn>%.0f%%  fail>%.0f%%)"
          % (base.get("commit"), cur.get("commit"), args.warn, args.fail))
    print("\n%-34s %10s %10s %9s  %s" %
          ("metric", "base", "cur", "delta", "status"))
    print("-" * 78)
    fails, warns, faster = 0, 0, 0
    for name, bo, co, pct, status in rows:
        mark = {"FAIL": "  <== REGRESSION", "warn": "  <- watch",
                "faster": "  (faster)", "ok": "", }[status]
        print("%-34s %10.2f %10.2f %+8.1f%%  %-6s%s"
              % (name[:34], bo, co, pct, status, mark))
        fails += status == "FAIL"
        warns += status == "warn"
        faster += status == "faster"
    only_base = sorted(set(base["results"]) - set(cur["results"]))
    only_cur = sorted(set(cur["results"]) - set(base["results"]))
    if only_base:
        print("\n  metrics missing from current run: %s" % ", ".join(only_base))
    if only_cur:
        print("  new metrics (not in baseline): %s" % ", ".join(only_cur))
    print("\nsummary: %d regressions, %d watch, %d faster, %d compared"
          % (fails, warns, faster, len(rows)))
    if exit_on_fail and fails:
        sys.exit(1)
    return rows


def cmd_gate(args):
    cmd_compare(args, exit_on_fail=True)
    print("perf gate: PASS")


# --------------------------------------------------------------------------- #
# grade — the "assess the project" view
# --------------------------------------------------------------------------- #
# Interpreter-overhead bands (oes/native ratio). An AST/bytecode interpreter that
# stays within ~15x of native C++ on tight arithmetic is competitive with CPython;
# ratios are the fair cross-machine yardstick (raw ns depend on the CPU).
def grade_ratio(ratio):
    if ratio is None:
        return "-"
    if ratio <= 5:   return "A"
    if ratio <= 15:  return "B"
    if ratio <= 40:  return "C"
    if ratio <= 100: return "D"
    return "E"


def cmd_grade(args):
    run = load_run(args.run) if args.run else load_run(
        latest_run() or sys.exit("perf: no run to grade (run first)"))
    res = run["results"]
    print("grade  commit=%s  %s\n" % (run.get("commit"), run.get("timestamp", "")))

    rated = [(n, o["ratio"], grade_ratio(o["ratio"]))
             for n, o in res.items() if o.get("ratio")]
    print("%-34s %8s  %s" % ("metric (has native baseline)", "ratio", "grade"))
    print("-" * 56)
    for name, ratio, g in sorted(rated, key=lambda x: x[1]):
        print("%-34s   x%6.2f    %s" % (name[:34], ratio, g))

    if rated:
        ratios = [r for _, r, _ in rated]
        geo = statistics.geometric_mean(ratios)
        overall = grade_ratio(geo)
        print("\noverhead (geo-mean of native-comparable metrics): x%.2f  ->  grade %s"
              % (geo, overall))

    # OES-only metrics (no native equivalent) — reported, not graded.
    oes_only = [(n, o["oes"], o["unit"]) for n, o in res.items()
                if not o.get("ratio")]
    if oes_only:
        print("\nOES-only figures (no native baseline — informational):")
        for name, val, unit in sorted(oes_only):
            print("  %-40s %10.2f %s" % (name[:40], val, unit))


# --------------------------------------------------------------------------- #
def build_parser():
    p = argparse.ArgumentParser(
        description="OES performance evaluation system (harness over the micro-benches)")
    sub = p.add_subparsers(dest="cmd", required=True)

    def add_common(sp):
        sp.add_argument("--exe", help="path to oes_tests(.exe); auto-detected if omitted")
        sp.add_argument("--filter", default="*Bench*",
                        help="gtest filter (default *Bench*)")
        sp.add_argument("--repeat", type=int, default=1,
                        help="run N times, take per-metric median (noise control)")
        sp.add_argument("--out", help="write run JSON here")
        sp.add_argument("--baseline", action="store_true",
                        help="also promote this run to the baseline")

    sr = sub.add_parser("run", help="run benches, parse, store a run JSON")
    add_common(sr)
    sr.set_defaults(func=cmd_run)

    sb = sub.add_parser("baseline", help="set the tracked baseline")
    add_common(sb)
    sb.add_argument("--run", help="promote an existing run JSON instead of running")
    sb.set_defaults(func=cmd_baseline)

    def add_cmp(sp):
        sp.add_argument("--run", help="run JSON to compare (default: latest)")
        sp.add_argument("--warn", type=float, default=DEFAULT_WARN_PCT)
        sp.add_argument("--fail", type=float, default=DEFAULT_FAIL_PCT)

    sc = sub.add_parser("compare", help="diff a run against the baseline")
    add_cmp(sc)
    sc.set_defaults(func=cmd_compare)

    sg = sub.add_parser("gate", help="compare, exit 1 on regression (CI)")
    add_cmp(sg)
    sg.set_defaults(func=cmd_gate)

    sgr = sub.add_parser("grade", help="letter-grade a run (assessment view)")
    sgr.add_argument("--run", help="run JSON to grade (default: latest)")
    sgr.set_defaults(func=cmd_grade)
    return p


def main():
    args = build_parser().parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
