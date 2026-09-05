#ifndef _MAIN_DESIGNER_APP_H__
#define _MAIN_DESIGNER_APP_H__

#include <wx/app.h>
#include <wx/aui/framemanager.h>
#include <wx/socket.h>

#include <memory>

#include "frontend/diagnostics/oesApp.h"

class ibAppDesigner : public ibWxApp {

	// FILE ENTRY
	wxString m_strFile;

	// SERVER ENTRY
	wxString m_strServer;
	wxString m_strPort;
	wxString m_strDatabase;
	wxString m_strUser;
	wxString m_strPassword;

	// IB ENTRY
	wxString m_strIBUser;
	wxString m_strIBPassword;

#ifdef DEBUG
	//LOCALE
	wxString m_strLocale = wxT("en");
#else
	//LOCALE
	wxString m_strLocale;
#endif // wxDEBUG

	// OES-CLI: 1C:Enterprise 8.3-compatible headless batch mode.
	// When any batch verb (/LoadCfg /DumpCfg /CheckConfig /CheckModules) is
	// present on the command line the designer runs WITHOUT a window: it opens
	// the infobase, performs the requested operation, writes messages to the
	// /Out file (and stdout), and returns an exit code (0 = ok, 1 = errors).
	// This removes the need for the GUI "load configuration from file" dialog.
	bool     m_batchMode = false;          // set in OnInitCmdLine, consumed in DoOnRun
	wxString m_batchLoadCfg;               // /LoadCfg <file>  — load config from file into the base
	wxString m_batchDumpCfg;               // /DumpCfg <file>  — save the base config to a file
	wxString m_batchOut;                   // /Out <file>      — write messages to this file
	bool     m_batchUpdateDBCfg = false;   // /UpdateDBCfg     — apply loaded config to the database
	bool     m_batchCheckConfig = false;   // /CheckConfig     — compile-check the whole configuration
	bool     m_batchCheckModules = false;  // /CheckModules    — compile-check all modules
	bool     m_batchRunTests = false;      // /RunTests        — run module unit tests (Тест*/Test* methods)
	wxString m_batchJunit;                 // /Junit <file>    — write a JUnit XML report of the test run

	// OES-TEST: --testagent[=port] starts the embedded test-automation agent (docs/test-automation.md).
	// -1 = off. A bare --testagent uses ibTestAgent::kDefaultTestAgentPort.
	int      m_testAgentPort = -1;

	// --mcp[=port]: embedded HTTP MCP server so Claude Code drives the running
	// Configurator (config edit + live UI/debug). -1 = off; bare --mcp uses the
	// default port. See tools/oes_mcp / docs.
	int      m_mcpPort = -1;

public:

	// ibWxApp pre-wires Install / WrapStartup / 3 exception overrides.
	wxString GetExeName() const override { return wxT("designer"); }

	// DoOnInit defaulted on the base (wxSocketBase::Initialize + wxApp::OnInit).
	int DoOnRun() override;

	int OnExit() override;

private:
	// OES-CLI: scan argv for a 1C-style batch verb (called from OnInitCmdLine
	// before wx parses, so we can relax the parser for '/'-prefixed tokens).
	bool DetectBatchMode() const;
	// OES-CLI: parse the 1C-style command line into the m_batch* members.
	void ParseBatchArgs();
	// OES-CLI: run the headless batch operation; returns the process exit code.
	int  RunBatch();
	// OES-CLI: write the accumulated batch report to /Out (and stdout).
	void WriteBatchReport(const wxString& report) const;
	// OES-TEST: /RunTests — discover & run module unit tests; true when all passed.
	bool RunModuleTests(class ibSession* session, wxString& report);

public:

public:

#if wxUSE_CMDLINE_PARSER
	// this one is called from OnInit() to add all supported options
	// to the given parser 
	virtual void OnInitCmdLine(wxCmdLineParser& parser);
	virtual bool OnCmdLineParsed(wxCmdLineParser& parser);
#endif // wx

	virtual int FilterEvent(wxEvent& event) override;

protected:

	//global process events:
	void OnKeyEvent(wxKeyEvent& event);
	void OnMouseEvent(wxMouseEvent& event);
	void OnSetFocus(wxFocusEvent& event);
};

wxDECLARE_APP(ibAppDesigner);

#endif 