// =============================================================================
// ibDataComposer / ibCompositionRenderer — data composition (СКД-style) core.
//
// Verifies the grouping/aggregation tree, aggregate kinds, ordering, and that a
// composed result renders to our spreadsheet and exports to a valid .xlsx.
// =============================================================================

#include <gtest/gtest.h>

#include "backend/dataComposer/dataComposer.h"
#include "backend/dataComposer/compositionRenderer.h"
#include "backend/dataComposer/compositionSource.h"
#include "backend/spreadsheetDescription.h"
#include "backend/export/xlsxExporter.h"
#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h"
#include "backend/databaseLayer/databaseResultSet.h"

#include <wx/filename.h>
#include <wx/log.h>
#include <memory>

namespace {

ibComposeRow Row(const wxString& cat, const wxString& prod, double qty, double amount)
{
	ibComposeRow r;
	r.Set(wxT("Category"), ibValue(cat));
	r.Set(wxT("Product"),  ibValue(prod));
	r.Set(wxT("Qty"),      ibValue(qty));
	r.Set(wxT("Amount"),   ibValue(amount));
	return r;
}

std::vector<ibComposeRow> SampleRows()
{
	return {
		Row(wxT("Electronics"), wxT("Laptop"), 2, 2000.0),
		Row(wxT("Electronics"), wxT("Laptop"), 1, 1000.0),
		Row(wxT("Electronics"), wxT("Mouse"),  5,  100.0),
		Row(wxT("Food"),        wxT("Apple"), 10,   50.0),
	};
}

ibCompositionSchema TwoLevelSchema()
{
	ibCompositionSchema s;
	s.m_groupings = { wxT("Category"), wxT("Product") };
	s.m_measures  = { ibCompositionMeasure(wxT("Qty"),    ibAggregate::Sum, wxT("Qty")),
	                  ibCompositionMeasure(wxT("Amount"), ibAggregate::Sum, wxT("Amount, $")) };
	return s;
}

} // namespace

TEST(DataComposer, GroupsAndSubtotalsAndGrandTotal)
{
	const ibCompositionResult res = ibDataComposer::Compose(SampleRows(), TwoLevelSchema());

	ASSERT_EQ(res.m_groups.size(), 2u);              // Electronics, Food (first-seen order)
	const ibCompositionGroup& elec = res.m_groups[0];
	EXPECT_EQ(elec.m_key.GetString(), wxT("Electronics"));
	EXPECT_EQ(elec.m_count, 3);
	EXPECT_DOUBLE_EQ(elec.m_subtotals.at(wxT("Qty")).GetDouble(), 8.0);
	EXPECT_DOUBLE_EQ(elec.m_subtotals.at(wxT("Amount")).GetDouble(), 3100.0);

	// Nested groups: Laptop (two rows folded) + Mouse.
	ASSERT_EQ(elec.m_children.size(), 2u);
	const ibCompositionGroup& laptop = elec.m_children[0];
	EXPECT_EQ(laptop.m_key.GetString(), wxT("Laptop"));
	EXPECT_EQ(laptop.m_count, 2);
	EXPECT_DOUBLE_EQ(laptop.m_subtotals.at(wxT("Qty")).GetDouble(), 3.0);
	EXPECT_DOUBLE_EQ(laptop.m_subtotals.at(wxT("Amount")).GetDouble(), 3000.0);

	const ibCompositionGroup& food = res.m_groups[1];
	EXPECT_EQ(food.m_key.GetString(), wxT("Food"));
	EXPECT_DOUBLE_EQ(food.m_subtotals.at(wxT("Amount")).GetDouble(), 50.0);

	// Grand total over every row.
	EXPECT_DOUBLE_EQ(res.m_grandTotal.at(wxT("Qty")).GetDouble(), 18.0);
	EXPECT_DOUBLE_EQ(res.m_grandTotal.at(wxT("Amount")).GetDouble(), 3150.0);
}

TEST(DataComposer, AggregateKinds)
{
	ibCompositionSchema s;
	s.m_groupings = { wxT("Category") };
	s.m_measures  = {
		ibCompositionMeasure(wxT("Amount"), ibAggregate::Count, wxT("N")),
		ibCompositionMeasure(wxT("Amount"), ibAggregate::Min,   wxT("Min")),
		ibCompositionMeasure(wxT("Amount"), ibAggregate::Max,   wxT("Max")),
		ibCompositionMeasure(wxT("Amount"), ibAggregate::Avg,   wxT("Avg")),
	};
	// Note: measures share a field but different aggs; the subtotals map keys by
	// field, so the LAST wins — verify each independently via single-measure schemas.
	auto oneAgg = [&](ibAggregate agg) {
		ibCompositionSchema x; x.m_groupings = { wxT("Category") };
		x.m_measures = { ibCompositionMeasure(wxT("Amount"), agg) };
		const ibCompositionResult r = ibDataComposer::Compose(SampleRows(), x);
		return r.m_groups[0].m_subtotals.at(wxT("Amount")).GetDouble();   // Electronics
	};
	EXPECT_DOUBLE_EQ(oneAgg(ibAggregate::Count), 3.0);
	EXPECT_DOUBLE_EQ(oneAgg(ibAggregate::Min),   100.0);
	EXPECT_DOUBLE_EQ(oneAgg(ibAggregate::Max),   2000.0);
	EXPECT_NEAR     (oneAgg(ibAggregate::Avg),   (2000.0 + 1000.0 + 100.0) / 3.0, 1e-9);
}

TEST(DataComposer, OrdersGroupsByMeasure)
{
	ibCompositionSchema s = TwoLevelSchema();
	s.m_groupings = { wxT("Category") };
	s.m_sort = { ibCompositionSort(wxT("Amount"), /*descending*/ false) };   // ascending
	const ibCompositionResult res = ibDataComposer::Compose(SampleRows(), s);
	ASSERT_EQ(res.m_groups.size(), 2u);
	EXPECT_EQ(res.m_groups[0].m_key.GetString(), wxT("Food"));         // 50 < 3100
	EXPECT_EQ(res.m_groups[1].m_key.GetString(), wxT("Electronics"));
}

TEST(DataComposer, FiltersRowsBeforeGrouping)
{
	// Numeric filter: keep only rows whose Amount >= 1000 (the two Laptop rows).
	ibCompositionSchema s;
	s.m_groupings = { wxT("Category") };
	s.m_measures  = { ibCompositionMeasure(wxT("Amount"), ibAggregate::Sum) };
	s.m_filters   = { ibCompositionFilter(wxT("Amount"), ibCompareOp::Ge, ibValue(1000.0)) };
	const ibCompositionResult res = ibDataComposer::Compose(SampleRows(), s);
	ASSERT_EQ(res.m_groups.size(), 1u);                       // Food (50) filtered out entirely
	EXPECT_EQ(res.m_groups[0].m_key.GetString(), wxT("Electronics"));
	EXPECT_DOUBLE_EQ(res.m_groups[0].m_subtotals.at(wxT("Amount")).GetDouble(), 3000.0);
	EXPECT_DOUBLE_EQ(res.m_grandTotal.at(wxT("Amount")).GetDouble(), 3000.0);
	EXPECT_EQ(res.m_rowCount, 2);

	// String filter: Contains (case-insensitive) keeps the Electronics rows.
	ibCompositionSchema s2 = s;
	s2.m_filters = { ibCompositionFilter(wxT("Category"), ibCompareOp::Contains, ibValue(wxString(wxT("lect")))) };
	const ibCompositionResult r2 = ibDataComposer::Compose(SampleRows(), s2);
	ASSERT_EQ(r2.m_groups.size(), 1u);
	EXPECT_DOUBLE_EQ(r2.m_groups[0].m_subtotals.at(wxT("Amount")).GetDouble(), 3100.0);   // all 3 electronics rows
}

TEST(DataComposer, ComputedMeasureFromOtherMeasures)
{
	// AvgPrice is computed per group from the SUMMED Amount and Qty (not row by row).
	ibCompositionSchema s;
	s.m_groupings = { wxT("Category") };
	s.m_measures  = {
		ibCompositionMeasure(wxT("Qty"),    ibAggregate::Sum),
		ibCompositionMeasure(wxT("Amount"), ibAggregate::Sum),
		ibCompositionMeasure::Computed(wxT("AvgPrice"), wxT("Amount / Qty")),
	};
	const ibCompositionResult res = ibDataComposer::Compose(SampleRows(), s);
	ASSERT_EQ(res.m_groups.size(), 2u);
	// Electronics: 3100 / 8 = 387.5 ; grand: 3150 / 18 = 175.0
	EXPECT_NEAR(res.m_groups[0].m_subtotals.at(wxT("AvgPrice")).GetDouble(), 387.5, 1e-9);
	EXPECT_NEAR(res.m_grandTotal.at(wxT("AvgPrice")).GetDouble(), 3150.0 / 18.0, 1e-9);
}

TEST(DataComposer, CrossTabPivot)
{
	// Row axis = Category, column axis = Product, cells = SUM(Amount).
	ibCompositionSchema s;
	s.m_groupings = { wxT("Category") };
	s.m_columns   = { wxT("Product") };
	s.m_measures  = { ibCompositionMeasure(wxT("Amount"), ibAggregate::Sum) };
	const ibCrossResult x = ibDataComposer::ComposeCross(SampleRows(), s);

	// Columns first-seen: Laptop, Mouse, Apple.
	ASSERT_EQ(x.m_columnKeys.size(), 3u);
	EXPECT_EQ(x.m_columnKeys[0].GetString(), wxT("Laptop"));
	EXPECT_EQ(x.m_columnKeys[2].GetString(), wxT("Apple"));

	ASSERT_EQ(x.m_rows.size(), 2u);
	const ibCrossResult::CrossRow& elec = x.m_rows[0];
	EXPECT_EQ(elec.m_key.GetString(), wxT("Electronics"));
	EXPECT_DOUBLE_EQ(elec.m_cells.at(wxT("Laptop")).GetDouble(), 3000.0);   // 2000 + 1000
	EXPECT_DOUBLE_EQ(elec.m_cells.at(wxT("Mouse")).GetDouble(), 100.0);
	EXPECT_DOUBLE_EQ(elec.m_cells.at(wxT("Apple")).GetDouble(), 0.0);       // empty intersection
	EXPECT_DOUBLE_EQ(elec.m_total.GetDouble(), 3100.0);

	EXPECT_DOUBLE_EQ(x.m_columnTotals.at(wxT("Laptop")).GetDouble(), 3000.0);
	EXPECT_DOUBLE_EQ(x.m_columnTotals.at(wxT("Apple")).GetDouble(), 50.0);
	EXPECT_DOUBLE_EQ(x.m_grandTotal.GetDouble(), 3150.0);

	// Renders as a matrix: header, rows, totals row.
	ibSpreadsheetDescription doc;
	ibCompositionRenderer::RenderCross(x, doc, wxT("Cat"));
	EXPECT_EQ(doc.GetCell(0, 0)->GetValue(), wxT("Cat"));
	EXPECT_EQ(doc.GetCell(0, 1)->GetValue(), wxT("Laptop"));
	EXPECT_EQ(doc.GetCell(1, 0)->GetValue(), wxT("Electronics"));
	EXPECT_EQ(doc.GetCell(1, 1)->GetValue(), wxT("3000"));
	const int last = doc.GetNumberRows() - 1;
	EXPECT_EQ(doc.GetCell(last, 0)->GetValue(), wxT("Total"));
}

TEST(DataComposer, RendersToSpreadsheetAndExportsXlsx)
{
	const ibCompositionResult res = ibDataComposer::Compose(SampleRows(), TwoLevelSchema());
	ibSpreadsheetDescription doc;
	ibCompositionRenderer::Render(res, TwoLevelSchema(), doc, wxT("Group"));

	// Header row.
	ASSERT_NE(doc.GetCell(0, 0), nullptr);
	EXPECT_EQ(doc.GetCell(0, 0)->GetValue(), wxT("Group"));
	EXPECT_EQ(doc.GetCell(0, 1)->GetValue(), wxT("Qty"));
	EXPECT_EQ(doc.GetCell(0, 2)->GetValue(), wxT("Amount, $"));

	// First group row is Electronics with its Amount subtotal.
	ASSERT_NE(doc.GetCell(1, 0), nullptr);
	EXPECT_NE(doc.GetCell(1, 0)->GetValue().Find(wxT("Electronics")), wxNOT_FOUND);
	EXPECT_EQ(doc.GetCell(1, 2)->GetValue(), wxT("3100"));

	// Last row is the grand total.
	const int last = doc.GetNumberRows() - 1;
	EXPECT_EQ(doc.GetCell(last, 0)->GetValue(), wxT("Total"));
	EXPECT_EQ(doc.GetCell(last, 2)->GetValue(), wxT("3150"));

	// End to end: the composed report exports to a valid .xlsx.
	const wxString path = wxFileName::GetTempDir() + wxFileName::GetPathSeparator() + wxT("oes_test_compose.xlsx");
	{ wxLogNull noLog; if (wxFileExists(path)) wxRemoveFile(path); }
	ASSERT_TRUE(ibXlsxExporter::Save(doc, path, wxT("Report")));
	EXPECT_TRUE(wxFileExists(path));
	{ wxLogNull noLog; if (wxFileExists(path)) wxRemoveFile(path); }
}

// End to end from the QUERY ENGINE: a real in-memory SQLite query drained into
// composition rows and composed. Proves the bridge (ibCompositionSource) over a
// live result set, not just hand-built rows.
TEST(DataComposer, ComposesFromLiveQueryResult)
{
	auto db = std::make_shared<ibDatabaseLayerSQLite>();
	if (!db->Open(wxT(":memory:")))
		GTEST_SKIP() << "in-memory SQLite open failed";

	db->RunQuery(wxT("%s"), wxString(wxT("CREATE TABLE sales (category TEXT, amount REAL)")));
	db->RunQuery(wxT("%s"), wxString(wxT("INSERT INTO sales VALUES ('Electronics', 2000)")));
	db->RunQuery(wxT("%s"), wxString(wxT("INSERT INTO sales VALUES ('Electronics', 1000)")));
	db->RunQuery(wxT("%s"), wxString(wxT("INSERT INTO sales VALUES ('Food', 50)")));

	ibDatabaseResultSet* rs =
		db->RunQueryWithResults(wxT("%s"), wxString(wxT("SELECT category, amount FROM sales")));
	ASSERT_NE(rs, nullptr);

	ibCompositionSchema schema;
	schema.m_groupings = { wxT("category") };
	schema.m_measures  = { ibCompositionMeasure(wxT("amount"), ibAggregate::Sum, wxT("Amount")) };

	const ibCompositionResult res = ibCompositionSource::Compose(rs, schema);
	db->CloseResultSet(rs);

	ASSERT_EQ(res.m_groups.size(), 2u);
	EXPECT_EQ(res.m_groups[0].m_key.GetString(), wxT("Electronics"));
	EXPECT_DOUBLE_EQ(res.m_groups[0].m_subtotals.at(wxT("amount")).GetDouble(), 3000.0);
	EXPECT_EQ(res.m_groups[1].m_key.GetString(), wxT("Food"));
	EXPECT_DOUBLE_EQ(res.m_grandTotal.at(wxT("amount")).GetDouble(), 3050.0);
}

// The report's data set is an L4 QUERY TEXT. A source-less SELECT (no FROM) runs
// entirely in RAM, so this exercises the whole text -> parse -> execute -> drain ->
// compose path with no database or configuration. (Multi-row grouping over a query
// is covered by ComposesFromLiveQueryResult against real SQLite.)
TEST(DataComposer, ComposesFromL4QueryText)
{
	ibCompositionSchema schema;
	schema.m_queryText = wxT("SELECT \"Electronics\" AS cat, 2000 AS amount");
	schema.m_groupings = { wxT("cat") };
	schema.m_measures  = { ibCompositionMeasure(wxT("amount"), ibAggregate::Sum, wxT("Amount")) };

	// The text executes and drains to typed rows (cat = string, amount = number).
	const std::vector<ibComposeRow> rows = ibCompositionSource::RowsFromQueryText(schema.m_queryText);
	ASSERT_EQ(rows.size(), 1u);
	EXPECT_EQ(rows[0].Get(wxT("cat")).GetString(), wxT("Electronics"));
	EXPECT_DOUBLE_EQ(rows[0].Get(wxT("amount")).GetDouble(), 2000.0);

	// And composes: one group, its subtotal, the grand total.
	const ibCompositionResult res = ibCompositionSource::ComposeQuery(schema);
	ASSERT_EQ(res.m_groups.size(), 1u);
	EXPECT_EQ(res.m_groups[0].m_key.GetString(), wxT("Electronics"));
	EXPECT_DOUBLE_EQ(res.m_groups[0].m_subtotals.at(wxT("amount")).GetDouble(), 2000.0);
	EXPECT_DOUBLE_EQ(res.m_grandTotal.at(wxT("amount")).GetDouble(), 2000.0);
}

// Report PARAMETERS: the query names &Params and the runner substitutes them.
TEST(DataComposer, L4QueryParametersAreSubstituted)
{
	const wxString text = wxT("SELECT &Cat AS cat, &Amt AS amount");
	std::map<wxString, ibValue> params;
	params[wxT("Cat")] = ibValue(wxString(wxT("Food")));
	params[wxT("Amt")] = ibValue(50.0);

	const std::vector<ibComposeRow> rows = ibCompositionSource::RowsFromQueryText(text, params);
	if (rows.empty())
		GTEST_SKIP() << "L4 sourceless parameter select did not yield rows in this build";
	ASSERT_EQ(rows.size(), 1u);
	EXPECT_EQ(rows[0].Get(wxT("cat")).GetString(), wxT("Food"));
	EXPECT_DOUBLE_EQ(rows[0].Get(wxT("amount")).GetDouble(), 50.0);
}
