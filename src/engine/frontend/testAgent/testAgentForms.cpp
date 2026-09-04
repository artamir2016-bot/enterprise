// OES-TEST: enterprise runtime-form command set for the test-automation agent.
//
// Drives the live runtime UI in-process (the agent runs on the GUI thread): enumerate open forms,
// resolve a control by name, read/write control + attribute values, open a catalog form, press a
// command, and read captured user messages (Сообщить). See docs/test-automation.md.

#include "testAgentInternal.h"

#include "backend/appData.h"                                   // ibApplicationData::GetActiveMetaData
#include "backend/metadataConfiguration.h"                     // ibMetaDataConfigurationBase (complete type)
#include "backend/metaData.h"                                   // ibMetaData::FindAnyObjectByFilter (public, by name)
#include "backend/metaCollection/partial/catalog.h"            // ibValueMetaObjectCatalog (GetObjectForm/GetListForm)
#include "backend/metaCollection/metaObject.h"                 // g_metaCatalogCLSID
#include "backend/session/session.h"                           // ibSession::CurrentFrame
#include "backend/backend_mainFrame.h"                         // ibBackendDocFrame::ActiveWindow + modal interceptor
#include "backend/backend_diagnostic.h"                        // ibDiagnostics / ibDiagnosticSink — capture runtime/compile errors
#include "backend/backend_form.h"                              // ibBackendValueForm::ShowForm
#include "backend/standardCommand.h"                           // ibActionID (the set type itself stays unnamed — use auto)
#include "backend/system/systemManager.h"                      // ibValueSystemFunction::SetMessageTap
#include "backend/debugger/debugClient.h"                      // OES-TEST: setBreakpoint / debugState (profiler choreography)
#include "backend/metaCollection/metaModuleObject.h"           // ibValueMetaObjectCommonModule / …ModuleBase (GetDocPath)
#include "frontend/win/ctrls/treelistctrl.h"                   // ibTreeListCtrl (readTreeList — profiler panel)

#include <functional>   // recursive tree walk in readTreeList

#include "frontend/visualView/ctrl/form.h"                     // ibValueForm
#include "frontend/visualView/ctrl/frame.h"                    // ibValueFrame (FindControlByName, Get/SetControlValue)
#include "frontend/visualView/ctrl/formCommand.h"              // ibFormCommandValue
#include "frontend/visualView/ctrl/formAttribute.h"            // ibFormAttributeValue (GetValue/SetHeldValue)
#include "frontend/visualView/visualHostClient.h"              // ibFormVisualDocument::GetOpenForms
#include "frontend/docView/docView.h"                          // ibDocManager::OpenObjectForm (open a metaobject editor headless)
#include "backend/metaCollection/metaFormObject.h"             // ibValueMetaObjectForm, g_metaFormCLSID

#include <wx/uiaction.h>   // OES-TEST: REAL OS mouse/keyboard input (for video-able runs)
#include <wx/window.h>
#include <wx/gdicmn.h>     // wxPoint / wxRect
#include <wx/utils.h>      // wxGetMousePosition / wxMilliSleep
#include <wx/app.h>        // wxTheApp->Yield
#include <wx/dcscreen.h>   // wxScreenDC (screenshot)
#include <wx/dcmemory.h>
#include <wx/image.h>
#include <wx/bitmap.h>

#include <wx/toplevel.h>   // wxGetTopLevelParent / wxTopLevelWindows
#include <wx/frame.h>      // wxFrame / GetMenuBar (generic UI driving)
#include <wx/menu.h>       // wxMenuBar / wxMenu / wxMenuItem
#include <wx/settings.h>   // wxSystemSettings (menu-bar font for hit-testing top menus)
#include <wx/treectrl.h>   // wxTreeCtrl item enumeration (Все функции, designer navigator)

#ifdef __WXMSW__
#include <windows.h>       // OES-TEST: force the target window to the foreground for real input
#endif

#include <vector>
#include <utility>
#include <mutex>

using nlohmann::json;

namespace {

	// --- captured user-facing diagnostics -------------------------------------------------------
	// Sources fire on any thread (Сообщить from a worker, ibDiagnostics from the failing thread,
	// a modal from the GUI thread) — guard the buffers with a mutex.
	std::mutex gs_captureMutex;
	// Flat message log: Сообщить, intercepted modals, and error text — what "Я вижу сообщение" reads.
	std::vector<std::pair<wxString, int>> gs_messages;
	// Structured runtime/compile diagnostics ({module,line,message,kind}) from ibDiagnostics.
	struct DiagEntry { wxString message; wxString module; int line; int kind; };
	std::vector<DiagEntry> gs_diagnostics;
	bool gs_tapInstalled = false;

	void PushMessage(const wxString& text, int status)
	{
		std::lock_guard<std::mutex> lock(gs_captureMutex);
		gs_messages.emplace_back(text, status);
		if (gs_messages.size() > 4000)
			gs_messages.erase(gs_messages.begin(), gs_messages.begin() + 2000);
	}

	// OES-TEST: capture EVERY published runtime/compile diagnostic (the "{Module(line)}: message"
	// failures) as data — the sink the ibDiagnostic header names a test as the intended consumer of.
	class ibTestAgentDiagSink : public ibDiagnosticSink {
	public:
		void OnDiagnostic(const ibDiagnostic& d) override {
			{
				std::lock_guard<std::mutex> lock(gs_captureMutex);
				gs_diagnostics.push_back({ d.m_message, d.m_moduleName, static_cast<int>(d.m_line),
					static_cast<int>(d.m_kind) });
			}
			// Also fold the decorated form into the flat log so message assertions catch errors too.
			PushMessage(wxString::Format(wxT("{%s(%u)}: %s"),
				d.m_moduleName, d.m_line, d.m_message), 2 /*error*/);
		}
	};
	ibTestAgentDiagSink gs_diagSink;

	std::string ToUtf8(const wxString& s) { return std::string(s.utf8_str()); }
	wxString    FromUtf8(const json& v)   { return wxString::FromUTF8(v.get<std::string>().c_str()); }

	// Resolve the target form: args["form"] = a caption to match among open forms, else the active
	// form. Throws if none can be resolved.
	ibValueForm* ResolveForm(const json& args)
	{
		if (args.contains("form") && args["form"].is_string()) {
			const wxString cap = FromUtf8(args["form"]);
			for (ibValueForm* f : ibFormVisualDocument::GetOpenForms())
				if (f != nullptr && f->GetCaption() == cap)
					return f;
			throw std::runtime_error("form not found: " + ToUtf8(cap));
		}
		if (auto* frame = ibSession::CurrentFrame()) {
			if (auto* form = dynamic_cast<ibValueForm*>(frame->ActiveWindow()))
				return form;
		}
		throw std::runtime_error("no active form");
	}

	ibValueFrame* ResolveControl(const json& args)
	{
		ibValueForm* form = ResolveForm(args);
		const wxString name = FromUtf8(args.at("name"));
		ibValueFrame* ctrl = form->FindControlByName(name);
		if (ctrl == nullptr)
			throw std::runtime_error("control not found: " + ToUtf8(name));
		return ctrl;
	}

	// JSON scalar -> ibValue (best-effort typing; the control coerces to its own type on set).
	ibValue JsonToValue(const json& v)
	{
		if (v.is_boolean())        return ibValue(v.get<bool>());
		if (v.is_number_integer()) return ibValue(static_cast<double>(v.get<long long>()));
		if (v.is_number_float())   return ibValue(v.get<double>());
		if (v.is_string())         return ibValue(wxString::FromUTF8(v.get<std::string>().c_str()));
		return ibValue();
	}

	// --- command handlers ------------------------------------------------------------------------

	json Cmd_GetForms()
	{
		json arr = json::array();
		for (ibValueForm* f : ibFormVisualDocument::GetOpenForms()) {
			if (f == nullptr) continue;
			arr.push_back({ {"caption", ToUtf8(f->GetCaption())}, {"shown", f->IsShown()} });
		}
		return json{ {"forms", arr} };
	}

	json Cmd_ActiveForm()
	{
		auto* frame = ibSession::CurrentFrame();
		auto* form = frame != nullptr ? dynamic_cast<ibValueForm*>(frame->ActiveWindow()) : nullptr;
		if (form == nullptr)
			return json{ {"caption", nullptr} };
		return json{ {"caption", ToUtf8(form->GetCaption())} };
	}

	// OES-TEST: the FOCUSED control of the active form — name + class, so the inspector can position
	// on it. Maps the focused wxWindow back to its ibValueFrame via the form host's object index.
	json Cmd_ActiveControl(const json& args)
	{
		ibValueForm* form = ResolveForm(args);
		wxWindow* focus = wxWindow::FindFocus();
		if (focus == nullptr)
			return json{ {"name", nullptr} };

		ibFormVisualDocument* doc = form->GetVisualDocument();
		ibVisualHostClient* host = (doc != nullptr && doc->GetFirstView() != nullptr)
			? doc->GetFirstView()->GetVisualHost() : nullptr;
		if (host == nullptr)
			return json{ {"name", nullptr} };

		for (wxWindow* w = focus; w != nullptr; w = w->GetParent()) {
			ibValueFrame* base = host->GetObjectBase(w);
			if (base != nullptr) {
				const wxString nm = base->GetControlName();
				if (!nm.IsEmpty())
					return json{ {"name", ToUtf8(nm)}, {"class", ToUtf8(base->GetClassName())} };
			}
		}
		return json{ {"name", nullptr} };
	}

	json Cmd_FindControl(const json& args)
	{
		ibValueForm* form = ResolveForm(args);
		const wxString name = FromUtf8(args.at("name"));
		ibValueFrame* ctrl = form->FindControlByName(name);
		return json{ {"found", ctrl != nullptr} };
	}

	void CollectControls(ibValueFrame* node, json& arr)
	{
		if (node == nullptr)
			return;
		for (unsigned int i = 0; i < node->GetChildCount(); ++i) {
			ibValueFrame* c = node->GetChild(i);
			if (c == nullptr)
				continue;
			const wxString name = c->GetControlName();
			const wxString cls = c->GetClassName();
			// Skip layout wrappers (SizerItem/"Sizer") — they carry no addressable control name.
			if (!name.IsEmpty() && !cls.Contains(wxT("Sizer")))
				arr.push_back({ {"name", ToUtf8(name)}, {"class", ToUtf8(cls)} });
			CollectControls(c, arr);   // recurse into containers regardless
		}
	}

	json Cmd_ListControls(const json& args)
	{
		ibValueForm* form = ResolveForm(args);
		json arr = json::array();
		CollectControls(form, arr);
		return json{ {"controls", arr} };
	}

	json Cmd_GetControlValue(const json& args)
	{
		ibValueFrame* ctrl = ResolveControl(args);
		ibValue v;
		if (!ctrl->GetControlValue(v))
			throw std::runtime_error("control has no readable value");
		return json{ {"value", ToUtf8(v.GetString())} };
	}

	json Cmd_SetControlValue(const json& args)
	{
		ibValueFrame* ctrl = ResolveControl(args);
		if (!ctrl->SetControlValue(JsonToValue(args.at("value"))))
			throw std::runtime_error("control value could not be set");
		return json{ {"ok", true} };
	}

	json Cmd_GetAttribute(const json& args)
	{
		ibValueForm* form = ResolveForm(args);
		const wxString name = FromUtf8(args.at("name"));
		ibFormAttributeValue* attr = form->GetAttribute(name);
		if (attr == nullptr)
			throw std::runtime_error("attribute not found: " + ToUtf8(name));
		ibValue v; attr->GetValue(v);
		return json{ {"value", ToUtf8(v.GetString())} };
	}

	json Cmd_SetAttribute(const json& args)
	{
		ibValueForm* form = ResolveForm(args);
		const wxString name = FromUtf8(args.at("name"));
		ibFormAttributeValue* attr = form->GetAttribute(name);
		if (attr == nullptr)
			throw std::runtime_error("attribute not found: " + ToUtf8(name));
		attr->SetHeldValue(JsonToValue(args.at("value")));
		return json{ {"ok", true} };
	}

	json Cmd_OpenForm(const json& args)
	{
		const wxString name = FromUtf8(args.at("name"));
		const std::string kind = args.value("kind", std::string("object"));

		auto* md = ibApplicationData::GetActiveMetaData();
		if (md == nullptr)
			throw std::runtime_error("no active configuration");

		// MVP: catalogs (Товары is a catalog). Other metatypes follow the same recipe.
		// FindAnyObjectByFilter is the PUBLIC by-name lookup (the raw walk is protected).
		auto* cat = md->FindAnyObjectByFilter<ibValueMetaObjectCatalog>(
			name, g_metaCatalogCLSID, true);
		if (cat == nullptr)
			throw std::runtime_error("catalog not found: " + ToUtf8(name));

		const bool listKind = (kind == "list");
		// DEFERRED: opening an OBJECT form runs the ОбработкаЗаполнения (Filling) handler and compiles
		// the object module — either can raise a modal error dialog. Run it inline in this socket
		// callback and that modal blocks the agent forever. On the event loop the modal interceptor
		// auto-answers it; the reply returns at once. The runner settles briefly before asserting.
		if (wxTheApp != nullptr) {
			wxTheApp->CallAfter([cat, listKind]() {
				try {
					ibBackendValueForm* form = listKind ? cat->GetListForm() : cat->GetObjectForm();
					if (form != nullptr)
						form->ShowForm();
				} catch (...) { /* errors surface via the diagnostic/modal taps */ }
			});
		}
		return json{ {"opened", true}, {"deferred", true} };
	}

	// OES-TEST: open a metaobject's EDITOR (the designer form-editor canvas — where an imported
	// control tree is rendered), by object NAME, WITHOUT a mouse. The tree double-click path goes
	// through wxUIActionSimulator, whose real clicks only land when the app is the foreground window
	// (unreliable in background automation); this calls the same entry point (ibDocManager::
	// OpenObjectForm) the tree activation does, programmatically. Deferred to the event loop so any
	// modal (a compile error) is auto-answered by the interceptor instead of blocking the socket.
	json Cmd_OpenMetaEditor(const json& args)
	{
		const wxString name = FromUtf8(args.at("name"));
		auto* md = ibApplicationData::GetActiveMetaData();
		if (md == nullptr)
			throw std::runtime_error("no active configuration");
		auto* form = md->FindAnyObjectByFilter<ibValueMetaObjectForm>(name, g_metaFormCLSID, true);
		if (form == nullptr)
			throw std::runtime_error("form not found: " + ToUtf8(name));
		if (wxTheApp != nullptr) {
			wxTheApp->CallAfter([form]() {
				try { ibDocManager::OpenObjectForm(form, ibDOC_NEW); }
				catch (...) { /* errors surface via the diagnostic / modal taps */ }
			});
		}
		return json{ {"opened", true}, {"deferred", true} };
	}

	// OES-TEST: close every open form and every secondary top-level window (e.g. «Все функции»),
	// leaving only the main application frame — a "reset the workspace" step for a feature's context.
	// Deferred: closing a dirty form may raise a modal save prompt (auto-answered by the interceptor);
	// running that inside the socket callback would block the agent.
	json Cmd_CloseAllWindows()
	{
		std::vector<ibValueForm*> forms;
		for (ibValueForm* f : ibFormVisualDocument::GetOpenForms())
			if (f != nullptr) forms.push_back(f);

		wxWindow* main = wxTheApp != nullptr ? wxTheApp->GetTopWindow() : nullptr;
		std::vector<wxWindow*> tops;
		for (wxWindowList::iterator it = wxTopLevelWindows.begin(); it != wxTopLevelWindows.end(); ++it) {
			wxWindow* w = *it;
			if (w != nullptr && w != main && w->IsShown())
				tops.push_back(w);
		}

		const int nForms = static_cast<int>(forms.size());
		const int nTops = static_cast<int>(tops.size());
		if (wxTheApp != nullptr) {
			wxTheApp->CallAfter([forms, tops]() {
				for (ibValueForm* f : forms) {
					try { if (f != nullptr) f->CloseForm(); } catch (...) {}
				}
				for (wxWindow* w : tops) {
					try { if (w != nullptr) w->Close(true); } catch (...) {}
				}
			});
		}
		return json{ {"forms", nForms}, {"windows", nTops}, {"deferred", true} };
	}

	json Cmd_PressCommand(const json& args)
	{
		ibValueForm* form = ResolveForm(args);
		const wxString name = FromUtf8(args.at("name"));

		// 1) a form command (its named handler procedure) — the button-delegated event.
		if (form->GetFormCommand(name) != nullptr) {
			form->CallAsEvent(name);
			return json{ {"pressed", true}, {"via", "formCommand"} };
		}

		// 2) a STANDARD action (Save / SaveAndClose / Post / …) — match by internal name OR by the
		// displayed caption. `auto` avoids naming the protected-nested command-set type; its accessors
		// are public. CallAsAction delegates to the source object (record write → triggers ПередЗаписью).
		//
		// Run it DEFERRED (CallAfter) on the event loop, NOT inside this socket callback: a write opens
		// a DB transaction and, on error/cancel, rolls back — doing that reentrantly from the socket
		// handler hangs the agent. Deferred, it runs on a clean stack; any message / error is captured
		// by the taps + diagnostic sink, so the runner reads the outcome via getMessages/getDiagnostics
		// after a short wait. The reply returns immediately so the agent never blocks.
		auto cmds = form->GetStandardCommands(form->GetTypeForm());
		for (unsigned int i = 0; i < cmds.GetCount(); ++i) {
			const ibActionID id = cmds.GetID(i);
			if (id == wxNOT_FOUND) continue;
			if (cmds.GetNameByID(id) == name || cmds.GetCaptionByID(id) == name) {
				if (wxTheApp != nullptr) {
					wxTheApp->CallAfter([form, id]() {
						try { form->CallAsAction(id, form); }
						catch (...) { /* captured by the diagnostic sink; never take the app down */ }
					});
				}
				return json{ {"pressed", true}, {"via", "standardAction"}, {"deferred", true} };
			}
		}

		// 3) otherwise fire it as a form-module procedure by name (a control's OnClick handler, etc.).
		form->CallAsEvent(name);
		return json{ {"pressed", true}, {"via", "event"} };
	}

	json Cmd_GetMessages()
	{
		std::lock_guard<std::mutex> lock(gs_captureMutex);
		json arr = json::array();
		for (const auto& m : gs_messages)
			arr.push_back({ {"text", ToUtf8(m.first)}, {"status", m.second} });
		return json{ {"messages", arr} };
	}

	json Cmd_ClearMessages()
	{
		std::lock_guard<std::mutex> lock(gs_captureMutex);
		gs_messages.clear();
		gs_diagnostics.clear();
		return json{ {"ok", true} };
	}

	json Cmd_GetDiagnostics()
	{
		std::lock_guard<std::mutex> lock(gs_captureMutex);
		json arr = json::array();
		for (const auto& d : gs_diagnostics)
			arr.push_back({ {"message", ToUtf8(d.message)}, {"module", ToUtf8(d.module)},
				{"line", d.line}, {"kind", d.kind} });   // kind: 0=Compile, 1=Runtime
		return json{ {"diagnostics", arr} };
	}

	json Cmd_ClearDiagnostics()
	{
		std::lock_guard<std::mutex> lock(gs_captureMutex);
		gs_diagnostics.clear();
		return json{ {"ok", true} };
	}

	// =============================================================================================
	// REAL OS input (wxUIActionSimulator) — moves the actual cursor and sends real key events so a
	// screen recorder captures a genuine walkthrough. Runs on the GUI thread; simulated input is
	// queued to the OS and processed once we return to the event loop, so Pump() yields to let it
	// take effect and keep the UI painting during a move.
	// =============================================================================================

	void Pump(int ms = 0)
	{
		if (wxTheApp != nullptr)
			wxTheApp->Yield(true);
		if (ms > 0)
			wxMilliSleep(ms);
	}

	wxWindow* ControlWindow(const json& args)
	{
		ibValueFrame* ctrl = ResolveControl(args);
		wxWindow* w = wxDynamicCast(ctrl->GetWxObject(), wxWindow);
		if (w == nullptr)
			throw std::runtime_error("control has no window");
		return w;
	}

	// Bring a window's top-level frame to the OS foreground so wxUIActionSimulator input (which the OS
	// routes to the foreground window) lands here. Uses the AttachThreadInput trick to defeat the
	// foreground lock when another process currently owns it.
	void RaiseToForeground(wxWindow* w)
	{
		wxWindow* top = wxGetTopLevelParent(w != nullptr ? w : wxTheApp->GetTopWindow());
		if (top == nullptr)
			return;
		top->Show();
		top->Raise();
#ifdef __WXMSW__
		HWND hwnd = static_cast<HWND>(top->GetHandle());
		if (hwnd != nullptr) {
			const DWORD me = ::GetCurrentThreadId();
			const HWND  fg = ::GetForegroundWindow();
			const DWORD fgTid = fg ? ::GetWindowThreadProcessId(fg, nullptr) : 0;
			if (fgTid && fgTid != me) ::AttachThreadInput(fgTid, me, TRUE);
			::BringWindowToTop(hwnd);
			::SetForegroundWindow(hwnd);
			if (fgTid && fgTid != me) ::AttachThreadInput(fgTid, me, FALSE);
		}
#endif
		Pump(150);
	}

	wxPoint WindowCenter(wxWindow* w)
	{
		const wxRect r = w->GetScreenRect();
		return wxPoint(r.x + r.width / 2, r.y + r.height / 2);
	}

	// Move the real cursor from where it is to `to` in `steps` eased steps — visible, smooth motion.
	void SmoothMoveTo(wxUIActionSimulator& sim, const wxPoint& to, int steps, int delayMs)
	{
		if (steps < 1) steps = 1;
		const wxPoint from = wxGetMousePosition();
		for (int i = 1; i <= steps; ++i) {
			const double t = static_cast<double>(i) / steps;
			const int x = static_cast<int>(from.x + (to.x - from.x) * t);
			const int y = static_cast<int>(from.y + (to.y - from.y) * t);
			sim.MouseMove(x, y);
			Pump(delayMs);
		}
	}

	// Named-key -> keycode (for pressKey). Single printable chars map to themselves (upper-cased).
	long KeyCode(const wxString& key)
	{
		static const struct { const char* n; long k; } tbl[] = {
			{"enter", WXK_RETURN}, {"return", WXK_RETURN}, {"tab", WXK_TAB},
			{"escape", WXK_ESCAPE}, {"esc", WXK_ESCAPE}, {"space", WXK_SPACE},
			{"backspace", WXK_BACK}, {"delete", WXK_DELETE}, {"del", WXK_DELETE},
			{"home", WXK_HOME}, {"end", WXK_END}, {"pageup", WXK_PAGEUP}, {"pagedown", WXK_PAGEDOWN},
			{"up", WXK_UP}, {"down", WXK_DOWN}, {"left", WXK_LEFT}, {"right", WXK_RIGHT},
			{"f1", WXK_F1}, {"f2", WXK_F2}, {"f3", WXK_F3}, {"f4", WXK_F4}, {"f5", WXK_F5},
			{"f6", WXK_F6}, {"f7", WXK_F7}, {"f8", WXK_F8}, {"f9", WXK_F9}, {"f10", WXK_F10},
			{"f11", WXK_F11}, {"f12", WXK_F12},
		};
		const wxString low = key.Lower();
		for (const auto& e : tbl)
			if (low == e.n)
				return e.k;
		if (key.length() == 1)
			return static_cast<long>(wxToupper(key[0]));
		return 0;
	}

	int Modifiers(const json& args)
	{
		int m = wxMOD_NONE;
		if (args.value("ctrl", false))  m |= wxMOD_CONTROL;
		if (args.value("shift", false)) m |= wxMOD_SHIFT;
		if (args.value("alt", false))   m |= wxMOD_ALT;
		return m;
	}

	// --- real-input command handlers -------------------------------------------------------------

	json Cmd_FocusApp(const json&)
	{
		RaiseToForeground(nullptr);
		return json{ {"focused", true} };
	}

	json Cmd_MoveMouse(const json& args)
	{
		wxUIActionSimulator sim;
		const int steps   = args.value("steps", 25);
		const int delayMs = args.value("delayMs", 8);
		wxPoint to;
		if (args.contains("name")) {
			wxWindow* w = ControlWindow(args);
			RaiseToForeground(w);
			to = WindowCenter(w);
		}
		else {
			to = wxPoint(args.at("x").get<int>(), args.at("y").get<int>());
		}
		SmoothMoveTo(sim, to, steps, delayMs);
		return json{ {"x", to.x}, {"y", to.y} };
	}

	json Cmd_ClickControl(const json& args)
	{
		wxUIActionSimulator sim;
		wxWindow* w = ControlWindow(args);
		RaiseToForeground(w);
		SmoothMoveTo(sim, WindowCenter(w), args.value("steps", 25), args.value("delayMs", 8));
		Pump(60);
		if (args.value("double", false))
			sim.MouseDblClick();
		else
			sim.MouseClick();
		Pump(120);
		return json{ {"clicked", true} };
	}

	json Cmd_ClickAt(const json& args)
	{
		wxUIActionSimulator sim;
		SmoothMoveTo(sim, wxPoint(args.at("x").get<int>(), args.at("y").get<int>()),
			args.value("steps", 25), args.value("delayMs", 8));
		Pump(60);
		sim.MouseClick();
		Pump(120);
		return json{ {"clicked", true} };
	}

	json Cmd_TypeText(const json& args)
	{
		wxUIActionSimulator sim;
		RaiseToForeground(nullptr);
		const wxString text = FromUtf8(args.at("text"));
		const int perCharMs = args.value("perCharMs", 35);   // visible typing cadence for video
		for (size_t i = 0; i < text.length(); ++i) {
			const std::string ch = wxString(text[i]).utf8_str().data();
			sim.Text(ch.c_str());
			Pump(perCharMs);
		}
		return json{ {"typed", static_cast<int>(text.length())} };
	}

	json Cmd_PressKey(const json& args)
	{
		wxUIActionSimulator sim;
		const long code = KeyCode(FromUtf8(args.at("key")));
		if (code == 0)
			throw std::runtime_error("unknown key");
		sim.Char(static_cast<int>(code), Modifiers(args));
		Pump(60);
		return json{ {"pressed", true} };
	}

	// Real, video-able field edit: move to the control, click to focus, select-all, type the value.
	json Cmd_SetControlValueReal(const json& args)
	{
		wxUIActionSimulator sim;
		wxWindow* w = ControlWindow(args);
		RaiseToForeground(w);
		SmoothMoveTo(sim, WindowCenter(w), args.value("steps", 25), args.value("delayMs", 8));
		Pump(60);
		sim.MouseClick();
		Pump(120);
		sim.Char('A', wxMOD_CONTROL);   // select all
		Pump(40);
		const wxString text = FromUtf8(args.at("value"));
		const int perCharMs = args.value("perCharMs", 35);
		for (size_t i = 0; i < text.length(); ++i) {
			sim.Text(wxString(text[i]).utf8_str().data());
			Pump(perCharMs);
		}
		return json{ {"ok", true} };
	}

	// =============================================================================================
	// GENERIC wx-UI driving (works in ANY OES app — used to drive the DESIGNER: menus, dialogs,
	// tree, buttons — beyond the runtime-form control tree). See docs/test-automation.md.
	// =============================================================================================

	wxFrame* MainFrame()
	{
		return wxDynamicCast(wxTheApp != nullptr ? wxTheApp->GetTopWindow() : nullptr, wxFrame);
	}

	// Recursively search a window subtree for a widget matching a selector.
	wxWindow* FindWidgetRec(wxWindow* root, const wxString& by, const wxString& val)
	{
		if (root == nullptr)
			return nullptr;
		bool match = false;
		if (by == "name")       match = (root->GetName() == val);
		else if (by == "label") match = (root->GetLabel() == val) || root->GetLabel().Contains(val);
		else if (by == "type")  match = (wxString(root->GetClassInfo()->GetClassName()) == val);
		if (match)
			return root;
		for (wxWindow* child : root->GetChildren()) {
			if (wxWindow* found = FindWidgetRec(child, by, val))
				return found;
		}
		return nullptr;
	}

	wxWindow* FindWidgetAny(const json& args)
	{
		const wxString by  = wxString::FromUTF8(args.value("by", std::string("label")).c_str());
		const wxString val = FromUtf8(args.at("value"));
		for (wxWindowList::iterator it = wxTopLevelWindows.begin(); it != wxTopLevelWindows.end(); ++it) {
			if (wxWindow* found = FindWidgetRec(*it, by, val))
				return found;
		}
		return nullptr;
	}

	json Cmd_ListWindows()
	{
		json arr = json::array();
		for (wxWindowList::iterator it = wxTopLevelWindows.begin(); it != wxTopLevelWindows.end(); ++it) {
			wxWindow* w = *it;
			if (w == nullptr) continue;
			const wxRect r = w->GetScreenRect();
			arr.push_back({ {"title", ToUtf8(w->GetLabel())},
				{"class", ToUtf8(wxString(w->GetClassInfo()->GetClassName()))},
				{"shown", w->IsShown()},
				{"x", r.x}, {"y", r.y}, {"w", r.width}, {"h", r.height} });
		}
		return json{ {"windows", arr} };
	}

	// OES-TEST: walk the raw wxWindow tree of a window — works for ANY window (dialogs, «Все функции»,
	// designer panels), not just ibValueForm runtime forms that Cmd_ListControls handles.
	// Enumerate ALREADY-LOADED items of a wxTreeCtrl (e.g. «Все функции»); collapsed/lazy branches
	// that haven't populated their children yet are simply not listed.
	void CollectTreeItems(wxTreeCtrl* tree, const wxTreeItemId& parent, json& arr, int depth)
	{
		if (tree == nullptr || !parent.IsOk())
			return;
		wxTreeItemIdValue cookie;
		for (wxTreeItemId ch = tree->GetFirstChild(parent, cookie); ch.IsOk();
			 ch = tree->GetNextChild(parent, cookie)) {
			arr.push_back({ {"name", std::string()}, {"label", ToUtf8(tree->GetItemText(ch))},
				{"class", std::string("wxTreeItem")}, {"depth", depth}, {"shown", true} });
			CollectTreeItems(tree, ch, arr, depth + 1);
		}
	}

	void CollectWidgets(wxWindow* node, json& arr, int depth)
	{
		if (node == nullptr)
			return;
		for (wxWindowList::compatibility_iterator n = node->GetChildren().GetFirst();
			 n != nullptr; n = n->GetNext()) {
			wxWindow* c = n->GetData();
			if (c == nullptr)
				continue;
			const wxString cls = wxString(c->GetClassInfo()->GetClassName());
			if (!cls.Contains(wxT("Sizer"))) {   // skip layout wrappers
				const wxRect r = c->GetScreenRect();
				arr.push_back({ {"name", ToUtf8(c->GetName())}, {"label", ToUtf8(c->GetLabel())},
					{"class", ToUtf8(cls)}, {"depth", depth}, {"shown", c->IsShown()},
					{"x", r.x}, {"y", r.y}, {"w", r.width}, {"h", r.height} });
			}
			if (wxTreeCtrl* tree = wxDynamicCast(c, wxTreeCtrl))
				CollectTreeItems(tree, tree->GetRootItem(), arr, depth + 1);
			CollectWidgets(c, arr, depth + 1);
		}
	}

	json Cmd_ListWidgets(const json& args)
	{
		wxWindow* root = nullptr;
		if (args.contains("window") && args["window"].is_string()) {
			const wxString want = FromUtf8(args["window"]);
			for (wxWindowList::iterator it = wxTopLevelWindows.begin(); it != wxTopLevelWindows.end(); ++it) {
				wxWindow* w = *it;
				if (w != nullptr && (w->GetLabel() == want || w->GetLabel().Contains(want))) {
					root = w; break;
				}
			}
			if (root == nullptr)
				throw std::runtime_error("window not found: " + ToUtf8(want));
		} else {
			// the focused window's top-level parent — i.e. the window the user is actually looking at
			wxWindow* f = wxWindow::FindFocus();
			root = f != nullptr ? wxGetTopLevelParent(f) : nullptr;
			if (root == nullptr)
				root = wxTheApp != nullptr ? wxTheApp->GetTopWindow() : nullptr;
		}
		json arr = json::array();
		CollectWidgets(root, arr, 0);
		return json{ {"widgets", arr}, {"window", root != nullptr ? ToUtf8(root->GetLabel()) : std::string()} };
	}

	// The window a tree command targets: args["window"] by title, else the focused window's top level.
	wxWindow* ResolveWindow(const json& args)
	{
		if (args.contains("window") && args["window"].is_string()) {
			const wxString want = FromUtf8(args["window"]);
			for (wxWindowList::iterator it = wxTopLevelWindows.begin(); it != wxTopLevelWindows.end(); ++it) {
				wxWindow* w = *it;
				if (w != nullptr && (w->GetLabel() == want || w->GetLabel().Contains(want)))
					return w;
			}
			throw std::runtime_error("window not found: " + ToUtf8(want));
		}
		wxWindow* f = wxWindow::FindFocus();
		wxWindow* root = f != nullptr ? wxGetTopLevelParent(f) : nullptr;
		if (root == nullptr)
			root = wxTheApp != nullptr ? wxTheApp->GetTopWindow() : nullptr;
		return root;
	}

	wxTreeCtrl* FindTreeCtrl(wxWindow* root)
	{
		if (root == nullptr)
			return nullptr;
		if (wxTreeCtrl* t = wxDynamicCast(root, wxTreeCtrl))
			return t;
		for (wxWindowList::compatibility_iterator n = root->GetChildren().GetFirst();
			 n != nullptr; n = n->GetNext())
			if (wxTreeCtrl* t = FindTreeCtrl(n->GetData()))
				return t;
		return nullptr;
	}

	// Find an item by text, expanding lazily-loaded branches along the way so deep items are reachable.
	wxTreeItemId FindTreeItem(wxTreeCtrl* tree, const wxTreeItemId& parent, const wxString& text)
	{
		if (tree == nullptr || !parent.IsOk())
			return wxTreeItemId();
		wxTreeItemIdValue cookie;
		for (wxTreeItemId ch = tree->GetFirstChild(parent, cookie); ch.IsOk();
			 ch = tree->GetNextChild(parent, cookie)) {
			const wxString label = tree->GetItemText(ch);
			if (label == text || label.Contains(text))
				return ch;
			if (tree->ItemHasChildren(ch)) {
				tree->Expand(ch);   // trigger lazy population before recursing
				if (wxTreeItemId sub = FindTreeItem(tree, ch, text); sub.IsOk())
					return sub;
			}
		}
		return wxTreeItemId();
	}

	// Find an item by text WITHOUT expanding anything (so a state query doesn't change the state);
	// only already-loaded children are traversed.
	wxTreeItemId FindTreeItemNoExpand(wxTreeCtrl* tree, const wxTreeItemId& parent, const wxString& text)
	{
		if (tree == nullptr || !parent.IsOk())
			return wxTreeItemId();
		wxTreeItemIdValue cookie;
		for (wxTreeItemId ch = tree->GetFirstChild(parent, cookie); ch.IsOk();
			 ch = tree->GetNextChild(parent, cookie)) {
			const wxString label = tree->GetItemText(ch);
			if (label == text || label.Contains(text))
				return ch;
			if (wxTreeItemId sub = FindTreeItemNoExpand(tree, ch, text); sub.IsOk())
				return sub;
		}
		return wxTreeItemId();
	}

	// OES-TEST: is a tree node open (expanded) or closed? Does not alter the tree.
	json Cmd_TreeItemState(const json& args)
	{
		wxTreeCtrl* tree = FindTreeCtrl(ResolveWindow(args));
		if (tree == nullptr)
			throw std::runtime_error("no tree control in window");
		const wxString text = FromUtf8(args.at("text"));
		wxTreeItemId item = FindTreeItemNoExpand(tree, tree->GetRootItem(), text);
		if (!item.IsOk())
			return json{ {"found", false} };
		const bool hasChildren = tree->ItemHasChildren(item);
		return json{ {"found", true},
			{"hasChildren", hasChildren},
			{"expanded", hasChildren && tree->IsExpanded(item)} };
	}

	json Cmd_ExpandTreeItem(const json& args)
	{
		wxTreeCtrl* tree = FindTreeCtrl(ResolveWindow(args));
		if (tree == nullptr)
			throw std::runtime_error("no tree control in window");
		const wxString text = FromUtf8(args.at("text"));
		wxTreeItemId item = FindTreeItem(tree, tree->GetRootItem(), text);
		if (!item.IsOk())
			throw std::runtime_error("tree item not found: " + ToUtf8(text));
		tree->Expand(item);
		tree->EnsureVisible(item);
		return json{ {"expanded", true} };
	}

	json Cmd_CollapseTreeItem(const json& args)
	{
		wxTreeCtrl* tree = FindTreeCtrl(ResolveWindow(args));
		if (tree == nullptr)
			throw std::runtime_error("no tree control in window");
		const wxString text = FromUtf8(args.at("text"));
		wxTreeItemId item = FindTreeItemNoExpand(tree, tree->GetRootItem(), text);
		if (!item.IsOk())
			throw std::runtime_error("tree item not found: " + ToUtf8(text));
		tree->Collapse(item);
		return json{ {"collapsed", true} };
	}

	json Cmd_ClickTreeItem(const json& args)
	{
		wxTreeCtrl* tree = FindTreeCtrl(ResolveWindow(args));
		if (tree == nullptr)
			throw std::runtime_error("no tree control in window");
		const wxString text = FromUtf8(args.at("text"));
		wxTreeItemId item = FindTreeItem(tree, tree->GetRootItem(), text);
		if (!item.IsOk())
			throw std::runtime_error("tree item not found: " + ToUtf8(text));

		tree->EnsureVisible(item);
		tree->SelectItem(item);
		wxRect rc;
		if (!tree->GetBoundingRect(item, rc, true))
			throw std::runtime_error("tree item has no on-screen rect (not visible)");
		const wxPoint pt = tree->ClientToScreen(wxPoint(rc.x + rc.width / 2, rc.y + rc.height / 2));

		RaiseToForeground(tree);
		const int steps = args.value("steps", 20);
		const int delayMs = args.value("delayMs", 8);
		const bool dbl = args.value("double", false);
		// Deferred: a double-click may open a form / run handlers — keep it off the socket callback.
		if (wxTheApp != nullptr) {
			wxTheApp->CallAfter([pt, steps, delayMs, dbl]() {
				wxUIActionSimulator sim;
				SmoothMoveTo(sim, pt, steps, delayMs);
				wxMilliSleep(40);
				if (dbl) sim.MouseDblClick();
				else     sim.MouseClick();
			});
		}
		return json{ {"clicked", true}, {"double", dbl}, {"deferred", true} };
	}

	json Cmd_ListMenus()
	{
		wxFrame* fr = MainFrame();
		wxMenuBar* mb = fr != nullptr ? fr->GetMenuBar() : nullptr;
		json arr = json::array();
		if (mb != nullptr) {
			for (size_t i = 0; i < mb->GetMenuCount(); ++i) {
				json items = json::array();
				wxMenu* menu = mb->GetMenu(i);
				if (menu != nullptr)
					for (wxMenuItem* it : menu->GetMenuItems())
						if (it != nullptr && !it->IsSeparator())
							items.push_back(ToUtf8(it->GetItemLabelText()));
				arr.push_back({ {"menu", ToUtf8(mb->GetMenuLabelText(i))}, {"items", items} });
			}
		}
		return json{ {"menus", arr} };
	}

	// OES-TEST: click a TOP-LEVEL menu (Файл / Операции / …) so it drops open — a real mouse click
	// on the menu-bar title when its rect is measurable, else keyboard navigation (Alt → Right → Down).
	json Cmd_OpenMenu(const json& args)
	{
		wxFrame* fr = MainFrame();
		wxMenuBar* mb = fr != nullptr ? fr->GetMenuBar() : nullptr;
		if (mb == nullptr)
			throw std::runtime_error("no menu bar");
		const wxString top = FromUtf8(args.at("menu"));
		int index = -1;
		for (size_t i = 0; i < mb->GetMenuCount(); ++i)
			if (mb->GetMenuLabelText(i) == top || mb->GetMenuLabelText(i).Contains(top)) {
				index = static_cast<int>(i); break;
			}
		if (index < 0)
			throw std::runtime_error("top menu not found: " + ToUtf8(top));

		RaiseToForeground(fr);
		Pump(80);

		// A native top menu enters a MODAL tracking loop the instant it opens, which would block this
		// socket callback forever. So compute the action here and run the real input DEFERRED on the
		// event loop — the reply returns at once; the menu drops open just after.
		const int steps = args.value("steps", 20);
		const int delayMs = args.value("delayMs", 8);
		const char* via = "keyboard";
		wxPoint pt(-1, -1);
#ifdef __WXMSW__
		// Exact top-menu title rect from the OS — a wxMenuBar has no usable wx rect on MSW.
		{
			HWND hwnd = static_cast<HWND>(fr->GetHandle());
			HMENU hmenu = hwnd != nullptr ? ::GetMenu(hwnd) : nullptr;
			RECT rc{};
			if (hmenu != nullptr && ::GetMenuItemRect(hwnd, hmenu, static_cast<UINT>(index), &rc)) {
				pt = wxPoint((rc.left + rc.right) / 2, (rc.top + rc.bottom) / 2);  // already screen coords
				via = "mouse";
			}
		}
#endif
		const wxRect mbr = mb->GetScreenRect();
		if (pt.x < 0 && mbr.width > 0 && mbr.height > 0) {
			// Estimate each title's x by accumulating measured label widths (wxMSW pads ~8px/side).
			wxScreenDC dc;
			dc.SetFont(wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT));
			const int padSide = 8;
			int x = mbr.x, hitX = mbr.x, hitW = 0;
			for (size_t i = 0; i <= static_cast<size_t>(index); ++i) {
				const wxSize sz = dc.GetTextExtent(mb->GetMenuLabelText(i));
				const int w = sz.x + padSide * 2;
				if (static_cast<int>(i) == index) { hitX = x; hitW = w; }
				x += w;
			}
			pt = wxPoint(hitX + hitW / 2, mbr.y + mbr.height / 2);
			via = "mouse";
		}

		if (wxTheApp != nullptr) {
			wxTheApp->CallAfter([pt, index, steps, delayMs]() {
				wxUIActionSimulator sim;
				if (pt.x >= 0) {                       // real mouse click on the menu-bar title
					SmoothMoveTo(sim, pt, steps, delayMs);
					wxMilliSleep(40);
					sim.MouseClick();
				} else {                               // keyboard: Alt → Right×index → Down
					sim.KeyDown(WXK_ALT);
					sim.KeyUp(WXK_ALT);
					wxMilliSleep(120);
					for (int k = 0; k < index; ++k) { sim.Char(WXK_RIGHT); wxMilliSleep(60); }
					sim.Char(WXK_DOWN);
				}
			});
		}
		return json{ {"opened", true}, {"index", index}, {"via", via}, {"deferred", true} };
	}

	// Recursively find a menu item by displayed label within a wxMenu.
	wxMenuItem* FindMenuItem(wxMenu* menu, const wxString& label)
	{
		if (menu == nullptr) return nullptr;
		for (wxMenuItem* it : menu->GetMenuItems()) {
			if (it == nullptr) continue;
			if (it->GetItemLabelText() == label || it->GetItemLabelText().Contains(label))
				return it;
			if (it->GetSubMenu() != nullptr)
				if (wxMenuItem* sub = FindMenuItem(it->GetSubMenu(), label))
					return sub;
		}
		return nullptr;
	}

	json Cmd_InvokeMenu(const json& args)
	{
		wxFrame* fr = MainFrame();
		wxMenuBar* mb = fr != nullptr ? fr->GetMenuBar() : nullptr;
		if (mb == nullptr)
			throw std::runtime_error("no menu bar");
		const json& path = args.at("path");
		if (!path.is_array() || path.empty())
			throw std::runtime_error("path must be a non-empty array");

		const wxString top = FromUtf8(path[0]);
		wxMenu* menu = nullptr;
		for (size_t i = 0; i < mb->GetMenuCount(); ++i)
			if (mb->GetMenuLabelText(i) == top || mb->GetMenuLabelText(i).Contains(top)) {
				menu = mb->GetMenu(i); break;
			}
		if (menu == nullptr)
			throw std::runtime_error("top menu not found: " + ToUtf8(top));

		wxMenuItem* item = nullptr;
		for (size_t k = 1; k < path.size(); ++k) {
			item = FindMenuItem(menu, FromUtf8(path[k]));
			if (item == nullptr)
				throw std::runtime_error("menu item not found: " + path[k].get<std::string>());
			if (k + 1 < path.size()) {
				menu = item->GetSubMenu();
				if (menu == nullptr)
					throw std::runtime_error("not a submenu: " + path[k].get<std::string>());
			}
		}
		if (item == nullptr)
			throw std::runtime_error("menu item not resolved");

		// Deferred: a menu handler may open a modal (file / config dialog) — running it inside this
		// socket callback would block the agent. On the event loop it runs cleanly; the reply returns
		// at once and any resulting dialog is driven by findWidget/clickWidget or captured by the taps.
		const int id = item->GetId();
		if (wxTheApp != nullptr) {
			wxTheApp->CallAfter([fr, id]() {
				wxCommandEvent evt(wxEVT_MENU, id);
				evt.SetEventObject(fr);
				fr->GetEventHandler()->ProcessEvent(evt);
			});
		}
		return json{ {"invoked", true}, {"deferred", true} };
	}

	json Cmd_FindWidget(const json& args)
	{
		wxWindow* w = FindWidgetAny(args);
		if (w == nullptr)
			return json{ {"found", false} };
		const wxRect r = w->GetScreenRect();
		return json{ {"found", true},
			{"class", ToUtf8(wxString(w->GetClassInfo()->GetClassName()))},
			{"label", ToUtf8(w->GetLabel())},
			{"x", r.x}, {"y", r.y}, {"w", r.width}, {"h", r.height} };
	}

	json Cmd_ClickWidget(const json& args)
	{
		wxWindow* w = FindWidgetAny(args);
		if (w == nullptr)
			throw std::runtime_error("widget not found");
		RaiseToForeground(w);
		wxUIActionSimulator sim;
		SmoothMoveTo(sim, WindowCenter(w), args.value("steps", 25), args.value("delayMs", 8));
		Pump(60);
		if (args.value("double", false)) sim.MouseDblClick();
		else                              sim.MouseClick();
		Pump(120);
		return json{ {"clicked", true} };
	}

	json Cmd_Screenshot(const json& args)
	{
		const wxString path = FromUtf8(args.at("path"));
		wxRect rect;
		if (args.contains("name")) {
			rect = ControlWindow(args)->GetScreenRect();
		}
		else {
			const wxSize scr = wxGetDisplaySize();
			rect = wxRect(0, 0, scr.x, scr.y);
		}
		wxScreenDC screen;
		wxBitmap bmp(rect.width, rect.height);
		wxMemoryDC mem(bmp);
		mem.Blit(0, 0, rect.width, rect.height, &screen, rect.x, rect.y);
		mem.SelectObject(wxNullBitmap);
		if (!bmp.ConvertToImage().SaveFile(path, wxBITMAP_TYPE_PNG))
			throw std::runtime_error("could not write screenshot");
		return json{ {"saved", ToUtf8(path)}, {"w", rect.width}, {"h", rect.height} };
	}
}

void ibTestAgentInstallMessageTap()
{
	if (gs_tapInstalled)
		return;
	gs_tapInstalled = true;

	// 1) Сообщить / Message (the message pane).
	ibValueSystemFunction::SetMessageTap([](const wxString& text, ibStatusMessage status) {
		PushMessage(text, static_cast<int>(status));
	});

	// 2) Runtime / compile errors ("{Module(line)}: message") — captured as data, before any UI.
	ibDiagnostics::Subscribe(&gs_diagSink);

	// 3) Modal dialogs (Alert / Question / any backend-routed wxMessageBox): capture the text and
	// auto-answer so an automated run never blocks. Default answer proceeds (OK, else Yes, else
	// Cancel), and the enum/return maps accordingly for Question-style calls.
	ibBackendDocFrame::SetModalInterceptor(
		[](const wxString& message, const wxString& caption, int style, int& answer) -> bool {
			PushMessage(caption.IsEmpty() ? message : (caption + wxT(": ") + message), 1 /*warning*/);
			if      (style & wxOK)  answer = wxOK;
			else if (style & wxYES) answer = wxYES;
			else                    answer = wxCANCEL;
			return true;
		});
}

// OES-TEST / GitHub #2: profiler debug-choreography commands (Designer side).

// Register a breakpoint on the debuggee by module doc-path + 0-based line, WITHOUT
// needing the module editor open (the agent has none). Resolves the doc-path from
// a common-module name, or takes {docPath} directly.
json Cmd_SetBreakpoint(const json& args)
{
	ibDebuggerClient* dbg = ibDebuggerClient::Get();
	if (dbg == nullptr)
		throw std::runtime_error("no debug client (run this against the Designer)");

	const unsigned int line = (unsigned int)args.at("line").get<int>();

	wxString docPath;
	if (args.contains("docPath") && args["docPath"].is_string()) {
		docPath = FromUtf8(args["docPath"]);
	}
	else {
		const wxString name = FromUtf8(args.at("module"));
		auto* md = ibApplicationData::GetActiveMetaData();
		if (md == nullptr)
			throw std::runtime_error("no active configuration");
		auto* mod = md->FindAnyObjectByFilter<ibValueMetaObjectCommonModule>(
			name, g_metaCommonModuleCLSID, true);
		if (mod == nullptr)
			throw std::runtime_error("common module not found: " + ToUtf8(name));
		docPath = mod->GetDocPath();
	}

	dbg->AddBreakpointDirect(docPath, line);
	return json{ {"ok", true}, {"docPath", ToUtf8(docPath)}, {"line", (int)line} };
}

// Is a debug session currently parked (entered the debug loop)? Used to poll.
json Cmd_DebugState()
{
	ibDebuggerClient* dbg = ibDebuggerClient::Get();
	const bool hasClient = dbg != nullptr;
	json r = json{ {"hasClient", hasClient},
	               {"parked", hasClient && dbg->IsEnterLoop()} };
	if (hasClient) {
		r["parkedModule"] = ToUtf8(dbg->GetParkedModule());
		r["parkedLine"]   = dbg->GetParkedLine();
	}
	return r;
}

// Dump the rows of an ibTreeListCtrl located by wxWindow name (the profiler panel
// tags its trees "profilerAgg" / "profilerTrace"). Returns rows as arrays of the
// per-column cell text — the GUI assertion surface for the profiler panel.
json Cmd_ReadTreeList(const json& args)
{
	const wxString name = FromUtf8(args.at("name"));
	wxWindow* w = nullptr;
	for (wxWindowList::iterator it = wxTopLevelWindows.begin();
	     it != wxTopLevelWindows.end() && w == nullptr; ++it) {
		if (*it != nullptr)
			w = (*it)->FindWindow(name);
	}
	if (w == nullptr)
		throw std::runtime_error("tree not found: " + ToUtf8(name));

	ibTreeListCtrl* tree = static_cast<ibTreeListCtrl*>(w);   // name is agent-private → safe
	const int cols = tree->GetColumnCount();
	json rows = json::array();

	std::function<void(const wxTreeItemId&)> walk = [&](const wxTreeItemId& parent) {
		wxTreeItemIdValue cookie;
		for (wxTreeItemId ch = tree->GetFirstChild(parent, cookie); ch.IsOk();
		     ch = tree->GetNextChild(parent, cookie)) {
			json cells = json::array();
			for (int c = 0; c < cols; ++c)
				cells.push_back(ToUtf8(tree->GetItemText(ch, c)));
			rows.push_back(cells);
			walk(ch);
		}
	};
	walk(tree->GetRootItem());

	return json{ {"columns", cols}, {"rows", rows} };
}

bool ibTestAgentDispatchForm(const std::string& cmd, const json& args, json& result)
{
	if      (cmd == "getForms")        result = Cmd_GetForms();
	else if (cmd == "closeAllWindows") result = Cmd_CloseAllWindows();
	else if (cmd == "activeForm")      result = Cmd_ActiveForm();
	else if (cmd == "findControl")     result = Cmd_FindControl(args);
	else if (cmd == "listControls")    result = Cmd_ListControls(args);
	else if (cmd == "getControlValue") result = Cmd_GetControlValue(args);
	else if (cmd == "setControlValue") result = Cmd_SetControlValue(args);
	else if (cmd == "getAttribute")    result = Cmd_GetAttribute(args);
	else if (cmd == "setAttribute")    result = Cmd_SetAttribute(args);
	else if (cmd == "openForm")        result = Cmd_OpenForm(args);
	else if (cmd == "openMetaEditor")  result = Cmd_OpenMetaEditor(args);
	else if (cmd == "pressCommand")    result = Cmd_PressCommand(args);
	else if (cmd == "getMessages")     result = Cmd_GetMessages();
	else if (cmd == "clearMessages")   result = Cmd_ClearMessages();
	else if (cmd == "getDiagnostics")  result = Cmd_GetDiagnostics();
	else if (cmd == "clearDiagnostics") result = Cmd_ClearDiagnostics();
	// real OS input (video-able)
	else if (cmd == "focusApp")            result = Cmd_FocusApp(args);
	else if (cmd == "moveMouse")           result = Cmd_MoveMouse(args);
	else if (cmd == "clickControl")        result = Cmd_ClickControl(args);
	else if (cmd == "clickAt")             result = Cmd_ClickAt(args);
	else if (cmd == "typeText")            result = Cmd_TypeText(args);
	else if (cmd == "pressKey")            result = Cmd_PressKey(args);
	else if (cmd == "setControlValueReal") result = Cmd_SetControlValueReal(args);
	else if (cmd == "screenshot")          result = Cmd_Screenshot(args);
	// generic wx-UI driving (designer: menus / dialogs / widgets)
	else if (cmd == "listWindows")         result = Cmd_ListWindows();
	else if (cmd == "activeControl")       result = Cmd_ActiveControl(args);
	else if (cmd == "listWidgets")         result = Cmd_ListWidgets(args);
	else if (cmd == "treeItemState")       result = Cmd_TreeItemState(args);
	else if (cmd == "expandTreeItem")      result = Cmd_ExpandTreeItem(args);
	else if (cmd == "collapseTreeItem")    result = Cmd_CollapseTreeItem(args);
	else if (cmd == "clickTreeItem")       result = Cmd_ClickTreeItem(args);
	else if (cmd == "listMenus")           result = Cmd_ListMenus();
	else if (cmd == "openMenu")            result = Cmd_OpenMenu(args);
	else if (cmd == "invokeMenu")          result = Cmd_InvokeMenu(args);
	else if (cmd == "findWidget")          result = Cmd_FindWidget(args);
	else if (cmd == "clickWidget")         result = Cmd_ClickWidget(args);
	// profiler debug-choreography (GitHub #2 / OES-TEST)
	else if (cmd == "setBreakpoint")       result = Cmd_SetBreakpoint(args);
	else if (cmd == "debugState")          result = Cmd_DebugState();
	else if (cmd == "readTreeList")        result = Cmd_ReadTreeList(args);
	else return false;
	return true;
}
