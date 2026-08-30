#ifndef __IB_XLSX_EXPORTER_H__
#define __IB_XLSX_EXPORTER_H__

// =============================================================================
// ibXlsxExporter — write our spreadsheet document to an .xlsx (Office Open XML /
// SpreadsheetML) file.
//
// This is an INDEPENDENT implementation from the PUBLIC ECMA-376 (OOXML) standard:
// an .xlsx is an OPC package — a ZIP of XML parts — and this writer emits the
// minimal valid part set (content types, relationships, workbook, one worksheet,
// styles) directly from our own ibSpreadsheetDescription model. It shares nothing
// with any third-party spreadsheet library or external source: the only inputs are
// the published file format and our in-memory document.
//
// Reads (never mutates) ibSpreadsheetDescription: cell text + position, merges,
// column widths / row heights, and per-cell formatting (font, fill, alignment,
// borders, rotation). Cell values are stored as strings in our model; a value that
// is unambiguously numeric is written as an XLSX number (so Excel can sum it), the
// rest as inline strings.
// =============================================================================

#include "backend/backend_core.h"   // BACKEND_API

struct ibSpreadsheetDescription;

class BACKEND_API ibXlsxExporter {
public:
	// Write `desc` to `fileName` as a single-worksheet .xlsx named `sheetName`.
	// Returns false if the file cannot be opened or the ZIP cannot be written.
	static bool Save(const ibSpreadsheetDescription& desc,
	                 const wxString& fileName,
	                 const wxString& sheetName = wxT("Sheet1"));
};

#endif // __IB_XLSX_EXPORTER_H__
