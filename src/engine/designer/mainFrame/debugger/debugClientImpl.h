#ifndef _DEBUG_CLIENT_IMPL_H__
#define _DEBUG_CLIENT_IMPL_H__

#include "backend/debugger/debugClientBridge.h"

class ibDebuggerClientBridgeDesigner : public ibDebuggerClientBridge {
public:

	//commands 
	virtual void OnSessionStart(wxSocketClient* sock);
	virtual void OnSessionEnd(wxSocketClient* sock);

	virtual void OnEnterLoop(wxSocketClient* sock, const ibDebugLineData& data);
	virtual void OnLeaveLoop(wxSocketClient* sock, const ibDebugLineData& data);

	virtual void OnAutoComplete(const ibDebugAutoCompleteData& data);
	virtual void OnMessageFromServer(const ibDebugLineData& data, const wxString& message);
	virtual void OnSetToolTip(const ibDebugExpressionData& data, const wxString& resultStr);

	virtual void OnSetStack(const ibStackData& stackData);

	virtual void OnSetLocalVariable(const ibLocalWindowData& data);

	virtual void OnSetVariable(const ibWatchWindowData& watchData);
	virtual void OnSetExpanded(const ibWatchWindowData& watchData);

	virtual void OnSetProfilerData(const ibProfilerReportData& data);   // GitHub #2
};

#endif