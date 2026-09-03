#ifndef _DEBUG_CLIENT_BRIDGE_H__
#define _DEBUG_CLIENT_BRIDGE_H__

#include <wx/wx.h>
#include <wx/socket.h>

#include "backend/backend_core.h"
#include "debugDefs.h"

class BACKEND_API ibDebuggerClientBridge {
public:

	static void SetDebuggerClientBridge(ibDebuggerClientBridge* bridge);

	virtual ~ibDebuggerClientBridge() {}

	//commands 
	virtual void OnSessionStart(wxSocketClient* sock) = 0;
	virtual void OnSessionEnd(wxSocketClient* sock) = 0;

	virtual void OnEnterLoop(wxSocketClient* sock, const ibDebugLineData& data) = 0;
	virtual void OnLeaveLoop(wxSocketClient* sock, const ibDebugLineData& data) = 0;

	virtual void OnAutoComplete(const ibDebugAutoCompleteData& data) = 0;
	virtual void OnMessageFromServer(const ibDebugLineData& data, const wxString& message) = 0;
	virtual void OnSetToolTip(const ibDebugExpressionData& data, const wxString& resultStr) = 0;

	virtual void OnSetStack(const ibStackData& stackData) = 0;

	virtual void OnSetLocalVariable(const ibLocalWindowData& watchData) = 0;

	virtual void OnSetVariable(const ibWatchWindowData& watchData) = 0;
	virtual void OnSetExpanded(const ibWatchWindowData& watchData) = 0;

	// Script-profiler report (GitHub #2). Non-pure: default no-op so bridge
	// implementations that don't surface a profiler need not override it.
	virtual void OnSetProfilerData(const ibProfilerReportData& data) {}
};


#endif