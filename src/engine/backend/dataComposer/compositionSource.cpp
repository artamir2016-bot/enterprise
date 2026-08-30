#include "backend/dataComposer/compositionSource.h"

#include "backend/databaseLayer/databaseResultSet.h"
#include "backend/databaseLayer/resultSetMetaData.h"

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
