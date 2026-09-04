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

// MERGE / EDIT: apply a spec onto an EXISTING configuration `cfg` (loaded from an
// .mcf or a base). Idempotent — objects, attributes, tabular sections, enum
// values and modules named in the spec are REUSED and edited if they already
// exist, or created if they don't. This is the "add a new object OR edit an
// existing one" path (the greenfield builder above is create-only). New forms are
// still additive. Returns false and fills `err` on any failure.
BACKEND_API bool ibApplyConfigSpec(const wxString& jsonText,
                                   ibMetaDataConfigurationFile& cfg,
                                   wxString& err);

// Convenience: load `mcfInPath`, apply the JSON spec at `jsonPath` as a merge,
// and save the result to `mcfOutPath` (may equal the input to edit in place).
BACKEND_API bool ibApplyConfigFileSpec(const wxString& mcfInPath,
                                       const wxString& jsonPath,
                                       const wxString& mcfOutPath,
                                       wxString& err);

#endif // _METADATA_CONFIG_SPEC_H__
