#include "backend/export/xlsxExporter.h"

#include "backend/spreadsheetDescription.h"

#include <wx/zipstrm.h>
#include <wx/wfstream.h>
#include <wx/sstream.h>

#include <map>
#include <vector>
#include <algorithm>

// =============================================================================
// Independent .xlsx (ECMA-376 SpreadsheetML) writer.
//
// The file is an OPC package (a ZIP) of XML parts. The minimal valid set this
// writer emits:
//   [Content_Types].xml          — the MIME type of every part
//   _rels/.rels                  — root relationship -> the workbook
//   xl/workbook.xml              — the one sheet and its relationship id
//   xl/_rels/workbook.xml.rels   — workbook -> worksheet + styles
//   xl/styles.xml                — fonts / fills / borders / cell formats (xf)
//   xl/worksheets/sheet1.xml     — columns, rows, cells, merges
//
// Every string used as an XML value is escaped; every identifier we emit
// (r="A1", the rId keys, the part paths) is ASCII by construction.
// =============================================================================

namespace {

// --- text helpers ------------------------------------------------------------

wxString XmlEsc(const wxString& s)
{
	wxString out;
	out.reserve(s.size());
	for (wxUniChar c : s) {
		switch (c.GetValue()) {
		case '&':  out += wxT("&amp;");  break;
		case '<':  out += wxT("&lt;");   break;
		case '>':  out += wxT("&gt;");   break;
		case '"':  out += wxT("&quot;"); break;
		case '\'': out += wxT("&apos;"); break;
		default:
			// Drop the XML-1.0-illegal control characters (a stray \x01 in a cell would
			// make the whole part unreadable); keep tab / LF / CR.
			if (c < 0x20 && c != '\t' && c != '\n' && c != '\r')
				break;
			out += c;
		}
	}
	return out;
}

// 0-based column index -> spreadsheet letters (0->A, 25->Z, 26->AA).
wxString ColRef(unsigned int col)
{
	wxString out;
	for (unsigned int n = col + 1; n > 0; n = (n - 1) / 26)
		out.Prepend(wxUniChar('A' + (n - 1) % 26));
	return out;
}

// 0-based (row,col) -> "A1".
wxString CellRef(unsigned int row, unsigned int col)
{
	return ColRef(col) + wxString::Format(wxT("%u"), row + 1);
}

// wxColour -> "FFRRGGBB" (XLSX ARGB, alpha first, fully opaque).
wxString ArgbHex(const wxColour& c)
{
	return wxString::Format(wxT("FF%02X%02X%02X"), c.Red(), c.Green(), c.Blue());
}

// A cell value is stored as text in our model. Write it as a NUMBER only when it is
// unambiguously one — so Excel can sum it — and otherwise as text, which is what
// preserves codes with leading zeros, long identifiers and anything locale-shaped.
bool LooksNumeric(const wxString& raw, wxString& canonical)
{
	const wxString s = raw.Strip(wxString::both);
	if (s.IsEmpty())
		return false;

	// A leading zero followed by another digit is a CODE, not a number (0042, 007).
	size_t signLen = (s[0] == '+' || s[0] == '-') ? 1u : 0u;
	if (s.length() > signLen + 1 && s[signLen] == '0' && wxIsdigit(s[signLen + 1]))
		return false;

	double v = 0.0;
	if (!s.ToCDouble(&v))
		return false;   // not a C-locale number (no thousands separators, '.' decimal)

	// More than 15 significant digits does not survive Excel's double — keep it text.
	int digits = 0;
	for (wxUniChar c : s)
		if (wxIsdigit(c)) ++digits;
	if (digits > 15)
		return false;

	canonical = s;   // already C-locale ('.' decimal) — write verbatim
	return true;
}

// --- style tables ------------------------------------------------------------
// styles.xml indexes fonts / fills / borders and combines them into cell formats
// (cellXfs). A cell references ONE xf by index. We dedup each table by a string key
// so identical formatting shares an entry, and xf 0 is the unformatted default.

struct StyleTables {
	std::vector<wxString> m_fonts;    // <font> bodies; [0] = default
	std::vector<wxString> m_fills;    // <fill> bodies; [0]=none [1]=gray125 (Excel reserves both)
	std::vector<wxString> m_borders;  // <border> bodies; [0] = empty
	std::vector<wxString> m_xfs;      // <xf> bodies; [0] = default

	std::map<wxString, int> m_fontKey, m_fillKey, m_borderKey, m_xfKey;

	StyleTables() {
		m_fonts.push_back(wxT("<font><sz val=\"11\"/><name val=\"Calibri\"/></font>"));
		m_fills.push_back(wxT("<fill><patternFill patternType=\"none\"/></fill>"));
		m_fills.push_back(wxT("<fill><patternFill patternType=\"gray125\"/></fill>"));
		m_borders.push_back(wxT("<border><left/><right/><top/><bottom/><diagonal/></border>"));
		m_xfs.push_back(wxT("<xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\"/>"));
	}

	int Intern(std::vector<wxString>& tbl, std::map<wxString, int>& key, const wxString& body) {
		auto it = key.find(body);
		if (it != key.end())
			return it->second;
		const int idx = (int)tbl.size();
		tbl.push_back(body);
		key[body] = idx;
		return idx;
	}
};

int FontIndex(StyleTables& st, const ibSpreadsheetCellDescription& c)
{
	const wxFont& f = c.m_font;
	wxString body = wxT("<font>");
	body += wxString::Format(wxT("<sz val=\"%d\"/>"), f.IsOk() ? f.GetPointSize() : 11);
	body += wxString::Format(wxT("<color rgb=\"%s\"/>"), ArgbHex(c.m_textColour));
	body += wxString::Format(wxT("<name val=\"%s\"/>"),
		XmlEsc(f.IsOk() && !f.GetFaceName().IsEmpty() ? f.GetFaceName() : wxString(wxT("Calibri"))));
	if (f.IsOk() && f.GetWeight() >= wxFONTWEIGHT_BOLD) body += wxT("<b/>");
	if (f.IsOk() && f.GetStyle() != wxFONTSTYLE_NORMAL) body += wxT("<i/>");
	if (f.IsOk() && f.GetUnderlined())                  body += wxT("<u/>");
	body += wxT("</font>");
	return st.Intern(st.m_fonts, st.m_fontKey, body);
}

int FillIndex(StyleTables& st, const ibSpreadsheetCellDescription& c)
{
	// Only a background that DIFFERS from the window default becomes a solid fill;
	// otherwise the cell keeps fill 0 (none), so an unstyled sheet stays clean.
	const wxColour def = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW);
	if (!c.m_backgroundColour.IsOk() || c.m_backgroundColour == def)
		return 0;
	const wxString body = wxT("<fill><patternFill patternType=\"solid\"><fgColor rgb=\"")
		+ ArgbHex(c.m_backgroundColour) + wxT("\"/><bgColor indexed=\"64\"/></patternFill></fill>");
	return st.Intern(st.m_fills, st.m_fillKey, body);
}

const wxChar* BorderStyleName(const ibSpreadsheetBorderDescription& b)
{
	if (b.m_style == wxPENSTYLE_TRANSPARENT)
		return nullptr;   // no edge
	switch (b.m_style) {
	case wxPENSTYLE_DOT:        return wxT("dotted");
	case wxPENSTYLE_LONG_DASH:
	case wxPENSTYLE_SHORT_DASH: return wxT("dashed");
	case wxPENSTYLE_DOT_DASH:   return wxT("dashDot");
	default:                    return (b.m_width >= 3) ? wxT("thick")
	                                 : (b.m_width == 2) ? wxT("medium") : wxT("thin");
	}
}

int BorderIndex(StyleTables& st, const ibSpreadsheetCellDescription& c)
{
	// m_borderAt is [left, right, top, bottom].
	auto edge = [](const wxChar* tag, const ibSpreadsheetBorderDescription& b) -> wxString {
		const wxChar* style = BorderStyleName(b);
		if (style == nullptr)
			return wxString::Format(wxT("<%s/>"), tag);
		return wxString::Format(wxT("<%s style=\"%s\"><color rgb=\"%s\"/></%s>"),
			tag, style, ArgbHex(b.m_colour), tag);
	};
	const wxString body = wxT("<border>")
		+ edge(wxT("left"),   c.m_borderAt[0])
		+ edge(wxT("right"),  c.m_borderAt[1])
		+ edge(wxT("top"),    c.m_borderAt[2])
		+ edge(wxT("bottom"), c.m_borderAt[3])
		+ wxT("<diagonal/></border>");
	if (body == st.m_borders[0])
		return 0;
	return st.Intern(st.m_borders, st.m_borderKey, body);
}

wxString AlignmentBody(const ibSpreadsheetCellDescription& c)
{
	const wxChar* h = ((c.m_alignHorz & wxALIGN_RIGHT) == wxALIGN_RIGHT)          ? wxT("right")
	                : ((c.m_alignHorz & wxALIGN_CENTER_HORIZONTAL) != 0)          ? wxT("center")
	                :                                                               wxT("left");
	const wxChar* v = ((c.m_alignVert & wxALIGN_BOTTOM) == wxALIGN_BOTTOM)        ? wxT("bottom")
	                : ((c.m_alignVert & wxALIGN_CENTER_VERTICAL) != 0)            ? wxT("center")
	                :                                                               wxT("top");
	wxString a = wxString::Format(wxT("<alignment horizontal=\"%s\" vertical=\"%s\""), h, v);
	if (c.m_textOrient == wxVERTICAL)
		a += wxT(" textRotation=\"90\"");
	a += wxT("/>");
	return a;
}

int XfIndex(StyleTables& st, const ibSpreadsheetCellDescription& c)
{
	const int fontId   = FontIndex(st, c);
	const int fillId   = FillIndex(st, c);
	const int borderId = BorderIndex(st, c);
	const wxString align = AlignmentBody(c);

	wxString body = wxString::Format(
		wxT("<xf numFmtId=\"0\" fontId=\"%d\" fillId=\"%d\" borderId=\"%d\" xfId=\"0\""),
		fontId, fillId, borderId);
	body += wxString::Format(wxT(" applyFont=\"%d\" applyFill=\"%d\" applyBorder=\"%d\" applyAlignment=\"1\">"),
		fontId ? 1 : 0, fillId ? 1 : 0, borderId ? 1 : 0);
	body += align;
	body += wxT("</xf>");
	return st.Intern(st.m_xfs, st.m_xfKey, body);
}

// --- part bodies -------------------------------------------------------------

wxString ContentTypes()
{
	return wxT("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>")
	       wxT("<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">")
	       wxT("<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>")
	       wxT("<Default Extension=\"xml\" ContentType=\"application/xml\"/>")
	       wxT("<Override PartName=\"/xl/workbook.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>")
	       wxT("<Override PartName=\"/xl/worksheets/sheet1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>")
	       wxT("<Override PartName=\"/xl/styles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml\"/>")
	       wxT("</Types>");
}

wxString RootRels()
{
	return wxT("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>")
	       wxT("<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">")
	       wxT("<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"xl/workbook.xml\"/>")
	       wxT("</Relationships>");
}

wxString WorkbookXml(const wxString& sheetName)
{
	return wxT("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>")
	       wxT("<workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" ")
	       wxT("xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">")
	       wxT("<sheets><sheet name=\"") + XmlEsc(sheetName) +
	       wxT("\" sheetId=\"1\" r:id=\"rId1\"/></sheets></workbook>");
}

wxString WorkbookRels()
{
	return wxT("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>")
	       wxT("<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">")
	       wxT("<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" Target=\"worksheets/sheet1.xml\"/>")
	       wxT("<Relationship Id=\"rId2\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" Target=\"styles.xml\"/>")
	       wxT("</Relationships>");
}

wxString StylesXml(const StyleTables& st)
{
	auto join = [](const std::vector<wxString>& v) {
		wxString o; for (const wxString& s : v) o += s; return o;
	};
	wxString out = wxT("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>");
	out += wxT("<styleSheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">");
	out += wxString::Format(wxT("<fonts count=\"%zu\">"), st.m_fonts.size())   + join(st.m_fonts)   + wxT("</fonts>");
	out += wxString::Format(wxT("<fills count=\"%zu\">"), st.m_fills.size())   + join(st.m_fills)   + wxT("</fills>");
	out += wxString::Format(wxT("<borders count=\"%zu\">"), st.m_borders.size()) + join(st.m_borders) + wxT("</borders>");
	out += wxT("<cellStyleXfs count=\"1\"><xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\"/></cellStyleXfs>");
	out += wxString::Format(wxT("<cellXfs count=\"%zu\">"), st.m_xfs.size())   + join(st.m_xfs)     + wxT("</cellXfs>");
	out += wxT("<cellStyles count=\"1\"><cellStyle name=\"Normal\" xfId=\"0\" builtinId=\"0\"/></cellStyles>");
	out += wxT("</styleSheet>");
	return out;
}

} // namespace

bool ibXlsxExporter::Save(const ibSpreadsheetDescription& desc,
                          const wxString& fileName, const wxString& sheetName)
{
	const int rows = desc.GetNumberRows();
	const int cols = desc.GetNumberCols();

	// --- pass 1: build the style tables and each visible cell's xf index --------
	StyleTables st;
	std::map<const ibSpreadsheetCellDescription*, int> cellXf;
	std::map<unsigned int, std::vector<const ibSpreadsheetCellDescription*>> byRow;
	std::vector<wxString> merges;

	for (int i = 0; i < desc.GetCellCount(); ++i) {
		const ibSpreadsheetCellDescription* c = desc.GetCellByIdx((size_t)i);
		if (c == nullptr)
			continue;
		int nr = 0, nc = 0;
		const int span = c->GetSize(&nr, &nc);
		if (span < 0)
			continue;   // covered by a merge — the anchor carries the value, this is empty
		if (span > 0)   // anchor of a multi-cell span
			merges.push_back(CellRef(c->m_row, c->m_col) + wxT(":")
				+ CellRef(c->m_row + nr - 1, c->m_col + nc - 1));
		cellXf[c] = XfIndex(st, *c);
		byRow[c->m_row].push_back(c);
	}

	// --- pass 2: the worksheet --------------------------------------------------
	wxString ws = wxT("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>");
	ws += wxT("<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">");
	if (rows > 0 && cols > 0)
		ws += wxT("<dimension ref=\"A1:") + CellRef(rows - 1, cols - 1) + wxT("\"/>");

	// Columns with a stored width (px). XLSX width is in CHARACTERS (~7 px each); a
	// width of 0 in our model is a hidden column.
	if (desc.GetSizeNumberCols() > 0) {
		wxString colsXml;
		for (int i = 0; i < desc.GetSizeNumberCols(); ++i) {
			const ibSpreadsheetColSizeDescription* cs = desc.GetColSizeByIdx((size_t)i);
			if (cs == nullptr)
				continue;
			const unsigned int col1 = cs->m_col + 1;
			if (cs->m_width == 0) {
				colsXml += wxString::Format(wxT("<col min=\"%u\" max=\"%u\" width=\"8.43\" hidden=\"1\"/>"), col1, col1);
			} else {
				const double chars = (double)cs->m_width / 7.0;
				colsXml += wxString::Format(wxT("<col min=\"%u\" max=\"%u\" width=\"%.2f\" customWidth=\"1\"/>"), col1, col1, chars);
			}
		}
		if (!colsXml.IsEmpty())
			ws += wxT("<cols>") + colsXml + wxT("</cols>");
	}

	ws += wxT("<sheetData>");
	for (auto& rowPair : byRow) {
		const unsigned int row = rowPair.first;
		// Row height (px -> points, 96dpi): only when a custom height is stored.
		wxString rowAttr = wxString::Format(wxT("<row r=\"%u\""), row + 1);
		{
			const int h = desc.GetRowSize((int)row);
			bool custom = false;
			for (int i = 0; i < desc.GetSizeNumberRows(); ++i) {
				const ibSpreadsheetRowSizeDescription* rs = desc.GetRowSizeByIdx((size_t)i);
				if (rs != nullptr && rs->m_row == row) { custom = true; break; }
			}
			if (custom) {
				if (h == 0) rowAttr += wxT(" hidden=\"1\"");
				else        rowAttr += wxString::Format(wxT(" ht=\"%.2f\" customHeight=\"1\""), (double)h * 0.75);
			}
		}
		ws += rowAttr + wxT(">");

		// Cells in ascending column order.
		std::vector<const ibSpreadsheetCellDescription*>& cells = rowPair.second;
		std::sort(cells.begin(), cells.end(),
			[](const ibSpreadsheetCellDescription* a, const ibSpreadsheetCellDescription* b) { return a->m_col < b->m_col; });

		for (const ibSpreadsheetCellDescription* c : cells) {
			const wxString ref = CellRef(c->m_row, c->m_col);
			const int s = cellXf[c];
			const wxString sAttr = s ? wxString::Format(wxT(" s=\"%d\""), s) : wxString();

			if (c->m_value.IsEmpty()) {
				ws += wxT("<c r=\"") + ref + wxT("\"") + sAttr + wxT("/>");
				continue;
			}
			wxString num;
			if (LooksNumeric(c->m_value, num)) {
				ws += wxT("<c r=\"") + ref + wxT("\"") + sAttr + wxT("><v>") + num + wxT("</v></c>");
			} else {
				ws += wxT("<c r=\"") + ref + wxT("\"") + sAttr + wxT(" t=\"inlineStr\"><is><t xml:space=\"preserve\">")
				    + XmlEsc(c->m_value) + wxT("</t></is></c>");
			}
		}
		ws += wxT("</row>");
	}
	ws += wxT("</sheetData>");

	if (!merges.empty()) {
		ws += wxString::Format(wxT("<mergeCells count=\"%zu\">"), merges.size());
		for (const wxString& m : merges)
			ws += wxT("<mergeCell ref=\"") + m + wxT("\"/>");
		ws += wxT("</mergeCells>");
	}
	ws += wxT("</worksheet>");

	// --- write the ZIP package --------------------------------------------------
	wxFFileOutputStream fileOut(fileName);
	if (!fileOut.IsOk())
		return false;
	wxZipOutputStream zip(fileOut);

	auto put = [&zip](const wxString& partName, const wxString& xml) -> bool {
		if (!zip.PutNextEntry(partName))
			return false;
		const wxScopedCharBuffer utf8 = xml.utf8_str();
		zip.Write(utf8.data(), utf8.length());
		return zip.IsOk();
	};

	bool ok = true;
	ok = ok && put(wxT("[Content_Types].xml"),        ContentTypes());
	ok = ok && put(wxT("_rels/.rels"),                RootRels());
	ok = ok && put(wxT("xl/workbook.xml"),            WorkbookXml(sheetName));
	ok = ok && put(wxT("xl/_rels/workbook.xml.rels"), WorkbookRels());
	ok = ok && put(wxT("xl/styles.xml"),              StylesXml(st));
	ok = ok && put(wxT("xl/worksheets/sheet1.xml"),   ws);

	ok = zip.Close() && ok;
	return ok;
}
