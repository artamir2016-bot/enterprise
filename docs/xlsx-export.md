# XLSX export — independent OOXML writer

Status: **built** (`ibXlsxExporter`, `ibBackendSpreadsheetObject::SaveToXlsx`), covered by
`tests/test_xlsxExport.cpp`, cross-validated against openpyxl.

## Why this document exists (provenance)

Upstream relicensed to PolyForm Noncommercial on 2026‑08‑23 and its own spreadsheet-format
code (`sheetFormat/`) is under that licence — it may **not** be copied into this product. The
licence permits the opposite: *"Learning from this and then building your own thing is NOT
forbidden … write your own implementation of it. Take the understanding, leave the source."*

So this feature is an **independent implementation**. It was written from:
- the **public** ECMA‑376 (Office Open XML) standard — the on-disk format of `.xlsx`; and
- **our own** `ibSpreadsheetDescription` model (LGPL, predates the relicence).

No upstream post‑cutoff source was read or ported. The design below is the *idea* (the file
format, which is public) mapped onto our data structures.

## The idea: what an `.xlsx` actually is

An `.xlsx` is an **OPC package** — a ZIP whose entries are XML "parts". A minimal valid
workbook needs exactly these parts:

| Part | Role |
|---|---|
| `[Content_Types].xml` | the MIME type of every part (by extension + per-part overrides) |
| `_rels/.rels` | the root relationship → points at the workbook |
| `xl/workbook.xml` | declares the sheets (name + relationship id) |
| `xl/_rels/workbook.xml.rels` | workbook → worksheet(s) + styles |
| `xl/styles.xml` | fonts / fills / borders combined into cell formats (`cellXfs`) |
| `xl/worksheets/sheet1.xml` | columns, rows, cells, merges |

Key format facts a writer must honour:
- **Cell references are A1-style**: column 0 → `A`, 25 → `Z`, 26 → `AA`; rows are 1-based.
- **A cell references ONE style** by index into `cellXfs` (`<c r="A1" s="3">`). Style 0 is the
  default. Fills 0 and 1 are reserved by Excel (`none`, `gray125`); custom fills start at 2.
- **Values**: a number is `<c r=..><v>123.45</v></c>`; text is either a shared string or, as
  here, an **inline string** `<c r=.. t="inlineStr"><is><t>…</t></is></c>`. Inline strings
  avoid a shared-string part and are fully valid.
- **Merged cells** are declared once per anchor in `<mergeCells>` as `A1:C1`.
- **Units differ from the screen**: column width is in *characters* (~7 px each for the default
  font); row height is in *points* (px × 72/96); border/fill colours are ARGB hex (`FFRRGGBB`).

## Our mapping (`ibSpreadsheetDescription` → OOXML)

Reads our model (never mutates it). Source of truth: `backend/spreadsheetDescription.h`.

| Our model | OOXML |
|---|---|
| `ibSpreadsheetCellDescription.m_value` (always text) | number `<v>` when unambiguously numeric, else inline `<t>` |
| `m_row` / `m_col` (0-based) | `r="A1"` |
| `SetSize(nr,nc)` anchor (`GetSize()==1`) | `<mergeCell ref="A1:C1"/>`; covered cells (`==-1`) skipped |
| `m_font` + `m_textColour` | `<font>` (size, name, `<b/>`/`<i/>`/`<u/>`, `<color rgb>`) |
| `m_backgroundColour` (≠ window default) | solid `<fill>` |
| `m_borderAt[4]` (L,R,T,B) | `<border>` per edge; pen style → `thin/medium/thick/dotted/dashed/dashDot` |
| `m_alignHorz` / `m_alignVert` / `m_textOrient` | `<alignment horizontal vertical textRotation>` |
| `SetColSize(col, px)` | `<col width="px/7" customWidth="1">`; width 0 → `hidden` |
| `SetRowSize(row, px)` | `<row ht="px*0.75" customHeight="1">`; 0 → `hidden` |

**Numeric detection** is deliberately conservative — a value becomes a number only when it is
one and would survive Excel's `double`: a C-locale parse must succeed, a leading zero before a
digit keeps it text (codes like `007`), and more than 15 significant digits stays text. This is
the one place a report's meaning could be silently corrupted, so it errs toward text.

**Styles are de-duplicated** by rendering each font/fill/border/xf to its XML body and interning
by that string, so identical formatting shares one entry and unformatted cells reference xf 0.

## Entry points

- `ibXlsxExporter::Save(const ibSpreadsheetDescription&, fileName, sheetName)` — the writer.
- `ibBackendSpreadsheetObject::SaveToXlsx(fileName, sheetName)` — convenience on the document.

The ZIP is written with `wxZipOutputStream` (wxBase, already used by the config store); every
part body is built as a `wxString` and written as UTF‑8. XML values are escaped; every
identifier we emit is ASCII by construction, so a Cyrillic sheet or cell text is safe.

## Not yet done (follow-ups)

- Shared-string table (smaller files for text-heavy sheets; inline strings are correct meanwhile).
- Number formats (`numFmt`) — dates/currency are written as their text today.
- Multiple worksheets; frozen panes / print areas (the model carries them; the writer ignores them).
- `.docx` (Word) export and reading foreign spreadsheets (out of scope here).
