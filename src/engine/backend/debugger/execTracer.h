#ifndef _EXEC_TRACER_H__
#define _EXEC_TRACER_H__

// ---------------------------------------------------------------------------
// ibExecTracer — statement-level execution profiler / tracer for user script.
//
// Records the ORDER in which user-code source lines actually execute (with call
// depth + timing), so a developer can SEE non-obvious control flow — e.g. an
// event handler firing mid-operation, or a chain of module procedures — as a
// ТаблицаЗначений (or a JSON table in the web client) they open.
//
// It piggybacks on the interpreter's per-instruction choke-point, right beside
// the debugger hook (`ibProcUnit::Execute`), gated so it is ZERO-COST when
// disabled (one relaxed atomic load).
//
// SCOPING. The collector is keyed by SESSION when one exists (`ibSession::
// Current()`), else by thread. The web worker pool serves a session's requests
// on DIFFERENT OS threads over time, so a per-thread buffer would not survive
// the start→action→stop flow — the per-session buffer does. A thread-local
// pointer cache makes the hot path near-lock-free (the map is only touched when
// the current session changes on a thread). The sessionless path (unit tests,
// bare interpreter) uses a plain thread-local collector.
//
// Triggers: the global script functions НачатьЗамерПроизводительности() /
// ЗакончитьЗамерПроизводительности() and the web /profile toggle.
// ---------------------------------------------------------------------------

#include <atomic>
#include <vector>
#include <cstdint>
#include <mutex>
#include <unordered_map>

#include <wx/string.h>

#include "backend/backend_core.h"

struct ibRunContext;
struct ibByteUnit;
class ibValue;
class ibSession;

// Process-wide singleton — the interpreter hot path reads it through this macro
// (mirrors `debugServer`). Get() never returns null (function-local static).
#define execTracer (ibExecTracer::Get())

class BACKEND_API ibExecTracer {
public:

	// One recorded statement — the presentation-neutral row. BuildResultTable
	// renders these into a ТаблицаЗначений; the web JSON emitter and the tests
	// read them directly, so collection is decoupled from any UI surface.
	struct ibTraceRow {
		std::uint64_t seq   = 0;
		int           depth = 0;
		int           line  = 0;   // 0-based source line as stored on the byte unit
		short         opcode = 0;
		wxString      module;
		wxString      func;
		wxString      docPath;
		// Timing (filled by Finalize before a Snapshot/Build reads the rows).
		std::int64_t  tEnterNs = 0;   // steady-clock nanoseconds at record time
		double        selfMs   = 0.0; // wall-clock gap to the next statement (own line time)
		double        totalMs  = 0.0; // inclusive time until control returns to <= this depth
	};

	static ibExecTracer* Get();

	// Hot-path gate: true only while at least one collector is active.
	bool IsEnabled() const { return m_enabledGlobal.load(std::memory_order_relaxed); }

	// Begin / end collecting for the CURRENT collector (this session if one is
	// bound, else this thread). Start clears the collector's buffer.
	void Start();
	void Stop();

	// Per-instruction record (called from the interpreter loop). Early-returns
	// unless the current collector is active; records only on a steppable
	// line-change, carrying its own `tracePrevLine` so it never disturbs the
	// debugger's.
	void Record(ibRunContext* runContext, const ibByteUnit& code, long& tracePrevLine);

	// A copy of the CURRENT collector's rows (does not stop or clear).
	std::vector<ibTraceRow> Snapshot() const;

	// Stop the current collector and materialise its trace as a ТаблицаЗначений
	// (value table). Runs in a live session (open configuration) — the script
	// return path. Returns an empty value if nothing was collected.
	ibValue BuildResultTable();

	// Drop a session's collector when the session ends (called from session
	// teardown). Safe to call for a session that never profiled.
	void DropSession(const ibSession* session);

	// A collector: the buffer + monotonic sequence for one session or thread.
	// Public only so the .cpp can declare the sessionless thread-local of this
	// exact type; callers do not use it.
	struct Collector {
		bool                    active = false;
		std::uint64_t           seq = 0;
		std::vector<ibTraceRow> rows;
	};

private:

	ibExecTracer() = default;

	// Resolve the current collector (session-keyed, else thread-local). When
	// `createForSession` and a session is bound, the session entry is created.
	Collector* CurrentCollector(bool createForSession);

	std::atomic<bool> m_enabledGlobal{ false };
	std::atomic<int>  m_activeCount{ 0 };

	// Per-session collectors. unordered_map keeps element pointers stable across
	// insert, so the thread-local cache below can hold one safely.
	mutable std::mutex                                 m_mtx;
	std::unordered_map<const ibSession*, Collector>    m_bySession;
};

#endif
