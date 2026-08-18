#ifndef _METADATA_CONFIG_SPEC_H__
#define _METADATA_CONFIG_SPEC_H__

// =============================================================================
// OES Enterprise — build a configuration from a friendly JSON spec.
//
// An EXTERNAL, extensible, compiler-independent path to create a configuration:
// any script (Python, etc.) emits a high-level JSON description; this engine
// maps it onto the robust metadata API (CreateMetaObject / RenameMetaObject /
// attribute type descriptors / module text) and produces a live configuration
// that can be saved to an .mcf file. No lossy round-trip — the JSON is an
// AUTHORING spec, not the serialization node format.
//
// Friendly JSON shape (extensible — add keys + a branch in the .cpp):
//   {
//     "name": "SampleConfiguration",
//     "catalogs":  [ { "name": "...", "attributes": [ { "name","type",
//                        "precision","scale","length" } ] } ],
//     "documents": [ { "name": "...", "attributes": [ ... ] } ],
//     "commonModules": [ { "name": "...", "code": "..." } ]
//   }
// Attribute "type": "Number" | "String" | "Date" | "Boolean".
//
// The engine lives in backend so both the CLI (tools/oes_config_gen.cpp) and a
// future Designer/Enterprise menu command can call the same code.
// =============================================================================

#include <wx/string.h>
#include "backend/backend.h"   // BACKEND_API

class ibMetaDataConfigurationFile;

// Build the configuration tree in `cfg` from a friendly JSON string.
// Returns false and fills `err` (human-readable) on any failure.
BACKEND_API bool ibBuildConfigFromJsonSpec(const wxString& jsonText,
                                           ibMetaDataConfigurationFile& cfg,
                                           wxString& err);

// Convenience: read a JSON spec file, build a fresh configuration and save it
// to `mcfPath`. Returns false and fills `err` on any failure.
BACKEND_API bool ibBuildConfigFileFromJsonSpec(const wxString& jsonPath,
                                               const wxString& mcfPath,
                                               wxString& err);

#endif // _METADATA_CONFIG_SPEC_H__
