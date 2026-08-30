// =============================================================================
// ibXlsxExporter — independent .xlsx (ECMA-376 SpreadsheetML) writer.
//
// Builds a small spreadsheet document (text, numeric, a leading-zero code, a merge,
// formatting, a column width), exports it, then reads the produced ZIP back and
// asserts the OPC part set is present and the worksheet/styles XML carries what the
// model held. No Excel needed — the format is verified against the written bytes.
// =============================================================================

#include <gtest/gtest.h>

#include <map>

#include "backend/spreadsheetDescription.h"
#include "backend/export/xlsxExporter.h"

#include <wx/zipstrm.h>
#include <wx/wfstream.h>
#include <wx/mstream.h>
#include <wx/filename.h>
#include <wx/log.h>

namespace {

// Read every part of the produced .xlsx back into name -> UTF-8 text.
std::map<wxString, wxString> ReadXlsxParts(const wxString& path)
{
	std::map<wxString, wxString> parts;
	wxFFileInputStream in(path);
	if (!in.IsOk())
		return parts;
	wxZipInputStream zip(in);
	wxZipEntry* entry = nullptr;
	while ((entry = zip.GetNextEntry()) != nullptr) {
		const wxString name = entry->GetName(wxPATH_UNIX);
		wxMemoryOutputStream mos;
		zip.Read(mos);
		wxStreamBuffer* buf = mos.GetOutputStreamBuffer();
		parts[name] = wxString::FromUTF8(
			(const char*)buf->GetBufferStart(), buf->GetBufferSize());
		delete entry;
	}
	return parts;
}

wxString TempXlsxPath()
{
	return wxFileName::GetTempDir() + wxFileName::GetPathSeparator() + wxT("oes_test_export.xlsx");
}

// Remove without the "file not found" log line when it was never created.
void QuietRemove(const wxString& path)
{
	wxLogNull noLog;
	if (wxFileExists(path))
		wxRemoveFile(path);
}

} // namespace

TEST(XlsxExport, WritesValidPackageWithCellsMergeAndStyles)
{
	ibSpreadsheetDescription desc;

	// Title row, bold, merged across three columns.
	{
		ibSpreadsheetCellDescription* c = desc.GetOrCreateCell(0, 0);
		c->SetValue(wxT("Report"));
		c->m_font = wxFont(12, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD);
		c->m_alignHorz = wxALIGN_CENTER_HORIZONTAL;
		c->SetSize(1, 3);   // 1 row x 3 cols -> a merge
	}

	// Header row with a background fill.
	const wxChar* headers[] = { wxT("Code"), wxT("Name"), wxT("Price") };
	for (int col = 0; col < 3; ++col) {
		ibSpreadsheetCellDescription* c = desc.GetOrCreateCell(1, col);
		c->SetValue(headers[col]);
		c->m_backgroundColour = wxColour(220, 230, 241);
		c->m_borderAt[3].m_style = wxPENSTYLE_SOLID;   // bottom edge
	}

	// Data row: a leading-zero code (must stay text), a name, a numeric price.
	desc.GetOrCreateCell(2, 0)->SetValue(wxT("007"));
	desc.GetOrCreateCell(2, 1)->SetValue(wxT("Widget & Co <A>"));   // exercises XML escaping
	{
		ibSpreadsheetCellDescription* c = desc.GetOrCreateCell(2, 2);
		c->SetValue(wxT("123.45"));
		c->m_alignHorz = wxALIGN_RIGHT;
	}

	desc.SetColSize(1, 210);   // a custom column width (px)

	const wxString path = TempXlsxPath();
	QuietRemove(path);
	ASSERT_TRUE(ibXlsxExporter::Save(desc, path, wxT("Data")));
	ASSERT_TRUE(wxFileExists(path));

	const std::map<wxString, wxString> parts = ReadXlsxParts(path);

	// The OPC part set every reader (and Excel) requires.
	EXPECT_TRUE(parts.count(wxT("[Content_Types].xml")));
	EXPECT_TRUE(parts.count(wxT("_rels/.rels")));
	EXPECT_TRUE(parts.count(wxT("xl/workbook.xml")));
	EXPECT_TRUE(parts.count(wxT("xl/_rels/workbook.xml.rels")));
	EXPECT_TRUE(parts.count(wxT("xl/styles.xml")));
	ASSERT_TRUE(parts.count(wxT("xl/worksheets/sheet1.xml")));

	const wxString& ws = parts.at(wxT("xl/worksheets/sheet1.xml"));

	// Numeric cell written as a NUMBER (so Excel can sum it).
	EXPECT_NE(ws.Find(wxT("<v>123.45</v>")), wxNOT_FOUND);
	// Leading-zero code kept as an inline STRING, not a number.
	EXPECT_NE(ws.Find(wxT(">007</t>")), wxNOT_FOUND);
	// XML special characters escaped.
	EXPECT_NE(ws.Find(wxT("Widget &amp; Co &lt;A&gt;")), wxNOT_FOUND);
	// The merge is declared over the title span.
	EXPECT_NE(ws.Find(wxT("<mergeCell ref=\"A1:C1\"/>")), wxNOT_FOUND);
	// A1 carries a style reference (bold/centered), so it is not the default xf.
	EXPECT_NE(ws.Find(wxT("<c r=\"A1\" s=\"")), wxNOT_FOUND);
	// The custom column width landed (column 2 = "B").
	EXPECT_NE(ws.Find(wxT("<col min=\"2\" max=\"2\"")), wxNOT_FOUND);

	// styles.xml declares more than the single default font (the bold title added one).
	const wxString& styles = parts.at(wxT("xl/styles.xml"));
	EXPECT_NE(styles.Find(wxT("<b/>")), wxNOT_FOUND);           // the bold font
	EXPECT_NE(styles.Find(wxT("patternType=\"solid\"")), wxNOT_FOUND);  // the header fill

	// The workbook names the sheet we asked for.
	EXPECT_NE(parts.at(wxT("xl/workbook.xml")).Find(wxT("name=\"Data\"")), wxNOT_FOUND);

	QuietRemove(path);
}

TEST(XlsxExport, EmptyDocumentStillProducesAValidPackage)
{
	ibSpreadsheetDescription desc;
	const wxString path = TempXlsxPath();
	QuietRemove(path);
	ASSERT_TRUE(ibXlsxExporter::Save(desc, path));
	const std::map<wxString, wxString> parts = ReadXlsxParts(path);
	EXPECT_TRUE(parts.count(wxT("xl/worksheets/sheet1.xml")));
	EXPECT_TRUE(parts.count(wxT("[Content_Types].xml")));
	QuietRemove(path);
}
