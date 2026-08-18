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

#include "backend/metadataConfiguration.h"
#include "backend/metadataConfigSpec.h"

namespace {

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
