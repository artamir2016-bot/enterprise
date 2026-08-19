// =============================================================================
// OES Enterprise — friendly-JSON configuration spec engine (ibBuildConfigFromJsonSpec)
//
// Proves the external-generation path: a high-level JSON spec builds a live
// configuration through the metadata API, serializes it, and reloads (byte
// fixed-point round-trip). Content is asserted on the serialized buffer — object
// / attribute names and module code are stored as UTF-8 (w_stringZ), so they
// appear verbatim in the blob. This is independent of the runtime object index
// (which is only populated when objects are "run"; the spec builds with
// runObject=false, like the Designer's load path).
//
// DB-free: ibMetaDataConfigurationFile has a public ctor and SaveConfigToBuffer
// serializes with saveToFileFlag (no DB hooks) — see test_metadataSerialize.cpp.
// =============================================================================

#include <gtest/gtest.h>

#include <cstring>
#include <algorithm>
#include <vector>
#include <functional>

#include "backend/metadataConfiguration.h"
#include "backend/metadataConfigSpec.h"
#include "backend/metaCollection/metaFormObject.h"     // ibValueMetaObjectForm / FormBlobToNode
#include "backend/serialize/dataBuilder.h"             // ibDataNode / ibDataValue (form control tree)
#include "backend/clsid.h"                             // control_to_clsid

namespace {

// Depth-first search for the first form metaobject under a config.
ibValueMetaObjectForm* FindFirstForm(ibValueMetaObject* obj) {
	if (obj == nullptr)
		return nullptr;
	if (auto* form = dynamic_cast<ibValueMetaObjectForm*>(obj))
		return form;
	for (unsigned int i = 0; i < obj->GetChildCount(); i++)
		if (ibValueMetaObjectForm* found = FindFirstForm(obj->GetChild(i)))
			return found;
	return nullptr;
}

// Recursively collect every control node clsid in a form-data node tree.
void CollectClsids(const ibDataNode& node, std::vector<ibClassID>& out) {
	for (const ibDataNode& child : node.Children()) {
		out.push_back(child.GetClsid());
		CollectClsids(child, out);
	}
}

const char* kSpec = R"JSON({
  "name": "SpecCfg",
  "catalogs": [
    { "name": "Products",
      "attributes": [
        { "name": "Price",   "type": "Number", "precision": 15, "scale": 2 },
        { "name": "Article", "type": "String", "length": 20 }
      ] }
  ],
  "documents": [
    { "name": "Invoice",
      "attributes": [ { "name": "Sum", "type": "Number" } ] }
  ],
  "commonModules": [
    { "name": "CommonFunctions",
      "code": "Function F() Export Return 1; EndFunction" }
  ]
})JSON";

// Does the raw serialized blob contain this ASCII needle?
bool BufferContains(const wxMemoryBuffer& buf, const char* needle) {
	const size_t n = std::strlen(needle);
	if (buf.GetDataLen() < n)
		return false;
	const char* hay = static_cast<const char*>(buf.GetData());
	const size_t end = buf.GetDataLen() - n;
	for (size_t i = 0; i <= end; ++i)
		if (std::memcmp(hay + i, needle, n) == 0)
			return true;
	return false;
}

} // namespace

TEST(ConfigSpec, BuildFromJson_SerializesObjectsAndModule) {
	ibMetaDataConfigurationFile cfg;
	wxString err;
	ASSERT_TRUE(ibBuildConfigFromJsonSpec(wxString::FromUTF8(kSpec), cfg, err)) << err.utf8_str();

	EXPECT_TRUE(cfg.GetCommonMetaObject()->GetName().IsSameAs(wxT("SpecCfg")));

	wxMemoryBuffer buf;
	ASSERT_TRUE(cfg.SaveConfigToBuffer(buf));
	ASSERT_GT(buf.GetDataLen(), 0u);

	EXPECT_TRUE(BufferContains(buf, "Products"));
	EXPECT_TRUE(BufferContains(buf, "Article"));
	EXPECT_TRUE(BufferContains(buf, "Invoice"));
	EXPECT_TRUE(BufferContains(buf, "Sum"));
	EXPECT_TRUE(BufferContains(buf, "CommonFunctions"));
	EXPECT_TRUE(BufferContains(buf, "Function F()"));  // module code survives
}

TEST(ConfigSpec, BuildFromJson_RoundTripsByteEqual) {
	ibMetaDataConfigurationFile cfg;
	wxString err;
	ASSERT_TRUE(ibBuildConfigFromJsonSpec(wxString::FromUTF8(kSpec), cfg, err)) << err.utf8_str();

	wxMemoryBuffer b1;
	ASSERT_TRUE(cfg.SaveConfigToBuffer(b1));

	ibMetaDataConfigurationFile back;
	ASSERT_TRUE(back.LoadConfigFromBuffer(b1));

	wxMemoryBuffer b2;
	ASSERT_TRUE(back.SaveConfigToBuffer(b2));

	ASSERT_EQ(b1.GetDataLen(), b2.GetDataLen());
	EXPECT_EQ(0, std::memcmp(b1.GetData(), b2.GetData(), b1.GetDataLen()));
}

// ---- Extended metatypes (1C-import MVP) -----------------------------------

const char* kSpecFull = R"JSON({
  "name": "FullCfg",
  "catalogs": [
    { "name": "Owner", "attributes": [ { "name": "Code", "type": "String", "length": 9 } ] },
    { "name": "Products",
      "attributes": [
        { "name": "Price", "type": "Number", "precision": 15, "scale": 2 },
        { "name": "Boss",  "type": "ref", "refs": ["Catalog.Owner"] }
      ],
      "tabularSections": [
        { "name": "Lines", "attributes": [ { "name": "Qty", "type": "Number" } ] }
      ],
      "objectModule": "Procedure OnWrite() Export\nEndProcedure" }
  ],
  "enums": [
    { "name": "Status", "values": ["Draft", "Posted", "Cancelled"] }
  ],
  "constants": [
    { "name": "CompanyName", "type": "String", "length": 100 }
  ],
  "informationRegisters": [
    { "name": "Prices",
      "dimensions": [ { "name": "Product", "type": "ref", "refs": ["Catalog.Products"] } ],
      "resources":  [ { "name": "Amount",  "type": "Number", "precision": 15, "scale": 2 } ] }
  ],
  "accumulationRegisters": [
    { "name": "Stock",
      "dimensions": [ { "name": "Product", "type": "ref", "refs": ["Catalog.Products"] } ],
      "resources":  [ { "name": "Count",   "type": "Number" } ] }
  ]
})JSON";

TEST(ConfigSpec, BuildFull_SerializesAllMetatypes) {
	ibMetaDataConfigurationFile cfg;
	wxString err;
	ASSERT_TRUE(ibBuildConfigFromJsonSpec(wxString::FromUTF8(kSpecFull), cfg, err)) << err.utf8_str();

	wxMemoryBuffer buf;
	ASSERT_TRUE(cfg.SaveConfigToBuffer(buf));

	for (const char* s : { "Owner", "Products", "Boss", "Lines", "Qty",
	                       "Status", "Draft", "Posted", "Cancelled",
	                       "CompanyName", "Prices", "Amount", "Stock", "Count", "Product" })
		EXPECT_TRUE(BufferContains(buf, s)) << "missing: " << s;
}

TEST(ConfigSpec, BuildFull_RoundTripsByteEqual) {
	ibMetaDataConfigurationFile cfg;
	wxString err;
	ASSERT_TRUE(ibBuildConfigFromJsonSpec(wxString::FromUTF8(kSpecFull), cfg, err)) << err.utf8_str();

	wxMemoryBuffer b1;
	ASSERT_TRUE(cfg.SaveConfigToBuffer(b1));
	ibMetaDataConfigurationFile back;
	ASSERT_TRUE(back.LoadConfigFromBuffer(b1));
	wxMemoryBuffer b2;
	ASSERT_TRUE(back.SaveConfigToBuffer(b2));

	ASSERT_EQ(b1.GetDataLen(), b2.GetDataLen());
	EXPECT_EQ(0, std::memcmp(b1.GetData(), b2.GetData(), b1.GetDataLen()));
}

TEST(ConfigSpec, BuildFull_UnresolvedRefDegradesGracefully) {
	// A reference to an out-of-MVP / missing target must not fail the import.
	ibMetaDataConfigurationFile cfg;
	wxString err;
	const char* spec = R"JSON({ "catalogs": [ { "name": "X",
		"attributes": [ { "name": "A", "type": "ref", "refs": ["ChartOfAccounts.Missing"] } ] } ] })JSON";
	EXPECT_TRUE(ibBuildConfigFromJsonSpec(wxString::FromUTF8(spec), cfg, err)) << err.utf8_str();
}

TEST(ConfigSpec, BuildFromJson_CreatesForms) {
	ibMetaDataConfigurationFile cfg;
	wxString err;
	const char* spec = R"JSON({
	  "catalogs": [
	    { "name": "Products",
	      "attributes": [ { "name": "Price", "type": "Number" } ],
	      "forms": [
	        { "name": "ItemForm", "type": "object",
	          "module": "Procedure OnOpen() Public\nEndProcedure" },
	        { "name": "ListForm", "type": "list" }
	      ] }
	  ]
	})JSON";
	ASSERT_TRUE(ibBuildConfigFromJsonSpec(wxString::FromUTF8(spec), cfg, err)) << err.utf8_str();

	wxMemoryBuffer buf;
	ASSERT_TRUE(cfg.SaveConfigToBuffer(buf));
	EXPECT_TRUE(BufferContains(buf, "ItemForm"));
	EXPECT_TRUE(BufferContains(buf, "ListForm"));
	EXPECT_TRUE(BufferContains(buf, "Procedure OnOpen()"));

	// Round-trip byte equality with forms present.
	ibMetaDataConfigurationFile back;
	ASSERT_TRUE(back.LoadConfigFromBuffer(buf));
	wxMemoryBuffer buf2;
	ASSERT_TRUE(back.SaveConfigToBuffer(buf2));
	ASSERT_EQ(buf.GetDataLen(), buf2.GetDataLen());
	EXPECT_EQ(0, std::memcmp(buf.GetData(), buf2.GetData(), buf.GetDataLen()));
}

TEST(ConfigSpec, BuildFromJson_CreatesFormControlTree) {
	// MVP-B: a form's "controls" tree is replicated into the FormData blob.
	ibMetaDataConfigurationFile cfg;
	wxString err;
	const char* spec = R"JSON({
	  "catalogs": [
	    { "name": "Products",
	      "attributes": [
	        { "name": "Price",  "type": "Number" },
	        { "name": "Active", "type": "Boolean" }
	      ],
	      "tabularSections": [
	        { "name": "Lines", "attributes": [ { "name": "Qty", "type": "Number" } ] }
	      ],
	      "forms": [
	        { "name": "ItemForm", "type": "object",
	          "controls": [
	            { "kind": "group", "name": "Header", "children": [
	              { "kind": "field",    "name": "PriceField", "attr": "Price" },
	              { "kind": "checkbox", "name": "ActiveFlag", "attr": "Active" }
	            ] },
	            { "kind": "table", "name": "LinesTable", "attr": "Lines", "children": [
	              { "kind": "column", "name": "QtyCol", "field": "Qty", "title": "Quantity" }
	            ] }
	          ] }
	      ] }
	  ]
	})JSON";
	ASSERT_TRUE(ibBuildConfigFromJsonSpec(wxString::FromUTF8(spec), cfg, err)) << err.utf8_str();

	// The form carries a non-empty FormData blob (control tree present, not auto-layout).
	ibValueMetaObjectForm* form = FindFirstForm(cfg.GetCommonMetaObject());
	ASSERT_NE(form, nullptr);
	const wxMemoryBuffer blob = form->GetFormData();
	ASSERT_GT(blob.GetDataLen(), 0u);

	// The blob decodes to a control node tree with the expected control clsids.
	const ibDataValue rootVal = ibValueMetaObjectFormBase::FormBlobToNode(blob);
	ASSERT_EQ(rootVal.Kind(), ibDataKind::Child);
	std::vector<ibClassID> clsids;
	CollectClsids(*rootVal.AsChild(), clsids);

	auto has = [&](ibClassID c) {
		return std::find(clsids.begin(), clsids.end(), c) != clsids.end();
	};
	EXPECT_TRUE(has(control_to_clsid("CT_SIZR")));  // SizerItem layout wrappers (required for visible layout)
	EXPECT_TRUE(has(control_to_clsid("CT_TXTC")));  // field
	EXPECT_TRUE(has(control_to_clsid("CT_CHKB")));  // checkbox
	EXPECT_TRUE(has(control_to_clsid("CT_TABL")));  // table
	EXPECT_TRUE(has(control_to_clsid("CT_TBLC")));  // column
	// Groups are flattened (their children lifted into the parent) — a nested Boxsizer
	// tree crashes the visual host's loaded-tree layout, so no CT_BSZR is emitted.
	EXPECT_FALSE(has(control_to_clsid("CT_BSZR")));

	// A widget must sit inside a SizerItem — verify the parent chain is ... -> SizerItem -> control.
	std::function<bool(const ibDataNode&, ibClassID)> hasChildClsid =
		[&](const ibDataNode& n, ibClassID want) {
			for (const ibDataNode& ch : n.Children())
				if (ch.GetClsid() == want) return true;
			return false;
		};
	std::function<bool(const ibDataNode&)> sizerItemWrapsAWidget =
		[&](const ibDataNode& n) -> bool {
			for (const ibDataNode& ch : n.Children()) {
				if (ch.GetClsid() == control_to_clsid("CT_SIZR") &&
				    (hasChildClsid(ch, control_to_clsid("CT_TXTC")) ||
				     hasChildClsid(ch, control_to_clsid("CT_NTBK"))))
					return true;
				if (sizerItemWrapsAWidget(ch)) return true;
			}
			return false;
		};
	EXPECT_TRUE(sizerItemWrapsAWidget(*rootVal.AsChild()));

	// Config byte round-trip holds with the control blob embedded.
	wxMemoryBuffer b1;
	ASSERT_TRUE(cfg.SaveConfigToBuffer(b1));
	ibMetaDataConfigurationFile back;
	ASSERT_TRUE(back.LoadConfigFromBuffer(b1));
	wxMemoryBuffer b2;
	ASSERT_TRUE(back.SaveConfigToBuffer(b2));
	ASSERT_EQ(b1.GetDataLen(), b2.GetDataLen());
	EXPECT_EQ(0, std::memcmp(b1.GetData(), b2.GetData(), b1.GetDataLen()));
}

TEST(ConfigSpec, BuildFromJson_LabelCarriesTitleIntoBlob) {
	// A 1C LabelDecoration (kind "label") with a caption must land in the FormData
	// blob as a Statictext whose "Title" holds the raw-loc-text verbatim. Without
	// it the caption resolves to empty (ibPropertyTString parses `code = '..';`).
	ibMetaDataConfigurationFile cfg;
	wxString err;
	const char* spec = R"JSON({
	  "catalogs": [
	    { "name": "Products",
	      "attributes": [ { "name": "Price", "type": "Number" } ],
	      "forms": [
	        { "name": "ItemForm", "type": "object",
	          "controls": [
	            { "kind": "label", "name": "Marker", "title": "ru = '%';ro = '%';" },
	            { "kind": "field", "name": "PriceField", "attr": "Price" }
	          ] }
	      ] }
	  ]
	})JSON";
	ASSERT_TRUE(ibBuildConfigFromJsonSpec(wxString::FromUTF8(spec), cfg, err)) << err.utf8_str();

	ibValueMetaObjectForm* form = FindFirstForm(cfg.GetCommonMetaObject());
	ASSERT_NE(form, nullptr);
	const wxMemoryBuffer blob = form->GetFormData();
	ASSERT_GT(blob.GetDataLen(), 0u);

	const ibDataValue rootVal = ibValueMetaObjectFormBase::FormBlobToNode(blob);
	ASSERT_EQ(rootVal.Kind(), ibDataKind::Child);

	// Walk to the Statictext node and read back its Title property.
	std::function<const ibDataNode*(const ibDataNode&)> findStatic =
		[&](const ibDataNode& n) -> const ibDataNode* {
			for (const ibDataNode& ch : n.Children()) {
				if (ch.GetClsid() == control_to_clsid("CT_STTX")) return &ch;
				if (const ibDataNode* r = findStatic(ch)) return r;
			}
			return nullptr;
		};
	const ibDataNode* stat = findStatic(*rootVal.AsChild());
	ASSERT_NE(stat, nullptr) << "the label emits a Statictext node";
	const ibDataValue title = stat->GetProperty(wxT("Title"));
	ASSERT_EQ(title.Kind(), ibDataKind::String);
	EXPECT_EQ(title.AsString(), wxT("ru = '%';ro = '%';"));
}

TEST(ConfigSpec, BuildFromJson_EmitsRealNotebookTabs) {
	// A 1C Pages/Page tree must become a real OES Notebook (CT_NTBK) holding
	// NotebookPage tabs (CT_NTPG) — not flattened. Bound fields inside a page
	// ride SizerItem cells whose parent is the page window (the shape that once
	// crashed the loaded-tree layout, now fixed).
	ibMetaDataConfigurationFile cfg;
	wxString err;
	const char* spec = R"JSON({
	  "catalogs": [
	    { "name": "Products",
	      "attributes": [ { "name": "Price", "type": "Number" } ],
	      "forms": [
	        { "name": "ItemForm", "type": "object",
	          "controls": [
	            { "kind": "pages", "name": "Tabs", "children": [
	              { "kind": "page", "name": "Main", "title": "ru = 'Base';", "children": [
	                { "kind": "field", "name": "PriceField", "attr": "Price" }
	              ] },
	              { "kind": "page", "name": "Extra", "children": [
	                { "kind": "group", "name": "Grp", "children": [
	                  { "kind": "label", "name": "Note", "title": "ru = 'x';" }
	                ] }
	              ] }
	            ] }
	          ] }
	      ] }
	  ]
	})JSON";
	ASSERT_TRUE(ibBuildConfigFromJsonSpec(wxString::FromUTF8(spec), cfg, err)) << err.utf8_str();

	ibValueMetaObjectForm* form = FindFirstForm(cfg.GetCommonMetaObject());
	ASSERT_NE(form, nullptr);
	const wxMemoryBuffer blob = form->GetFormData();
	ASSERT_GT(blob.GetDataLen(), 0u);

	const ibDataValue rootVal = ibValueMetaObjectFormBase::FormBlobToNode(blob);
	ASSERT_EQ(rootVal.Kind(), ibDataKind::Child);
	std::vector<ibClassID> clsids;
	CollectClsids(*rootVal.AsChild(), clsids);
	auto has = [&](ibClassID c) {
		return std::find(clsids.begin(), clsids.end(), c) != clsids.end();
	};
	EXPECT_TRUE(has(control_to_clsid("CT_NTBK")));  // the notebook
	EXPECT_TRUE(has(control_to_clsid("CT_NTPG")));  // its pages
	EXPECT_TRUE(has(control_to_clsid("CT_TXTC")));  // bound field inside a page
	// Two pages emitted.
	const long pageCount = std::count(clsids.begin(), clsids.end(), control_to_clsid("CT_NTPG"));
	EXPECT_EQ(pageCount, 2);
	// Groups inside a page are still flattened (no Boxsizer emitted).
	EXPECT_FALSE(has(control_to_clsid("CT_BSZR")));

	// A NotebookPage is added to the Notebook DIRECTLY (no SizerItem between
	// them); the notebook itself and the page's fields DO ride SizerItem cells.
	std::function<bool(const ibDataNode&)> notebookHoldsPageDirectly =
		[&](const ibDataNode& n) -> bool {
			for (const ibDataNode& ch : n.Children()) {
				if (ch.GetClsid() == control_to_clsid("CT_NTBK")) {
					for (const ibDataNode& pg : ch.Children())
						if (pg.GetClsid() == control_to_clsid("CT_NTPG"))
							return true;   // page is a direct child of the notebook
				}
				if (notebookHoldsPageDirectly(ch)) return true;
			}
			return false;
		};
	EXPECT_TRUE(notebookHoldsPageDirectly(*rootVal.AsChild()));

	// The first page carries its tab caption (raw-loc-text) as the Title property.
	std::function<const ibDataNode*(const ibDataNode&)> firstPage =
		[&](const ibDataNode& n) -> const ibDataNode* {
			for (const ibDataNode& ch : n.Children()) {
				if (ch.GetClsid() == control_to_clsid("CT_NTPG")) return &ch;
				if (const ibDataNode* r = firstPage(ch)) return r;
			}
			return nullptr;
		};
	const ibDataNode* page = firstPage(*rootVal.AsChild());
	ASSERT_NE(page, nullptr);
	const ibDataValue pageTitle = page->GetProperty(wxT("Title"));
	ASSERT_EQ(pageTitle.Kind(), ibDataKind::String);
	EXPECT_EQ(pageTitle.AsString(), wxT("ru = 'Base';"));

	// Byte round-trip holds with the notebook blob embedded.
	wxMemoryBuffer b1;
	ASSERT_TRUE(cfg.SaveConfigToBuffer(b1));
	ibMetaDataConfigurationFile back;
	ASSERT_TRUE(back.LoadConfigFromBuffer(b1));
	wxMemoryBuffer b2;
	ASSERT_TRUE(back.SaveConfigToBuffer(b2));
	ASSERT_EQ(b1.GetDataLen(), b2.GetDataLen());
	EXPECT_EQ(0, std::memcmp(b1.GetData(), b2.GetData(), b1.GetDataLen()));
}

TEST(ConfigSpec, BuildFromJson_RejectsMalformedJson) {
	ibMetaDataConfigurationFile cfg;
	wxString err;
	const char* bad = R"JSON({ "catalogs": [ { "name": "X" )JSON";  // truncated
	EXPECT_FALSE(ibBuildConfigFromJsonSpec(wxString::FromUTF8(bad), cfg, err));
	EXPECT_FALSE(err.IsEmpty());
}

TEST(ConfigSpec, BuildFromJson_RejectsMissingName) {
	ibMetaDataConfigurationFile cfg;
	wxString err;
	const char* bad = R"JSON({ "catalogs": [ { "attributes": [] } ] })JSON";
	EXPECT_FALSE(ibBuildConfigFromJsonSpec(wxString::FromUTF8(bad), cfg, err));
	EXPECT_FALSE(err.IsEmpty());
}
