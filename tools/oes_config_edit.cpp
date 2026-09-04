// =============================================================================
// OES Enterprise — CLI configuration EDITOR (incremental merge).
//
// Applies a friendly JSON spec onto an EXISTING .mcf configuration, using the
// backend engine ibApplyConfigFileSpec. Unlike oes_config_gen (greenfield,
// create-only), this MERGES: objects / attributes / tabular sections / enum
// values / modules named in the spec are reused and EDITED if they already
// exist, or created if they don't — so an AI (or any script) can add new objects
// or modify existing ones from the command line. No GUI, backend only.
//
// Usage:
//   oes_config_edit <in.mcf> <patch.json> [out.mcf]
//     out.mcf defaults to in.mcf (edit in place).
//
// The friendly JSON is the same shape as oes_config_gen (see metadataConfigSpec.h):
//   { "catalogs":[{"name":"Товары","attributes":[{"name":"Штрихкод","type":"String","length":13}]}] }
// Only the objects/attributes/modules you mention are touched; the rest of the
// configuration is preserved.
// =============================================================================

#include <cstdio>

#include "backend/metadataConfigSpec.h"
#include "backend/metadataConfiguration.h"

int main(int argc, char** argv)
{
	if (argc < 3) {
		std::fprintf(stderr, "usage: %s <in.mcf> <patch.json> [out.mcf]\n",
			argc > 0 ? argv[0] : "oes_config_edit");
		return 2;
	}

	const wxString inPath   = wxString::FromUTF8(argv[1]);
	const wxString jsonPath = wxString::FromUTF8(argv[2]);
	const wxString outPath  = (argc > 3) ? wxString::FromUTF8(argv[3]) : inPath;

	wxString err;
	if (!ibApplyConfigFileSpec(inPath, jsonPath, outPath, err)) {
		std::fprintf(stderr, "ERROR: %s\n", (const char*)err.utf8_str());
		return 1;
	}
	std::printf("OK: applied %s onto %s -> %s\n",
		(const char*)jsonPath.utf8_str(),
		(const char*)inPath.utf8_str(),
		(const char*)outPath.utf8_str());

	// Round-trip self-check: reload the result.
	ibMetaDataConfigurationFile check;
	if (!check.LoadConfigFromFile(outPath)) {
		std::fprintf(stderr, "VERIFY FAILED: cannot reload %s\n", (const char*)outPath.utf8_str());
		return 1;
	}
	std::printf("VERIFY: reloaded OK\n");
	return 0;
}
