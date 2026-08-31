#include "backend/dataComposer/compositionSource.h"

#include "backend/databaseLayer/databaseResultSet.h"
#include "backend/databaseLayer/resultSetMetaData.h"
#include "backend/query/queryParser.h"     // ibQueryParser::ParsePackage — L4 text -> package
#include "backend/query/queryLowering.h"   // ibQueryLowering::ExecutePackage / OutputColumn
#include "backend/query/dataQueryBuilder.h" // ibDataQueryResult — the L3 result to drain

// Columns are 1-based here (see ibResultSetMetaData::GetColumnIndex, which walks
// 1..GetColumnCount()).

std::vector<ibComposeRow> ibCompositionSource::RowsFromResultSet(ibDatabaseResultSet* rs)
{
	std::vector<ibComposeRow> rows;
	if (rs == nullptr)
		return rows;

	ibResultSetMetaData* meta = rs->GetMetaData();
	if (meta == nullptr)
		return rows;

	const int cols = meta->GetColumnCount();

	// Snapshot column names + types once (they do not change per row).
	std::vector<wxString> names(cols + 1);
	std::vector<int>      types(cols + 1);
	for (int i = 1; i <= cols; ++i) {
		names[i] = meta->GetColumnName(i);
		types[i] = meta->GetColumnType(i);
	}

	while (rs->Next()) {
		ibComposeRow row;
		for (int i = 1; i <= cols; ++i) {
			if (rs->IsFieldNull(i)) {
				row.Set(names[i], ibValue());   // NULL -> empty value
				continue;
			}
			switch (types[i]) {
			case ibResultSetMetaData::COLUMN_INTEGER:
			case ibResultSetMetaData::COLUMN_DOUBLE:
				row.Set(names[i], ibValue(rs->GetResultNumber(i)));   // exact via ibNumber
				break;
			case ibResultSetMetaData::COLUMN_BOOL:
				row.Set(names[i], ibValue(rs->GetResultBool(i)));
				break;
			case ibResultSetMetaData::COLUMN_STRING:
			case ibResultSetMetaData::COLUMN_DATE:   // dates as text for now (stable to group/sort)
			default:
				row.Set(names[i], ibValue(rs->GetResultString(i)));
				break;
			}
		}
		rows.push_back(std::move(row));
	}

	return rows;
}

ibCompositionResult ibCompositionSource::Compose(ibDatabaseResultSet* rs,
                                                 const ibCompositionSchema& schema)
{
	return ibDataComposer::Compose(RowsFromResultSet(rs), schema);
}

std::vector<ibComposeRow> ibCompositionSource::RowsFromQueryText(const wxString& queryText,
                                                                const std::map<wxString, ibValue>& params)
{
	std::vector<ibComposeRow> rows;
	if (queryText.Strip(wxString::both).IsEmpty())
		return rows;

	// L4 text -> package (one or more statements) -> execute. A plain SELECT (incl. a UNION of
	// literal selects, which runs entirely in RAM) yields a result table we drain here; a report's
	// query is that single select, so take the first statement that produced a result.
	const ibQueryPackage package = ibQueryParser().ParsePackage(queryText);
	std::vector<ibQueryLowering::PackageResult> results =
		ibQueryLowering::ExecutePackage(package, params, /*store*/ nullptr);

	for (ibQueryLowering::PackageResult& pr : results) {
		if (pr.m_result == nullptr)
			continue;   // an INTO-temp / DROP statement — no table to compose

		ibDataQueryResult* result = pr.m_result.get();
		const std::vector<ibQueryLowering::OutputColumn>& schemaCols = pr.m_schema;

		while (result->Next()) {
			ibComposeRow row;
			for (const ibQueryLowering::OutputColumn& oc : schemaCols) {
				const ibValue v = (oc.m_byAlias || oc.m_col == nullptr)
					? result->GetColumn(oc.m_alias)
					: result->GetValue(oc.m_col);
				row.Set(oc.m_name, v);
			}
			rows.push_back(std::move(row));
		}
		break;   // the report is one select
	}
	return rows;
}

ibCompositionResult ibCompositionSource::ComposeQuery(const ibCompositionSchema& schema,
                                                      const std::map<wxString, ibValue>& params)
{
	return ibDataComposer::Compose(RowsFromQueryText(schema.m_queryText, params), schema);
}
