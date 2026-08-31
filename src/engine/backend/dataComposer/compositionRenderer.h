#ifndef __IB_COMPOSITION_RENDERER_H__
#define __IB_COMPOSITION_RENDERER_H__

// =============================================================================
// ibCompositionRenderer — turn a composed result (the group tree + grand total
// from ibDataComposer) into our spreadsheet document (ibSpreadsheetDescription).
//
// Layout: a header row (a grouping column + one column per measure), then one row
// per group — the key indented by its nesting level, its subtotals in the measure
// columns — recursively, optional detail rows at the leaf, and a bold grand-total
// row. The result feeds straight into the grid view, printing, or the XLSX writer
// (ibXlsxExporter), so a composed report exports to Excel with no extra step.
// =============================================================================

#include "backend/backend_core.h"
#include "backend/dataComposer/dataComposer.h"

struct ibCompositionSchema;
struct ibCompositionResult;
struct ibCrossResult;
struct ibSpreadsheetDescription;

class BACKEND_API ibCompositionRenderer {
public:
	// Render into `out` (cleared first). `groupingHeader` is the caption of the
	// first (key) column.
	static void Render(const ibCompositionResult& result,
	                   const ibCompositionSchema& schema,
	                   ibSpreadsheetDescription& out,
	                   const wxString& groupingHeader = wxT("Grouping"));

	// Render a cross-tab (pivot) as a matrix: a header row of column-axis values, one
	// row per row-axis value with its cells and row total, and a totals row.
	static void RenderCross(const ibCrossResult& cross,
	                        ibSpreadsheetDescription& out,
	                        const wxString& rowHeader = wxT("Grouping"));
};

#endif // __IB_COMPOSITION_RENDERER_H__
