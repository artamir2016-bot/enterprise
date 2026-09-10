# OES-IMPORT: incrementally bring the 1C dump into a WORKING Firebird base.
#
# Unlike import_topasig.py (which only round-trips the .mcf in memory), this actually
# LOADS each spec into a fresh file base via `designer /F<base> /LoadCfg <mcf> /UpdateDBCfg`
# and treats the DB schema update as the pass/fail gate — that is where restructuring and
# schema-contribution bugs surface. Object-at-a-time bisection isolates a failing object.
#
# Subcommands:
#   convert <onlyKinds> <out.json>         — 1C XML -> JSON (subset of kinds)
#   load    <in.json>                       — build mcf + load into a FRESH base; print PASS/FAIL + log
#   grow    <onlyKinds>                     — add objects one-by-one, keep those that load, drop failures
import argparse, json, os, subprocess, sys, shutil

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SRC = r"E:\1CBases\Leonid\Topasig_ib\src\cf"
BIN = os.path.join(ROOT, "build", "windows-x64-release", "bin", "Release")
GEN = os.path.join(BIN, "oes_config_gen.exe")
DESIGNER = os.path.join(BIN, "designer.exe")
PY = sys.executable
WORK = os.path.join(ROOT, "testbase", "grow")
os.makedirs(WORK, exist_ok=True)

KIND_KEY = {
    "Enums": "enums", "Constants": "constants", "Catalogs": "catalogs",
    "Documents": "documents", "InformationRegisters": "informationRegisters",
    "AccumulationRegisters": "accumulationRegisters", "CommonModules": "commonModules",
    "ChartsOfCharacteristicTypes": "chartsOfCharacteristicTypes",
    "ChartsOfAccounts": "chartsOfAccounts",
    "ChartsOfCalculationTypes": "chartsOfCalculationTypes",
    "CalculationRegisters": "calculationRegisters",
    "AccountingRegisters": "accountingRegisters",
    "Roles": "roles", "Subsystems": "subsystems",
    "DataProcessors": "dataProcessors", "Reports": "reports",
    "SessionParameters": "sessionParameters", "ScheduledJobs": "scheduledJobs",
    "CommonForms": "commonForms",
}

ERR_MARKERS = ("error", "ошибк", "exception", "fail", "cannot", "не удал", "assert", "0xC0000")
OK_MARKERS  = ("Database configuration updated", "no errors detected")


def kill():
    for exe in ("enterprise.exe", "designer.exe"):
        subprocess.run(["taskkill", "/F", "/IM", exe], capture_output=True)


def convert(kinds, out_json, limit=0):
    cmd = [PY, os.path.join(HERE, "onec_to_spec.py"), SRC, out_json, "--only", kinds]
    if limit:
        cmd += ["--limit", str(limit)]
    r = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace")
    return r.returncode == 0, (r.stdout or "") + (r.stderr or "")


def build_mcf(in_json, mcf):
    r = subprocess.run([GEN, in_json, mcf], capture_output=True, text=True, encoding="utf-8", errors="replace")
    return r.returncode == 0, (r.stdout or "") + (r.stderr or "")


def load_base(mcf, base):
    """Load mcf into a FRESH base with /UpdateDBCfg. Returns (ok, log)."""
    kill()
    shutil.rmtree(base, ignore_errors=True)
    os.makedirs(base, exist_ok=True)
    r = subprocess.run([DESIGNER, f"/F{base}", "/LoadCfg", mcf, "/UpdateDBCfg"],
                       cwd=BIN, capture_output=True, text=True, encoding="utf-8", errors="replace",
                       timeout=int(os.environ.get("GROW_TIMEOUT", "600")))
    log = (r.stdout or "") + "\n" + (r.stderr or "")
    ok = ("Database configuration updated" in log) and (r.returncode == 0)
    # a crash return code is a hard fail regardless
    if (r.returncode & 0xFFFFFFFF) == 0xC0000409 or r.returncode < 0:
        ok = False
    return ok, log


def load_json_obj(in_json, base=None):
    in_json = os.path.abspath(in_json)
    base = os.path.abspath(base or os.path.join(WORK, "base"))
    mcf = os.path.splitext(in_json)[0] + ".mcf"
    ok, blog = build_mcf(in_json, mcf)
    if not ok:
        return False, "BUILD FAILED:\n" + blog
    return load_base(mcf, base)


def cmd_load(args):
    ok, log = load_json_obj(args.in_json)
    print("PASS" if ok else "FAIL")
    # print only interesting lines
    for line in log.splitlines():
        low = line.lower()
        if any(m in low for m in ERR_MARKERS) or any(m in line for m in OK_MARKERS):
            print("  " + line.strip()[:200])
    sys.exit(0 if ok else 1)


def _safe(s):
    return s.encode(sys.stdout.encoding or "utf-8", "replace").decode(sys.stdout.encoding or "utf-8", "replace")


def cmd_convert(args):
    ok, log = convert(args.kinds, args.out_json, args.limit)
    print(_safe(log[-2000:]))
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="sub", required=True)
    c = sub.add_parser("convert"); c.add_argument("kinds"); c.add_argument("out_json"); c.add_argument("--limit", type=int, default=0)
    l = sub.add_parser("load"); l.add_argument("in_json")
    args = ap.parse_args()
    if args.sub == "convert": cmd_convert(args)
    elif args.sub == "load":  cmd_load(args)
