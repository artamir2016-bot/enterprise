////////////////////////////////////////////////////////////////////////////
//	Description : Designer performance-profiler panel (GitHub #2)
////////////////////////////////////////////////////////////////////////////

#include "profilerWindow.h"

#include <algorithm>
#include <vector>

#include <wx/sizer.h>
#include <wx/button.h>
#include <wx/notebook.h>
#include <wx/stattext.h>

#include "frontend/win/ctrls/treelistctrl.h"

#include "backend/compiler/scriptProfiler.h"
#include "backend/compiler/procUnitState.h"
#include "backend/session/session.h"
#include "backend/debugger/debugClient.h"      // request over the debug transport
#include "backend/debugger/debugDefs.h"        // ibProfilerReportData

enum {
	wxID_PROFILER_REFRESH = wxID_HIGHEST + 4200,
	wxID_PROFILER_CLEAR,
};

wxBEGIN_EVENT_TABLE(ibProfilerWindow, wxPanel)
	EVT_BUTTON(wxID_PROFILER_REFRESH, ibProfilerWindow::OnRefresh)
	EVT_BUTTON(wxID_PROFILER_CLEAR, ibProfilerWindow::OnClear)
wxEND_EVENT_TABLE()

// Milliseconds text from a nanosecond count.
static wxString MsText(std::uint64_t ns)
{
	return wxString::Format(wxT("%.3f"), double(ns) / 1e6);
}

ibProfilerWindow::ibProfilerWindow(wxWindow* parent, wxWindowID id)
	: wxPanel(parent, id)
{
	wxBoxSizer* topSizer = new wxBoxSizer(wxVERTICAL);

	// --- toolbar row ---------------------------------------------------------
	wxBoxSizer* toolRow = new wxBoxSizer(wxHORIZONTAL);
	wxButton* btnRefresh = new wxButton(this, wxID_PROFILER_REFRESH, _("Refresh"));
	wxButton* btnClear   = new wxButton(this, wxID_PROFILER_CLEAR, _("Clear"));
	toolRow->Add(btnRefresh, 0, wxALL, 3);
	toolRow->Add(btnClear, 0, wxALL, 3);
	m_statusText = new wxStaticText(this, wxID_ANY, wxEmptyString);
	toolRow->Add(m_statusText, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, 8);
	topSizer->Add(toolRow, 0, wxEXPAND);

	// --- two views in a notebook --------------------------------------------
	m_notebook = new wxNotebook(this, wxID_ANY);

	const long treeStyle = wxTR_HIDE_ROOT | wxTR_FULL_ROW_HIGHLIGHT
	                     | wxTR_ROW_LINES | wxTR_HAS_BUTTONS;

	m_aggCtrl = new ibTreeListCtrl(m_notebook, wxID_ANY,
		wxDefaultPosition, wxDefaultSize, treeStyle, wxDefaultValidator, wxT(""));
	m_aggCtrl->AddColumn(_("Procedure"), 220, wxALIGN_LEFT);
	m_aggCtrl->AddColumn(_("Module"),    180, wxALIGN_LEFT);
	m_aggCtrl->AddColumn(_("Calls"),      80, wxALIGN_RIGHT);
	m_aggCtrl->AddColumn(_("Self, ms"),  100, wxALIGN_RIGHT);
	m_aggCtrl->AddColumn(_("Total, ms"), 100, wxALIGN_RIGHT);

	m_traceCtrl = new ibTreeListCtrl(m_notebook, wxID_ANY,
		wxDefaultPosition, wxDefaultSize, treeStyle, wxDefaultValidator, wxT(""));
	m_traceCtrl->AddColumn(_("Procedure"),   260, wxALIGN_LEFT);
	m_traceCtrl->AddColumn(_("Module"),      180, wxALIGN_LEFT);
	m_traceCtrl->AddColumn(_("Enter, ms"),   100, wxALIGN_RIGHT);
	m_traceCtrl->AddColumn(_("Duration, ms"),110, wxALIGN_RIGHT);

	m_notebook->AddPage(m_aggCtrl, _("Hot spots"), true);
	m_notebook->AddPage(m_traceCtrl, _("Call sequence"), false);

	topSizer->Add(m_notebook, 1, wxEXPAND);
	SetSizer(topSizer);

	RefreshData();
}

ibProfilerWindow::~ibProfilerWindow()
{
}

// Resolve this session's profiler in-process. Null when no measurement exists.
static ibScriptProfiler* CurrentProfiler()
{
	ibProcUnitState* state = ibSession::GetPUState();
	return state != nullptr ? state->Profiler() : nullptr;
}

void ibProfilerWindow::ClearView()
{
	m_aggCtrl->DeleteRoot();
	m_traceCtrl->DeleteRoot();
	if (m_statusText != nullptr)
		m_statusText->SetLabel(wxEmptyString);
}

wxTreeItemId ibProfilerWindow::AddAggRow(const wxTreeItemId& root, const wxString& proc,
	const wxString& module, unsigned long long count,
	unsigned long long selfNs, unsigned long long inclNs)
{
	const wxTreeItemId item = m_aggCtrl->AppendItem(root,
		proc.IsEmpty() ? wxString(_("<module body>")) : proc);
	m_aggCtrl->SetItemText(item, 1, module);
	m_aggCtrl->SetItemText(item, 2, wxString::Format(wxT("%llu"), count));
	m_aggCtrl->SetItemText(item, 3, MsText(selfNs));
	m_aggCtrl->SetItemText(item, 4, MsText(inclNs));
	return item;
}

// Append one trace node under the correct parent, keeping the per-depth cursor
// (lastAtDepth) so a record at depth d hangs under the most recent d-1 record.
static void AppendTraceNode(ibTreeListCtrl* ctrl, const wxTreeItemId& root,
	std::vector<wxTreeItemId>& lastAtDepth,
	const wxString& proc, const wxString& module, int depthIn,
	unsigned long long enterNs, unsigned long long durNs)
{
	const int depth = depthIn < 0 ? 0 : depthIn;
	wxTreeItemId parent = root;
	if (depth > 0 && (size_t)depth <= lastAtDepth.size())
		parent = lastAtDepth[depth - 1];

	const wxTreeItemId item = ctrl->AppendItem(parent,
		proc.IsEmpty() ? wxString(_("<module body>")) : proc);
	ctrl->SetItemText(item, 1, module);
	ctrl->SetItemText(item, 2, MsText(enterNs));
	ctrl->SetItemText(item, 3, MsText(durNs));

	if ((size_t)depth >= lastAtDepth.size())
		lastAtDepth.resize(depth + 1);
	lastAtDepth[depth] = item;
	lastAtDepth.resize(depth + 1);   // drop any deeper stale entries
}

void ibProfilerWindow::RefreshData()
{
	// If a session is parked in the debug loop, the meaningful profiler lives in
	// the DEBUGGEE, not here. Ask it over the transport; LoadReport applies the
	// reply when it arrives.
	ibDebuggerClient* dbg = ibDebuggerClient::Get();
	if (dbg != nullptr && dbg->IsEnterLoop()) {
		if (m_statusText != nullptr)
			m_statusText->SetLabel(_("Requesting from debuggee…"));
		dbg->RequestProfilerData();
		return;
	}

	// Otherwise read this process's own in-process profiler.
	PopulateAggregate();
	PopulateTrace();

	ibScriptProfiler* prof = CurrentProfiler();
	if (m_statusText != nullptr) {
		if (prof == nullptr)
			m_statusText->SetLabel(_("No measurement — run code with StartPerformanceMeasurement()"));
		else if (prof->GetTraceDropped() != 0)
			m_statusText->SetLabel(wxString::Format(
				_("Trace truncated: %zu calls dropped"), prof->GetTraceDropped()));
		else
			m_statusText->SetLabel(prof->IsActive() ? _("Measuring…") : _("Ready"));
	}
}

void ibProfilerWindow::LoadReport(const ibProfilerReportData& data)
{
	// Aggregate — already sorted by self time on the debuggee side.
	m_aggCtrl->DeleteRoot();
	const wxTreeItemId aggRoot = m_aggCtrl->AddRoot(wxEmptyString);
	for (const ibProfilerReportData::AggRow& r : data.m_agg)
		AddAggRow(aggRoot, r.m_name, r.m_module, r.m_count, r.m_selfNs, r.m_inclNs);

	// Trace — completion order on the wire; sort by entry time for call order.
	m_traceCtrl->DeleteRoot();
	const wxTreeItemId traceRoot = m_traceCtrl->AddRoot(wxEmptyString);
	std::vector<ibProfilerReportData::TraceRow> recs = data.m_trace;
	std::sort(recs.begin(), recs.end(),
		[](const ibProfilerReportData::TraceRow& a, const ibProfilerReportData::TraceRow& b) {
			return a.m_enterNs < b.m_enterNs;
		});
	std::vector<wxTreeItemId> lastAtDepth;
	for (const ibProfilerReportData::TraceRow& r : recs)
		AppendTraceNode(m_traceCtrl, traceRoot, lastAtDepth,
			r.m_name, r.m_module, r.m_depth, r.m_enterNs, r.m_durNs);
	if (traceRoot.IsOk())
		m_traceCtrl->Expand(traceRoot);

	if (m_statusText != nullptr) {
		if (!data.m_hasProfiler)
			m_statusText->SetLabel(_("No measurement in the debuggee — call StartPerformanceMeasurement()"));
		else if (data.m_dropped != 0)
			m_statusText->SetLabel(wxString::Format(
				_("From debuggee — trace truncated: %llu calls dropped"), data.m_dropped));
		else
			m_statusText->SetLabel(_("From debuggee"));
	}
}

void ibProfilerWindow::PopulateAggregate()
{
	m_aggCtrl->DeleteRoot();
	const wxTreeItemId root = m_aggCtrl->AddRoot(wxEmptyString);

	ibScriptProfiler* prof = CurrentProfiler();
	if (prof == nullptr)
		return;

	for (const ibProfileNode& n : prof->Aggregate())   // sorted by self desc
		AddAggRow(root, n.m_name, n.m_module, n.m_count, n.m_selfNs, n.m_inclNs);
}

void ibProfilerWindow::PopulateTrace()
{
	m_traceCtrl->DeleteRoot();
	const wxTreeItemId root = m_traceCtrl->AddRoot(wxEmptyString);

	ibScriptProfiler* prof = CurrentProfiler();
	if (prof == nullptr)
		return;

	// Copy the trace (completion order) and sort by entry time for call order.
	std::vector<ibProfileTrace> recs = prof->Trace();
	std::sort(recs.begin(), recs.end(),
		[](const ibProfileTrace& a, const ibProfileTrace& b) {
			return a.m_enterNs < b.m_enterNs;
		});

	std::vector<wxTreeItemId> lastAtDepth;
	wxString module, name;
	for (const ibProfileTrace& r : recs) {
		module.clear();
		name.clear();
		prof->ResolveKey(r.m_key, module, name);
		AppendTraceNode(m_traceCtrl, root, lastAtDepth,
			name, module, r.m_depth, r.m_enterNs, r.m_durNs);
	}

	if (root.IsOk())
		m_traceCtrl->Expand(root);
}

void ibProfilerWindow::OnRefresh(wxCommandEvent& WXUNUSED(event))
{
	RefreshData();
}

void ibProfilerWindow::OnClear(wxCommandEvent& WXUNUSED(event))
{
	ClearView();
}
