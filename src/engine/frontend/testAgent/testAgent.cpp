// OES-TEST: test-automation agent — TCP transport + framing (see testAgent.h).

#include "testAgent.h"

#include <wx/socket.h>
#include <wx/app.h>
#include <wx/log.h>

namespace {
	// Distinct wxSocket event ids so the server socket and the client socket route to different
	// handlers on this same wxEvtHandler.
	constexpr int kIdServer = 42101;
	constexpr int kIdClient = 42102;

	// Frame = 4-byte little-endian length prefix + payload. Keep a sane ceiling so a bad/hostile
	// length can never make us allocate wildly (the channel is localhost + opt-in, but still).
	constexpr std::uint32_t kMaxFrameBytes = 64u * 1024u * 1024u;   // 64 MiB (screenshots fit)
}

ibTestAgent& ibTestAgent::Get()
{
	static ibTestAgent s_instance;
	return s_instance;
}

ibTestAgent::~ibTestAgent()
{
	Stop();
}

bool ibTestAgent::Start(int port)
{
	if (m_server != nullptr)
		return true;   // already listening — idempotent

	m_port = port;

	// wxSocketBase::Initialize() runs in each app's DoOnInit (ibWxApp base); safe if already done.
	if (!wxSocketBase::IsInitialized())
		wxSocketBase::Initialize();

	wxIPV4address addr;
	addr.Hostname(wxT("127.0.0.1"));   // localhost only — the port carries the ability to drive the app
	addr.Service(port);

	auto* server = new wxSocketServer(addr, wxSOCKET_NOWAIT | wxSOCKET_REUSEADDR);
	if (!server->IsOk()) {
		delete server;
		wxLogWarning(wxT("OES test agent: failed to bind 127.0.0.1:%d"), port);
		return false;
	}

	server->SetEventHandler(*this, kIdServer);
	server->SetNotify(wxSOCKET_CONNECTION_FLAG);
	server->Notify(true);
	Bind(wxEVT_SOCKET, &ibTestAgent::OnServerEvent, this, kIdServer);
	Bind(wxEVT_SOCKET, &ibTestAgent::OnClientEvent, this, kIdClient);

	m_server = server;
	wxLogMessage(wxT("OES test agent listening on 127.0.0.1:%d"), port);
	return true;
}

void ibTestAgent::Stop()
{
	DropClient();
	if (m_server != nullptr) {
		m_server->Notify(false);
		m_server->Destroy();
		m_server = nullptr;
	}
}

void ibTestAgent::DropClient()
{
	if (m_client != nullptr) {
		m_client->Notify(false);
		m_client->Destroy();
		m_client = nullptr;
	}
	m_rx.clear();
}

void ibTestAgent::OnServerEvent(wxSocketEvent& event)
{
	if (event.GetSocketEvent() != wxSOCKET_CONNECTION || m_server == nullptr)
		return;

	wxSocketBase* client = m_server->Accept(false);
	if (client == nullptr)
		return;

	// One runner at a time: a new connection replaces the old.
	DropClient();

	client->SetFlags(wxSOCKET_NOWAIT);
	client->SetEventHandler(*this, kIdClient);
	client->SetNotify(wxSOCKET_INPUT_FLAG | wxSOCKET_LOST_FLAG);
	client->Notify(true);
	m_client = client;
}

void ibTestAgent::OnClientEvent(wxSocketEvent& event)
{
	wxSocketBase* client = event.GetSocket();
	if (client == nullptr || client != m_client)
		return;

	switch (event.GetSocketEvent()) {
	case wxSOCKET_INPUT:
		ReadAvailable(client);
		ProcessFrames(client);
		break;
	case wxSOCKET_LOST:
		DropClient();
		break;
	default:
		break;
	}
}

void ibTestAgent::ReadAvailable(wxSocketBase* client)
{
	char chunk[8192];
	for (;;) {
		client->Read(chunk, sizeof(chunk));
		const wxUint32 got = client->LastCount();
		if (got == 0)
			break;
		m_rx.append(chunk, got);
		if (client->Error())   // NOWAIT: WOULD_BLOCK reports as error with 0 count — already handled
			break;
	}
}

void ibTestAgent::ProcessFrames(wxSocketBase* client)
{
	for (;;) {
		if (m_rx.size() < 4)
			return;   // not even a length prefix yet

		const unsigned char* p = reinterpret_cast<const unsigned char*>(m_rx.data());
		const std::uint32_t len = static_cast<std::uint32_t>(p[0])
			| (static_cast<std::uint32_t>(p[1]) << 8)
			| (static_cast<std::uint32_t>(p[2]) << 16)
			| (static_cast<std::uint32_t>(p[3]) << 24);

		if (len > kMaxFrameBytes) {   // corrupt/hostile — resync is impossible, drop the connection
			DropClient();
			return;
		}
		if (m_rx.size() < 4u + len)
			return;   // full payload not arrived yet

		const std::string request = m_rx.substr(4, len);
		m_rx.erase(0, 4u + len);

		const std::string response = HandleRequest(request);
		SendFrame(client, response);
	}
}

void ibTestAgent::SendFrame(wxSocketBase* client, const std::string& payload) const
{
	if (client == nullptr)
		return;
	const std::uint32_t len = static_cast<std::uint32_t>(payload.size());
	unsigned char header[4] = {
		static_cast<unsigned char>(len & 0xFF),
		static_cast<unsigned char>((len >> 8) & 0xFF),
		static_cast<unsigned char>((len >> 16) & 0xFF),
		static_cast<unsigned char>((len >> 24) & 0xFF),
	};
	// Blocking-ish write on the GUI thread: WAITALL so a full frame goes out in one call.
	client->SetFlags(wxSOCKET_WAITALL);
	client->Write(header, 4);
	if (len > 0)
		client->Write(payload.data(), len);
	client->SetFlags(wxSOCKET_NOWAIT);
}
