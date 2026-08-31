#include "backend/dataComposer/dataComposer.h"

#include <algorithm>

// =============================================================================
// The composition algorithm (adapted from onebase's report/compose, MIT):
//   * bucket rows by the grouping field of the current level, in first-seen order;
//   * aggregate each bucket's measures into the group's subtotals;
//   * recurse into deeper groupings, or attach detail rows at the leaf;
//   * aggregate ALL rows once more for the grand total;
//   * order groups at each level by the sort keys.
// Sums/averages accumulate through ibNumber (exact decimal); min/max/sort compare
// on ToDouble (adequate for ordering — sums stay exact).
// =============================================================================

// --- ibComposeRow ------------------------------------------------------------

ibValue ibComposeRow::Get(const wxString& field) const
{
	auto it = m_cells.find(field);
	if (it != m_cells.end())
		return it->second;
	for (const auto& kv : m_cells)
		if (kv.first.IsSameAs(field, /*caseSensitive*/ false))
			return kv.second;
	return ibValue();
}

bool ibComposeRow::Has(const wxString& field) const
{
	if (m_cells.find(field) != m_cells.end())
		return true;
	for (const auto& kv : m_cells)
		if (kv.first.IsSameAs(field, false))
			return true;
	return false;
}

namespace {

// A group key rendered to a stable string, so rows with the same value bucket
// together regardless of the driver's returned type (1.0 vs "1").
wxString KeyText(const ibValue& v)
{
	return v.GetString();
}

// Aggregate one measure over a set of rows.
ibValue AggregateMeasure(const std::vector<const ibComposeRow*>& rows, const ibCompositionMeasure& m)
{
	if (m.m_agg == ibAggregate::Count)
		return ibValue((double)rows.size());

	if (rows.empty())
		return ibValue(ibNumber(0));

	switch (m.m_agg) {
	case ibAggregate::Sum:
	case ibAggregate::Avg: {
		ibNumber acc(0);
		int n = 0;
		for (const ibComposeRow* r : rows) {
			if (!r->Has(m.m_field))
				continue;
			acc += r->Get(m.m_field).GetNumber();
			++n;
		}
		if (m.m_agg == ibAggregate::Avg && n > 0)
			acc /= ibNumber(n);
		return ibValue(acc);
	}
	case ibAggregate::Min:
	case ibAggregate::Max: {
		bool have = false;
		double best = 0.0;
		ibValue bestVal;
		for (const ibComposeRow* r : rows) {
			if (!r->Has(m.m_field))
				continue;
			const ibValue cur = r->Get(m.m_field);
			const double d = cur.GetNumber().ToDouble();
			if (!have || (m.m_agg == ibAggregate::Min ? d < best : d > best)) {
				best = d; bestVal = cur; have = true;
			}
		}
		return have ? bestVal : ibValue(ibNumber(0));
	}
	default:
		return ibValue(ibNumber(0));
	}
}

// --- a tiny arithmetic evaluator for computed measures -----------------------
// Grammar: expr = term (('+'|'-') term)* ; term = factor (('*'|'/') factor)* ;
// factor = number | identifier | '(' expr ')' | '-' factor. An identifier is a
// measure name looked up in `vals` (absent -> 0). Cyrillic names are fine — an
// identifier is any run that is not an operator, parenthesis, space or a number.
struct ExprEval {
	const wxString& m_s;
	const std::map<wxString, ibValue>& m_vals;
	size_t m_pos = 0;
	bool   m_ok = true;

	ExprEval(const wxString& s, const std::map<wxString, ibValue>& vals) : m_s(s), m_vals(vals) {}

	void Skip() { while (m_pos < m_s.size() && (m_s[m_pos] == ' ' || m_s[m_pos] == '\t')) ++m_pos; }
	wxUniChar Peek() { Skip(); return m_pos < m_s.size() ? m_s[m_pos] : wxUniChar(0); }

	double Parse() { const double v = Expr(); Skip(); if (m_pos != m_s.size()) m_ok = false; return v; }

	double Expr() {
		double v = Term();
		for (;;) {
			const wxUniChar c = Peek();
			if (c == '+') { ++m_pos; v += Term(); }
			else if (c == '-') { ++m_pos; v -= Term(); }
			else break;
		}
		return v;
	}
	double Term() {
		double v = Factor();
		for (;;) {
			const wxUniChar c = Peek();
			if (c == '*') { ++m_pos; v *= Factor(); }
			else if (c == '/') { ++m_pos; const double d = Factor(); v = (d != 0.0) ? v / d : 0.0; }
			else break;
		}
		return v;
	}
	double Factor() {
		wxUniChar c = Peek();
		if (c == '-') { ++m_pos; return -Factor(); }
		if (c == '(') {
			++m_pos; const double v = Expr();
			if (Peek() == ')') ++m_pos; else m_ok = false;
			return v;
		}
		if (wxIsdigit(c) || c == '.') {
			const size_t start = m_pos;
			while (m_pos < m_s.size() && (wxIsdigit(m_s[m_pos]) || m_s[m_pos] == '.')) ++m_pos;
			double d = 0.0; m_s.Mid(start, m_pos - start).ToCDouble(&d);
			return d;
		}
		// identifier -> measure value
		const size_t start = m_pos;
		while (m_pos < m_s.size()) {
			const wxUniChar ic = m_s[m_pos];
			if (ic == '+' || ic == '-' || ic == '*' || ic == '/' || ic == '(' || ic == ')' || ic == ' ' || ic == '\t')
				break;
			++m_pos;
		}
		if (m_pos == start) { m_ok = false; return 0.0; }
		const wxString name = m_s.Mid(start, m_pos - start);
		auto it = m_vals.find(name);
		if (it != m_vals.end())
			return it->second.GetNumber().ToDouble();
		for (const auto& kv : m_vals)                     // case-insensitive fallback
			if (kv.first.IsSameAs(name, false))
				return kv.second.GetNumber().ToDouble();
		return 0.0;   // unknown name -> 0 (keeps a report rendering rather than failing)
	}
};

std::map<wxString, ibValue> AggregateAll(const std::vector<const ibComposeRow*>& rows,
                                         const ibCompositionSchema& schema)
{
	std::map<wxString, ibValue> out;
	// Pass 1 — the aggregated (non-computed) measures.
	for (const ibCompositionMeasure& m : schema.m_measures)
		if (m.m_expression.IsEmpty())
			out[m.m_field] = AggregateMeasure(rows, m);
	// Pass 2 — the computed measures, over the values gathered so far (declaration
	// order: a computed measure sees earlier base and computed measures).
	for (const ibCompositionMeasure& m : schema.m_measures)
		if (!m.m_expression.IsEmpty()) {
			ExprEval ev(m.m_expression, out);
			out[m.m_field] = ibValue(ev.Parse());
		}
	return out;
}

// True when a row passes every filter (AND-combined).
bool RowMatchesFilters(const ibComposeRow& row, const std::vector<ibCompositionFilter>& filters)
{
	for (const ibCompositionFilter& f : filters) {
		const ibValue a = row.Get(f.m_field);
		const ibValue& b = f.m_value;
		bool ok = false;
		if (f.m_op == ibCompareOp::Contains) {
			ok = a.GetString().Lower().Contains(b.GetString().Lower());
		}
		else {
			int cmp;
			if (a.GetType() == ibValueTypes::TYPE_NUMBER) {
				const double x = a.GetNumber().ToDouble(), y = b.GetNumber().ToDouble();
				cmp = (x < y) ? -1 : (x > y) ? 1 : 0;
			}
			else {
				cmp = a.GetString().Cmp(b.GetString());
			}
			switch (f.m_op) {
			case ibCompareOp::Eq: ok = (cmp == 0); break;
			case ibCompareOp::Ne: ok = (cmp != 0); break;
			case ibCompareOp::Gt: ok = (cmp > 0);  break;
			case ibCompareOp::Ge: ok = (cmp >= 0); break;
			case ibCompareOp::Lt: ok = (cmp < 0);  break;
			case ibCompareOp::Le: ok = (cmp <= 0); break;
			default: ok = false; break;
			}
		}
		if (!ok)
			return false;
	}
	return true;
}

// Order a level's groups by the sort keys (a grouping key or a measure subtotal).
void SortGroups(std::vector<ibCompositionGroup>& groups, const ibCompositionSchema& schema)
{
	if (schema.m_sort.empty())
		return;
	std::stable_sort(groups.begin(), groups.end(),
		[&](const ibCompositionGroup& a, const ibCompositionGroup& b) {
			for (const ibCompositionSort& s : schema.m_sort) {
				double cmp = 0.0;
				bool measure = false;
				for (const auto& kv : a.m_subtotals)
					if (kv.first.IsSameAs(s.m_field, false)) { measure = true; break; }
				if (measure) {
					auto ai = a.m_subtotals.find(s.m_field);
					auto bi = b.m_subtotals.find(s.m_field);
					const double av = (ai != a.m_subtotals.end()) ? ai->second.GetNumber().ToDouble() : 0.0;
					const double bv = (bi != b.m_subtotals.end()) ? bi->second.GetNumber().ToDouble() : 0.0;
					cmp = av - bv;
				}
				else if (a.m_field.IsSameAs(s.m_field, false)) {
					cmp = (double)a.m_key.GetString().Cmp(b.m_key.GetString());
				}
				if (cmp != 0.0)
					return s.m_descending ? (cmp > 0.0) : (cmp < 0.0);
			}
			return false;
		});
}

// Recursively build the groups for `rows` starting at grouping `level`.
std::vector<ibCompositionGroup> BuildGroups(const std::vector<const ibComposeRow*>& rows,
                                            const ibCompositionSchema& schema, size_t level)
{
	std::vector<ibCompositionGroup> groups;
	if (level >= schema.m_groupings.size())
		return groups;

	const wxString& field = schema.m_groupings[level];

	// Bucket rows by the grouping field's value, preserving first-seen order.
	std::vector<wxString> order;
	std::map<wxString, std::vector<const ibComposeRow*>> buckets;
	std::map<wxString, ibValue> keyValue;
	for (const ibComposeRow* r : rows) {
		const ibValue v = r->Get(field);
		const wxString k = KeyText(v);
		if (buckets.find(k) == buckets.end()) {
			order.push_back(k);
			keyValue[k] = v;
		}
		buckets[k].push_back(r);
	}

	for (const wxString& k : order) {
		const std::vector<const ibComposeRow*>& bucket = buckets[k];
		ibCompositionGroup g;
		g.m_field     = field;
		g.m_key       = keyValue[k];
		g.m_count     = (int)bucket.size();
		g.m_subtotals = AggregateAll(bucket, schema);

		if (level + 1 < schema.m_groupings.size())
			g.m_children = BuildGroups(bucket, schema, level + 1);
		else if (schema.m_detail)
			for (const ibComposeRow* r : bucket)
				g.m_details.push_back(*r);

		groups.push_back(std::move(g));
	}

	SortGroups(groups, schema);
	return groups;
}

} // namespace

ibCompositionResult ibDataComposer::Compose(const std::vector<ibComposeRow>& rows,
                                            const ibCompositionSchema& schema)
{
	// Row filters are applied FIRST — engine-independent, before grouping (onebase's
	// ApplyFilters ordering), so subtotals and the grand total see only kept rows.
	std::vector<const ibComposeRow*> ptrs;
	ptrs.reserve(rows.size());
	for (const ibComposeRow& r : rows)
		if (schema.m_filters.empty() || RowMatchesFilters(r, schema.m_filters))
			ptrs.push_back(&r);

	ibCompositionResult res;
	res.m_rowCount = (int)ptrs.size();
	res.m_groups   = BuildGroups(ptrs, schema, 0);
	if (schema.m_grandTotal)
		res.m_grandTotal = AggregateAll(ptrs, schema);
	return res;
}

ibCrossResult ibDataComposer::ComposeCross(const std::vector<ibComposeRow>& rows,
                                           const ibCompositionSchema& schema)
{
	ibCrossResult res;
	if (schema.m_groupings.empty() || schema.m_columns.empty() || schema.m_measures.empty())
		return res;

	const wxString& rowField = schema.m_groupings[0];
	const wxString& colField = schema.m_columns[0];
	const ibCompositionMeasure& measure = schema.m_measures[0];
	res.m_measureField = measure.m_field;

	// Filters first, as everywhere else.
	std::vector<const ibComposeRow*> kept;
	for (const ibComposeRow& r : rows)
		if (schema.m_filters.empty() || RowMatchesFilters(r, schema.m_filters))
			kept.push_back(&r);

	// Distinct row-axis and column-axis values (first-seen order), and the rows at each
	// (row, column) cell plus the rows of each whole row.
	std::vector<wxString> rowOrder, colOrder;
	std::map<wxString, ibValue> rowVal, colVal;
	std::map<wxString, std::map<wxString, std::vector<const ibComposeRow*>>> cell;
	std::map<wxString, std::vector<const ibComposeRow*>> rowAll;

	for (const ibComposeRow* r : kept) {
		const ibValue rv = r->Get(rowField); const wxString rk = rv.GetString();
		const ibValue cv = r->Get(colField); const wxString ck = cv.GetString();
		if (rowVal.find(rk) == rowVal.end()) { rowOrder.push_back(rk); rowVal[rk] = rv; }
		if (colVal.find(ck) == colVal.end()) { colOrder.push_back(ck); colVal[ck] = cv; }
		cell[rk][ck].push_back(r);
		rowAll[rk].push_back(r);
	}

	for (const wxString& ck : colOrder)
		res.m_columnKeys.push_back(colVal[ck]);

	for (const wxString& rk : rowOrder) {
		ibCrossResult::CrossRow cr;
		cr.m_key = rowVal[rk];
		for (const wxString& ck : colOrder) {
			auto it = cell[rk].find(ck);
			cr.m_cells[ck] = (it != cell[rk].end())
				? AggregateMeasure(it->second, measure) : ibValue(ibNumber(0));
		}
		cr.m_total = AggregateMeasure(rowAll[rk], measure);
		res.m_rows.push_back(std::move(cr));
	}

	for (const wxString& ck : colOrder) {
		std::vector<const ibComposeRow*> colRows;
		for (const wxString& rk : rowOrder) {
			auto it = cell[rk].find(ck);
			if (it != cell[rk].end())
				colRows.insert(colRows.end(), it->second.begin(), it->second.end());
		}
		res.m_columnTotals[ck] = AggregateMeasure(colRows, measure);
	}
	res.m_grandTotal = AggregateMeasure(kept, measure);
	return res;
}
