#ifndef __OES_MCP_HTTP_SERVER_H__
#define __OES_MCP_HTTP_SERVER_H__

// =============================================================================
// OES MCP over HTTP — an MCP (Model Context Protocol) server embedded IN the app
// (Configurator / Enterprise), so Claude Code connects to the running app as a
// remote HTTP MCP server (variant A). Minimal HTTP/1.1 + JSON-RPC 2.0 over
// wxSocketServer, localhost only, opt-in via --mcp[=port]. Reuses the same
// command surface as the test agent (ibTestAgentDispatchForm) plus in-process
// configuration-editing tools (metadataConfigSpec).
//
// Transport: one request per connection (Connection: close). POST carries a
// JSON-RPC message; the response is application/json (Streamable-HTTP minimal —
// no server->client SSE stream; a GET gets 405). Runs on the GUI thread, so
// tools execute without marshaling, same as the test agent.
// =============================================================================

#include "frontend/frontend.h"   // FRONTEND_API

#include <map>
#include <string>

#include <wx/event.h>

class wxSocketServer;
class wxSocketBase;
class wxSocketEvent;

class FRONTEND_API ibMcpHttpServer : public wxEvtHandler {
public:
	static const int kDefaultPort = 8765;

	static ibMcpHttpServer& Get();

	// Bind localhost:port and start listening. Idempotent. False if the bind fails.
	bool Start(int port = kDefaultPort);
	void Stop();

	bool IsRunning() const { return m_server != nullptr; }
	int  GetPort() const { return m_port; }

private:
	ibMcpHttpServer() = default;
	~ibMcpHttpServer() override;
	ibMcpHttpServer(const ibMcpHttpServer&) = delete;
	ibMcpHttpServer& operator=(const ibMcpHttpServer&) = delete;

	void OnServerEvent(wxSocketEvent& event);
	void OnClientEvent(wxSocketEvent& event);
	void DropClient(wxSocketBase* client);
	void ReadAvailable(wxSocketBase* client);
	void TryHandle(wxSocketBase* client);   // parse a complete HTTP request, respond
	void SendRaw(wxSocketBase* client, const std::string& data);

	// Build the JSON-RPC response body for one request line (empty => notification).
	std::string HandleJsonRpc(const std::string& body);

	wxSocketServer* m_server = nullptr;
	std::map<wxSocketBase*, std::string> m_rx;   // per-connection receive buffer
	int m_port = kDefaultPort;
};

#endif // __OES_MCP_HTTP_SERVER_H__
