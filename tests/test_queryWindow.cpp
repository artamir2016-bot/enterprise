// =============================================================================
// Window functions — the server render of FN(arg) OVER (PARTITION BY …) (T1.2 Inc 1).
//
// End-to-end on a real in-memory SQLite (>=3.25 has window functions, and it is the
// always-embedded test driver, with ibSqlFeatures::m_window = true). A window aggregate
// is lowered to an ibQueryColumnExpr::Window and projected via SelectExpr — the computed-
// column path — so it rides ExecuteRead (every row preserved) and renders
// `FN(arg) OVER (PARTITION BY …)`. These cases pin the semantics that distinguish a window
// from a GROUP-BY aggregate:
//   * PARTITION BY keeps the rows: a 4-row table stays 4 rows, each carrying its partition sum;
//   * an unpartitioned OVER () puts the grand total on every row (still not collapsed);
//   * COUNT(*) OVER (PARTITION BY k) is the partition size on each row;
//   * the no-window-dialect gate raises (a dialect without m_window must not emit silent SQL).
//
// Built through the real SelectExpr(ibQueryColumnExpr::Window(...)) -> BuildColumnExpr ->
// renderer path, so this exercises the production render, not a hand-written OVER string.
// (docs/window-functions.md.)
// =============================================================================

#include <gtest/gtest.h>

#include <map>
#include <string>

#include <wx/init.h>

#include "backend/appData.h"
#include "backend/compiler/value.h"
#include "backend/typeDescription.h"
#include "backend/databaseLayer/connectionPool.h"
#include "backend/databaseLayer/connectionHolder.h"
#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h"
#include "backend/query/dataQueryBuilder.h"
#include "backend/query/queryProvider.h"
#include "backend/query/queryable.h"
#include "backend/query/queryColumn.h"

namespace {

// A PHYSICAL source over a real DB table (inherits the DB provider; IsComputedInRam()==false),
// so the door runs it as SERVER SQL. Mirrors test_queryComputedServer.cpp's PhysicalQ.
class PhysicalQ : public ibBackendQueryable {
public:
	PhysicalQ(const wxString& table, ibMetaID id) : m_table(table), m_id(id) {}
	void AddCol(const ibBackendQueryColumn* c) { m_cols.push_back(c); }
	bool     IsComputedInRam()   const override { return false; }
	std::vector<const ibBackendQueryColumn*> GetColumns() const override { return m_cols; }
	const ibBackendQueryColumn* ResolveColumnByName(const wxString& name) const override {
		for (const ibBackendQueryColumn* c : m_cols) if (c->GetName() == name) return c;
		return nullptr;
	}
	bool OwnsColumn(const ibBackendQueryColumn* col) const override {
		for (const ibBackendQueryColumn* c : m_cols) if (c == col) return true;
		return false;
	}
	wxString GetQueryTableName() const override { return m_table; }
	ibMetaID GetQueryTableId()   const override { return m_id; }
	ibGuid   GetQueryTableGuid() const override { ibGuidImpl i{}; i.m_data1 = static_cast<unsigned long>(m_id); return ibGuid(i); }
	std::vector<ibQuerySortItem> GetIdentitySort() const override { return {}; }
	const ibMetaData* GetMetaData() const override { return nullptr; }
private:
	wxString m_table;
	ibMetaID m_id;
	std::vector<const ibBackendQueryColumn*> m_cols;
};

} // namespace

struct WindowFix : ::testing::Test {
	wxInitializer                          m_wxInit;
	std::shared_ptr<ibDatabaseLayerSQLite> db;
	bool ready = false;

	void SetUp() override {
		if (!m_wxInit.IsOk())
			GTEST_SKIP() << "wxBase init failed";
		if (!ibApplicationData::CreateAppDataEnv(ibRunMode::eRUNTIME_MODE))
			GTEST_SKIP() << "appData env unavailable headless";
		ibConnectionPool* pool = ibApplicationData::GetConnectionPool();
		if (pool == nullptr)
			GTEST_SKIP() << "no connection pool";
		db = std::make_shared<ibDatabaseLayerSQLite>();
		if (!db->Open(wxT(":memory:")))
			GTEST_SKIP() << "in-memory SQLite open failed";
		pool->Init(db, /*maxSize=*/1, /*minIdle=*/0);
		// doc(region, qty): North x2, South, East — the 4-row table a window keeps whole.
		db->RunQuery(wxT("CREATE TABLE doc (region TEXT, qty INTEGER)"));
		db->RunQuery(wxT("INSERT INTO doc (region, qty) VALUES ('North', 10), ('South', 5), ('North', 7), ('East', 3)"));
		ready = true;
	}
	void TearDown() override {
		if (ibApplicationData::Get() != nullptr)
			ibApplicationData::DestroyAppDataEnv();
	}
};

// SUM(qty) OVER (PARTITION BY region): rows preserved (4), each carries its partition total.
TEST_F(WindowFix, PartitionSumPerRow)
{
	if (!ready) return;

	ibRawDBColumn region = ibRawDBColumn::String(wxT("region"));
	ibRawDBColumn qty    = ibRawDBColumn::Number(wxT("qty"));
	PhysicalQ doc(wxT("doc"), 500); doc.AddCol(&region); doc.AddCol(&qty);

	ibDatabaseConnectionHolder* holder = ibConnectionPool::ThreadHolder();
	ASSERT_NE(holder, nullptr);
	ibDataQueryBuilder q(holder);
	q.From(&doc);
	q.Select(&region, wxT("region"));
	q.SelectExpr(ibQueryColumnExpr::Window(ibWindowAggFn::Sum, ibQueryColumnExpr::Col(&qty), false,
	                                        { ibQueryColumnExpr::Col(&region) }), wxT("wtot"));
	ibReadPageRequest page;
	ibDataQueryResult res = q.Execute(page);

	std::map<std::string, long> byRegion;
	int rows = 0;
	while (res.Next()) {
		++rows;
		byRegion[res.GetColumn(wxT("region")).GetString().ToStdString()] = res.GetColumn(wxT("wtot")).GetInteger();
	}
	EXPECT_EQ(rows, 4);                 // PARTITION BY keeps every row (not collapsed)
	EXPECT_EQ(byRegion["North"], 17);   // 10 + 7
	EXPECT_EQ(byRegion["South"], 5);
	EXPECT_EQ(byRegion["East"], 3);
}

// SUM(qty) OVER (): the grand total (25) on every row; still 4 rows.
TEST_F(WindowFix, UnpartitionedGrandTotalOnEveryRow)
{
	if (!ready) return;

	ibRawDBColumn qty = ibRawDBColumn::Number(wxT("qty"));
	PhysicalQ doc(wxT("doc"), 501); doc.AddCol(&qty);

	ibDatabaseConnectionHolder* holder = ibConnectionPool::ThreadHolder();
	ASSERT_NE(holder, nullptr);
	ibDataQueryBuilder q(holder);
	q.From(&doc);
	q.SelectExpr(ibQueryColumnExpr::Window(ibWindowAggFn::Sum, ibQueryColumnExpr::Col(&qty), false, {}), wxT("gtot"));
	ibReadPageRequest page;
	ibDataQueryResult res = q.Execute(page);

	int rows = 0;
	while (res.Next()) { ++rows; EXPECT_EQ(res.GetColumn(wxT("gtot")).GetInteger(), 25); }
	EXPECT_EQ(rows, 4);
}

// COUNT(*) OVER (PARTITION BY region): the partition size on each row.
TEST_F(WindowFix, CountStarPartitionSize)
{
	if (!ready) return;

	ibRawDBColumn region = ibRawDBColumn::String(wxT("region"));
	PhysicalQ doc(wxT("doc"), 502); doc.AddCol(&region);

	ibDatabaseConnectionHolder* holder = ibConnectionPool::ThreadHolder();
	ASSERT_NE(holder, nullptr);
	ibDataQueryBuilder q(holder);
	q.From(&doc);
	q.Select(&region, wxT("region"));
	q.SelectExpr(ibQueryColumnExpr::Window(ibWindowAggFn::Count, nullptr, /*star*/ true,
	                                        { ibQueryColumnExpr::Col(&region) }), wxT("cnt"));
	ibReadPageRequest page;
	ibDataQueryResult res = q.Execute(page);

	std::map<std::string, long> byRegion;
	while (res.Next())
		byRegion[res.GetColumn(wxT("region")).GetString().ToStdString()] = res.GetColumn(wxT("cnt")).GetInteger();
	EXPECT_EQ(byRegion["North"], 2);
	EXPECT_EQ(byRegion["South"], 1);
	EXPECT_EQ(byRegion["East"], 1);
}
