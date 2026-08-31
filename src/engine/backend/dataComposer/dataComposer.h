#ifndef __IB_DATA_COMPOSER_H__
#define __IB_DATA_COMPOSER_H__

// =============================================================================
// ibDataComposer — data composition (a "СКД"-style report engine).
//
// Takes a stream of already-fetched ROWS plus a COMPOSITION SCHEMA (which fields
// group the rows, which fields are aggregated and how, how to order) and produces
// a GROUP TREE: nested groups, each carrying its subtotals, plus a grand total.
// A separate renderer turns that tree into our spreadsheet document.
//
// The architecture is adapted from the report engine of the MIT-licensed onebase
// project (github.com/ivanarama/onebase) — the recursive bucket-by-grouping-field
// build, per-group aggregation, and the group/result tree shape. This is an
// independent C++ implementation over our own ibValue / ibNumber / spreadsheet
// types; see NOTICE.md for the attribution and docs/data-composition.md for the
// design. It shares nothing with the (differently licensed) upstream engine.
//
// This core is DB-FREE and deterministic: it composes over rows a caller has
// already produced (from the query engine, a script, or a test), which is what
// makes the grouping/aggregation algorithm testable on its own. Wiring it to run
// a real query is a separate, mechanical step (see the doc's follow-ups).
// =============================================================================

#include "backend/backend_core.h"   // BACKEND_API, wxString
#include "backend/compiler/value.h" // ibValue — the universal value type

#include <map>
#include <vector>

// One source row: field name -> value. Field lookup is case-insensitive (query
// drivers differ on identifier case), exact match preferred.
class BACKEND_API ibComposeRow {
public:
	void Set(const wxString& field, const ibValue& value) { m_cells[field] = value; }
	// Exact match first, then a case-insensitive scan; empty value if absent.
	ibValue Get(const wxString& field) const;
	bool Has(const wxString& field) const;
	const std::map<wxString, ibValue>& Cells() const { return m_cells; }
private:
	std::map<wxString, ibValue> m_cells;
};

// How a measure (resource) is aggregated over the rows of a group.
enum class ibAggregate {
	Sum,     // Σ (exact, via ibNumber) — the default
	Count,   // number of rows
	Avg,     // Σ / count
	Min,     // least value
	Max,     // greatest value
};

// A measure: a field that is aggregated, with a display title. A COMPUTED measure
// carries an expression instead of aggregating rows — it is evaluated once per group
// (and the grand total) over the other measures' values, so a rate/ratio is computed
// from the summed numerator and denominator, not row by row.
struct ibCompositionMeasure {
	wxString    m_field;
	ibAggregate m_agg   = ibAggregate::Sum;
	wxString    m_title;                        // column caption; m_field if empty
	wxString    m_expression;                   // non-empty => computed (e.g. "Amount / Qty")
	ibCompositionMeasure() = default;
	ibCompositionMeasure(const wxString& field, ibAggregate agg = ibAggregate::Sum, const wxString& title = wxEmptyString)
		: m_field(field), m_agg(agg), m_title(title) {}
	// A computed measure: `field` names the output, `expression` computes it.
	static ibCompositionMeasure Computed(const wxString& field, const wxString& expression, const wxString& title = wxEmptyString) {
		ibCompositionMeasure m; m.m_field = field; m.m_expression = expression; m.m_title = title; return m;
	}
};

// A row FILTER (a user's отбор). Applied to source rows BEFORE grouping — engine-
// and dialect-independent, and it references result column names (which may be
// query aliases), exactly as onebase does.
enum class ibCompareOp { Eq, Ne, Gt, Ge, Lt, Le, Contains };

struct ibCompositionFilter {
	wxString     m_field;
	ibCompareOp  m_op = ibCompareOp::Eq;
	ibValue      m_value;
	ibCompositionFilter() = default;
	ibCompositionFilter(const wxString& field, ibCompareOp op, const ibValue& value)
		: m_field(field), m_op(op), m_value(value) {}
};

// One ordering key over the composed groups. The field may be a grouping field
// (sort by the group key) or a measure field (sort by its subtotal).
struct ibCompositionSort {
	wxString m_field;
	bool     m_descending = false;
	ibCompositionSort() = default;
	ibCompositionSort(const wxString& field, bool desc = false) : m_field(field), m_descending(desc) {}
};

// The composition SCHEMA: the blueprint of the report.
struct ibCompositionSchema {
	// The DATA SET. A report either carries its own query TEXT (the L4 query language —
	// run by ibCompositionSource::ComposeQuery), or the caller composes pre-fetched rows
	// directly (ibDataComposer::Compose). Empty here means "the caller supplies the rows".
	wxString                          m_queryText;

	std::vector<wxString>             m_groupings;   // ordered grouping fields (hierarchical)
	std::vector<ibCompositionMeasure> m_measures;    // aggregated + computed fields
	std::vector<ibCompositionFilter>  m_filters;     // row filters, applied before grouping
	std::vector<ibCompositionSort>    m_sort;        // ordering of groups at each level
	bool m_grandTotal = true;                        // emit the grand total
	bool m_detail     = false;                       // emit leaf detail rows under the deepest group
};

// A composed group: one distinct value of its grouping field, with subtotals and
// either nested child groups (a deeper grouping) or detail rows (the leaf).
struct ibCompositionGroup {
	wxString                        m_field;      // the grouping field this level is on
	ibValue                         m_key;        // the distinct value
	int                             m_count = 0;  // rows in this group (recursively)
	std::map<wxString, ibValue>     m_subtotals;  // measure field -> aggregated value
	std::vector<ibCompositionGroup> m_children;   // deeper grouping (empty at the leaf)
	std::vector<ibComposeRow>       m_details;    // leaf rows (only when schema.m_detail)
};

// The composed RESULT: the root groups + the grand total over all rows.
struct ibCompositionResult {
	std::vector<ibCompositionGroup> m_groups;     // top-level groups (empty schema.m_groupings -> none)
	std::map<wxString, ibValue>     m_grandTotal; // measure field -> aggregated value over every row
	int                             m_rowCount = 0;
};

class BACKEND_API ibDataComposer {
public:
	// Compose the rows into the group tree described by the schema.
	static ibCompositionResult Compose(const std::vector<ibComposeRow>& rows,
	                                    const ibCompositionSchema& schema);
};

#endif // __IB_DATA_COMPOSER_H__
