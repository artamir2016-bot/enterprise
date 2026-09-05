#ifndef __OES_MODULE_TEST_RUNNER_H__
#define __OES_MODULE_TEST_RUNNER_H__

// OES-TEST: run configuration module unit tests against a LIVE runtime session.
// A test is a PUBLIC method whose name starts with "Тест" or "Test" in any common
// module; optional per-module BeforeEach / AfterEach run around each. Discovery is
// by parsing module text; execution is through each module's own procUnit (by name),
// so an assert library's ВызватьИсключение surfaces as a failure.
//
// MUST run on a RUNTIME session (Enterprise / Service) — a Designer session has no
// per-module runtime (AttachRuntime short-circuits), so nothing runs there.
//
// Appends a red/green text report to `report`, writes a JUnit XML file if junitPath
// is non-empty, and returns the number of FAILED tests (0 = all passed).

#include "frontend/frontend.h"   // FRONTEND_API
#include <wx/string.h>

class ibSession;

FRONTEND_API int ibRunModuleTests(ibSession* session,
                                  const wxString& junitPath,
                                  wxString& report);

#endif // __OES_MODULE_TEST_RUNNER_H__
