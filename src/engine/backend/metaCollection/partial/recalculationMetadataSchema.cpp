////////////////////////////////////////////////////////////////////////////
//	Description : Recalculation (Перерасчёт) — the STRUCTURE it declares.
//
//	ContributeTables and nothing else: the ONE physical table a recalculation
//	becomes in the database — the record being recalculated + the calculation
//	type + one column per dimension child, keyed uniquely by (object, dimensions).
//	Shaped after commonObjectSchema.cpp's register contribution.
////////////////////////////////////////////////////////////////////////////

#include "recalculation.h"

#include "backend/query/schemaSnapshot.h"   // ibSchemaSnapshot / ibSchemaTable / ibDeclareRecordsKey

void ibValueMetaObjectRecalculation::ContributeTables(ibSchemaSnapshot& out) const
{
	ibSchemaTable& t = out.CreateSchemaTable(GetQueryable());

	// The two synthetic reference columns — reuse the SAME stable pointers the queryable vends (their
	// ids are derived from this recalculation's metaID with high bits set, so the differ tracks them by
	// identity and they never collide with the dimension attribute ids in the same table).
	const ibRecalculationQueryable* q =
		static_cast<const ibRecalculationQueryable*>(GetQueryable());
	const ibBackendQueryColumn* recalcObject = q->RecalculationObjectColumn();
	const ibBackendQueryColumn* calcType     = q->CalculationTypeColumn();

	t.Add(recalcObject);
	t.Add(calcType);

	// One column per dimension child.
	for (const auto dimension : GetDimensionArrayObject())
		t.Add(dimension);

	// THE KEY: the recalculation object then the dimension columns. UNIQUE — at most one recalculation
	// row per (object, dimension tuple). A wide key degrades to a non-unique lookup index rather than an
	// unbuildable unique one (see ibDeclareRecordsKey).
	std::vector<const ibBackendQueryColumn*> idxCols;
	idxCols.push_back(recalcObject);
	for (const auto dimension : GetDimensionArrayObject())
		idxCols.push_back(dimension);
	if (!idxCols.empty())
		ibDeclareRecordsKey(t, t.m_name + wxT("_INDEX"), idxCols);
}
