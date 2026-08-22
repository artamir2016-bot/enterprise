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
#include "backend/backend_mainFrame.h"                         // ibBackendDocFrame::ActiveWindow
#include "backend/backend_form.h"                              // ibBackendValueForm::ShowForm
#include "backend/system/systemManager.h"                      // ibValueSystemFunction::SetMessageTap

#include "frontend/visualView/ctrl/form.h"                     // ibValueForm
#include "frontend/visualView/ctrl/frame.h"                    // ibValueFrame (FindControlByName, Get/SetControlValue)
#include "frontend/visualView/ctrl/formCommand.h"              // ibFormCommandValue
#include "frontend/visualView/ctrl/formAttribute.h"            // ibFormAttributeValue (GetValue/SetHeldValue)
#include "frontend/visualView/visualHostClient.h"              // ibFormVisualDocument::GetOpenForms

#include <vector>
#include <utility>

using nlohmann::json;

namespace {

	// --- captured user messages (Сообщить / Message) ---------------------------------------------
	// Populated by the tap on the GUI thread; read by getMessages on the same thread — no lock.
	std::vector<std::pair<wxString, int>> gs_messages;
	bool gs_tapInstalled = false;

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

	json Cmd_FindControl(const json& args)
	{
		ibValueForm* form = ResolveForm(args);
		const wxString name = FromUtf8(args.at("name"));
		ibValueFrame* ctrl = form->FindControlByName(name);
		return json{ {"found", ctrl != nullptr} };
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

		ibBackendValueForm* form = (kind == "list") ? cat->GetListForm() : cat->GetObjectForm();
		if (form == nullptr)
			throw std::runtime_error("could not create form");
		form->ShowForm();
		return json{ {"opened", true} };
	}

	json Cmd_PressCommand(const json& args)
	{
		ibValueForm* form = ResolveForm(args);
		const wxString name = FromUtf8(args.at("name"));

		// A form command (its named handler procedure) — the button-delegated event.
		if (form->GetFormCommand(name) != nullptr) {
			form->CallAsEvent(name);
			return json{ {"pressed", true}, {"via", "formCommand"} };
		}

		// Otherwise fire it as a form-module procedure by name (a control's OnClick handler, etc.).
		// (Pressing STANDARD actions — записать/провести — needs the protected command set and is a
		// documented follow-up; see docs/test-automation.md.)
		form->CallAsEvent(name);
		return json{ {"pressed", true}, {"via", "event"} };
	}

	json Cmd_GetMessages()
	{
		json arr = json::array();
		for (const auto& m : gs_messages)
			arr.push_back({ {"text", ToUtf8(m.first)}, {"status", m.second} });
		return json{ {"messages", arr} };
	}

	json Cmd_ClearMessages()
	{
		gs_messages.clear();
		return json{ {"ok", true} };
	}
}

void ibTestAgentInstallMessageTap()
{
	if (gs_tapInstalled)
		return;
	gs_tapInstalled = true;
	ibValueSystemFunction::SetMessageTap([](const wxString& text, ibStatusMessage status) {
		gs_messages.emplace_back(text, static_cast<int>(status));
		if (gs_messages.size() > 2000)                 // bound the buffer
			gs_messages.erase(gs_messages.begin(), gs_messages.begin() + 1000);
	});
}

bool ibTestAgentDispatchForm(const std::string& cmd, const json& args, json& result)
{
	if      (cmd == "getForms")        result = Cmd_GetForms();
	else if (cmd == "activeForm")      result = Cmd_ActiveForm();
	else if (cmd == "findControl")     result = Cmd_FindControl(args);
	else if (cmd == "getControlValue") result = Cmd_GetControlValue(args);
	else if (cmd == "setControlValue") result = Cmd_SetControlValue(args);
	else if (cmd == "getAttribute")    result = Cmd_GetAttribute(args);
	else if (cmd == "setAttribute")    result = Cmd_SetAttribute(args);
	else if (cmd == "openForm")        result = Cmd_OpenForm(args);
	else if (cmd == "pressCommand")    result = Cmd_PressCommand(args);
	else if (cmd == "getMessages")     result = Cmd_GetMessages();
	else if (cmd == "clearMessages")   result = Cmd_ClearMessages();
	else return false;
	return true;
}
