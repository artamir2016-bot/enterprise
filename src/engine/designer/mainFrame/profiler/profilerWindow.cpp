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

#include <wx/filedlg.h>
#include <wx/msgdlg.h>

#include "backend/compiler/scriptProfiler.h"
#include "backend/compiler/procUnitState.h"
#include "backend/session/session.h"
#include "backend/debugger/debugClient.h"      // request over the debug transport
#include "backend/export/xlsxExporter.h"       // .xlsx export
#include "backend/spreadsheetDescription.h"

enum {
	wxID_PROFILER_REFRESH = wxID_HIGHEST + 4200,
	wxID_PROFILER_CLEAR,
	wxID_PROFILER_EXPORT,
};

wxBEGIN_EVENT_TABLE(ibProfilerWindow, wxPanel)
	EVT_BUTTON(wxID_PROFILER_REFRESH, ibProfilerWindow::OnRefresh)
	EVT_BUTTON(wxID_PROFILER_CLEAR, ibProfilerWindow::OnClear)
	EVT_BUTTON(wxID_PROFILER_EXPORT, ibProfilerWindow::OnExport)
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
	wxButton* btnExport  = new wxButton(this, wxID_PROFILER_EXPORT, _("Export XLSX…"));
	toolRow->Add(btnRefresh, 0, wxALL, 3);
	toolRow->Add(btnClear, 0, wxALL, 3);
	toolRow->Add(btnExport, 0, wxALL, 3);
	m_statusText = new wxStaticText(this, wxID_ANY, wxEmptyString);
	toolRow->Add(m_statusText, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, 8);
	topSizer->Add(toolRow, 0, wxEXPAND);

	// --- two views in a notebook --------------------------------------------
	m_notebook = new wxNotebook(this, wxID_ANY);

	const long treeStyle = wxTR_HIDE_ROOT | wxTR_FULL_ROW_HIGHLIGHT
	                     | wxTR_ROW_LINES | wxTR_HAS_BUTTONS;

	m_aggCtrl = new ibTreeListCtrl(m_notebook, wxID_ANY,
		wxDefaultPosition, wxDefaultSize, treeStyle, wxDefaultValidator, wxT(""));
	m_aggCtrl->SetName(wxT("profilerAgg"));   // OES-TEST: found by the test agent's readTreeList
	m_aggCtrl->AddColumn(_("Procedure"), 220, wxALIGN_LEFT);
	m_aggCtrl->AddColumn(_("Module"),    180, wxALIGN_LEFT);
	m_aggCtrl->AddColumn(_("Calls"),      80, wxALIGN_RIGHT);
	m_aggCtrl->AddColumn(_("Self, ms"),  100, wxALIGN_RIGHT);
	m_aggCtrl->AddColumn(_("Total, ms"), 100, wxALIGN_RIGHT);

	m_traceCtrl = new ibTreeListCtrl(m_notebook, wxID_ANY,
		wxDefaultPosition, wxDefaultSize, treeStyle, wxDefaultValidator, wxT(""));
	m_traceCtrl->SetName(wxT("profilerTrace"));   // OES-TEST: found by readTreeList
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
	m_report = ibProfilerReportData();
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

void ibProfilerWindow::CaptureInProcess()
{
	m_report = ibProfilerReportData();

	ibScriptProfiler* prof = CurrentProfiler();
	if (prof == nullptr)
		return;   // m_hasProfiler stays false

	m_report.m_hasProfiler = true;
	m_report.m_dropped     = prof->GetTraceDropped();

	for (const ibProfileNode& n : prof->Aggregate()) {   // sorted by self desc
		ibProfilerReportData::AggRow row;
		row.m_module = n.m_module;
		row.m_name   = n.m_name;
		row.m_count  = n.m_count;
		row.m_selfNs = n.m_selfNs;
		row.m_inclNs = n.m_inclNs;
		m_report.m_agg.push_back(row);
	}

	for (const ibProfileTrace& r : prof->Trace()) {   // completion order
		ibProfilerReportData::TraceRow row;
		prof->ResolveKey(r.m_key, row.m_module, row.m_name);
		row.m_depth   = r.m_depth;
		row.m_enterNs = r.m_enterNs;
		row.m_durNs   = r.m_durNs;
		m_report.m_trace.push_back(row);
	}
}

void ibProfilerWindow::RenderFromReport()
{
	// Aggregate — already sorted by self time.
	m_aggCtrl->DeleteRoot();
	const wxTreeItemId aggRoot = m_aggCtrl->AddRoot(wxEmptyString);
	for (const ibProfilerReportData::AggRow& r : m_report.m_agg)
		AddAggRow(aggRoot, r.m_name, r.m_module, r.m_count, r.m_selfNs, r.m_inclNs);

	// Trace — held in completion order; sort by entry time for call order.
	m_traceCtrl->DeleteRoot();
	const wxTreeItemId traceRoot = m_traceCtrl->AddRoot(wxEmptyString);
	std::vector<ibProfilerReportData::TraceRow> recs = m_report.m_trace;
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
	CaptureInProcess();
	RenderFromReport();

	if (m_statusText != nullptr) {
		if (!m_report.m_hasProfiler)
			m_statusText->SetLabel(_("No measurement — run code with StartPerformanceMeasurement()"));
		else if (m_report.m_dropped != 0)
			m_statusText->SetLabel(wxString::Format(
				_("Trace truncated: %llu calls dropped"), m_report.m_dropped));
		else
			m_statusText->SetLabel(_("Ready"));
	}
}

void ibProfilerWindow::LoadReport(const ibProfilerReportData& data)
{
	m_report = data;
	RenderFromReport();

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

bool ibProfilerWindow::ExportToXlsx(const wxString& fileName)
{
	// One worksheet: the aggregate table, a blank row, then the call sequence
	// (indented by depth so the tree shape survives in the flat grid). Numeric
	// cells are written as strings; the exporter promotes unambiguously-numeric
	// text to real XLSX numbers, so Excel can sum the columns.
	ibSpreadsheetDescription desc;
	int row = 0;

	auto put = [&desc](int r, int c, const wxString& text) {
		ibSpreadsheetCellDescription* cell = desc.GetOrCreateCell(r, c);
		if (cell != nullptr) cell->SetValue(text);
	};

	// --- Aggregate ("Hot spots") --------------------------------------------
	put(row, 0, _("Hot spots"));
	row++;
	put(row, 0, _("Procedure"));
	put(row, 1, _("Module"));
	put(row, 2, _("Calls"));
	put(row, 3, _("Self, ms"));
	put(row, 4, _("Total, ms"));
	row++;
	for (const ibProfilerReportData::AggRow& r : m_report.m_agg) {
		put(row, 0, r.m_name.IsEmpty() ? wxString(_("<module body>")) : r.m_name);
		put(row, 1, r.m_module);
		put(row, 2, wxString::Format(wxT("%llu"), r.m_count));
		put(row, 3, MsText(r.m_selfNs));
		put(row, 4, MsText(r.m_inclNs));
		row++;
	}

	row++;   // blank separator

	// --- Trace ("Call sequence") — sorted by entry time, indented by depth ---
	put(row, 0, _("Call sequence"));
	row++;
	put(row, 0, _("Depth"));
	put(row, 1, _("Procedure"));
	put(row, 2, _("Module"));
	put(row, 3, _("Enter, ms"));
	put(row, 4, _("Duration, ms"));
	row++;

	std::vector<ibProfilerReportData::TraceRow> recs = m_report.m_trace;
	std::sort(recs.begin(), recs.end(),
		[](const ibProfilerReportData::TraceRow& a, const ibProfilerReportData::TraceRow& b) {
			return a.m_enterNs < b.m_enterNs;
		});
	for (const ibProfilerReportData::TraceRow& r : recs) {
		const int depth = r.m_depth < 0 ? 0 : r.m_depth;
		wxString proc = r.m_name.IsEmpty() ? wxString(_("<module body>")) : r.m_name;
		if (depth > 0)
			proc.Prepend(wxString(wxT(' '), (size_t)depth * 2));   // visual indent
		put(row, 0, wxString::Format(wxT("%d"), depth));
		put(row, 1, proc);
		put(row, 2, r.m_module);
		put(row, 3, MsText(r.m_enterNs));
		put(row, 4, MsText(r.m_durNs));
		row++;
	}

	return ibXlsxExporter::Save(desc, fileName, _("Profiler"));
}

void ibProfilerWindow::OnRefresh(wxCommandEvent& WXUNUSED(event))
{
	RefreshData();
}

void ibProfilerWindow::OnClear(wxCommandEvent& WXUNUSED(event))
{
	ClearView();
}

void ibProfilerWindow::OnExport(wxCommandEvent& WXUNUSED(event))
{
	if (m_report.m_agg.empty() && m_report.m_trace.empty()) {
		wxMessageBox(_("Nothing to export — refresh the profiler first."),
			_("Performance profiler"), wxOK | wxICON_INFORMATION, this);
		return;
	}

	wxFileDialog dlg(this, _("Export profiler report"), wxEmptyString,
		wxT("profiler.xlsx"), wxT("Excel workbook (*.xlsx)|*.xlsx"),
		wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
	if (dlg.ShowModal() != wxID_OK)
		return;

	if (ExportToXlsx(dlg.GetPath()))
		wxMessageBox(_("Profiler report exported."),
			_("Performance profiler"), wxOK | wxICON_INFORMATION, this);
	else
		wxMessageBox(_("Failed to write the .xlsx file."),
			_("Performance profiler"), wxOK | wxICON_ERROR, this);
}
