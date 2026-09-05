// OES-TEST: module unit-test runner (see moduleTestRunner.h).

#include "moduleTestRunner.h"

#include "backend/appData.h"                        // ibApplicationData
#include "backend/metadataConfiguration.h"          // activeMetaData macro (appEnv::ActiveMetaData)
#include "backend/session/session.h"                // GetManagerModule
#include "backend/moduleManager/moduleManager.h"    // FindCommonModule / ibValueModuleUnit
#include "backend/compiler/procUnit.h"              // CallAsFunc by name
#include "backend/backend_exception.h"              // ibBackendException
#include "backend/system/systemManager.h"           // ibValueSystemFunction::SetMessageTap
#include "backend/metaData.h"                       // GetAnyArrayObject
#include "backend/metaCollection/metaObject.h"      // g_metaCommonModuleCLSID
#include "backend/metaCollection/metaModuleObject.h" // ibValueMetaObjectCommonModule
#include "frontend/win/editor/codeEditor/codeEditorParser.h" // ibParserModule

#include <vector>
#include <wx/ffile.h>

int ibRunModuleTests(ibSession* session, const wxString& junitPath, wxString& report)
{
	auto emit = [&report](const wxString& line) { report += line; report += wxT("\n"); };

	if (session == nullptr) { emit(wxT("RunTests: no session")); return 1; }
	// Scope this session as Current on this thread: eval-mode (checked via
	// ibSession::Current) and CurrentFrame() then resolve to THIS session, so
	// the error path is suppressed correctly during test runs.
	ibSessionScope scope(session);
	ibValueModuleManagerRuntimeConfiguration* mm = session->GetManagerModule();
	if (mm == nullptr) {
		emit(wxT("RunTests: no runtime manager (run against an Enterprise session, not the Designer)"));
		return 1;
	}

	const wxString kTestRu = wxString::FromUTF8("\xD0\xA2\xD0\xB5\xD1\x81\xD1\x82"); // "Тест"
	auto isTestName = [&](const wxString& n) {
		return n.StartsWith(kTestRu) || n.Lower().StartsWith(wxT("test"));
	};

	// Assertion failures are reported NOT by throwing (a headless enterprise run
	// has no interactive frame; the interpreter's throw/unwind path is an access
	// violation there, which C++ catch cannot intercept) but by emitting a marked
	// message. The assert library (common module «Проверки») calls
	//   Сообщить("##OESFAIL## <text>")
	// and we tap the message stream to collect those without ever unwinding. The
	// marker is ASCII so it survives every codepage on the wire.
	static const wxString kFailMark = wxT("##OESFAIL##");
	std::vector<wxString> captured;
	ibValueSystemFunction::SetMessageTap(
		[&captured](const wxString& text, ibStatusMessage) { captured.push_back(text); });

	unsigned passed = 0, failed = 0;
	wxString junit;

	for (auto* modMeta : activeMetaData->GetAnyArrayObject<ibValueMetaObjectCommonModule>(g_metaCommonModuleCLSID)) {
		if (modMeta == nullptr) continue;
		const wxString moduleName = modMeta->GetName();

		auto* moduleUnit = mm->FindCommonModule(modMeta);
		if (moduleUnit == nullptr) continue;
		std::shared_ptr<ibProcUnit> mpu = moduleUnit->GetProcUnit();
		if (!mpu) continue;

		// Returns "" on success, else the failure text. A method that did not RUN
		// (compile error → not found) must fail loudly, not silently pass, so the
		// CallAsFunc bool is checked; `optional` distinguishes "absent" (for the
		// optional BeforeEach/AfterEach) from "present but failed".
		auto run = [&mpu](const wxString& name, bool optional) -> wxString {
			ibValue result;
			// NOTE: we deliberately do NOT run in eval mode. Assertions report by
			// message (Сообщить "##OESFAIL## …"), and Message() no-ops under eval
			// mode — so eval mode would silently swallow every failure. The assert
			// library never throws, so the frameless-throw crash path is not hit.
			// The try/catch stays as defence for a test that raises a genuine error.
			wxString err;
			try {
				if (!mpu->CallAsFunc(name, result))   // 0-arg variadic -> {nullptr}, size 0
					err = optional ? wxEmptyString
					               : wxString(wxT("did not run (compile error or not exported?)"));
			}
			catch (const ibBackendException& e) { const wxString m = e.GetErrorDescription(); err = m.IsEmpty() ? wxString(wxT("failed")) : m; }
			catch (...) { err = wxT("unknown error"); }
			return err;
		};

		// Discover test methods by parsing the module text.
		std::vector<wxString> tests;
		{
			ibParserModule parser;
			if (parser.ParseModule(modMeta->GetModuleText())) {
				for (const ibModuleElement& el : parser.GetAllContent()) {
					if ((el.m_eType == eExportProcedure || el.m_eType == eExportFunction)
					    && isTestName(el.m_name))
						tests.push_back(el.m_name);
				}
			}
		}
		if (tests.empty()) continue;

		for (const wxString& testName : tests) {
			captured.clear();
			run(wxT("BeforeEach"), /*optional*/ true);
			wxString err = run(testName, /*optional*/ false);
			run(wxT("AfterEach"), /*optional*/ true);

			// A marked failure message wins even if the call itself "ran" clean:
			// the assert library reports by message, not by return code.
			if (err.IsEmpty()) {
				for (const wxString& msg : captured) {
					if (msg.StartsWith(kFailMark)) {
						err = msg.Mid(kFailMark.length()).Trim(false);
						break;
					}
				}
			}

			const wxString full = moduleName + wxT(".") + testName;
			if (err.IsEmpty()) {
				passed++;
				emit(wxT("  PASS ") + full);
				junit += wxString::Format(wxT("  <testcase classname=\"%s\" name=\"%s\"/>\n"), moduleName, testName);
			} else {
				failed++;
				emit(wxT("  FAIL ") + full + wxT(": ") + err);
				junit += wxString::Format(
					wxT("  <testcase classname=\"%s\" name=\"%s\"><failure message=\"%s\"/></testcase>\n"),
					moduleName, testName, err);
			}
		}
	}

	// Drop the tap before `captured` goes out of scope — it holds a reference to it.
	ibValueSystemFunction::SetMessageTap(nullptr);

	emit(wxString::Format(wxT("Tests: %u, passed %u, failed %u"), passed + failed, passed, failed));

	if (!junitPath.IsEmpty()) {
		wxString xml = wxT("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
		xml += wxString::Format(wxT("<testsuite name=\"OES module tests\" tests=\"%u\" failures=\"%u\">\n"),
			passed + failed, failed);
		xml += junit + wxT("</testsuite>\n");
		wxFFile jf(junitPath, wxT("w"));
		if (jf.IsOpened()) { jf.Write(xml, wxConvUTF8); jf.Close(); }
	}

	return (int)failed;
}
