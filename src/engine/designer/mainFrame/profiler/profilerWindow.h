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
#include <wx/treebase.h>   // wxTreeItemId

#include "backend/debugger/debugDefs.h"   // ibProfilerReportData (held by value)

class ibTreeListCtrl;
class wxNotebook;
class wxStaticText;

class ibProfilerWindow : public wxPanel {
public:
	ibProfilerWindow(wxWindow* parent, wxWindowID id = wxID_ANY);
	virtual ~ibProfilerWindow();

	// Refresh both views. When a debug session is parked, this asks the debuggee
	// for its profiler over the debug transport (the reply arrives later via
	// LoadReport); otherwise it reads this process's own in-process profiler.
	void RefreshData();

	// Populate both views from a report received over the debug transport.
	void LoadReport(const ibProfilerReportData& data);

	// Empty both views without touching the underlying profiler data.
	void ClearView();

private:
	void OnRefresh(wxCommandEvent& event);
	void OnClear(wxCommandEvent& event);
	void OnExport(wxCommandEvent& event);

	// Read this process's own profiler into m_report (codeRunner / in-process).
	void CaptureInProcess();
	// Render both views from m_report (the single source of truth).
	void RenderFromReport();
	// Write m_report to an .xlsx (aggregate + trace on two sheets' worth of rows).
	bool ExportToXlsx(const wxString& fileName);

	wxTreeItemId AddAggRow(const wxTreeItemId& root, const wxString& proc,
		const wxString& module, unsigned long long count,
		unsigned long long selfNs, unsigned long long inclNs);

	// The last data shown — filled by CaptureInProcess or LoadReport; the source
	// for both the tree views and the XLSX export.
	ibProfilerReportData m_report;

	wxNotebook*     m_notebook   = nullptr;
	ibTreeListCtrl* m_aggCtrl    = nullptr;   // Hot spots
	ibTreeListCtrl* m_traceCtrl  = nullptr;   // Call sequence
	wxStaticText*   m_statusText = nullptr;

	wxDECLARE_EVENT_TABLE();
};

#endif // _PROFILER_WINDOW_H__
