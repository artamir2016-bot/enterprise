// =============================================================================
// OES Enterprise — sample configuration generator
//
// Builds a MINIMAL OES configuration programmatically and writes it to an .mcf
// file. Contents:
//   * Catalog  "Products"  with an attribute "Price" and an object module
//   * Document "Invoice"   with an attribute "Sum"   and an object module
//   * CommonModule "CommonFunctions" with several procedures/functions
//
// Module code is written in the VES dialect (the 1C/BSL-flavoured syntax:
// Function…EndFunction, Procedure…EndProcedure, If…Then…EndIf, While…Do…EndDo).
//
// This is DB-free by construction: ibMetaDataConfigurationFile has a public ctor
// and SaveConfigToFile serializes with saveToFileFlag (no DB hooks). See the
// header comment of tests/test_metadataSerialize.cpp.
//
// Usage:
//   oes_make_sample_config [output.mcf]
// Default output: ./SampleConfig.mcf
// =============================================================================

#include <cstdio>

#include "backend/metadataConfiguration.h"
#include "backend/clsid.h"
#include "backend/metaCollection/metaObject.h"
#include "backend/metaCollection/metaModuleObject.h"

static ibValueMetaObject* MakeChild(ibMetaDataConfigurationFile& cfg,
                                    const ibClassID& clsid,
                                    ibValueMetaObject* parent,
                                    const wxString& name)
{
	ibValueMetaObject* obj = cfg.CreateMetaObject(clsid, parent, /*runObject*/ false, name);
	if (obj == nullptr) {
		std::fprintf(stderr, "FAILED to create metaobject '%s'\n", (const char*)name.utf8_str());
		return nullptr;
	}
	return obj;
}

int main(int argc, char** argv)
{
	const wxString outPath = (argc > 1)
		? wxString::FromUTF8(argv[1])
		: wxString(wxT("SampleConfig.mcf"));

	ibMetaDataConfigurationFile cfg;
	ibValueMetaObjectConfiguration* root = cfg.GetCommonMetaObject();
	if (root == nullptr) {
		std::fprintf(stderr, "no root configuration object\n");
		return 1;
	}
	root->SetName(wxT("SampleConfiguration"));

	// --- Catalog "Products" -------------------------------------------------
	ibValueMetaObject* catalog = MakeChild(cfg, g_metaCatalogCLSID, root, wxT("Products"));
	if (!catalog) return 1;
	MakeChild(cfg, g_metaAttributeCLSID, catalog, wxT("Price"));
	MakeChild(cfg, g_metaAttributeCLSID, catalog, wxT("Article"));

	// --- Document "Invoice" -------------------------------------------------
	ibValueMetaObject* document = MakeChild(cfg, g_metaDocumentCLSID, root, wxT("Invoice"));
	if (!document) return 1;
	MakeChild(cfg, g_metaAttributeCLSID, document, wxT("Sum"));

	// --- CommonModule "CommonFunctions" ------------------------------------
	ibValueMetaObject* cmod = MakeChild(cfg, g_metaCommonModuleCLSID, root, wxT("CommonFunctions"));
	if (!cmod) return 1;

	if (auto* module = dynamic_cast<ibValueMetaObjectCommonModule*>(cmod)) {
		const wxString code =
			wxT("// Sample common module — VES (1C/BSL-flavoured) dialect.\n")
			wxT("\n")
			wxT("Function CalculateLineTotal(Quantity, Price) Export\n")
			wxT("\tReturn Quantity * Price;\n")
			wxT("EndFunction\n")
			wxT("\n")
			wxT("Function ApplyDiscount(Amount, Percent) Export\n")
			wxT("\tIf Percent <= 0 Then\n")
			wxT("\t\tReturn Amount;\n")
			wxT("\tEndIf;\n")
			wxT("\tReturn Amount - Amount * Percent / 100;\n")
			wxT("EndFunction\n")
			wxT("\n")
			wxT("Function Factorial(N) Export\n")
			wxT("\tVar Result;\n")
			wxT("\tResult = 1;\n")
			wxT("\tWhile N > 1 Do\n")
			wxT("\t\tResult = Result * N;\n")
			wxT("\t\tN = N - 1;\n")
			wxT("\tEndDo;\n")
			wxT("\tReturn Result;\n")
			wxT("EndFunction\n")
			wxT("\n")
			wxT("Procedure ShowGreeting(Name) Export\n")
			wxT("\tIf Name = \"\" Then\n")
			wxT("\t\tMessage(\"Hello, guest!\");\n")
			wxT("\tElse\n")
			wxT("\t\tMessage(\"Hello, \" + Name + \"!\");\n")
			wxT("\tEndIf;\n")
			wxT("EndProcedure\n");
		module->SetModuleText(code);
	} else {
		std::fprintf(stderr, "CommonFunctions is not a common module\n");
		return 1;
	}

	if (!cfg.SaveConfigToFile(outPath)) {
		std::fprintf(stderr, "SaveConfigToFile FAILED: %s\n", (const char*)outPath.utf8_str());
		return 1;
	}

	std::printf("OK: wrote %s\n", (const char*)outPath.utf8_str());
	std::printf("  Catalog  Products (Price, Article)\n");
	std::printf("  Document Invoice  (Sum)\n");
	std::printf("  CommonModule CommonFunctions (4 routines, VES dialect)\n");

	// --- Round-trip verification: reload the file into a fresh holder --------
	ibMetaDataConfigurationFile check;
	if (!check.LoadConfigFromFile(outPath)) {
		std::fprintf(stderr, "VERIFY FAILED: could not reload %s\n", (const char*)outPath.utf8_str());
		return 1;
	}
	std::printf("VERIFY: reloaded OK (round-trip)\n");
	return 0;
}
