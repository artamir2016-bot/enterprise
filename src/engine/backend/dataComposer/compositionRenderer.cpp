#include "backend/dataComposer/compositionRenderer.h"

#include "backend/spreadsheetDescription.h"

namespace {

wxString Indent(int level)
{
	wxString s;
	for (int i = 0; i < level; ++i)
		s += wxT("    ");   // four spaces per nesting level
	return s;
}

void SetBold(ibSpreadsheetCellDescription* cell)
{
	if (cell == nullptr)
		return;
	wxFont f = cell->m_font;
	f.SetWeight(wxFONTWEIGHT_BOLD);
	cell->m_font = f;
}

// Write one measure column set (cols 1..M) from a subtotal/grand map at `row`.
void PutMeasures(ibSpreadsheetDescription& out, int row,
                 const std::vector<ibCompositionMeasure>& measures,
                 const std::map<wxString, ibValue>& values, bool bold)
{
	int col = 1;
	for (const ibCompositionMeasure& m : measures) {
		ibSpreadsheetCellDescription* cell = out.GetOrCreateCell(row, col);
		auto it = values.find(m.m_field);
		cell->SetValue(it != values.end() ? it->second.GetString() : wxString());
		cell->m_alignHorz = wxALIGN_RIGHT;
		if (bold)
			SetBold(cell);
		++col;
	}
}

// Recursively emit a group and its descendants; `row` is advanced.
void EmitGroup(ibSpreadsheetDescription& out, int& row, int level,
               const ibCompositionGroup& g, const ibCompositionSchema& schema)
{
	ibSpreadsheetCellDescription* label = out.GetOrCreateCell(row, 0);
	label->SetValue(Indent(level) + g.m_key.GetString());
	SetBold(label);                                   // group rows are subtotals — emphasise
	PutMeasures(out, row, schema.m_measures, g.m_subtotals, /*bold*/ true);

	// Conditional appearance resolved at compose: colour / bold the whole group row.
	if (g.m_style.IsSet()) {
		const wxColour fg(g.m_style.m_textColor);
		const wxColour bg(g.m_style.m_backColor);
		for (int c = 0; c <= (int)schema.m_measures.size(); ++c) {
			ibSpreadsheetCellDescription* cell = out.GetOrCreateCell(row, c);
			if (g.m_style.m_bold) SetBold(cell);
			if (!g.m_style.m_textColor.IsEmpty() && fg.IsOk()) cell->m_textColour = fg;
			if (!g.m_style.m_backColor.IsEmpty() && bg.IsOk()) cell->m_backgroundColour = bg;
		}
	}
	++row;

	for (const ibCompositionGroup& child : g.m_children)
		EmitGroup(out, row, level + 1, child, schema);

	// Leaf detail rows (only when requested and this is the deepest level).
	for (const ibComposeRow& d : g.m_details) {
		ibSpreadsheetCellDescription* dl = out.GetOrCreateCell(row, 0);
		dl->SetValue(Indent(level + 1));
		int col = 1;
		for (const ibCompositionMeasure& m : schema.m_measures) {
			ibSpreadsheetCellDescription* cell = out.GetOrCreateCell(row, col);
			cell->SetValue(d.Get(m.m_field).GetString());
			cell->m_alignHorz = wxALIGN_RIGHT;
			++col;
		}
		++row;
	}
}

} // namespace

void ibCompositionRenderer::Render(const ibCompositionResult& result,
                                   const ibCompositionSchema& schema,
                                   ibSpreadsheetDescription& out,
                                   const wxString& groupingHeader)
{
	out.ClearSpreadsheet();

	// Header row.
	int row = 0;
	{
		ibSpreadsheetCellDescription* h0 = out.GetOrCreateCell(row, 0);
		h0->SetValue(groupingHeader);
		SetBold(h0);
		int col = 1;
		for (const ibCompositionMeasure& m : schema.m_measures) {
			ibSpreadsheetCellDescription* h = out.GetOrCreateCell(row, col);
			h->SetValue(m.m_title.IsEmpty() ? m.m_field : m.m_title);
			h->m_alignHorz = wxALIGN_CENTER_HORIZONTAL;
			SetBold(h);
			++col;
		}
		++row;
	}

	// Group tree.
	for (const ibCompositionGroup& g : result.m_groups)
		EmitGroup(out, row, 0, g, schema);

	// Grand total.
	if (!result.m_grandTotal.empty()) {
		ibSpreadsheetCellDescription* t0 = out.GetOrCreateCell(row, 0);
		t0->SetValue(wxT("Total"));
		SetBold(t0);
		PutMeasures(out, row, schema.m_measures, result.m_grandTotal, /*bold*/ true);
		++row;
	}
}

void ibCompositionRenderer::RenderCross(const ibCrossResult& cross,
                                        ibSpreadsheetDescription& out,
                                        const wxString& rowHeader)
{
	out.ClearSpreadsheet();

	auto numCell = [&](int r, int c, const ibValue& v, bool bold) {
		ibSpreadsheetCellDescription* cell = out.GetOrCreateCell(r, c);
		cell->SetValue(v.GetString());
		cell->m_alignHorz = wxALIGN_RIGHT;
		if (bold) SetBold(cell);
	};

	// Header: rowHeader | <column keys...> | Total.
	int row = 0;
	{
		ibSpreadsheetCellDescription* h0 = out.GetOrCreateCell(row, 0);
		h0->SetValue(rowHeader);
		SetBold(h0);
		int col = 1;
		for (const ibValue& ck : cross.m_columnKeys) {
			ibSpreadsheetCellDescription* h = out.GetOrCreateCell(row, col++);
			h->SetValue(ck.GetString());
			h->m_alignHorz = wxALIGN_CENTER_HORIZONTAL;
			SetBold(h);
		}
		ibSpreadsheetCellDescription* ht = out.GetOrCreateCell(row, col);
		ht->SetValue(wxT("Total"));
		ht->m_alignHorz = wxALIGN_CENTER_HORIZONTAL;
		SetBold(ht);
		++row;
	}

	// One row per row-axis value: key | cells | row total.
	for (const ibCrossResult::CrossRow& cr : cross.m_rows) {
		ibSpreadsheetCellDescription* label = out.GetOrCreateCell(row, 0);
		label->SetValue(cr.m_key.GetString());
		int col = 1;
		for (const ibValue& ck : cross.m_columnKeys) {
			auto it = cr.m_cells.find(ck.GetString());
			numCell(row, col++, it != cr.m_cells.end() ? it->second : ibValue(ibNumber(0)), false);
		}
		numCell(row, col, cr.m_total, /*bold*/ true);
		++row;
	}

	// Totals row: Total | column totals | grand total.
	{
		ibSpreadsheetCellDescription* t0 = out.GetOrCreateCell(row, 0);
		t0->SetValue(wxT("Total"));
		SetBold(t0);
		int col = 1;
		for (const ibValue& ck : cross.m_columnKeys) {
			auto it = cross.m_columnTotals.find(ck.GetString());
			numCell(row, col++, it != cross.m_columnTotals.end() ? it->second : ibValue(ibNumber(0)), true);
		}
		numCell(row, col, cross.m_grandTotal, /*bold*/ true);
		++row;
	}
}
