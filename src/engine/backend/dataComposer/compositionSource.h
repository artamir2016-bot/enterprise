#ifndef __IB_COMPOSITION_SOURCE_H__
#define __IB_COMPOSITION_SOURCE_H__

// =============================================================================
// ibCompositionSource — the bridge from the QUERY ENGINE to the data composer.
//
// The composer core (ibDataComposer) is deliberately DB-free: it composes rows a
// caller already holds. This file is the one place that knows about the database
// layer — it drains a driver result set into ibComposeRow values (column name ->
// typed ibValue) so a real query can feed a composition. Kept separate so the
// grouping/aggregation core stays testable without a database.
// =============================================================================

#include "backend/backend_core.h"
#include "backend/dataComposer/dataComposer.h"

#include <vector>

class ibDatabaseResultSet;

class BACKEND_API ibCompositionSource {
public:
	// Drain `rs` (from ibDatabaseLayer::RunQueryWithResults / a prepared statement)
	// into composition rows. Column names come from the result metadata; each cell
	// is a typed ibValue (numbers exact via ibNumber, NULL -> empty). Does not take
	// ownership of rs; leaves it positioned past the last row.
	static std::vector<ibComposeRow> RowsFromResultSet(ibDatabaseResultSet* rs);

	// Convenience: drain + compose in one call.
	static ibCompositionResult Compose(ibDatabaseResultSet* rs,
	                                   const ibCompositionSchema& schema);

	// Run an L4 QUERY TEXT (the query language) and drain its first result into rows.
	// Parses with ibQueryParser and executes with ibQueryLowering — so the report's data
	// set can be a query, not pre-fetched rows. `params` supplies &Parameter values.
	static std::vector<ibComposeRow> RowsFromQueryText(const wxString& queryText,
	                                                   const std::map<wxString, ibValue>& params = {});

	// Run schema.m_queryText and compose the result by the schema.
	static ibCompositionResult ComposeQuery(const ibCompositionSchema& schema,
	                                        const std::map<wxString, ibValue>& params = {});
};

#endif // __IB_COMPOSITION_SOURCE_H__
