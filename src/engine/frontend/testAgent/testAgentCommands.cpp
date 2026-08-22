// OES-TEST: test-automation agent — command dispatch (see testAgent.h / docs/test-automation.md).
//
// HandleRequest maps a JSON request {"id","cmd","args"} to a JSON response. Kept separate from the
// transport so the command surface can grow (subtask 2: generic wx-UI commands; subtask 4:
// enterprise runtime-form commands) without touching the socket/framing code.

#include "testAgent.h"
#include "testAgentInternal.h"   // ibTestAgentDispatchForm — enterprise runtime-form commands

#include "../../../3rdparty/nlohmann/json.hpp"

#include <wx/app.h>
#include <wx/utils.h>     // wxGetProcessId
#include <wx/window.h>
#include <wx/toplevel.h>

using nlohmann::json;

namespace {
	// Bump when the wire protocol changes so the runner can negotiate/verify.
	constexpr int kAgentProtocolVersion = 1;

	std::string AppName()
	{
		return wxTheApp != nullptr ? std::string(wxTheApp->GetAppName().utf8_str()) : std::string();
	}

	// --- individual command handlers -------------------------------------------------------------
	// Each returns the "result" object (or throws std::runtime_error, caught by HandleRequest).

	json Cmd_Ping(const json&)
	{
		return json{ {"pong", true} };
	}

	json Cmd_AppInfo(const json&)
	{
		return json{
			{"app", AppName()},
			{"pid", static_cast<long long>(::wxGetProcessId())},
			{"protocol", kAgentProtocolVersion},
			{"agent", "oes-test-agent"},
		};
	}

	json Cmd_Quit(const json&)
	{
		// Respond first, then close on the next event-loop turn so the reply is flushed.
		if (wxTheApp != nullptr) {
			wxTheApp->CallAfter([]() {
				if (wxWindow* top = wxTheApp->GetTopWindow())
					top->Close(true);
			});
		}
		return json{ {"closing", true} };
	}
}

std::string ibTestAgent::HandleRequest(const std::string& request)
{
	json id = nullptr;
	json response;
	try {
		const json req = json::parse(request);
		id = req.contains("id") ? req.at("id") : json(nullptr);
		const std::string cmd = req.value("cmd", std::string());
		const json args = req.contains("args") ? req.at("args") : json::object();

		json result;
		if      (cmd == "ping")    result = Cmd_Ping(args);
		else if (cmd == "appInfo") result = Cmd_AppInfo(args);
		else if (cmd == "quit")    result = Cmd_Quit(args);
		else if (!ibTestAgentDispatchForm(cmd, args, result))   // enterprise runtime-form commands
			throw std::runtime_error("unknown command: " + cmd);

		response = json{ {"id", id}, {"ok", true}, {"result", result} };
	}
	catch (const std::exception& e) {
		response = json{ {"id", id}, {"ok", false}, {"error", std::string(e.what())} };
	}
	catch (...) {
		response = json{ {"id", id}, {"ok", false}, {"error", "unknown agent failure"} };
	}
	return response.dump();
}
