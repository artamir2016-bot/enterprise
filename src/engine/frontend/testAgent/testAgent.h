#ifndef __OES_TEST_AGENT_H__
#define __OES_TEST_AGENT_H__

// OES-TEST: in-process test-automation agent (fork feature — see docs/test-automation.md).
//
// A localhost-only TCP control server embedded in every OES GUI app (designer.exe / enterprise.exe
// both link frontend.dll, so one agent serves both). Enabled explicitly by the --testagent[=port]
// command-line flag; OFF by default. An external runner (oes_testrunner) connects and drives the
// app: enumerate windows, find controls, click, type, read state, and (in enterprise) run
// runtime-form commands — the OES analogue of Vanessa Automation's tested-application API, but
// out-of-process so ONE scenario can drive the Designer and then the Enterprise.
//
// Transport: framed JSON. Each message is a 4-byte little-endian length prefix followed by a UTF-8
// JSON payload. Request  = {"id":<n>, "cmd":"<name>", "args":{...}}. Response = {"id":<n>,
// "ok":true, "result":{...}} or {"id":<n>, "ok":false, "error":"<msg>"}.
//
// Threading: event-driven on the GUI thread via wxSocket events, so every command executes on the
// same thread the UI lives on — no cross-thread marshaling needed to touch windows/controls.

#include "frontend/frontend.h"   // FRONTEND_API

#include <wx/event.h>
#include <wx/string.h>
#include <string>

class wxSocketServer;
class wxSocketBase;
class wxSocketEvent;

class FRONTEND_API ibTestAgent : public wxEvtHandler {
public:
	static const int kDefaultTestAgentPort = 1651;

	// Process-wide singleton (one agent per app instance).
	static ibTestAgent& Get();

	// Bind a localhost:port server and start listening. Returns false if the port is taken or the
	// bind fails. Call AFTER the main frame exists so windows are enumerable. Idempotent.
	bool Start(int port = kDefaultTestAgentPort);

	// Tear the server (and any live connection) down. Safe to call when not running.
	void Stop();

	bool IsRunning() const { return m_server != nullptr; }
	int  GetPort() const { return m_port; }

private:
	ibTestAgent() = default;
	~ibTestAgent() override;
	ibTestAgent(const ibTestAgent&) = delete;
	ibTestAgent& operator=(const ibTestAgent&) = delete;

	void OnServerEvent(wxSocketEvent& event);   // incoming connection
	void OnClientEvent(wxSocketEvent& event);   // input / lost on the live connection
	void DropClient();
	void ReadAvailable(wxSocketBase* client);   // drain socket bytes into m_rx
	void ProcessFrames(wxSocketBase* client);   // extract + handle complete frames
	void SendFrame(wxSocketBase* client, const std::string& payload) const;

	// Handle one JSON request string, return the JSON response string. Defined in
	// testAgentCommands.cpp so the command surface can grow without touching the transport.
	std::string HandleRequest(const std::string& request);

	wxSocketServer* m_server = nullptr;
	wxSocketBase*   m_client = nullptr;   // one connection at a time (a single runner drives an app)
	std::string     m_rx;                 // byte accumulator for length-prefixed framing
	int             m_port = kDefaultTestAgentPort;
};

#endif // __OES_TEST_AGENT_H__
