#ifndef __IB_SCRIPT_PROFILER_H__
#define __IB_SCRIPT_PROFILER_H__

// =============================================================================
// Script-code profiler — per-session call counts + time, and a call-sequence
// trace, for configuration code running on the bytecode interpreter.
//
// Two products (GitHub #2):
//   * AGGREGATE  — per procedure/function: call count, inclusive time (with
//                  nested calls) and self time (without), from which average
//                  and %-of-total are derived at readout.
//   * TRACE      — the call sequence: one record per invocation, each carrying
//                  its entry time (m_enterNs), depth and inclusive duration, so
//                  "who called whom, when, for how long" is readable. Records
//                  are stored in completion order; sort by m_enterNs for the
//                  call-order view. Bounded — a long run truncates rather than
//                  growing memory without limit, and says so (GetTraceDropped()).
//
// Driven from ibProcStackGuard (procUnit.cpp), which brackets EVERY interpreter
// Execute (named call, lambda, module body). When no profiler is active the
// guard pays a single null-pointer test — the measurement is strictly opt-in.
//
// GUI-free: lives in backend.dll. Readout is plain structs the frontend renders.
// =============================================================================

#include <chrono>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include <wx/string.h>

#include "backend/backend.h"   // BACKEND_API

// One aggregate row — a procedure/function (or a module body).
struct ibProfileNode {
	wxString      m_module;      // owning module (bytecode) name
	wxString      m_name;        // function name; empty ⇒ module body
	std::uint64_t m_count  = 0;  // number of invocations
	std::uint64_t m_inclNs = 0;  // inclusive time (with nested calls)
	std::uint64_t m_selfNs = 0;  // self time (inclusive minus children)
};

// One trace record — a single invocation, in call order.
struct ibProfileTrace {
	const void*   m_key    = nullptr;  // aggregate key (resolve name via the map)
	int           m_depth  = 0;        // call nesting depth (0 = outermost profiled)
	std::uint64_t m_enterNs = 0;       // nanoseconds since the profiling session started
	std::uint64_t m_durNs   = 0;       // inclusive duration; filled on exit
};

class BACKEND_API ibScriptProfiler {
public:
	explicit ibScriptProfiler(std::size_t traceCap = 200000);

	// Lifecycle. Start clears prior data and begins measuring; Stop ends
	// measuring but keeps the collected data for readout. Both are safe to
	// call between runs (i.e. with the call stack empty).
	void Start();
	void Stop();
	bool IsActive() const { return m_active; }

	// --- guard-side hooks (hot path; only reached when a profiler is active) --
	// A frame token the guard keeps between enter and exit. Trivially copyable.
	//
	// Identity (key/name/module) is supplied at EXIT, not entry: a named
	// function's ibRunContext::m_currentFunction is stamped by its OPER_FUNC
	// opcode INSIDE the body, so it is not yet known when the guard is built —
	// but it is known by the time the guard is destroyed. So OnEnter only starts
	// the clock and the child-time frame; OnExit does the attribution.
	struct Frame {
		std::chrono::steady_clock::time_point m_enter{};
		int                                   m_depth = 0;   // active-ancestor count
	};
	Frame OnEnter();
	// key: the ibByteFunction* for a named function, or the ibByteCode* for a
	// module body (both are stable, distinct addresses within a loaded config).
	// A null key ⇒ module body (name empty).
	void  OnExit(const Frame& frame, const void* key,
	             const wxString& module, const wxString& name);

	// --- readout -------------------------------------------------------------
	// Aggregate rows, sorted by self time descending (hot spots first).
	std::vector<ibProfileNode>          Aggregate() const;
	const std::vector<ibProfileTrace>&  Trace()        const { return m_trace; }
	std::size_t                         GetTraceDropped() const { return m_dropped; }
	// Resolve a trace record's m_key back to its owning module / function name
	// (a null key ⇒ module body: module set, name empty). False when the key is
	// unknown — should not happen for a key that came out of Trace().
	bool ResolveKey(const void* key, wxString& module, wxString& name) const;

private:
	bool                                              m_active = false;
	std::chrono::steady_clock::time_point             m_startTp{};
	std::unordered_map<const void*, ibProfileNode>    m_agg;
	// Per active frame: nanoseconds spent in its children so far. Self time of a
	// frame = its inclusive time minus the top-of-stack value when it exits.
	std::vector<std::uint64_t>                        m_childStack;
	std::vector<ibProfileTrace>                       m_trace;
	std::size_t                                       m_traceCap;
	std::size_t                                       m_dropped = 0;
};

#endif // __IB_SCRIPT_PROFILER_H__
