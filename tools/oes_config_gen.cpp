// =============================================================================
// OES Enterprise — CLI configuration generator.
//
// Reads a friendly JSON spec and writes an .mcf configuration file, using the
// backend engine ibBuildConfigFileFromJsonSpec. This is the EXTERNAL,
// compiler-independent path: an external script (Python, etc.) emits JSON;
// this tool turns it into a loadable configuration. No GUI, backend only.
//
// Usage:
//   oes_config_gen <spec.json> <out.mcf>
// =============================================================================

#include <cstdio>
#include <cstring>

#include "backend/metadataConfigSpec.h"
#include "backend/metadataConfiguration.h"

// Build a standalone file configuration from a friendly JSON spec. Mirrors
// tools/make_sample_config.cpp: no appData bring-up — ibMetaDataConfigurationFile
// is DB-free and self-contained, and a global runtime env only adds shared state
// that the standalone builder does not need.
int main(int argc, char** argv)
{
	if (argc < 3) {
		std::fprintf(stderr, "usage: %s <spec.json> <out.mcf>\n",
			argc > 0 ? argv[0] : "oes_config_gen");
		return 2;
	}

	const wxString specPath = wxString::FromUTF8(argv[1]);
	const wxString mcfPath  = wxString::FromUTF8(argv[2]);

	wxString err;
	const bool ok = ibBuildConfigFileFromJsonSpec(specPath, mcfPath, err);

	if (!ok) {
		std::fprintf(stderr, "ERROR: %s\n", (const char*)err.utf8_str());
		return 1;
	}
	std::printf("OK: %s -> %s\n",
		(const char*)specPath.utf8_str(), (const char*)mcfPath.utf8_str());

	// Optional round-trip self-check: reload the .mcf we just wrote.
	if (argc > 3 && std::strcmp(argv[3], "--verify") == 0) {
		ibMetaDataConfigurationFile check;
		if (!check.LoadConfigFromFile(mcfPath)) {
			std::fprintf(stderr, "VERIFY FAILED: cannot reload %s\n", (const char*)mcfPath.utf8_str());
			return 1;
		}
		std::printf("VERIFY: reloaded OK\n");
	}
	return 0;
}
