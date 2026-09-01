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

void ibProfilerWindow::RefreshData()
{
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

void ibProfilerWindow::PopulateAggregate()
{
	m_aggCtrl->DeleteRoot();
	const wxTreeItemId root = m_aggCtrl->AddRoot(wxEmptyString);

	ibScriptProfiler* prof = CurrentProfiler();
	if (prof == nullptr)
		return;

	for (const ibProfileNode& n : prof->Aggregate()) {   // sorted by self desc
		const wxString proc = n.m_name.IsEmpty() ? wxString(_("<module body>")) : n.m_name;
		const wxTreeItemId item = m_aggCtrl->AppendItem(root, proc);
		m_aggCtrl->SetItemText(item, 1, n.m_module);
		m_aggCtrl->SetItemText(item, 2, wxString::Format(wxT("%llu"),
			(unsigned long long)n.m_count));
		m_aggCtrl->SetItemText(item, 3, MsText(n.m_selfNs));
		m_aggCtrl->SetItemText(item, 4, MsText(n.m_inclNs));
	}
}

void ibProfilerWindow::PopulateTrace()
{
	m_traceCtrl->DeleteRoot();
	const wxTreeItemId root = m_traceCtrl->AddRoot(wxEmptyString);

	ibScriptProfiler* prof = CurrentProfiler();
	if (prof == nullptr)
		return;

	// Copy the trace (stored in completion order) and sort by entry time so the
	// nodes appear in call order — parent before its children.
	std::vector<ibProfileTrace> recs = prof->Trace();
	std::sort(recs.begin(), recs.end(),
		[](const ibProfileTrace& a, const ibProfileTrace& b) {
			return a.m_enterNs < b.m_enterNs;
		});

	// Rebuild the call tree from the per-record depth: a record at depth d hangs
	// under the most recent record seen at depth d-1.
	std::vector<wxTreeItemId> lastAtDepth;
	wxString module, name;
	for (const ibProfileTrace& r : recs) {
		module.clear();
		name.clear();
		prof->ResolveKey(r.m_key, module, name);
		const wxString proc = name.IsEmpty() ? wxString(_("<module body>")) : name;

		const int depth = r.m_depth < 0 ? 0 : r.m_depth;
		wxTreeItemId parent = root;
		if (depth > 0 && (size_t)depth <= lastAtDepth.size())
			parent = lastAtDepth[depth - 1];

		const wxTreeItemId item = m_traceCtrl->AppendItem(parent, proc);
		m_traceCtrl->SetItemText(item, 1, module);
		m_traceCtrl->SetItemText(item, 2, MsText(r.m_enterNs));
		m_traceCtrl->SetItemText(item, 3, MsText(r.m_durNs));

		if ((size_t)depth >= lastAtDepth.size())
			lastAtDepth.resize(depth + 1);
		lastAtDepth[depth] = item;
		lastAtDepth.resize(depth + 1);   // drop any deeper stale entries
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
