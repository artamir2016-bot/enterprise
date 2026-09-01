#include "scriptProfiler.h"

#include <algorithm>

#include "procUnitState.h"   // out-of-line ~ibProcUnitState (owns the profiler)

ibScriptProfiler::ibScriptProfiler(std::size_t traceCap)
	: m_traceCap(traceCap)
{
}

void ibScriptProfiler::Start()
{
	// Fresh measurement. Clearing here (not in Stop) means the data survives
	// after Stop for readout, and a new Start is the single reset point.
	m_agg.clear();
	m_childStack.clear();
	m_trace.clear();
	m_dropped = 0;
	m_startTp = std::chrono::steady_clock::now();
	m_active  = true;
}

void ibScriptProfiler::Stop()
{
	// Only stops NEW frames from being profiled (the guard tests IsActive at
	// entry). Frames already in flight keep their captured decision and close
	// cleanly through OnExit, so the child-time stack stays balanced.
	m_active = false;
}

ibScriptProfiler::Frame ibScriptProfiler::OnEnter()
{
	Frame f;
	f.m_depth = (int)m_childStack.size();   // active ancestors = nesting depth
	f.m_enter = std::chrono::steady_clock::now();
	m_childStack.push_back(0);              // this frame's children-time accumulator
	return f;
}

void ibScriptProfiler::OnExit(const Frame& frame, const void* key,
                              const wxString& module, const wxString& name)
{
	const auto now = std::chrono::steady_clock::now();
	const std::uint64_t inclNs = (std::uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
		now - frame.m_enter).count();

	// Self = inclusive minus the time this frame's children accumulated.
	std::uint64_t childNs = 0;
	if (!m_childStack.empty()) {
		childNs = m_childStack.back();
		m_childStack.pop_back();
	}
	const std::uint64_t selfNs = inclNs > childNs ? inclNs - childNs : 0;

	// This frame's whole inclusive time counts as a child of its parent (the
	// new top of the stack). Runs on the unwind path too (guard dtor), so a
	// call that threw is still attributed and the stack stays balanced.
	if (!m_childStack.empty())
		m_childStack.back() += inclNs;

	ibProfileNode& node = m_agg[key];
	if (node.m_count == 0) {
		node.m_module = module;
		node.m_name   = name;
	}
	++node.m_count;
	node.m_inclNs += inclNs;
	node.m_selfNs += selfNs;

	// One trace record per completed call. Appended in COMPLETION order; the
	// call-sequence view sorts by m_enterNs (recorded here from the captured
	// entry time). Bounded — past the cap, count the drop, don't grow memory.
	if (m_trace.size() < m_traceCap) {
		ibProfileTrace rec;
		rec.m_key     = key;
		rec.m_depth   = frame.m_depth;
		rec.m_enterNs = (std::uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
			frame.m_enter - m_startTp).count();
		rec.m_durNs   = inclNs;
		m_trace.push_back(rec);
	}
	else {
		++m_dropped;
	}
}

std::vector<ibProfileNode> ibScriptProfiler::Aggregate() const
{
	std::vector<ibProfileNode> rows;
	rows.reserve(m_agg.size());
	for (const auto& kv : m_agg)
		rows.push_back(kv.second);
	std::sort(rows.begin(), rows.end(),
		[](const ibProfileNode& a, const ibProfileNode& b) {
			return a.m_selfNs > b.m_selfNs;   // hot spots first
		});
	return rows;
}

ibScriptProfiler& ibProcUnitState::EnsureProfiler()
{
	if (!m_profiler)
		m_profiler = std::make_unique<ibScriptProfiler>();
	return *m_profiler;
}

// Out-of-line ctor/dtor for the state that OWNS the profiler by unique_ptr —
// defined here where ibScriptProfiler is a complete type, so procUnitState.h
// can hold a unique_ptr to the forward-declared profiler without pulling this
// header into every translation unit that touches the state.
ibProcUnitState::ibProcUnitState()  = default;
ibProcUnitState::~ibProcUnitState() = default;
