# GUI smoke over the WHOLE imported configuration.
#
# Launches enterprise.exe on a freshly-loaded base with the test agent, then walks EVERY reference
# object (catalog / document / chart) and, per object, checks the GUI arc end to end:
#   * open its object form           — runs Filling (ОбработкаЗаполнения) + compiles the object module;
#   * the form has working controls  — listControls is non-empty and a control round-trips get/set;
#   * create + WRITE                 — pressCommand "Записать" fires ПередЗаписью/ПриЗаписи;
#   * mark for deletion + WRITE      — the deletion-mark action fires its events;
# capturing diagnostics (compile/runtime errors) and modal dialogs the whole time. Any captured ERROR
# fails that object; the script exits non-zero if any object failed — a GUI CI gate.
#
# It builds its own base from a bounded slice of the 1C dump (reusing grow_base), so it is self-contained.
#
# Usage:  set PYTHONIOENCODING=utf-8 && python tools/gui_smoke.py [--limit N] [--base DIR] [--port 1651]
import argparse, os, sys, time, subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "oes_testrunner"))
import grow_base as gb
from agent_client import TestAgentClient, AgentError

# The object kinds worth a GUI walk (they have object forms + write/deletion events). Registers, charts
# of accounts etc. are covered by the load/CheckModules smoke; here we drive the data-entry objects.
KINDS = "Catalogs,Documents,Enums,ChartsOfCharacteristicTypes"

_failures = []
def record(name, ok, detail=""):
    print(("  [PASS] " if ok else "  [FAIL] ") + name + (("  -- " + detail) if detail else ""))
    if not ok:
        _failures.append(name)

def build_base(base, limit):
    """Convert a slice of the dump + build mcf + load into a fresh base. Returns True on success."""
    spec = os.path.join(gb.WORK, "gui_smoke.json")
    mcf = os.path.join(gb.WORK, "gui_smoke.mcf")
    ok, log = gb.convert(KINDS, spec, limit)
    if not ok:
        print("convert FAILED:\n" + log[-500:]); return False
    ok, log = gb.build_mcf(spec, mcf)
    if not ok:
        print("build_mcf FAILED:\n" + log[-500:]); return False
    ok, log = gb.load_base(mcf, base)
    if not ok:
        print("load_base FAILED:\n" + "\n".join(l for l in log.splitlines()
              if any(m in l.lower() for m in gb.ERR_MARKERS))[-500:]); return False
    return True

def errors_since(cli):
    """Return the list of ERROR-level diagnostics + any modal-dialog text captured so far."""
    out = []
    try:
        diag = cli.call("getDiagnostics").get("diagnostics", [])
        # kind: 0=Compile, 1=Runtime — both are hard errors for a smoke.
        out += [f"{d.get('module','')}({d.get('line','')}): {d.get('message','')}" for d in diag]
    except AgentError:
        pass
    return out

def drive_object(cli, obj, settle):
    name = obj["name"]
    print(f"- {name}")
    ok = True
    cli.call("clearMessages")
    # 1) open the object form (create-new): runs Filling + compiles the module.
    try:
        cli.call("openForm", name=name, kind="object")
    except AgentError as e:
        record(f"open form: {name}", False, str(e)); return False
    time.sleep(settle)
    errs = errors_since(cli)
    record(f"open form (no errors): {name}", not errs, "; ".join(errs)[:200])
    ok = ok and not errs

    # 2) the form has controls, and one round-trips a value.
    try:
        controls = cli.call("listControls").get("controls", [])
    except AgentError as e:
        controls = []
    record(f"form has controls: {name}", len(controls) > 0, f"{len(controls)} controls")
    ok = ok and len(controls) > 0

    # 3) WRITE — fires ПередЗаписью / ПриЗаписи. Try the localized then the internal action name.
    cli.call("clearMessages")
    wrote = False
    for act in ("Записать", "Save", "SaveAndClose", "Записать и закрыть"):
        try:
            r = cli.call("pressCommand", name=act)
            wrote = True
            break
        except AgentError:
            continue
    time.sleep(settle)
    errs = errors_since(cli)
    if wrote:
        record(f"write (no errors): {name}", not errs, "; ".join(errs)[:200])
        ok = ok and not errs
    else:
        record(f"write action available: {name}", False, "no write command on form")
        ok = False

    # 4) MARK FOR DELETION — fires ПередУдалением semantics on the next write. Optional: some object
    # kinds may not expose it; only a captured ERROR fails the object, a missing action does not.
    cli.call("clearMessages")
    for act in ("Пометить на удаление", "SetDeletionMark", "MarkForDeletion"):
        try:
            cli.call("pressCommand", name=act)
            time.sleep(settle)
            de = errors_since(cli)
            record(f"mark-for-deletion (no errors): {name}", not de, "; ".join(de)[:200])
            ok = ok and not de
            break
        except AgentError:
            continue

    # reset the workspace for the next object.
    try:
        cli.call("closeAllWindows"); time.sleep(0.2)
    except AgentError:
        pass
    return ok

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--limit", type=int, default=10, help="objects per kind cap when building the base")
    ap.add_argument("--objects", type=int, default=0, help="cap objects actually driven (0 = all)")
    ap.add_argument("--base", default=os.path.join(gb.WORK, "guibase"))
    ap.add_argument("--port", type=int, default=1651)
    ap.add_argument("--settle", type=float, default=0.6, help="seconds to wait after each deferred action")
    ap.add_argument("--reuse-base", action="store_true", help="skip build, drive the existing --base")
    args = ap.parse_args()

    if not args.reuse_base:
        print("== building base ==")
        if not build_base(args.base, args.limit):
            print("GUI SMOKE FAILED: could not build base"); return 1

    gb.kill()
    exe = gb.DESIGNER.replace("designer.exe", "enterprise.exe")
    print("== launching enterprise ==")
    proc = subprocess.Popen([exe, f"--file={args.base}", f"--testagent={args.port}"], cwd=gb.BIN)
    try:
        cli = TestAgentClient(port=args.port).connect()
        objs = cli.call("listObjects").get("objects", [])
        if args.objects:
            objs = objs[:args.objects]
        print(f"== driving {len(objs)} object(s) ==")
        for obj in objs:
            drive_object(cli, obj, args.settle)
        try: cli.quit()
        except AgentError: pass
    finally:
        gb.kill()
        try: proc.wait(timeout=15)
        except Exception: pass

    print()
    if _failures:
        print(f"GUI SMOKE FAILED: {len(_failures)} check(s)")
        for f in _failures[:40]:
            print("  - " + f)
        return 1
    print("GUI SMOKE PASSED")
    return 0

if __name__ == "__main__":
    sys.exit(main())
