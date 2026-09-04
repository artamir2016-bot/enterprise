#!/usr/bin/env python3
# OES-TEST (GitHub #2): end-to-end GUI proof that the script profiler works
# DRIVEN FROM THE CONFIGURATOR (Designer).
#
# Full debug choreography, all through the Designer + its test agent:
#   1. launch the Designer (--testagent) on the demo base;
#   2. set two breakpoints in Товары.ObjectModule (before / after the measured work);
#   3. Debug -> Start debugging -> Thick client  (F5) — spawns enterprise.exe as the
#      debuggee, auto-instrumented with its own --testagent (RunApplication passthrough);
#   4. in enterprise: open the Товары object form and press Save -> ПередЗаписью runs
#      (non-eval) -> parks at breakpoint #1;
#   5. Debug -> Start performance measurement  (drives the debuggee's profiler over the
#      debug transport);
#   6. Debug -> Continue -> the object module calls Работа() three times -> parks at #2;
#   7. Debug -> Stop performance measurement; Debug -> Performance profiler; Refresh;
#   8. read the panel's Hot-spots ibTreeListCtrl and ASSERT Работа was measured 3x.
#
# Usage:  python test_profiler_from_configurator.py [--bin DIR] [--base DIR]
# Exit 0 = pass. Requires the demo base with the Работа driver in Товары.ObjectModule.

import argparse, os, subprocess, sys, time
from agent_client import TestAgentClient

DESIGNER_PORT = 1652   # enterprise debuggee gets DESIGNER_PORT-1 (1651) via passthrough
ENTERPRISE_PORT = 1651


def _call(port, cmd, **args):
    # One-shot: the agent socket resets when a debug session parks, so reconnect per call.
    last = None
    for _ in range(3):
        try:
            c = TestAgentClient(port=port).connect(retries=2, delay=0.4)
            r = c.call(cmd, **args); c.close(); return r
        except Exception as ex:
            last = ex; time.sleep(0.5)
    raise last

def dcall(cmd, **a): return _call(DESIGNER_PORT, cmd, **a)

def ecall(cmd, **a):
    try: return _call(ENTERPRISE_PORT, cmd, **a)
    except Exception as ex: return {'_err': str(ex)}   # parks abort the socket — expected

def wait_parked(want=True, timeout=30.0):
    t0 = time.time()
    while time.time() - t0 < timeout:
        try:
            st = dcall('debugState')
            if st.get('parked') == want: return st
        except Exception: pass
        time.sleep(0.5)
    return dcall('debugState')

def wait_agent(port, timeout=40.0):
    t0 = time.time()
    while time.time() - t0 < timeout:
        try:
            c = TestAgentClient(port=port).connect(retries=1, delay=0.5); c.close(); return True
        except Exception: time.sleep(0.5)
    return False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--bin', default=r'E:\Projects\OES\build-perf\bin\Release')
    ap.add_argument('--base', default=r'E:\Projects\OES\testbase\demo_ru_base')
    args = ap.parse_args()

    designer = os.path.join(args.bin, 'designer.exe')
    proc = subprocess.Popen([designer, f'--file={args.base}', f'--testagent={DESIGNER_PORT}'],
                            cwd=args.bin)
    ok = False
    try:
        if not wait_agent(DESIGNER_PORT):
            print('FAIL: designer agent did not come up'); return 2

        print('bp1:', dcall('setBreakpoint', catalog='Товары', line=1))   # before Работа calls
        print('bp2:', dcall('setBreakpoint', catalog='Товары', line=4))   # after Работа calls
        print('startDebug:', dcall('invokeMenu', path=['Debug', 'Start debugging', 'Thick client (GUI)']))

        if not wait_agent(ENTERPRISE_PORT):
            print('FAIL: enterprise debuggee agent did not come up'); return 2
        print('enterprise:', ecall('appInfo'))
        time.sleep(2)

        print('openForm:', ecall('openForm', name='Товары', kind='object')); time.sleep(2.5)
        print('save:', ecall('pressCommand', name='Save'))   # -> ПередЗаписью -> park

        st = wait_parked(True); print('PARK #1:', st)
        assert st.get('parked'), 'did not park at breakpoint #1'

        print('start measure:', dcall('invokeMenu', path=['Debug', 'Start performance measurement']))
        print('continue:', dcall('invokeMenu', path=['Debug', 'Continue']))
        st = wait_parked(True); print('PARK #2:', st)
        assert st.get('parked'), 'did not park at breakpoint #2'
        print('stop measure:', dcall('invokeMenu', path=['Debug', 'Stop performance measurement']))

        print('open panel:', dcall('invokeMenu', path=['Debug', 'Performance profiler'])); time.sleep(1)
        print('refresh:', dcall('clickWidget', by='label', value='Refresh')); time.sleep(1.5)

        agg = dcall('readTreeList', name='profilerAgg')
        print('AGG ROWS:', agg)
        rabota = [r for r in agg.get('rows', []) if r and r[0] == 'Работа']
        assert rabota, 'Работа not present in the profiler panel'
        # column 2 is the call count; Работа is called 3x between the breakpoints.
        assert rabota[0][2] == '3', f'expected Работа count 3, got {rabota[0][2]}'
        print('PASS: profiler measured Работа x3, driven from the Configurator.')
        ok = True
        return 0
    finally:
        try: proc.kill()
        except Exception: pass
        for pn in ('enterprise.exe',):
            subprocess.run(['taskkill', '/F', '/IM', pn], capture_output=True)
        print('RESULT:', 'PASS' if ok else 'FAIL')


if __name__ == '__main__':
    sys.exit(main())
