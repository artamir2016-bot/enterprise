#include "debugClient.h"

void ibDebuggerClient::ibDebuggerClientAdapter::OnSessionStart(wxSocketClient* sock)
{
	if (m_debugBridge != nullptr) {
		m_debugBridge->OnSessionStart(sock);
	}
}

void ibDebuggerClient::ibDebuggerClientAdapter::OnSessionEnd(wxSocketClient* sock)
{
	if (m_debugBridge != nullptr) {
		m_debugBridge->OnSessionEnd(sock);
	}
}

void ibDebuggerClient::ibDebuggerClientAdapter::OnEnterLoop(wxSocketClient* sock, const ibDebugLineData& data)
{
	if (m_debugBridge != nullptr) {
		m_debugBridge->OnEnterLoop(sock, data);
	}
}

void ibDebuggerClient::ibDebuggerClientAdapter::OnLeaveLoop(wxSocketClient* sock, const ibDebugLineData& data)
{
	if (m_debugBridge != nullptr) {
		m_debugBridge->OnLeaveLoop(sock, data);
	}
}

void ibDebuggerClient::ibDebuggerClientAdapter::OnAutoComplete(const ibDebugAutoCompleteData& data)
{
	if (m_debugBridge != nullptr) {
		m_debugBridge->OnAutoComplete(data);
	}
}

void ibDebuggerClient::ibDebuggerClientAdapter::OnMessageFromServer(const ibDebugLineData& data, const wxString& message)
{
	if (m_debugBridge != nullptr) {
		m_debugBridge->OnMessageFromServer(data, message);
	}
}

void ibDebuggerClient::ibDebuggerClientAdapter::OnSetToolTip(const ibDebugExpressionData& data, const wxString& strResult)
{
	if (m_debugBridge != nullptr) {
		m_debugBridge->OnSetToolTip(data, strResult);
	}
}

void ibDebuggerClient::ibDebuggerClientAdapter::OnSetStack(const ibStackData& data)
{
	if (m_debugBridge != nullptr) {
		m_debugBridge->OnSetStack(data);
	}
}

void ibDebuggerClient::ibDebuggerClientAdapter::OnSetLocalVariable(const ibLocalWindowData& data)
{
	if (m_debugBridge != nullptr) {
		m_debugBridge->OnSetLocalVariable(data);
	}
}

void ibDebuggerClient::ibDebuggerClientAdapter::OnSetVariable(const ibWatchWindowData& data)
{
	if (m_debugBridge != nullptr) {
		m_debugBridge->OnSetVariable(data);
	}
}

void ibDebuggerClient::ibDebuggerClientAdapter::OnSetExpanded(const ibWatchWindowData& data)
{
	if (m_debugBridge != nullptr) {
		m_debugBridge->OnSetExpanded(data);
	}
}

void ibDebuggerClient::ibDebuggerClientAdapter::OnSetProfilerData(const ibProfilerReportData& data)
{
	if (m_debugBridge != nullptr) {
		m_debugBridge->OnSetProfilerData(data);
	}
}
