"""OES-IMPORT: bisect which preprocessed MANAGER module crashes the compiler at base open.

Strategy: keep manager modules only for a subset of objects (strip the rest), build a .mcf, load a
fresh base, launch enterprise and see whether it crashes at open (0xC0000409) or comes up. Binary
search the subset down to the single crashing module.
"""
import json, os, subprocess, sys, time, shutil

ROOT = r"E:\Projects\OES"
BIN = os.path.join(ROOT, r"build\windows-x64-release\bin\Release")
GEN = os.path.join(BIN, "oes_config_gen.exe")
SPEC = os.path.join(ROOT, r"testbase\topasig\catdoc_raw.json")   # preprocessed, with managers
WORK = os.path.join(ROOT, r"testbase\topasig\bisect.json")
MCF = os.path.join(ROOT, r"testbase\topasig\bisect.mcf")
BASE = os.path.join(ROOT, r"testbase\bisect_base")

full = json.load(open(SPEC, encoding="utf-8"))

# objects (catalogs+documents) that carry a manager module
mgr_objs = []
for k in ("catalogs", "documents"):
    for o in full.get(k, []):
        if o.get("managerModule"):
            mgr_objs.append((k, o["name"]))


def build(keep: set) -> None:
    d = json.loads(json.dumps(full))
    for k in ("catalogs", "documents"):
        for o in d.get(k, []):
            if (k, o["name"]) not in keep and "managerModule" in o:
                del o["managerModule"]
    json.dump(d, open(WORK, "w", encoding="utf-8"), ensure_ascii=False)
    subprocess.run([GEN, WORK, MCF], capture_output=True)


def test_open(keep: set) -> bool:
    """True = CRASHES at open; False = opens fine."""
    build(keep)
    subprocess.run(["taskkill", "/F", "/IM", "enterprise.exe"], capture_output=True)
    subprocess.run(["taskkill", "/F", "/IM", "designer.exe"], capture_output=True)
    shutil.rmtree(BASE, ignore_errors=True)
    os.makedirs(BASE, exist_ok=True)
    subprocess.run([os.path.join(BIN, "designer.exe"), f"/F{BASE}", "/LoadCfg", MCF, "/UpdateDBCfg"],
                   cwd=BIN)
    p = subprocess.Popen([os.path.join(BIN, "enterprise.exe"), f"--file={BASE}",
                          "--testagent=1799", "--minimized"], cwd=BIN)
    crashed = False
    for _ in range(24):   # up to ~24s
        time.sleep(1)
        rc = p.poll()
        if rc is not None:
            crashed = (rc & 0xFFFFFFFF) == 0xC0000409
            break
    try:
        p.terminate()
    except Exception:
        pass
    subprocess.run(["taskkill", "/F", "/IM", "enterprise.exe"], capture_output=True)
    return crashed


def main():
    print(f"manager-bearing objects: {len(mgr_objs)}")
    # sanity: full set should crash
    cur = list(mgr_objs)
    if not test_open(set(cur)):
        print("NOTE: full manager set did NOT crash — nothing to bisect (or flaky).")
        return
    print("full set crashes — bisecting…")
    while len(cur) > 1:
        half = cur[: len(cur) // 2]
        crashed = test_open(set(half))
        print(f"  test {len(half)} of {len(cur)}: {'CRASH' if crashed else 'ok'}")
        cur = half if crashed else cur[len(cur) // 2:]
    print("CULPRIT manager module:", cur[0])
    open(os.path.join(ROOT, r"testbase\topasig\culprit_mgr.txt"), "w", encoding="utf-8").write(str(cur[0]))


if __name__ == "__main__":
    main()
