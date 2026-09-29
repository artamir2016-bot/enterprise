#ifndef _EXEC_TRACER_H__
#define _EXEC_TRACER_H__

// ---------------------------------------------------------------------------
// ibExecTracer — statement-level execution profiler / tracer for user script.
//
// Records the ORDER in which user-code source lines actually execute (with call
// depth + timing), so a developer can SEE non-obvious control flow — e.g. an
// event handler firing mid-operation, or a chain of module procedures — as a
// ТаблицаЗначений they open in any form.
//
// It piggybacks on the interpreter's per-instruction choke-point, right beside
// the debugger hook (`ibProcUnit::Execute` → procUnit.cpp), gated so it is
// ZERO-COST when disabled (one relaxed atomic load). The buffer is thread_local
// (each session runs on its own worker thread — no cross-session contamination,
// no locks on the hot path). Start/Stop/Build all run on the collecting thread.
//
// Triggers: the global script functions НачатьЗамерПроизводительности() /
// ЗакончитьЗамерПроизводительности() (systemManager) and, later, a web UI toggle.
// ---------------------------------------------------------------------------

#include <atomic>
#include <vector>
#include <cstdint>

#include <wx/string.h>

#include "backend/backend_core.h"

struct ibRunContext;
struct ibByteUnit;
class ibValue;

// Process-wide singleton — the interpreter hot path reads it through this macro
// (mirrors `debugServer`). Get() never returns null (function-local static).
#define execTracer (ibExecTracer::Get())

class BACKEND_API ibExecTracer {
public:

	// One recorded statement — the presentation-neutral row. BuildResultTable
	// renders these into a ТаблицаЗначений; the web JSON emitter (Inc 2) and the
	// tests read them directly, so collection is decoupled from any UI surface.
	struct ibTraceRow {
		std::uint64_t seq   = 0;
		int           depth = 0;
		int           line  = 0;   // 0-based source line as stored on the byte unit
		short         opcode = 0;
		wxString      module;
		wxString      func;
		wxString      docPath;
	};

	static ibExecTracer* Get();

	// Hot-path gate: true only while at least one thread is collecting.
	bool IsEnabled() const { return m_enabledGlobal.load(std::memory_order_relaxed); }

	// Begin / end collecting on THE CALLING worker thread. Start clears this
	// thread's buffer; Stop flips it off (and clears the global gate when the
	// last collecting thread stops).
	void StartThisThread();
	void StopThisThread();

	// Per-instruction record (called from the interpreter loop). Early-returns
	// unless this thread is active; records only on a steppable line-change,
	// carrying its own `tracePrevLine` so it never disturbs the debugger's.
	void Record(ibRunContext* runContext, const ibByteUnit& code, long& tracePrevLine);

	// A copy of THIS thread's collected rows (does not stop or clear). The tests
	// and the web JSON emitter read the trace through this — no value-table /
	// configuration dependency.
	std::vector<ibTraceRow> SnapshotThisThread() const;

	// Stop this thread and materialise the collected trace as a ТаблицаЗначений
	// (value table). Returns an empty value if nothing was collected. Runs in a
	// live session (open configuration) — the script return path.
	ibValue BuildResultTable();

private:

	ibExecTracer() = default;

	// True while ≥1 thread collects; the hot-path gate. m_activeThreads tracks
	// how many threads are collecting so the gate clears on the last Stop.
	std::atomic<bool> m_enabledGlobal{ false };
	std::atomic<int>  m_activeThreads{ 0 };
};

#endif
