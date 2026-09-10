# Smoke test for the IMPORTED CONFIGURATION.
#
# Runs the real 1C importer on the reference dump and checks the whole arc end to end:
#   1. convert   — 1C XML -> JSON spec, and every requested metatype kind is present;
#   2. invariants — the imported spec is not just non-empty but SHAPED right (documents carry
#      movements, accounting registers carry a chart of accounts, charts carry predefined data,
#      common forms carry a control tree, etc.);
#   3. build      — the spec compiles into a .mcf;
#   4. load       — the .mcf updates a FRESH Firebird base ("Database configuration updated.");
#   5. modules    — /CheckModules compiles every imported module with no errors.
#
# Each check prints [PASS]/[FAIL]; the script exits non-zero if any failed, so it doubles as a CI gate.
# It is a SMOKE test, not a unit test: bounded by --limit for speed, it proves the pipeline is wired and
# the imported configuration is loadable and internally consistent — not that every object is perfect.
#
# Usage:  python tools/import_smoke.py [--limit N]
import argparse, os, re, subprocess, sys, json
import grow_base as gb   # reuse convert / build_mcf / load_base + BIN / DESIGNER / markers

# The metatypes the importer supports today. Kept here so a newly-wired kind is added in one place and
# the smoke test starts guarding it.
KINDS = [
    "Catalogs", "Documents", "Enums", "Constants",
    "InformationRegisters", "AccumulationRegisters", "AccountingRegisters",
    "ChartsOfCharacteristicTypes", "ChartsOfAccounts", "ChartsOfCalculationTypes",
    "CalculationRegisters", "CommonModules",
    "Roles", "Subsystems",
    "SessionParameters", "ScheduledJobs", "CommonForms",
]

# JSON spec key per kind (matches onec_to_spec / metadataConfigSpec).
KEY = {
    "Catalogs": "catalogs", "Documents": "documents", "Enums": "enums", "Constants": "constants",
    "InformationRegisters": "informationRegisters", "AccumulationRegisters": "accumulationRegisters",
    "AccountingRegisters": "accountingRegisters", "ChartsOfCharacteristicTypes": "chartsOfCharacteristicTypes",
    "ChartsOfAccounts": "chartsOfAccounts", "ChartsOfCalculationTypes": "chartsOfCalculationTypes",
    "CalculationRegisters": "calculationRegisters", "CommonModules": "commonModules",
    "Roles": "roles", "Subsystems": "subsystems", "SessionParameters": "sessionParameters",
    "ScheduledJobs": "scheduledJobs", "CommonForms": "commonForms",
}

_failures = []

def check(name, ok, detail=""):
    print(("[PASS] " if ok else "[FAIL] ") + name + (("  -- " + detail) if detail else ""))
    if not ok:
        _failures.append(name)
    return ok

def any_(seq, pred):
    return any(pred(x) for x in (seq or []))

def run_check_modules(base):
    r = subprocess.run([gb.DESIGNER, f"/F{base}", "/CheckModules"], cwd=gb.BIN,
                       capture_output=True, text=True, encoding="utf-8", errors="replace",
                       timeout=int(os.environ.get("GROW_TIMEOUT", "600")))
    log = (r.stdout or "") + "\n" + (r.stderr or "")
    return ("no errors detected" in log), log

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--limit", type=int, default=15, help="per-kind object cap (0 = no cap)")
    ap.add_argument("--keep", action="store_true", help="keep the work base after the run")
    args = ap.parse_args()

    work = os.path.join(gb.WORK, "smoke")
    os.makedirs(work, exist_ok=True)
    spec_json = os.path.join(work, "smoke.json")
    mcf = os.path.join(work, "smoke.mcf")
    base = os.path.join(work, "smokebase")

    # 1. CONVERT ------------------------------------------------------------------------------------
    ok, log = gb.convert(",".join(KINDS), spec_json, args.limit)
    if not check("convert 1C dump -> JSON", ok, log[-300:] if not ok else ""):
        return finish()
    with open(spec_json, encoding="utf-8") as f:
        spec = json.load(f)

    # 2a. COUNTS — every requested kind imported at least one object. ------------------------------
    for k in KINDS:
        arr = spec.get(KEY[k], [])
        check(f"count {k}", len(arr) > 0, f"{len(arr)} objects")

    # 2b. INVARIANTS — the imported spec is shaped, not just present. ------------------------------
    docs = spec.get("documents", [])
    check("some document carries movements (RegisterRecords)",
          any_(docs, lambda d: d.get("registerRecords")))
    check("some accounting register is bound to a chart of accounts",
          any_(spec.get("accountingRegisters", []), lambda r: r.get("chartOfAccounts")))
    check("some accumulation register has resources",
          any_(spec.get("accumulationRegisters", []), lambda r: r.get("resources")))
    check("some chart of calculation types carries predefined data",
          any_(spec.get("chartsOfCalculationTypes", []), lambda c: c.get("predefined")))
    check("some calculation register binds a chart of calculation types",
          any_(spec.get("calculationRegisters", []), lambda r: r.get("chartOfCalculationTypes")))
    check("some common form carries a control tree",
          any_(spec.get("commonForms", []), lambda f: f.get("controls")))
    check("some common module carries code",
          any_(spec.get("commonModules", []), lambda m: m.get("code")))
    check("some session parameter is typed",
          any_(spec.get("sessionParameters", []), lambda p: p.get("type") or p.get("refs")))
    check("some scheduled job is in use",
          any_(spec.get("scheduledJobs", []), lambda j: j.get("use")))

    # 3. BUILD --------------------------------------------------------------------------------------
    ok, log = gb.build_mcf(spec_json, mcf)
    if not check("build .mcf", ok, log[-300:] if not ok else ""):
        return finish()

    # 4. LOAD into a fresh Firebird base ------------------------------------------------------------
    ok, log = gb.load_base(mcf, base)
    check("load into fresh Firebird base", ok,
          "" if ok else " | ".join(l.strip() for l in log.splitlines()
                                    if any(m in l.lower() for m in gb.ERR_MARKERS))[:300])

    # 5. MODULES compile ----------------------------------------------------------------------------
    if ok:
        mok, mlog = run_check_modules(base)
        check("CheckModules (all imported modules compile)", mok,
              "" if mok else " | ".join(l.strip() for l in mlog.splitlines()
                                        if "error" in l.lower())[:300])

    if not args.keep:
        gb.kill()
    return finish()

def finish():
    print()
    if _failures:
        print(f"SMOKE FAILED: {len(_failures)} check(s) -> " + ", ".join(_failures))
        return 1
    print("SMOKE PASSED")
    return 0

if __name__ == "__main__":
    sys.exit(main())
