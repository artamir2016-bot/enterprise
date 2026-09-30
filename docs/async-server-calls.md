# Async cancelable server calls (client → server as background jobs)

## Goal
A client-env call to a `&НаСервере` procedure must not freeze the UI. Fast calls stay
inline; slow ones auto-promote to a **Tenant** background job tied to the session, with
the form in a **busy-lock + Cancel** state. User cancel routes to the interpreter's
cooperative interrupt.

## Decisions (confirmed 2026-09-29)
- **Promotion = by threshold (auto).** `/srv-call` runs on the worker with a short grace
  window (~150 ms). Completes in time → return result inline (zero regression for the
  common fast call). Exceeds it → return `{pending, jobId}`, keep running as a Tenant
  `ibBackgroundRun`. No application-code change, no new directive.
- **Form UX = busy-lock + Cancel.** While the job runs the form disables input and shows
  an overlay «Выполняется… [Отмена]»; on completion it applies the returned context.
  Avoids the edit-vs-returned-context clobber problem entirely.

## Existing primitives (all built — this is assembly)
- Worker pool, per-session FIFO + lease: `ibWorkerPoolHeadless`.
- Cooperative cancel: `ibSession::RequestCancel()/IsCancelRequested()`, polled every
  ~1024 opcodes in `procUnit.cpp:845`; throws `ibBackendInterruptException`.
- Background run w/ own session+connection: `ibJobManager::StartBackground(proc,args)` →
  `ibBackgroundRun{IsComplete,Wait,Result,Error,Cancel,Activity}`; `ibJobTenancy::Tenant`
  ties lifetime to the parent session.
- SSE push: `/stream` + `ibWebApplication::MarkDirty()` / `WaitForChange()`.
- Client already async: `serverHop()` Promise, `OES.srvCall` await-fetch (clientvm.js /
  client.html).
- Blocker to remove: `/srv-call` and `/form-action` call `RunOnWorker(...).get()` on the
  HTTP handler thread (wfrontend.cpp:1517 / 1313).

## Increments
### Inc 1 — non-blocking `/srv-call` + busy-lock/cancel (web)
- Server: `ServerCallInSession` submits the proc to the worker, waits on the future with a
  ~150 ms grace. Done → return `{context, ret}` as today. Not done → register the running
  future as a Tenant background run keyed by a new `jobId`, return `{pending:true, jobId}`.
  New route `POST /job/<jobId>/cancel` → `session->RequestCancel()` (or run->Cancel()).
  Completion pushes via `MarkDirty()`; a `GET /job/<jobId>` (or an SSE frame tagged with
  jobId) delivers the final `{context, ret}`.
- Client (clientvm.js `serverHop` + client.html transport): if the response is `pending`,
  enter form busy-lock, show «Выполняется… [Отмена]», subscribe to /stream (or poll
  /job), apply context + resume the VM on completion, or surface the interrupt on cancel.
- Verify: a `&НаСервере` proc with a deliberate long loop → form locks with a live Cancel,
  the rest of the app stays responsive, Cancel stops it (interrupt), fast procs unchanged.

### Inc 2 — progress
- Script API `Состояние(text, percent)` pushes a job-tagged SSE frame; overlay renders the
  bar + text. Reuses MarkDirty/WaitForChange.

### Inc 3 — form-command path
- `/form-action` (long list/report commands) gets the same threshold + busy-lock/cancel.

### Inc 4 — desktop parity (frontend.dll)
- Same job+cancel plumbing where the freeze is a real same-thread block; reuse
  `ibBackgroundRun` + a modal-with-cancel long-operation window.

## Status (2026-09-30)

- **Inc 1 — shipped & live-verified** (commit `71ba99d4`): threshold promotion,
  busy-lock overlay «Выполняется… [Отмена]», poll, and cancel confirmed in the
  browser (cancel interrupted a 300M-iteration server loop, UI unblocked).
- **List-form fix — shipped** (commit `311c266f`): ordinary `type:"list"` forms now
  get a dynamic-list main source + a synthesized tablebox, so they open, adopt a
  tab, and surface the standard command bar. Unblocked the async test stand.
- **Web publication — shipped** (commit `6245e2f3`): `--publish=iis|apache|both`.

### Inc 2 — progress indicator (ShowStatus / Состояние): CODE COMPLETE, HELD (uncommitted WIP in a git stash)
Implements: `ibSession::SetProgress/GetProgress/ClearProgress` (mutex + atomic, no
DB); the `ShowStatus(text[,percent])` / `Состояние` script proc → session progress;
`/job/<id>` poll returns `{ pending, progress:{text,percent} }`; the client overlay
renders a progress bar (`OES.updateBusy`). Builds green. Held because of the two
findings below.

### Two findings from Inc 2 debugging
1. **`Object 'Picture' is exist` at web config-open — ROOT-CAUSED & FIXED (in the WIP).**
   Adding `#include "backend/session/session.h"` to `systemManager.cpp` perturbed
   static-init and caused a duplicate metatype registration at open. Fix: call the
   session via a forward-declared free helper `ibReportCurrentSessionProgress`
   (defined in session.cpp) instead of including the header. With that, bases open.
   *Lesson: do not pull session.h into systemManager.cpp.*
2. **Pre-existing intermittent web-server crash on login/open — NEW, TOP PRIORITY.**
   `wenterprise-server` crashes intermittently on the login → open-meta path even
   on **HEAD with no Inc 2** (measured ~2/5 runs; no minidump ⇒ a fast-fail, i.e.
   heap/stack corruption or a race, not a normal AV). Inc 2's `ibSession` layout
   change (three new members) appears to **aggravate** it (~6/6), which is why Inc 2
   is held: it must not ship until this underlying crash is fixed. This is the
   blocker for reliable web operation AND for live web publication.

**Next steps:** (a) fix the pre-existing login/open crash (run under a debugger to
catch the fast-fail; suspect the web session/worker-pool startup or an
uninitialized/racy field); (b) re-verify Inc 2 (progress bar) on the now-stable
stack and commit it (`git stash pop` restores the WIP).

## Open questions (per increment)
- Grace-window value (150 ms start; tune).
- Delivery of the final result: dedicated `GET /job/<id>` poll vs SSE frame tagged with
  jobId (lean SSE, poll fallback — mirrors the existing /stream+poll duo).
- Cancel granularity: native/CPU-bound or blocking-I/O sections are not interruptible
  (cooperative only) — document the limit, same as today's script cancel.
