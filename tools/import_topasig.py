"""OES-TEST/IMPORT: batched importer + self-test for the Topasig 1C dump.

Imports the 1C configuration at E:\\1CBases\\Leonid\\Topasig_ib\\src\\cf into an OES .mcf a few
objects at a time and TESTS each batch:
  1. convert  (onec_to_spec.py)  — 1C XML -> friendly JSON
  2. build    (oes_config_gen)   — JSON -> .mcf
  3. reload   (--verify)         — .mcf round-trips (loads back)
  4. counts   — object count in the produced JSON matches the requested batch size

Usage:
  python tools/import_topasig.py --kinds Catalogs --limit 5
  python tools/import_topasig.py --kinds Catalogs,Documents,Enums --limit 10
  python tools/import_topasig.py --all --limit 20         # small slice of every kind
Exit code 0 = all batch checks passed, 1 = a check failed.
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SRC = r"E:\1CBases\Leonid\Topasig_ib\src\cf"
OUT = os.path.join(ROOT, "testbase", "topasig")
GEN = os.path.join(ROOT, "build", "windows-x64-release", "bin", "Release", "oes_config_gen.exe")
PY = sys.executable

# JSON spec key per 1C dump kind (matches onec_to_spec.py output).
KIND_KEY = {
    "Catalogs": "catalogs",
    "Documents": "documents",
    "Enums": "enums",
    "Constants": "constants",
    "InformationRegisters": "informationRegisters",
    "AccumulationRegisters": "accumulationRegisters",
    "CommonModules": "commonModules",
}
ALL_KINDS = list(KIND_KEY.keys())


def _src_count(kind: str) -> int:
    d = os.path.join(SRC, kind)
    if not os.path.isdir(d):
        return 0
    return len([f for f in os.listdir(d) if f.endswith(".xml")])


def run_batch(kinds: list[str], limit: int, name: str) -> bool:
    os.makedirs(OUT, exist_ok=True)
    spec = os.path.join(OUT, f"{name}.json")
    mcf = os.path.join(OUT, f"{name}.mcf")
    env = dict(os.environ, PYTHONIOENCODING="utf-8")

    print(f"\n=== batch '{name}': kinds={kinds} limit={limit} ===")

    # 1. convert
    cmd = [PY, os.path.join(HERE, "onec_to_spec.py"), SRC, spec, "--only", ",".join(kinds)]
    if limit:
        cmd += ["--limit", str(limit)]
    r = subprocess.run(cmd, env=env, capture_output=True, text=True, encoding="utf-8")
    if r.returncode != 0:
        print("  [FAIL] convert:", r.stderr[-500:])
        return False
    print("  [ok] convert")

    # 2. build
    r = subprocess.run([GEN, spec, mcf, "--verify"], capture_output=True, text=True, encoding="utf-8")
    out = (r.stdout or "") + (r.stderr or "")
    if r.returncode != 0 or "VERIFY: reloaded OK" not in out:
        print("  [FAIL] build/verify:", out[-500:])
        return False
    print("  [ok] build + reload (--verify)")

    # 3. counts: JSON objects per kind == min(source, limit)
    with open(spec, encoding="utf-8") as f:
        data = json.load(f)
    ok = True
    for k in kinds:
        got = len(data.get(KIND_KEY[k], []))
        exp = _src_count(k)
        if limit:
            exp = min(exp, limit)
        status = "ok" if got == exp else "FAIL"
        if got != exp:
            ok = False
        print(f"  [{status}] count {k}: json={got} expected={exp}")
    return ok


def main() -> int:
    ap = argparse.ArgumentParser(description="Batched Topasig 1C import with per-batch self-tests")
    ap.add_argument("--kinds", default="Catalogs", help="comma list: " + ",".join(ALL_KINDS))
    ap.add_argument("--all", action="store_true", help="all supported kinds")
    ap.add_argument("--limit", type=int, default=5, help="max objects per kind (0 = all)")
    ap.add_argument("--name", default=None, help="output batch name (spec/.mcf basename)")
    args = ap.parse_args()

    kinds = ALL_KINDS if args.all else [k.strip() for k in args.kinds.split(",") if k.strip()]
    bad = [k for k in kinds if k not in KIND_KEY]
    if bad:
        print("unknown kinds:", bad, "-- supported:", ALL_KINDS)
        return 2
    name = args.name or ("all" if args.all else "_".join(kinds).lower()) + (f"_{args.limit}" if args.limit else "")

    ok = run_batch(kinds, args.limit, name)
    print("\nИТОГ:", "PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
