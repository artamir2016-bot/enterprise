#include "backend/metadataConfigSpec.h"

#include <map>
#include <vector>

#include <wx/file.h>

#include "backend/clsid.h"                                // reference_to_clsid
#include "backend/metadataConfiguration.h"
#include "backend/metaData.h"
#include "backend/metaCollection/metaObject.h"            // g_meta*CLSID
#include "backend/metaCollection/metaModuleObject.h"      // ibValueMetaObjectModule/CommonModule
#include "backend/metaCollection/attribute/metaAttributeObject.h"  // ibValueMetaObjectAttribute
#include "backend/metaCollection/partial/commonObject.h"  // ibValueMetaObjectRecordData (module accessors)
#include "backend/metaCollection/partial/constant.h"      // ibValueMetaObjectConstant
#include "backend/metaCollection/partial/chartOfCharacteristicTypes.h" // ibValueMetaObjectChartOfCharacteristicTypes (value type of characteristics)
#include "backend/metaCollection/partial/calculationRegister.h"        // ibValueMetaObjectCalculationRegister (action-period flag)
#include "backend/metaCollection/partial/chartOfAccounts.h"            // ibValueMetaObjectChartOfAccounts (chart-of-characteristic-types binding)
#include "backend/propertyManager/property/propertyChartOfCharacteristicTypes.h" // ibPropertyChartOfCharacteristicTypes::SetValue
#include "backend/metaCollection/metaFormObject.h"        // ibValueMetaObjectForm
#include "backend/serialize/dataBuilder.h"                 // ibDataNode / ibDataValue (form control tree)
#include "backend/sourceDescription.h"                     // ibSourceDescription / ibSourceDescriptionMemory (control Source binding)
#include "backend/commandDescription.h"                    // ibCommandDescription / ibCommandDescriptionMemory (button -> form command)
#include "backend/typeDescription.h"                        // ibTypeDescription / ibTypeDescriptionMemory (form main-attribute Type)

#include "3rdparty/nlohmann/json.hpp"

using nlohmann::json;

namespace {

// name ("Catalog.X" / "Document.Y" / "Enum.Z") -> created metaobject, for
// resolving reference attribute types in the second pass.
using RefMap = std::map<wxString, ibValueMetaObject*>;

wxString JStr(const json& obj, const char* key, const wxString& def = wxEmptyString) {
	auto it = obj.find(key);
	if (it == obj.end() || !it->is_string())
		return def;
	return wxString::FromUTF8(it->get<std::string>().c_str());
}

long JInt(const json& obj, const char* key, long def) {
	auto it = obj.find(key);
	if (it == obj.end() || !it->is_number_integer())
		return def;
	return static_cast<long>(it->get<long long>());
}

// Apply a friendly type descriptor (primitive with qualifiers, or reference /
// composite reference) onto `td`. Unknown / unresolved references degrade to
// String rather than failing the import.
//   { "type": "Number", "precision": 15, "scale": 2 }
//   { "type": "String", "length": 25 }
//   { "type": "Date" } | { "type": "Boolean" }
//   { "type": "ref", "refs": ["Catalog.X", "Document.Y"] }
bool ApplyType(ibTypeDescription& td, const json& a, const RefMap& refMap,
               const wxString& ctx, wxString& err) {
	const wxString typeName = JStr(a, "type", wxT("String"));

	if (typeName.IsSameAs(wxT("Number"), false)) {
		td.SetDefaultMetaType(ibValueTypes::TYPE_NUMBER);
		td.m_typeData.SetNumber(
			static_cast<unsigned char>(JInt(a, "precision", 10)),
			static_cast<unsigned char>(JInt(a, "scale", 0)));
		return true;
	}
	if (typeName.IsSameAs(wxT("Date"), false)) {
		td.SetDefaultMetaType(ibValueTypes::TYPE_DATE);
		return true;
	}
	if (typeName.IsSameAs(wxT("Boolean"), false)) {
		td.SetDefaultMetaType(ibValueTypes::TYPE_BOOLEAN);
		return true;
	}
	if (typeName.IsSameAs(wxT("ref"), false)) {
		std::vector<ibClassID> refs;
		auto it = a.find("refs");
		if (it != a.end() && it->is_array()) {
			for (const json& r : *it) {
				if (!r.is_string()) continue;
				const wxString key = wxString::FromUTF8(r.get<std::string>().c_str());
				auto found = refMap.find(key);
				if (found != refMap.end() && found->second != nullptr)
					refs.push_back(reference_to_clsid((ibClassID)found->second->GetMetaID()));
			}
		}
		if (refs.empty()) {
			// Unresolved / out-of-MVP target — degrade to a NARROW string placeholder so the
			// import stays structural instead of failing. A GUID-width bound (36) — NOT length 0,
			// which maps to VARCHAR(255): a wide document with dozens of unresolved refs (a
			// partial import where the targets aren't in this slice) would otherwise blow past
			// the DB's per-row byte limit and stall restructuring. A resolved ref is a 16-byte
			// key, so 36 keeps a partial import's row width close to the real one.
			td.SetDefaultMetaType(ibValueTypes::TYPE_STRING);
			td.m_typeData.SetString(36);
			return true;
		}
		if (refs.size() == 1)
			td.SetDefaultMetaType(refs.front());
		else
			td.SetDefaultMetaType(refs);
		return true;
	}
	// Default / "String" and any unknown primitive name.
	if (!typeName.IsSameAs(wxT("String"), false) && !typeName.IsEmpty()) {
		// Unknown scalar name — degrade to String (structural import).
		td.SetDefaultMetaType(ibValueTypes::TYPE_STRING);
		td.m_typeData.SetString(static_cast<unsigned short>(JInt(a, "length", 0)));
		(void)ctx; (void)err;
		return true;
	}
	td.SetDefaultMetaType(ibValueTypes::TYPE_STRING);
	td.m_typeData.SetString(static_cast<unsigned short>(JInt(a, "length", 10)));
	return true;
}

// Create a typed child (attribute / dimension / resource) and apply its type.
bool AddTypedChild(ibMetaDataConfigurationFile& cfg, ibValueMetaObject* owner,
                   const ibClassID& clsid, const json& a, const RefMap& refMap,
                   const char* kind, wxString& err) {
	const wxString name = JStr(a, "name");
	if (name.IsEmpty()) {
		err = wxString::Format(wxT("%s without a 'name'"), wxString::FromUTF8(kind));
		return false;
	}
	ibValueMetaObject* obj = cfg.CreateMetaObject(clsid, owner, /*runObject*/ false, name);
	if (obj == nullptr) {
		err = wxString::Format(wxT("failed to create %s '%s'"), wxString::FromUTF8(kind), name);
		return false;
	}
	auto* attr = dynamic_cast<ibValueMetaObjectAttribute*>(obj);
	if (attr == nullptr) {
		err = wxString::Format(wxT("%s '%s' is not typed"), wxString::FromUTF8(kind), name);
		return false;
	}
	return ApplyType(attr->GetTypeDesc(), a, refMap, name, err);
}

// Create every typed child of one JSON array key under `owner`.
bool AddTypedChildren(ibMetaDataConfigurationFile& cfg, ibValueMetaObject* owner,
                      const json& node, const char* key, const ibClassID& clsid,
                      const RefMap& refMap, wxString& err) {
	auto it = node.find(key);
	if (it == node.end())
		return true;
	if (!it->is_array()) {
		err = wxString::Format(wxT("'%s' must be an array"), wxString::FromUTF8(key));
		return false;
	}
	for (const json& a : *it)
		if (!AddTypedChild(cfg, owner, clsid, a, refMap, key, err))
			return false;
	return true;
}

// Set object/manager module text on a record-data object, if present in JSON.
void ApplyObjectModules(ibValueMetaObject* obj, const json& node) {
	auto* rec = dynamic_cast<ibValueMetaObjectRecordData*>(obj);
	if (rec == nullptr)
		return;
	const wxString objCode = JStr(node, "objectModule");
	if (!objCode.IsEmpty()) {
		if (auto* m = const_cast<ibValueMetaObjectModule*>(rec->GetObjectModule()))
			m->SetModuleText(objCode);
	}
	const wxString mgrCode = JStr(node, "managerModule");
	if (!mgrCode.IsEmpty()) {
		if (auto* m = const_cast<ibValueMetaObjectCommonModule*>(rec->GetManagerModule()))
			m->SetModuleText(mgrCode);
	}
}

// ---- Form control tree (MVP-B) -------------------------------------------
//
// A form's FormData blob is a binary-provider serialization of an ibDataNode
// control tree (frame.cpp ibValueFrame::Save/LoadNode). Every control node is:
//   fields   "ControlId" (s32) / "Name" (wxString) / "Expanded" (bool)
//   props    per-control (absent -> the property keeps its default on load;
//            SetNodeValue intercepts the empty value, so a minimal node is safe)
//   children sub-controls (recursion)
// The control TYPE is the node clsid = control_to_clsid("CT_XXX") — a pure
// backend hash, no frontend dependency. Data binding rides the "Source" property
// as an ordered metaId path: for an OBJECT form the head hop is the form's MAIN
// attribute (id 1, rebuilt from the source in the form ctor), then the object's
// attribute metaId (BuildForm: Source={mainAttrId,fieldId}); a table column adds
// the column's metaId ({mainAttrId,tsId,colId}). A non-empty FormData skips the
// auto-layout (CreateAndBuildForm), so this tree is authoritative.

// Control clsids — same strings the frontend registers (widgets.h / tableBox.h /
// notebook.h / boxsizer.cpp), computed the same way (control_to_clsid).
constexpr ibClassID kCtrlText     = control_to_clsid("CT_TXTC");  // Textctrl
constexpr ibClassID kCtrlCheckbox = control_to_clsid("CT_CHKB");  // Checkbox
constexpr ibClassID kCtrlStatic   = control_to_clsid("CT_STTX");  // Statictext
constexpr ibClassID kCtrlTable    = control_to_clsid("CT_TABL");  // Tablebox
constexpr ibClassID kCtrlColumn   = control_to_clsid("CT_TBLC");  // TableboxColumn
constexpr ibClassID kCtrlNotebook = control_to_clsid("CT_NTBK");  // Notebook
constexpr ibClassID kCtrlPage     = control_to_clsid("CT_NTPG");  // NotebookPage
constexpr ibClassID kCtrlBox      = control_to_clsid("CT_BSZR");  // Boxsizer
constexpr ibClassID kCtrlStaticBox = control_to_clsid("CT_SSZER"); // Staticboxsizer (titled/bordered group)
constexpr ibClassID kCtrlSizerItem = control_to_clsid("CT_SIZR"); // SizerItem — the layout wrapper
constexpr ibClassID kCtrlButton    = control_to_clsid("CT_BUTN"); // Button — delegates its click to a form command

// wxOrientation values (wx/defs.h, stable ABI) — used for the box sizer "Orient" enum
// (ibValueEnumOrient stores the raw wx constant). Hardcoded so this backend TU stays
// GUI-header-free (no wx/defs.h include in backend.dll).
constexpr int kOrientHorizontal = 4;   // wxHORIZONTAL
constexpr int kOrientVertical   = 8;   // wxVERTICAL

constexpr ibMetaID kFormMainAttrId = 1;   // object form's main attribute (ctor-assigned, first attribute)
constexpr int      kFormRootId     = 1;   // the form control's own id (frontend defaultFormId)
const     ibClassID kFormAttrClsid = system_to_clsid("FormAttributeValue"); // node clsid of a form attribute (formAttribute.cpp)
const     ibClassID kFormCommandClsid = system_to_clsid("FormCommandValue"); // node clsid of a form command (formCommand.cpp)
// Form-command ids live in a HIGH id space so a command hop id never collides with a source / attribute
// metaId — MUST match kFormCommandIdBase in frontend/visualView/ctrl/formCommand.h.
constexpr ibMetaID kFormCommandIdBase = 0x40000000;

// wxStretch flag for the SizerItem "Stretch" property (wx 3.3, stable ABI): the ctor default is
// wxSHRINK (0x1000) — right for a plain widget — and a container/sizer wants wxEXPAND (0x2000) to
// fill its cell. Mirrors SetDefaultLayoutProperties (frontend formFactory.cpp).
constexpr int kStretchExpand = 0x2000;

// Where a control sits decides whether it needs a SizerItem wrapper. A frame / sizer / notebook page
// lays its children out through the sizer, so each child is wrapped in a SizerItem (the LOAD path
// NewObject does NOT auto-insert it — only the interactive editor does). A notebook holds pages
// directly and a tablebox holds columns directly — no wrapper there.
enum class Host { Sizerable, Notebook, Table };

// The layout intent a SizerItem gets for the control kind it wraps (mirrors SetDefaultLayoutProperties):
// a plain widget keeps the SizerItem defaults; a sizer/group drops the border; a container expands.
enum class Layout { Widget, Sizer, Container };

// A tabular section's resolved ids: the section metaId + its columns' metaIds by name.
struct TabInfo {
	ibMetaID id = 0;
	std::map<wxString, ibMetaID> cols;
};

// Owner attribute / tabular-section name -> metaId maps, for resolving control bindings.
struct AttrMaps {
	std::map<wxString, ibMetaID> attrs;      // owner attribute name -> metaId (bound via the main object, 2-hop)
	std::map<wxString, TabInfo>  tabs;       // tabular-section name -> {id, cols}
	std::map<wxString, ibMetaID> formAttrs;  // FORM attribute name -> its own attribute id (bound directly, 1-hop)
	std::map<wxString, ibMetaID> commands;   // FORM command name -> its form-unique command id (button binds by 1-hop path)
};

void BuildAttrMaps(ibValueMetaObject* owner, AttrMaps& out) {
	// 1C standard attributes: a catalog's Code / Description are OES predefined attributes with their
	// own metaIds. Map the 1C DataPath names ("Code"/"Description") to them so those bound fields
	// resolve and render (otherwise Наименование / Код stay unbound and don't draw).
	if (auto* h = dynamic_cast<ibValueMetaObjectRecordDataHierarchyMutableRef*>(owner)) {
		if (auto* code = h->GetDataCode())        out.attrs[wxT("Code")]        = code->GetMetaID();
		if (auto* desc = h->GetDataDescription()) out.attrs[wxT("Description")] = desc->GetMetaID();
	}
	for (unsigned int i = 0; i < owner->GetChildCount(); i++) {
		ibValueMetaObject* child = owner->GetChild(i);
		if (child == nullptr)
			continue;
		if (auto* a = dynamic_cast<ibValueMetaObjectAttribute*>(child)) {
			out.attrs[a->GetName()] = a->GetMetaID();
		}
		else if (child->GetClassType() == g_metaTableRefCLSID) {
			TabInfo ti;
			ti.id = child->GetMetaID();
			for (unsigned int j = 0; j < child->GetChildCount(); j++)
				if (auto* col = dynamic_cast<ibValueMetaObjectAttribute*>(child->GetChild(j)))
					ti.cols[col->GetName()] = col->GetMetaID();
			out.tabs[child->GetName()] = ti;
		}
	}
}

// Encode a control Source binding (an ordered metaId hop path) as a node property value.
ibDataValue MakeSource(const std::vector<ibSourceId>& hops) {
	ibSourceDescription desc;
	for (ibSourceId id : hops)
		desc.AppendSource(id);
	ibDataValue value;
	ibSourceDescriptionMemory::WriteNode(value, desc);
	return value;
}

// Encode a button's command binding — a 1-hop path that binds STRAIGHT to a form command by its id
// (ibCommandDescription, the command-door parallel of a Source path). The button's click walks this
// path and runs the command's Action event (the imported form-module handler).
ibDataValue MakeCommandDesc(ibMetaID commandId) {
	ibCommandDescription desc(commandId);
	ibDataValue value;
	ibCommandDescriptionMemory::WriteNode(value, desc);
	return value;
}

// Create a SizerItem wrapper node under `parent` (the layout cell the sizer positions) and set its
// layout properties for the control it will hold. Returns the SizerItem node; the real control is
// added as ITS child. Widgets keep the ctor defaults (proportion 0, wxSHRINK, wxALL border, size 5).
ibDataNode& AddSizerItem(ibDataNode& parent, int& nextId, Layout layout) {
	const int id = nextId++;
	ibDataNode& si = parent.AddChild(kCtrlSizerItem, id);
	si.SetValue(wxT("ControlId"), (s32)id);
	si.SetValue(wxT("Name"), wxString());   // system control — the designer hides it
	si.SetValue(wxT("Expanded"), true);
	if (layout == Layout::Container) {          // notebook / tablebox — fill the cell
		si.SetProp<s32>(wxT("Proportion"), 1);
		si.SetProperty(wxT("Stretch"), ibDataValue::Int(kStretchExpand));
	}
	else if (layout == Layout::Sizer) {         // group / boxsizer — expand, no outer border
		si.SetProp<s32>(wxT("BorderSize"), 0);
		si.SetProperty(wxT("Stretch"), ibDataValue::Int(kStretchExpand));
	}
	return si;
}

// Set a control node's event handlers from the spec's "events" map (OES-event-name -> procedure
// name). Each becomes an ibEventControl property — serialised as a Child node { Name, Value } — so
// the loaded control fires that form-module procedure through CallAsEvent. The importer has already
// mapped 1C event ids to OES's own per-kind ids, so this sets exactly what it is handed.
void SetControlEvents(ibDataNode& node, const json& c) {
	auto ev = c.find("events");
	if (ev == c.end() || !ev->is_object())
		return;
	for (auto it = ev->begin(); it != ev->end(); ++it) {
		if (!it.value().is_string())
			continue;
		const wxString evName  = wxString::FromUTF8(it.key().c_str());
		const wxString handler = wxString::FromUTF8(it.value().get<std::string>().c_str());
		if (evName.IsEmpty() || handler.IsEmpty())
			continue;
		auto evNode = std::make_shared<ibDataNode>();
		evNode->SetValue(wxT("Name"),  evName);    // the event's binding name
		evNode->SetValue(wxT("Value"), handler);   // the form-module procedure to call
		node.SetProperty(evName, ibDataValue::Child(evNode));
	}
}

// Emit one control (and its children) under `parent`. `host` says how the parent lays children out:
// a Sizerable parent wraps each child in a SizerItem, a Notebook/Table parent adds pages/columns
// directly. `tableId` is the enclosing tablebox's section metaId (0 otherwise) so a column resolves
// its 3-hop source. `nextId` hands out form-unique control ids.
void BuildControlNode(ibDataNode& parent, const json& c, const AttrMaps& maps,
                      int& nextId, ibMetaID tableId, Host host) {
	const wxString kind = JStr(c, "kind", wxT("field")).Lower();
	const wxString name = JStr(c, "name");

	// GROUPS (1C UsualGroup / ColumnGroup) are now emitted as REAL nested boxes,
	// honouring the group's own layout properties:
	//   * "orient" (from 1C <Group>) → the box sizer orientation. A ColumnGroup and
	//     a Horizontal UsualGroup lay children side-by-side; the default is vertical.
	//   * a shown title (ShowTitle on + Title present) → a Staticboxsizer (a bordered,
	//     captioned group frame); a layout-only group → a plain Boxsizer.
	// Children are laid out INSIDE the box (Host::Sizerable), so field grouping and
	// side-by-side arrangement survive instead of collapsing into one flat column.
	//
	// NOTEBOOKS / PAGES are likewise REAL controls (a CT_NTBK holding CT_NTPG pages).
	if (kind == wxT("group") || kind == wxT("box")) {
		auto ch = c.find("children");
		const bool hasChildren = (ch != c.end() && ch->is_array() && !ch->empty());
		const wxString title = JStr(c, "title");
		// An empty group (e.g. a ButtonGroup whose buttons aren't imported) has
		// nothing to lay out — drop it rather than emit a stray empty box.
		if (!hasChildren)
			return;

		const bool titled = !title.IsEmpty();
		const int orient = (JStr(c, "orient").Lower() == wxT("horizontal"))
			? kOrientHorizontal : kOrientVertical;

		// The box rides a SizerItem cell in a Sizerable parent (expanded to fill it);
		// in the degenerate non-sizerable case it attaches directly.
		ibDataNode* boxParent = &parent;
		if (host == Host::Sizerable)
			boxParent = &AddSizerItem(parent, nextId, Layout::Sizer);
		const int id = nextId++;
		ibDataNode& box = boxParent->AddChild(titled ? kCtrlStaticBox : kCtrlBox, id);
		box.SetValue(wxT("ControlId"), (s32)id);
		box.SetValue(wxT("Name"), name);
		box.SetValue(wxT("Expanded"), true);
		box.SetProperty(wxT("Orient"), ibDataValue::Int(orient));
		if (titled)
			box.SetProp<wxString>(wxT("Title"), title);

		for (const json& sub : *ch)
			BuildControlNode(box, sub, maps, nextId, tableId, Host::Sizerable);
		return;
	}

	// An empty decoration (a 1C LabelDecoration with no caption — Отступ*, used only
	// for vertical spacing) has nothing to show. Skip it, or OES fills it with its
	// default "Static text" placeholder. A label WITH a title stays (it's a caption).
	if ((kind == wxT("label") || kind == wxT("statictext")) && JStr(c, "title").IsEmpty())
		return;

	ibClassID clsid = kCtrlText;
	Layout    layout = Layout::Widget;
	Host      childHost = Host::Sizerable;
	if      (kind == wxT("field"))                                { clsid = kCtrlText; }
	else if (kind == wxT("checkbox"))                            { clsid = kCtrlCheckbox; }
	else if (kind == wxT("button"))                             { clsid = kCtrlButton; }
	else if (kind == wxT("label") || kind == wxT("statictext")) { clsid = kCtrlStatic; }
	else if (kind == wxT("table"))                              { clsid = kCtrlTable;    layout = Layout::Container; childHost = Host::Table; }
	else if (kind == wxT("column"))                             { clsid = kCtrlColumn; }
	else if (kind == wxT("pages") || kind == wxT("notebook"))  { clsid = kCtrlNotebook; layout = Layout::Container; childHost = Host::Notebook; }
	else if (kind == wxT("page"))                               { clsid = kCtrlPage;     childHost = Host::Sizerable; }
	else if (kind == wxT("group") || kind == wxT("box"))       { clsid = kCtrlBox;      layout = Layout::Sizer;     childHost = Host::Sizerable; }

	// A Sizerable parent lays out through its sizer, so the control rides a SizerItem cell; a notebook
	// takes pages directly and a tablebox takes columns directly.
	ibDataNode* controlParent = &parent;
	if (host == Host::Sizerable)
		controlParent = &AddSizerItem(parent, nextId, layout);

	const int id = nextId++;
	ibDataNode& node = controlParent->AddChild(clsid, id);
	node.SetValue(wxT("ControlId"), (s32)id);
	node.SetValue(wxT("Name"), name);
	node.SetValue(wxT("Expanded"), true);

	// EVENT HANDLERS. "events" is a map of OES-event-name -> form-module procedure. Each becomes an
	// ibEventControl property (a Child node with Name + Value): serialised as the handler NAME, it is
	// materialised at run time into a named ibValueEvent and fired through ibValueFrame::CallAsEvent ->
	// CallAsProc(handler). So an imported OnChange / OnCheckboxClicked / Selection etc. actually runs
	// the form procedure the 1C form bound to that event. The names are already mapped to OES's own
	// event ids (per control kind) by the importer, so this just sets what it is handed.
	SetControlEvents(node, c);

	// Data binding. Field/checkbox/statictext bind to an object attribute; a
	// table binds to its tabular section; a column adds the leaf column hop.
	ibMetaID childTableId = tableId;
	if (kind == wxT("field") || kind == wxT("checkbox") || kind == wxT("label") || kind == wxT("statictext")) {
		const wxString attr = JStr(c, "attr");
		auto it = maps.attrs.find(attr);
		if (!attr.IsEmpty() && it != maps.attrs.end()) {
			// Owner attribute — reached THROUGH the main object (id 1) as its field.
			node.SetProperty(wxT("Source"), MakeSource({ (ibSourceId)kFormMainAttrId, (ibSourceId)it->second }));
		}
		else if (!attr.IsEmpty()) {
			// A FORM attribute (ПолеПрописи…, СуммаЧисло, …) is the value itself —
			// bind directly to its own id, a single hop. Without this the field has
			// no type and does not render.
			auto fit = maps.formAttrs.find(attr);
			if (fit != maps.formAttrs.end())
				node.SetProperty(wxT("Source"), MakeSource({ (ibSourceId)fit->second }));
		}
		const wxString title = JStr(c, "title");
		if (!title.IsEmpty())
			node.SetProp<wxString>(wxT("Title"), title);
	}
	else if (kind == wxT("table")) {
		const wxString attr = JStr(c, "attr");
		auto it = maps.tabs.find(attr);
		if (it != maps.tabs.end()) {
			node.SetProperty(wxT("Source"), MakeSource({ (ibSourceId)kFormMainAttrId, (ibSourceId)it->second.id }));
			childTableId = it->second.id;   // columns resolve against this section
		}
	}
	else if (kind == wxT("button")) {
		// A button carries NO data source — it delegates its click to a form command. Bind by the
		// command's name (1C ButtonName/CommandName) to the id EmitFormCommands assigned it; the
		// "Command" property is a 1-hop command path the click walks to run the command's Action.
		const wxString cmd = JStr(c, "command");
		auto cit = maps.commands.find(cmd);
		if (!cmd.IsEmpty() && cit != maps.commands.end())
			node.SetProperty(wxT("Command"), MakeCommandDesc(cit->second));
		const wxString title = JStr(c, "title");
		if (!title.IsEmpty())
			node.SetProp<wxString>(wxT("Title"), title);
	}
	else if (kind == wxT("page")) {
		// The NotebookPage's Title is its TAB caption (default "New page" otherwise).
		const wxString title = JStr(c, "title");
		if (!title.IsEmpty())
			node.SetProp<wxString>(wxT("Title"), title);
	}
	else if (kind == wxT("column")) {
		const wxString field = JStr(c, "field");
		const wxString title = JStr(c, "title");
		// Find the column's metaId within the enclosing table's section.
		ibMetaID colId = 0;
		for (const auto& t : maps.tabs)
			if (t.second.id == tableId) {
				auto cit = t.second.cols.find(field);
				if (cit != t.second.cols.end()) colId = cit->second;
				break;
			}
		if (colId != 0 && tableId != 0)
			node.SetProperty(wxT("Source"), MakeSource({ (ibSourceId)kFormMainAttrId, (ibSourceId)tableId, (ibSourceId)colId }));
		if (!title.IsEmpty())
			node.SetProp<wxString>(wxT("Title"), title);
	}

	// Recurse — children hang off the real control node (not the SizerItem wrapper).
	auto ch = c.find("children");
	if (ch != c.end() && ch->is_array())
		for (const json& sub : *ch)
			BuildControlNode(node, sub, maps, nextId, childTableId, childHost);
}

// Emit the form's MAIN attribute (the "Attributes" section, ibValueForm::WriteAttributes
// shape). An OBJECT form binds its controls through this attribute (Source head = its id 1):
// without it the controls cannot resolve their field types and DON'T RENDER in the form
// editor (only unbound labels show). Typed to the owning object (object_to_clsid), so a
// control's {mainAttr, attrId} path resolves attrId as a field of the object.
void EmitMainAttribute(ibDataNode& attrs, ibMetaID ownerMetaID, const ibMetaData* metaData) {
	ibDataNode& a = attrs.AddChild(kFormAttrClsid, kFormMainAttrId);   // one attribute, id 1
	a.SetValue(wxT("AttributeId"), (s32)kFormMainAttrId);
	a.SetValue(wxT("Main"), true);
	a.SetProp<wxString>(wxT("Name"), wxT("Object"));                   // ThisForm.Object (id is what bindings use)
	ibTypeDescription td;
	td.SetDefaultMetaType(object_to_clsid(ownerMetaID));              // the object type — its fields are the object's attributes
	ibDataValue typeVal;
	ibTypeDescriptionMemory::WriteNode(typeVal, td, metaData);
	a.SetProperty(wxT("Type"), typeVal);
}

// Emit the form's OWN attributes (1C form attributes: ПолеПрописи…, СуммаЧисло, …) into the
// Attributes section, each with an id AFTER the main attribute's (id 1). A field bound to a form
// attribute resolves its type from it and renders; without this such fields stay typeless / blank.
// Records name -> id in maps.formAttrs so BuildControlNode can bind fields to them (single hop).
void EmitFormAttributes(ibDataNode& attrs, const json& f, const RefMap& refMap,
                        const ibMetaData* metaData, int& nextAttrId, AttrMaps& maps) {
	auto it = f.find("formAttributes");
	if (it == f.end() || !it->is_array())
		return;
	for (const json& a : *it) {
		const wxString name = JStr(a, "name");
		if (name.IsEmpty())
			continue;
		const int id = nextAttrId++;
		ibDataNode& node = attrs.AddChild(kFormAttrClsid, id);
		node.SetValue(wxT("AttributeId"), (s32)id);
		node.SetValue(wxT("Main"), false);
		node.SetProp<wxString>(wxT("Name"), name);
		ibTypeDescription td;
		wxString err;
		ApplyType(td, a, refMap, name, err);              // String / Number / Boolean / Date (ref degrades to String)
		ibDataValue typeVal;
		ibTypeDescriptionMemory::WriteNode(typeVal, td, metaData);
		node.SetProperty(wxT("Type"), typeVal);
		maps.formAttrs[name] = (ibMetaID)id;
	}
}

// Emit the form's COMMANDS (1C form commands: each a named "button pressed" event the buttons delegate
// to). Each becomes an ibFormCommandValue node under the root's "FormCommands" collection, carrying its
// Name / Caption and — the point of it — its Action event (a Child node { Name:"Action", Value:handler })
// naming the form-module procedure the click runs. Records name -> form-unique id in maps.commands so a
// Button can bind to it by a 1-hop command path. Ids come from the HIGH kFormCommandIdBase space.
void EmitFormCommands(ibDataNode& root, const json& f, AttrMaps& maps) {
	auto it = f.find("commands");
	if (it == f.end() || !it->is_array() || it->empty())
		return;
	ibDataNode& cmds = root.Child(wxT("FormCommands"));
	ibMetaID nextCmdId = kFormCommandIdBase;
	for (const json& c : *it) {
		const wxString name = JStr(c, "name");
		if (name.IsEmpty())
			continue;
		const ibMetaID id = nextCmdId++;
		ibDataNode& node = cmds.AddChild(kFormCommandClsid, id);
		node.SetValue(wxT("CommandId"), (int)id);           // form-unique id (matches ibFormCommandValue::ReadProperty)
		node.SetProp<wxString>(wxT("Name"), name);
		const wxString caption = JStr(c, "caption");
		if (!caption.IsEmpty())
			node.SetProp<wxString>(wxT("Caption"), caption);
		// The Action event — same shape as a control event: a Child { Name, Value } naming the handler.
		const wxString handler = JStr(c, "action");
		if (!handler.IsEmpty()) {
			auto ev = std::make_shared<ibDataNode>();
			ev->SetValue(wxT("Name"),  wxString(wxT("Action")));
			ev->SetValue(wxT("Value"), handler);
			node.SetProperty(wxT("Action"), ibDataValue::Child(ev));
		}
		maps.commands[name] = id;
	}
}

// Build the whole FormData blob for a form node with a "controls" tree.
// Returns an empty buffer when there are no controls (caller leaves FormData
// empty -> auto-layout, the MVP-A behaviour).
wxMemoryBuffer BuildFormData(const json& f, const wxString& formName, const AttrMaps& ownerMaps,
                             ibMetaID ownerMetaID, const RefMap& refMap, const ibMetaData* metaData) {
	auto it = f.find("controls");
	if (it == f.end() || !it->is_array() || it->empty())
		return wxMemoryBuffer();

	auto root = std::make_shared<ibDataNode>();
	root->SetValue(wxT("ControlId"), (s32)kFormRootId);   // form's own control id (1)
	root->SetValue(wxT("Name"), formName);
	root->SetValue(wxT("Expanded"), true);

	// Form attributes are FORM-local, so work on a per-form copy of the owner maps.
	AttrMaps maps = ownerMaps;

	// One "Attributes" section (Child() creates a new node per call, so build it once
	// and share it between the main object attribute and the form's own attributes).
	ibDataNode& attrs = root->Child(wxT("Attributes"));
	EmitMainAttribute(attrs, ownerMetaID, metaData);      // the object the controls bind through (id 1)
	int nextAttrId = kFormMainAttrId + 1;                 // form attributes get ids after the main
	EmitFormAttributes(attrs, f, refMap, metaData, nextAttrId, maps);

	// Form commands (buttons delegate their click to these) — must precede the control walk so a
	// Button node can resolve its command name to the emitted command's id.
	EmitFormCommands(*root, f, maps);

	int nextId = kFormRootId + 1;   // children start after the form
	for (const json& c : *it)
		BuildControlNode(*root, c, maps, nextId, /*tableId*/ 0, Host::Sizerable);

	return ibValueMetaObjectFormBase::FormNodeToBlob(ibDataValue::Child(root));
}

// Create form child metaobjects under an owner (catalog/document/register).
// Form name + module (BSL already translated to VES upstream). When the form JSON
// carries a "controls" tree (MVP-B), replicate it into the FormData blob so the
// 1C layout is preserved; otherwise FormData stays empty and OES auto-builds the
// layout from the object's attributes on open (MVP-A). Form type has no public
// setter; it stays default.
bool AddForms(ibMetaDataConfigurationFile& cfg, ibValueMetaObject* owner,
              const json& node, const RefMap& refMap, wxString& err) {
	auto it = node.find("forms");
	if (it == node.end())
		return true;
	if (!it->is_array()) {
		err = wxT("'forms' must be an array");
		return false;
	}

	AttrMaps maps;
	BuildAttrMaps(owner, maps);   // resolve control bindings against the owner's attributes

	for (const json& f : *it) {
		const wxString name = JStr(f, "name");
		if (name.IsEmpty()) {
			err = wxT("form without a 'name'");
			return false;
		}
		ibValueMetaObject* obj = cfg.CreateMetaObject(g_metaFormCLSID, owner, /*runObject*/ false, name);
		if (obj == nullptr) {
			err = wxString::Format(wxT("failed to create form '%s'"), name);
			return false;
		}
		if (auto* form = dynamic_cast<ibValueMetaObjectForm*>(obj)) {
			const wxString code = JStr(f, "module");
			if (!code.IsEmpty())
				form->SetModuleText(code);
			const wxMemoryBuffer formData = BuildFormData(f, name, maps,
				owner->GetMetaID(), refMap, owner->GetMetaData());
			if (!formData.IsEmpty())
				form->SetFormData(formData);

			// Assign the imported form as the owner's DEFAULT form for its kind.
			// Without this the catalog/document has no default-form property set, so
			// GetObjectForm() synthesises a flat auto-form and the imported control
			// tree (its groups / layout) is never shown. 1C form type -> OES FormType
			// id + the owner default-form property (ids match across record metatypes:
			// eFormObject=1, eFormList=2; catalog-only eFormSelect=3 / eFormFolder=4).
			const wxString ftype = JStr(f, "type", wxT("object")).Lower();
			int      formTypeId = 0;
			wxString defProp;
			if      (ftype == wxT("object")) { formTypeId = 1; defProp = wxT("DefaultFormObject"); }
			else if (ftype == wxT("list"))   { formTypeId = 2; defProp = wxT("DefaultFormList");   }
			else if (ftype == wxT("select")) { formTypeId = 3; defProp = wxT("DefaultFormSelect");  }
			else if (ftype == wxT("folder")) { formTypeId = 4; defProp = wxT("DefaultFormFolder");  }

			if (formTypeId != 0) {
				// Tag the form with its OES type first — the default-form list validates
				// candidates by GetTypeForm(), so this must precede the assignment.
				if (ibProperty* ftp = form->GetProperty(wxT("FormType")))
					ftp->SetValue(wxVariant((long)formTypeId));
				// First imported form of each kind wins (leave a user-set default alone).
				if (ibProperty* dfp = owner->GetProperty(defProp))
					if (dfp->IsEmptyProperty())
						dfp->SetValue(wxVariant((long)obj->GetMetaID()));
			}
		}
	}
	return true;
}

// ---- Pass 2 fillers -------------------------------------------------------

// Catalog / Document: attributes, tabular sections, object/manager modules.
bool FillRecordObject(ibMetaDataConfigurationFile& cfg, ibValueMetaObject* obj,
                      const json& node, const RefMap& refMap, wxString& err) {
	if (!AddTypedChildren(cfg, obj, node, "attributes", g_metaAttributeCLSID, refMap, err))
		return false;

	auto ts = node.find("tabularSections");
	if (ts != node.end()) {
		if (!ts->is_array()) { err = wxT("'tabularSections' must be an array"); return false; }
		for (const json& t : *ts) {
			const wxString tname = JStr(t, "name");
			if (tname.IsEmpty()) { err = wxT("tabularSection without a 'name'"); return false; }
			ibValueMetaObject* tab = cfg.CreateMetaObject(g_metaTableRefCLSID, obj, /*runObject*/ false, tname);
			if (tab == nullptr) {
				err = wxString::Format(wxT("failed to create tabular section '%s'"), tname);
				return false;
			}
			if (!AddTypedChildren(cfg, tab, t, "attributes", g_metaAttributeCLSID, refMap, err))
				return false;
		}
	}
	ApplyObjectModules(obj, node);
	if (!AddForms(cfg, obj, node, refMap, err))
		return false;
	return true;
}

// Chart of characteristic types: a record object (attributes + tabular sections + modules + forms)
// PLUS the value type its characteristics may hold ("valueType") — the contour an accounting register's
// dimension slot and a characteristic value are typed by. Absent -> the metatype's default (empty type).
bool FillChartOfCharacteristicTypes(ibMetaDataConfigurationFile& cfg, ibValueMetaObject* obj,
                                    const json& node, const RefMap& refMap, wxString& err) {
	if (!FillRecordObject(cfg, obj, node, refMap, err))
		return false;
	auto* cct = dynamic_cast<ibValueMetaObjectChartOfCharacteristicTypes*>(obj);
	if (cct != nullptr) {
		auto vt = node.find("valueType");
		if (vt != node.end() && vt->is_object())
			if (!ApplyType(cct->GetTypesOfCharacteristics(), *vt, refMap, cct->GetName(), err))
				return false;
	}
	return true;
}

// Chart of accounts: a record object PLUS the mandatory binding to a chart of characteristic types
// ("chartOfCharacteristicTypes": a "ChartOfCharacteristicTypes.<Name>" ref key). OES refuses to save a
// chart of accounts without it — the account's analytics-kind columns are ELEMENTS of that chart, so
// with no chart the columns have no type. When the binding target is not in this import slice the bind
// is skipped and the save will report the requirement (structural import, no hang).
bool FillChartOfAccounts(ibMetaDataConfigurationFile& cfg, ibValueMetaObject* obj,
                         const json& node, const RefMap& refMap, wxString& err) {
	if (!FillRecordObject(cfg, obj, node, refMap, err))
		return false;
	auto* coa = dynamic_cast<ibValueMetaObjectChartOfAccounts*>(obj);
	if (coa != nullptr) {
		const wxString cctKey = JStr(node, "chartOfCharacteristicTypes");
		if (!cctKey.IsEmpty()) {
			auto found = refMap.find(cctKey);
			if (found != refMap.end() && found->second != nullptr) {
				ibMetaDescription md;
				md.AppendMetaType(found->second->GetMetaID());
				coa->GetChartOfCharacteristicTypes()->SetValue(md);
			}
		}
	}
	return true;
}

// Register: dimensions, resources, attributes, object/manager modules.
bool FillRegister(ibMetaDataConfigurationFile& cfg, ibValueMetaObject* obj,
                  const json& node, const RefMap& refMap, wxString& err) {
	if (!AddTypedChildren(cfg, obj, node, "dimensions", g_metaDimensionCLSID, refMap, err)) return false;
	if (!AddTypedChildren(cfg, obj, node, "resources",  g_metaResourceCLSID,  refMap, err)) return false;
	if (!AddTypedChildren(cfg, obj, node, "attributes", g_metaAttributeCLSID, refMap, err)) return false;
	ApplyObjectModules(obj, node);
	if (!AddForms(cfg, obj, node, refMap, err))
		return false;
	return true;
}

// ---- Pass 1 helpers (create + register in refMap) -------------------------

// Create top-level objects of one kind, register each in refMap under
// "<refPrefix>.<name>", and collect (object, node) for the second pass.
bool CreateObjects(ibMetaDataConfigurationFile& cfg, ibValueMetaObject* root,
                   const json& spec, const char* key, const ibClassID& clsid,
                   const wxString& refPrefix, RefMap& refMap,
                   std::vector<std::pair<ibValueMetaObject*, const json*>>& out,
                   wxString& err) {
	auto it = spec.find(key);
	if (it == spec.end())
		return true;
	if (!it->is_array()) {
		err = wxString::Format(wxT("'%s' must be an array"), wxString::FromUTF8(key));
		return false;
	}
	for (const json& node : *it) {
		const wxString name = JStr(node, "name");
		if (name.IsEmpty()) {
			err = wxString::Format(wxT("%s entry without a 'name'"), wxString::FromUTF8(key));
			return false;
		}
		ibValueMetaObject* obj = cfg.CreateMetaObject(clsid, root, /*runObject*/ false, name);
		if (obj == nullptr) {
			err = wxString::Format(wxT("failed to create %s '%s'"), wxString::FromUTF8(key), name);
			return false;
		}
		if (!refPrefix.IsEmpty())
			refMap[refPrefix + wxT(".") + name] = obj;
		out.emplace_back(obj, &node);
	}
	return true;
}

// Subsystems (Sections) are HIERARCHICAL and metadata-only — no DB tables, not reference targets.
// Create each under its parent and recurse into nested "subsystems", so the configuration's
// command-interface tree comes across with its shape. Content (the objects a subsystem groups) and
// rights are a later iteration; the tree itself is what this brings.
bool CreateSubsystems(ibMetaDataConfigurationFile& cfg, ibValueMetaObject* parent,
                      const json& arr, wxString& err) {
	if (!arr.is_array()) { err = wxT("'subsystems' must be an array"); return false; }
	for (const json& node : arr) {
		const wxString name = JStr(node, "name");
		if (name.IsEmpty()) { err = wxT("subsystem without a 'name'"); return false; }
		ibValueMetaObject* sec = cfg.CreateMetaObject(g_metaSectionCLSID, parent, /*runObject*/ false, name);
		if (sec == nullptr) {
			err = wxString::Format(wxT("failed to create subsystem '%s'"), name);
			return false;
		}
		auto sub = node.find("subsystems");
		if (sub != node.end() && sub->is_array())
			if (!CreateSubsystems(cfg, sec, *sub, err)) return false;
	}
	return true;
}

} // namespace

bool ibBuildConfigFromJsonSpec(const wxString& jsonText,
                               ibMetaDataConfigurationFile& cfg,
                               wxString& err) {
	json spec;
	try {
		spec = json::parse(jsonText.utf8_str().data());
	} catch (const json::exception& ex) {
		err = wxString::Format(wxT("JSON parse error: %s"), wxString::FromUTF8(ex.what()));
		return false;
	}
	if (!spec.is_object()) {
		err = wxT("top-level JSON must be an object");
		return false;
	}

	ibValueMetaObjectConfiguration* root = cfg.GetCommonMetaObject();
	if (root == nullptr) {
		err = wxT("no root configuration object");
		return false;
	}

	const wxString cfgName = JStr(spec, "name");
	if (!cfgName.IsEmpty())
		root->SetName(cfgName);

	// OES: optional configuration syntax — "ves" (Visual-Basic-style: Если … Тогда … КонецЕсли)
	// or "ces" (C-style). Russian 1C-style business logic is VES. Absent -> platform default (CES).
	const wxString syntax = JStr(spec, "syntax");
	if (syntax.CmpNoCase(wxT("ves")) == 0)
		root->SetCompileSyntax(syntax_ves);
	else if (syntax.CmpNoCase(wxT("ces")) == 0)
		root->SetCompileSyntax(syntax_ces);

	RefMap refMap;
	std::vector<std::pair<ibValueMetaObject*, const json*>> records;  // catalogs + documents
	std::vector<std::pair<ibValueMetaObject*, const json*>> infoRegs;
	std::vector<std::pair<ibValueMetaObject*, const json*>> accumRegs;
	std::vector<std::pair<ibValueMetaObject*, const json*>> constants;
	std::vector<std::pair<ibValueMetaObject*, const json*>> chartsCCT;   // charts of characteristic types
	std::vector<std::pair<ibValueMetaObject*, const json*>> chartsCOA;   // charts of accounts
	std::vector<std::pair<ibValueMetaObject*, const json*>> chartsCLT;   // charts of calculation types
	std::vector<std::pair<ibValueMetaObject*, const json*>> calcRegs;   // calculation registers

	// ---- Pass 1: create all objects (so references resolve) ----
	if (!CreateObjects(cfg, root, spec, "catalogs",  g_metaCatalogCLSID,  wxT("Catalog"),  refMap, records, err)) return false;
	if (!CreateObjects(cfg, root, spec, "documents", g_metaDocumentCLSID, wxT("Document"), refMap, records, err)) return false;
	if (!CreateObjects(cfg, root, spec, "informationRegisters",  g_metaInformationRegisterCLSID,  wxEmptyString, refMap, infoRegs,  err)) return false;
	if (!CreateObjects(cfg, root, spec, "accumulationRegisters", g_metaAccumulationRegisterCLSID, wxEmptyString, refMap, accumRegs, err)) return false;
	if (!CreateObjects(cfg, root, spec, "constants", g_metaConstantCLSID, wxEmptyString, refMap, constants, err)) return false;
	// Charts are valid reference targets: register each under its KIND name so a *Ссылка -> *Ref
	// dimension/attribute in any object resolves in the second pass.
	if (!CreateObjects(cfg, root, spec, "chartsOfCharacteristicTypes", g_metaChartOfCharacteristicTypesCLSID, wxT("ChartOfCharacteristicTypes"), refMap, chartsCCT, err)) return false;
	if (!CreateObjects(cfg, root, spec, "chartsOfAccounts",            g_metaChartOfAccountsCLSID,            wxT("ChartOfAccounts"),            refMap, chartsCOA, err)) return false;
	if (!CreateObjects(cfg, root, spec, "chartsOfCalculationTypes",    g_metaChartOfCalculationTypesCLSID,    wxT("ChartOfCalculationTypes"),    refMap, chartsCLT, err)) return false;
	if (!CreateObjects(cfg, root, spec, "calculationRegisters",        g_metaCalculationRegisterCLSID,        wxEmptyString,                     refMap, calcRegs,  err)) return false;

	// Enumerations + their values (values are child metaobjects). Enum is a valid
	// reference target, so register it in refMap.
	{
		auto it = spec.find("enums");
		if (it != spec.end()) {
			if (!it->is_array()) { err = wxT("'enums' must be an array"); return false; }
			for (const json& e : *it) {
				const wxString ename = JStr(e, "name");
				if (ename.IsEmpty()) { err = wxT("enum entry without a 'name'"); return false; }
				ibValueMetaObject* en = cfg.CreateMetaObject(g_metaEnumerationCLSID, root, /*runObject*/ false, ename);
				if (en == nullptr) { err = wxString::Format(wxT("failed to create enum '%s'"), ename); return false; }
				refMap[wxT("Enum.") + ename] = en;
				auto vals = e.find("values");
				if (vals != e.end() && vals->is_array()) {
					for (const json& v : *vals) {
						const wxString vname = v.is_string()
							? wxString::FromUTF8(v.get<std::string>().c_str())
							: JStr(v, "name");
						if (vname.IsEmpty()) continue;
						if (cfg.CreateMetaObject(g_metaEnumCLSID, en, /*runObject*/ false, vname) == nullptr) {
							err = wxString::Format(wxT("failed to create enum value '%s.%s'"), ename, vname);
							return false;
						}
					}
				}
			}
		}
	}

	// Roles — metadata-only, structural. An empty named role (rights are a later iteration): the role
	// exists in the tree and can be granted, but carries no permissions yet.
	{
		auto it = spec.find("roles");
		if (it != spec.end()) {
			if (!it->is_array()) { err = wxT("'roles' must be an array"); return false; }
			for (const json& node : *it) {
				const wxString name = JStr(node, "name");
				if (name.IsEmpty()) { err = wxT("role entry without a 'name'"); return false; }
				if (cfg.CreateMetaObject(g_metaRoleCLSID, root, /*runObject*/ false, name) == nullptr) {
					err = wxString::Format(wxT("failed to create role '%s'"), name); return false;
				}
			}
		}
	}

	// Subsystems — hierarchical (Sections), metadata-only.
	{
		auto it = spec.find("subsystems");
		if (it != spec.end())
			if (!CreateSubsystems(cfg, root, *it, err)) return false;
	}

	// Common modules (no references; set code now).
	{
		auto it = spec.find("commonModules");
		if (it != spec.end()) {
			if (!it->is_array()) { err = wxT("'commonModules' must be an array"); return false; }
			for (const json& node : *it) {
				const wxString name = JStr(node, "name");
				if (name.IsEmpty()) { err = wxT("commonModule entry without a 'name'"); return false; }
				ibValueMetaObject* obj = cfg.CreateMetaObject(g_metaCommonModuleCLSID, root, /*runObject*/ false, name);
				if (obj == nullptr) { err = wxString::Format(wxT("failed to create common module '%s'"), name); return false; }
				auto* mod = dynamic_cast<ibValueMetaObjectCommonModule*>(obj);
				if (mod == nullptr) { err = wxString::Format(wxT("'%s' is not a common module"), name); return false; }
				mod->SetModuleText(JStr(node, "code"));
				// OES-IMPORT: honour the 1C CommonModule visibility context. A global module
				// merges into the global namespace (unqualified calls) and compiles eagerly at
				// base open; a plain one is bound by name (qualified Name.Method()). The importer
				// only marks server-visible modules global.
				{
					auto gi = node.find("global");
					if (gi != node.end() && gi->is_boolean())
						mod->SetGlobalModule(gi->get<bool>());
				}
			}
		}
	}

	// ---- Pass 2: fill attributes / types / tabular sections / modules ----
	for (auto& rec : records)
		if (!FillRecordObject(cfg, rec.first, *rec.second, refMap, err)) return false;
	for (auto& r : infoRegs)
		if (!FillRegister(cfg, r.first, *r.second, refMap, err)) return false;
	for (auto& r : accumRegs)
		if (!FillRegister(cfg, r.first, *r.second, refMap, err)) return false;
	for (auto& c : chartsCCT)
		if (!FillChartOfCharacteristicTypes(cfg, c.first, *c.second, refMap, err)) return false;
	for (auto& c : chartsCOA)
		if (!FillChartOfAccounts(cfg, c.first, *c.second, refMap, err)) return false;
	for (auto& c : chartsCLT)   // charts of calculation types: plain reference hierarchy
		if (!FillRecordObject(cfg, c.first, *c.second, refMap, err)) return false;
	for (auto& r : calcRegs) {   // calculation registers: dimensions/resources/attributes (recorder-based)
		if (!FillRegister(cfg, r.first, *r.second, refMap, err)) return false;
		// Action-period semantics: the 1C register's ActionPeriod flag turns the record from a point
		// event into an interval [start, end] and adds the action/registration-period standard columns.
		if (auto* cr = dynamic_cast<ibValueMetaObjectCalculationRegister*>(r.first)) {
			auto ap = r.second->find("useActionPeriod");
			if (ap != r.second->end() && ap->is_boolean())
				cr->SetUseActionPeriod(ap->get<bool>());
		}
	}
	for (auto& c : constants) {
		auto* konst = dynamic_cast<ibValueMetaObjectConstant*>(c.first);
		if (konst == nullptr) { err = wxT("constant object is not a constant"); return false; }
		if (!ApplyType(konst->GetTypeDesc(), *c.second, refMap, konst->GetName(), err)) return false;
	}

	return true;
}

bool ibBuildConfigFileFromJsonSpec(const wxString& jsonPath,
                                   const wxString& mcfPath,
                                   wxString& err) {
	if (!wxFile::Exists(jsonPath)) {
		err = wxString::Format(wxT("spec file not found: %s"), jsonPath);
		return false;
	}
	wxFile in(jsonPath, wxFile::read);
	if (!in.IsOpened()) {
		err = wxString::Format(wxT("cannot open spec file: %s"), jsonPath);
		return false;
	}
	wxString jsonText;
	if (!in.ReadAll(&jsonText, wxConvUTF8)) {
		err = wxString::Format(wxT("cannot read spec file: %s"), jsonPath);
		return false;
	}

	ibMetaDataConfigurationFile cfg;
	if (!ibBuildConfigFromJsonSpec(jsonText, cfg, err))
		return false;

	if (!cfg.SaveConfigToFile(mcfPath)) {
		err = wxString::Format(wxT("failed to save configuration: %s"), mcfPath);
		return false;
	}
	return true;
}
