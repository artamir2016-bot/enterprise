// OES MCP over HTTP — embedded MCP server (see mcpHttpServer.h).

#include "mcpHttpServer.h"
#include "testAgentInternal.h"   // ibTestAgentDispatchForm — live app command surface

#include "../../../3rdparty/nlohmann/json.hpp"

#include "backend/metadataConfigSpec.h"        // ibBuildConfigFromJsonSpec / ibApplyConfigSpec
#include "backend/metadataConfiguration.h"     // ibMetaDataConfigurationFile

#include <wx/socket.h>
#include <wx/log.h>
#include <wx/app.h>

#include <cctype>
#include <cstdlib>
#include <cstring>

using nlohmann::json;

namespace {
	constexpr int kIdServer = 42201;
	constexpr int kIdClient = 42202;
	constexpr std::size_t kMaxRequestBytes = 64u * 1024u * 1024u;   // guard runaway/hostile input

	const char* kProtocolVersion = "2024-11-05";

	std::string U(const wxString& s) { return std::string(s.utf8_str()); }
	wxString    W(const json& j, const char* k) {
		return (j.contains(k) && j[k].is_string())
			? wxString::FromUTF8(j[k].get<std::string>().c_str()) : wxString();
	}

	// ---- tool implementations (in-process; GUI thread) ----------------------

	// Greenfield build: JSON spec text -> new .mcf.
	json ToolGenerate(const json& a) {
		const wxString out = W(a, "out_mcf");
		if (out.IsEmpty()) return json{{"text", "ERROR: out_mcf required"}, {"isError", true}};
		const wxString spec = W(a, "spec");
		ibMetaDataConfigurationFile cfg;
		wxString err;
		if (!ibBuildConfigFromJsonSpec(spec, cfg, err))
			return json{{"text", "ERROR: " + U(err)}, {"isError", true}};
		if (!cfg.SaveConfigToFile(out))
			return json{{"text", "ERROR: cannot save " + U(out)}, {"isError", true}};
		return json{{"text", "OK: wrote " + U(out)}, {"isError", false}};
	}

	// Merge/edit: apply a JSON patch onto an existing .mcf (add/edit objects).
	json ToolEdit(const json& a) {
		const wxString in = W(a, "in_mcf");
		if (in.IsEmpty()) return json{{"text", "ERROR: in_mcf required"}, {"isError", true}};
		const wxString out = a.contains("out_mcf") && a["out_mcf"].is_string() ? W(a, "out_mcf") : in;
		const wxString patch = W(a, "patch");
		ibMetaDataConfigurationFile cfg;
		wxString err;
		if (!cfg.LoadConfigFromFile(in))
			return json{{"text", "ERROR: cannot load " + U(in)}, {"isError", true}};
		if (!ibApplyConfigSpec(patch, cfg, err))
			return json{{"text", "ERROR: " + U(err)}, {"isError", true}};
		if (!cfg.SaveConfigToFile(out))
			return json{{"text", "ERROR: cannot save " + U(out)}, {"isError", true}};
		return json{{"text", "OK: merged patch onto " + U(in) + " -> " + U(out)}, {"isError", false}};
	}

	// Live app command: reuse the test-agent surface (forms, controls, menus,
	// breakpoints, debug, profiler, metadata) — in-process on the GUI thread.
	json ToolApp(const json& a) {
		const std::string cmd = a.value("cmd", std::string());
		const json args = a.contains("args") && a["args"].is_object() ? a["args"] : json::object();
		json result;
		if (cmd == "ping") {
			result = json{{"pong", true}};
		}
		else if (cmd == "appInfo") {
			result = json{{"app", wxTheApp ? std::string(wxTheApp->GetAppName().utf8_str()) : std::string()},
			              {"agent", "oes-mcp-http"}};
		}
		else if (!ibTestAgentDispatchForm(cmd, args, result)) {
			return json{{"text", "ERROR: unknown app command: " + cmd}, {"isError", true}};
		}
		return json{{"text", result.dump(2)}, {"isError", false}};
	}

	json ToolsList() {
		return json::array({
			{{"name", "oes_config_generate"},
			 {"description", "Build a NEW .mcf from a friendly JSON spec (greenfield). "
			   "spec: JSON text {name,catalogs,documents,enums,constants,commonModules}. Writes out_mcf."},
			 {"inputSchema", {{"type","object"},
			    {"properties", {{"spec",{{"type","string"}}}, {"out_mcf",{{"type","string"}}}}},
			    {"required", json::array({"spec","out_mcf"})}}}},
			{{"name", "oes_config_edit"},
			 {"description", "MERGE a JSON patch onto an existing .mcf: add new objects/attributes/modules "
			   "or edit existing ones (idempotent by name). out_mcf defaults to in_mcf."},
			 {"inputSchema", {{"type","object"},
			    {"properties", {{"in_mcf",{{"type","string"}}}, {"patch",{{"type","string"}}}, {"out_mcf",{{"type","string"}}}}},
			    {"required", json::array({"in_mcf","patch"})}}}},
			{{"name", "oes_app"},
			 {"description", "Run a command in THIS app (the running Configurator/Enterprise): forms, "
			   "controls, menus (invokeMenu), metadata (openMetaEditor), debug (setBreakpoint/debugState), "
			   "profiler (readTreeList), etc. Pass cmd + args."},
			 {"inputSchema", {{"type","object"},
			    {"properties", {{"cmd",{{"type","string"}}}, {"args",{{"type","object"}}}}},
			    {"required", json::array({"cmd"})}}}},
		});
	}

	json CallTool(const std::string& name, const json& args) {
		json r;
		if      (name == "oes_config_generate") r = ToolGenerate(args);
		else if (name == "oes_config_edit")     r = ToolEdit(args);
		else if (name == "oes_app")             r = ToolApp(args);
		else return json{{"content", json::array({{{"type","text"},{"text","unknown tool: " + name}}})}, {"isError", true}};
		return json{{"content", json::array({{{"type","text"},{"text", r.value("text", std::string())}}})},
		            {"isError", r.value("isError", false)}};
	}
}

// ---- JSON-RPC dispatch -------------------------------------------------------

std::string ibMcpHttpServer::HandleJsonRpc(const std::string& body)
{
	json rpc;
	try { rpc = json::parse(body); }
	catch (...) { return std::string(); }   // unparseable -> treat as notification (no reply)

	const std::string method = rpc.value("method", std::string());
	const bool hasId = rpc.contains("id") && !rpc["id"].is_null();
	json id = hasId ? rpc["id"] : json(nullptr);

	// Notifications (no id): no response body.
	if (!hasId)
		return std::string();

	json result;
	try {
		if (method == "initialize") {
			result = {{"protocolVersion", kProtocolVersion},
			          {"capabilities", {{"tools", json::object()}}},
			          {"serverInfo", {{"name", "oes-configurator"}, {"version", "0.1.0"}}}};
		}
		else if (method == "ping") {
			result = json::object();
		}
		else if (method == "tools/list") {
			result = {{"tools", ToolsList()}};
		}
		else if (method == "tools/call") {
			const json params = rpc.value("params", json::object());
			result = CallTool(params.value("name", std::string()),
			                  params.contains("arguments") ? params["arguments"] : json::object());
		}
		else {
			return json{{"jsonrpc","2.0"}, {"id", id},
			            {"error", {{"code",-32601},{"message","method not found: " + method}}}}.dump();
		}
	}
	catch (const std::exception& e) {
		return json{{"jsonrpc","2.0"}, {"id", id},
		            {"error", {{"code",-32603},{"message", std::string(e.what())}}}}.dump();
	}
	return json{{"jsonrpc","2.0"}, {"id", id}, {"result", result}}.dump();
}

// ---- transport ---------------------------------------------------------------

ibMcpHttpServer& ibMcpHttpServer::Get()
{
	static ibMcpHttpServer s_instance;
	return s_instance;
}

ibMcpHttpServer::~ibMcpHttpServer() { Stop(); }

bool ibMcpHttpServer::Start(int port)
{
	if (m_server != nullptr)
		return true;
	m_port = port;
	if (!wxSocketBase::IsInitialized())
		wxSocketBase::Initialize();

	wxIPV4address addr;
	addr.Hostname(wxT("127.0.0.1"));
	addr.Service(port);

	auto* server = new wxSocketServer(addr, wxSOCKET_NOWAIT | wxSOCKET_REUSEADDR);
	if (!server->IsOk()) {
		delete server;
		wxLogWarning(wxT("OES MCP HTTP: failed to bind 127.0.0.1:%d"), port);
		return false;
	}
	server->SetEventHandler(*this, kIdServer);
	server->SetNotify(wxSOCKET_CONNECTION_FLAG);
	server->Notify(true);
	Bind(wxEVT_SOCKET, &ibMcpHttpServer::OnServerEvent, this, kIdServer);
	Bind(wxEVT_SOCKET, &ibMcpHttpServer::OnClientEvent, this, kIdClient);
	m_server = server;
	wxLogMessage(wxT("OES MCP HTTP listening on http://127.0.0.1:%d/"), port);
	return true;
}

void ibMcpHttpServer::Stop()
{
	for (auto& kv : m_rx) {
		if (kv.first) { kv.first->Notify(false); kv.first->Destroy(); }
	}
	m_rx.clear();
	if (m_server != nullptr) {
		m_server->Notify(false);
		m_server->Destroy();
		m_server = nullptr;
	}
}

void ibMcpHttpServer::DropClient(wxSocketBase* client)
{
	if (client == nullptr) return;
	m_rx.erase(client);
	client->Notify(false);
	client->Destroy();
}

void ibMcpHttpServer::OnServerEvent(wxSocketEvent& event)
{
	if (event.GetSocketEvent() != wxSOCKET_CONNECTION || m_server == nullptr)
		return;
	wxSocketBase* client = m_server->Accept(false);
	if (client == nullptr) return;
	client->SetFlags(wxSOCKET_NOWAIT);
	client->SetEventHandler(*this, kIdClient);
	client->SetNotify(wxSOCKET_INPUT_FLAG | wxSOCKET_LOST_FLAG);
	client->Notify(true);
	m_rx[client];   // create empty buffer
}

void ibMcpHttpServer::OnClientEvent(wxSocketEvent& event)
{
	wxSocketBase* client = event.GetSocket();
	if (client == nullptr) return;
	switch (event.GetSocketEvent()) {
	case wxSOCKET_INPUT:
		ReadAvailable(client);
		TryHandle(client);
		break;
	case wxSOCKET_LOST:
		DropClient(client);
		break;
	default:
		break;
	}
}

void ibMcpHttpServer::ReadAvailable(wxSocketBase* client)
{
	auto it = m_rx.find(client);
	if (it == m_rx.end()) return;
	char chunk[8192];
	for (;;) {
		client->Read(chunk, sizeof(chunk));
		const wxUint32 got = client->LastCount();
		if (got == 0) break;
		it->second.append(chunk, got);
		if (it->second.size() > kMaxRequestBytes) { DropClient(client); return; }
		if (client->Error()) break;
	}
}

void ibMcpHttpServer::TryHandle(wxSocketBase* client)
{
	auto it = m_rx.find(client);
	if (it == m_rx.end()) return;
	std::string& rx = it->second;

	const std::size_t headEnd = rx.find("\r\n\r\n");
	if (headEnd == std::string::npos)
		return;   // headers not complete yet

	// Parse the request line + Content-Length (case-insensitive header name).
	const std::string head = rx.substr(0, headEnd);
	const std::string method = head.substr(0, head.find(' '));
	std::size_t contentLen = 0;
	{
		std::string lower = head;
		for (char& c : lower) c = (char)std::tolower((unsigned char)c);
		const std::size_t p = lower.find("content-length:");
		if (p != std::string::npos) {
			std::size_t q = p + 15;
			while (q < lower.size() && (lower[q] == ' ' || lower[q] == '\t')) ++q;
			contentLen = (std::size_t)std::strtoul(lower.c_str() + q, nullptr, 10);
		}
	}
	const std::size_t bodyStart = headEnd + 4;
	if (rx.size() < bodyStart + contentLen)
		return;   // body not fully arrived

	const std::string body = rx.substr(bodyStart, contentLen);

	std::string httpResp;
	if (method != "POST") {
		// GET / others: no SSE stream in this minimal server.
		httpResp = "HTTP/1.1 405 Method Not Allowed\r\nAllow: POST\r\n"
		           "Content-Length: 0\r\nConnection: close\r\n\r\n";
	}
	else {
		const std::string rpc = HandleJsonRpc(body);
		if (rpc.empty()) {
			// notification -> 202 Accepted, no body
			httpResp = "HTTP/1.1 202 Accepted\r\nContent-Length: 0\r\n"
			           "Mcp-Session-Id: oes\r\nConnection: close\r\n\r\n";
		}
		else {
			httpResp = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
			           "Mcp-Session-Id: oes\r\nConnection: close\r\nContent-Length: "
			           + std::to_string(rpc.size()) + "\r\n\r\n" + rpc;
		}
	}
	SendRaw(client, httpResp);
	// One request per connection.
	DropClient(client);
}

void ibMcpHttpServer::SendRaw(wxSocketBase* client, const std::string& data)
{
	if (client == nullptr || data.empty()) return;
	client->SetFlags(wxSOCKET_WAITALL);
	client->Write(data.data(), (wxUint32)data.size());
	client->SetFlags(wxSOCKET_NOWAIT);
}
