#ifndef _PROFILER_WINDOW_H__
#define _PROFILER_WINDOW_H__

////////////////////////////////////////////////////////////////////////////
//	Description : Designer performance-profiler panel (GitHub #2)
//
//	Two views over the script profiler (backend ibScriptProfiler):
//	  * Hot spots     — the aggregate: per procedure/function, call count,
//	                    self and total time, sorted by self time descending.
//	  * Call sequence — the trace: one node per invocation, rebuilt into the
//	                    call TREE from the per-record depth, in entry order.
//
//	The data is read in-process from this session's profiler
//	(ibSession::GetPUState()->Profiler()). That is populated when configuration
//	code runs IN this process (codeRunner / an in-process run) and brackets a
//	section with StartPerformanceMeasurement() … StopPerformanceMeasurement().
//	Reading a remote debuggee's profiler over the debug transport is a
//	follow-up (see docs/performance-evaluation.md).
////////////////////////////////////////////////////////////////////////////

#include <wx/panel.h>

class ibTreeListCtrl;
class wxNotebook;
class wxStaticText;

class ibProfilerWindow : public wxPanel {
public:
	ibProfilerWindow(wxWindow* parent, wxWindowID id = wxID_ANY);
	virtual ~ibProfilerWindow();

	// Re-read the in-process profiler and repopulate both views. Safe to call
	// with no measurement taken — the views simply come up empty.
	void RefreshData();

	// Empty both views without touching the underlying profiler data.
	void ClearView();

private:
	void OnRefresh(wxCommandEvent& event);
	void OnClear(wxCommandEvent& event);

	void PopulateAggregate();
	void PopulateTrace();

	wxNotebook*     m_notebook   = nullptr;
	ibTreeListCtrl* m_aggCtrl    = nullptr;   // Hot spots
	ibTreeListCtrl* m_traceCtrl  = nullptr;   // Call sequence
	wxStaticText*   m_statusText = nullptr;

	wxDECLARE_EVENT_TABLE();
};

#endif // _PROFILER_WINDOW_H__
