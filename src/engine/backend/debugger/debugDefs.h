#ifndef _DEBUGGER_DEFS_H__
#define _DEBUGGER_DEFS_H__

#include <vector>
#include <wx/string.h>

enum MessageType
{
	MessageType_Normal = 0,
	MessageType_Warning = 1,
	MessageType_Error = 2,
};

enum CodeState
{
	CodeState_Normal = 0,    // The code is normal.
	CodeState_Unavailable = 1,    // The code for the script was not available.
	CodeState_Binary = 2,    // The code was loaded as a binary/compiled file
};

enum EventId
{
	EventId_SessionStart = 1,   // Sent when the backend is ready to have its initialize function called
	EventId_SessionEnd = 2,    // This is used internally and shouldn't be sent.
	EventId_EnterLoop = 3,     // When enter to loop in enterprise mode 
	EventId_LeaveLoop = 4,      // When leave from loop in enterprise mode 

	EventId_SetToolTip = 5,      // When activate tooltip 
	EventId_StartAutocomplete = 6,      // When activate autocomplete 
	EventId_ShowAutocomplete = 7,      // When activate autocomplete 
	EventId_SetData = 8,      // When set data 

	EventId_MessageFromEnterprise = 9,      // Message from enterprise
};

enum CommandId
{
	CommandId_VerifyConnection = 1, // verify configuration before connection
	CommandId_SetConnectionType = 2, // return if connection not passed verify 

	CommandId_StartSession = 3, // If verify is ok then start debugging 
	CommandId_Continue = 4,    // Continues execution until the next break point.
	CommandId_StepOver = 5,    // Steps to the next line, not entering any functions.
	CommandId_StepInto = 6,    // Steps to the next line, entering any functions.
	CommandId_ToggleBreakpoint = 7,    // Toggles a breakpoint on a line on and off.
	CommandId_RemoveBreakpoint = 8,    // Remove a breakpoint on a line on and off.
	CommandId_Pause = 9,    // Instructs the debugger to break on the next line of script code.
	CommandId_Detach = 10,    // Soft stop: disconnect debug client + reset debug state, listener stays so the debugger can reattach.
	CommandId_Destroy = 11,    // Hard stop: same as Detach plus ForceExit on desktop (kill enterprise.exe). Web mode skips the ForceExit (wes serves other tabs) — effectively degrades to Detach.

	CommandId_PatchInsertLine = 12,   // Adds a new line of code.
	CommandId_PatchDeleteLine = 13,   // Deletes a line of code.

	CommandId_PatchComplete = 14,   // Refresh patch line for dataProcessors

	CommandId_DeleteAllBreakpoints = 15,// Instructs the backend to clear all breakpoints set

	CommandId_EnterLoop = 16,// Instructs the backend to enter debug loop
	CommandId_LeaveLoop = 17,// Instructs the backend to leave debug loop

	CommandId_AddExpression = 18, // Get variables from enterprise 
	CommandId_ExpandExpression = 19, // Expand variable from enterprise 
	CommandId_RemoveExpression = 20, // Remove variable from enterprise 

	CommandId_GetArrayBreakpoint = 22, // Wait breakpoits from designer
	CommandId_SetArrayBreakpoint = 23, // Set breakpoits to enterprise

	CommandId_SetStack = 24, // Set up stack in designer 
	CommandId_SetLocalVariables = 25, // Set up local variables to designer 
	CommandId_SetExpressions = 26, //Set up all expressions

	CommandId_EvalToolTip = 27,
	CommandId_EvalAutocomplete = 28,

	CommandId_MessageFromServer = 29, // When catch error in enterprise mode

	CommandId_GetProfilerData = 30, // Designer → debuggee: request the script-profiler report (GitHub #2)
	CommandId_SetProfilerData = 31  // debuggee → Designer: the profiler report (aggregate + trace)
};

enum ConnectionType {

	ConnectionType_Scanner,
	ConnectionType_Waiter,
	ConnectionType_Debugger,

	ConnectionType_Unknown = 100
};

/////////////////////////////////////////////////////////////////////////////////////////////

struct ibDebugData {
	// Identifies which ibSession (web tab or main desktop) the event
	// came from / should be routed to. Empty on protocol-level packets
	// (verify, set-connection-type) — only event/command packets that
	// touch a specific runtime carry it.
	wxString m_sessionGuid;
	wxString m_fileName;
	wxString m_moduleName;
};

struct ibDebugLineData : public ibDebugData {
	unsigned int m_line;
};

struct ibDebugExpressionData : public ibDebugData {
	wxString m_expression;
};

struct ibDebugAutoCompleteData : public ibDebugExpressionData {

	struct ibDebugVariableData {
		wxString m_variableName;
	};

	std::vector<ibDebugVariableData> m_arrVar;

	struct ibDebugMethodData {
		wxString m_methodName;
		wxString m_methodHelper;
		bool m_methodRet;
	};

	std::vector<ibDebugMethodData> m_arrMeth;

public:

	wxString m_keyword;
	int      m_currentPos;
};

///////////////////////////////////////////////////////////////////////////////////////////// 

struct ibStackData {

	struct ibStackRow {
		wxString m_moduleName;
		unsigned int m_moduleLine;
	public:
		ibStackRow(const wxString& strModuleName, unsigned int moduleLine) :
			m_moduleName(strModuleName), m_moduleLine(moduleLine) {
		}
	};

	std::vector<ibStackRow> m_stackData;

public:

	void AppendStack(const wxString& strModuleName, unsigned int moduleLine) {
		m_stackData.emplace_back(
			strModuleName, moduleLine
		);
	}

	unsigned int GetStackCount() const {
		return m_stackData.size();
	}

	wxString GetModuleName(unsigned int idx) const {
		if (idx > m_stackData.size()) return wxEmptyString;
		return m_stackData[idx].m_moduleName;
	}

	wxString GetModuleLine(unsigned int idx) const {
		if (idx > m_stackData.size()) return wxEmptyString;
		return std::to_string(m_stackData[idx].m_moduleLine);
	}
};

/////////////////////////////////////////////////////////////////////////////////////////////

#include <wx/treectrl.h>

struct ibLocalWindowData {

	struct ibLocalWindowItem {

		wxString	 m_name;
		wxString	 m_value;
		wxString	 m_type;
		bool		 m_hasAttributes;

		ibLocalWindowItem(const wxString& n, const wxString& v, const wxString& t, bool a) :
			m_name(n), m_value(v), m_type(t), m_hasAttributes(a) {
		}
	};

	std::vector<ibLocalWindowItem> m_listExpression;

public:

	ibLocalWindowData() {}

	void AddLocalVar(const wxString& name, const wxString& value, const wxString& type, bool hasAttributes) {
		m_listExpression.emplace_back(name, value, type, hasAttributes);
	}

	wxString GetName(unsigned int idx) const {
		return m_listExpression[idx].m_name;
	}

	wxString GetValue(unsigned int idx) const {
		return m_listExpression[idx].m_value;
	}
	wxString GetType(unsigned int idx) const {
		return m_listExpression[idx].m_type;
	}

	bool HasAttributes(unsigned int idx) const {
		return m_listExpression[idx].m_hasAttributes;
	}

	unsigned int GetVarCount() const {
		return m_listExpression.size();
	}
};

struct ibWatchWindowData {

	wxTreeItemId m_item;

	struct ibWatchWindowItem {

		wxTreeItemId m_item;
		wxString	 m_name;
		wxString	 m_value;
		wxString	 m_type;
		bool		 m_hasAttributes;

		ibWatchWindowItem(const wxString& n, const wxString& v, const wxString& t, bool a, const wxTreeItemId& i) :
			m_item(i), m_name(n), m_value(v), m_type(t), m_hasAttributes(a) {
		}
	};

	std::vector<ibWatchWindowItem> m_listExpression;

public:

	ibWatchWindowData(const wxTreeItemId& item = nullptr) : m_item(item) {}

	wxTreeItemId GetItem() const {
		return m_item;
	}

	void AddWatch(const wxString& name, const wxString& value, const wxString& type, bool hasAttributes) {
		m_listExpression.emplace_back(name, value, type, hasAttributes, m_item);
	}

	void AddWatch(const wxString& name, const wxString& value, const wxString& type, bool hasAttributes, const wxTreeItemId& item) {
		m_listExpression.emplace_back(name, value, type, hasAttributes, item);
	}

	wxTreeItemId GetItem(unsigned int idx) const {
		return m_listExpression[idx].m_item;
	}

	wxString GetName(unsigned int idx) const {
		return m_listExpression[idx].m_name;
	}

	wxString GetValue(unsigned int idx) const {
		return m_listExpression[idx].m_value;
	}
	wxString GetType(unsigned int idx) const {
		return m_listExpression[idx].m_type;
	}

	bool HasAttributes(unsigned int idx) const {
		return m_listExpression[idx].m_hasAttributes;
	}

	unsigned int GetWatchCount() const {
		return m_listExpression.size();
	}
};

/////////////////////////////////////////////////////////////////////////////////////////////

// Script-profiler report carried over the debug transport (GitHub #2). Mirrors
// ibProfileNode / ibProfileTrace (backend/compiler/scriptProfiler.h) but with
// wire-friendly plain types — the debuggee fills it from its in-process
// profiler, the Designer renders it in the profiler panel.
struct ibProfilerReportData {

	struct AggRow {
		wxString           m_module;
		wxString           m_name;    // empty ⇒ module body
		unsigned long long m_count = 0;
		unsigned long long m_selfNs = 0;
		unsigned long long m_inclNs = 0;
	};

	struct TraceRow {
		wxString           m_module;
		wxString           m_name;
		int                m_depth = 0;
		unsigned long long m_enterNs = 0;
		unsigned long long m_durNs = 0;
	};

	std::vector<AggRow>   m_agg;
	std::vector<TraceRow> m_trace;
	unsigned long long    m_dropped = 0;   // trace records dropped past the cap
	bool                  m_hasProfiler = false;  // false ⇒ no measurement in the debuggee
};

/////////////////////////////////////////////////////////////////////////////////////////////

#define defaultDebuggerPort 1650
#define diapasonDebuggerPort 10
#define defaultHost wxT("localhost")

/////////////////////////////////////////////////////////////////////////////////////////////
#define numberOfConnectionAttempts 1
/////////////////////////////////////////////////////////////////////////////////////////////

#define waitVerifyDebuggerTimeout 5 //sec
#define waitDebuggerTimeout 50 //msec

/////////////////////////////////////////////////////////////////////////////////////////////

#endif 