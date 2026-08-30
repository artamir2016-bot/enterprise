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

std::map<wxString, ibValue> AggregateAll(const std::vector<const ibComposeRow*>& rows,
                                         const ibCompositionSchema& schema)
{
	std::map<wxString, ibValue> out;
	for (const ibCompositionMeasure& m : schema.m_measures)
		out[m.m_field] = AggregateMeasure(rows, m);
	return out;
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
	std::vector<const ibComposeRow*> ptrs;
	ptrs.reserve(rows.size());
	for (const ibComposeRow& r : rows)
		ptrs.push_back(&r);

	ibCompositionResult res;
	res.m_rowCount = (int)rows.size();
	res.m_groups   = BuildGroups(ptrs, schema, 0);
	if (schema.m_grandTotal)
		res.m_grandTotal = AggregateAll(ptrs, schema);
	return res;
}
