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
#include "backend/metaCollection/metaFormObject.h"        // ibValueMetaObjectForm
#include "backend/serialize/dataBuilder.h"                 // ibDataNode / ibDataValue (form control tree)
#include "backend/sourceDescription.h"                     // ibSourceDescription / ibSourceDescriptionMemory (control Source binding)

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
			// Unresolved / out-of-MVP target — degrade to String so the import
			// stays structural instead of failing.
			td.SetDefaultMetaType(ibValueTypes::TYPE_STRING);
			td.m_typeData.SetString(0);
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

constexpr ibMetaID kFormMainAttrId = 1;   // object form's main attribute (ctor-assigned, first attribute)
constexpr int      kFormRootId     = 1;   // the form control's own id (frontend defaultFormId)

// A tabular section's resolved ids: the section metaId + its columns' metaIds by name.
struct TabInfo {
	ibMetaID id = 0;
	std::map<wxString, ibMetaID> cols;
};

// Owner attribute / tabular-section name -> metaId maps, for resolving control bindings.
struct AttrMaps {
	std::map<wxString, ibMetaID> attrs;      // direct attribute name -> metaId
	std::map<wxString, TabInfo>  tabs;       // tabular-section name -> {id, cols}
};

void BuildAttrMaps(ibValueMetaObject* owner, AttrMaps& out) {
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

// Emit one control (and its children) as a child node under `parent`. `tableId`
// is the enclosing tablebox's tabular-section metaId (0 outside a table) so a
// column resolves its 3-hop source. `nextId` hands out form-unique control ids.
void BuildControlNode(ibDataNode& parent, const json& c, const AttrMaps& maps,
                      int& nextId, ibMetaID tableId) {
	const wxString kind = JStr(c, "kind", wxT("field")).Lower();
	const wxString name = JStr(c, "name");

	ibClassID clsid = kCtrlText;
	if      (kind == wxT("field"))                                clsid = kCtrlText;
	else if (kind == wxT("checkbox"))                            clsid = kCtrlCheckbox;
	else if (kind == wxT("label") || kind == wxT("statictext")) clsid = kCtrlStatic;
	else if (kind == wxT("table"))                              clsid = kCtrlTable;
	else if (kind == wxT("column"))                             clsid = kCtrlColumn;
	else if (kind == wxT("pages") || kind == wxT("notebook"))  clsid = kCtrlNotebook;
	else if (kind == wxT("page"))                               clsid = kCtrlPage;
	else if (kind == wxT("group") || kind == wxT("box"))       clsid = kCtrlBox;

	const int id = nextId++;
	ibDataNode& node = parent.AddChild(clsid, id);
	node.SetValue(wxT("ControlId"), (s32)id);
	node.SetValue(wxT("Name"), name);
	node.SetValue(wxT("Expanded"), true);

	// Data binding. Field/checkbox/statictext bind to an object attribute; a
	// table binds to its tabular section; a column adds the leaf column hop.
	ibMetaID childTableId = tableId;
	if (kind == wxT("field") || kind == wxT("checkbox") || kind == wxT("label") || kind == wxT("statictext")) {
		const wxString attr = JStr(c, "attr");
		auto it = maps.attrs.find(attr);
		if (!attr.IsEmpty() && it != maps.attrs.end())
			node.SetProperty(wxT("Source"), MakeSource({ (ibSourceId)kFormMainAttrId, (ibSourceId)it->second }));
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

	// Recurse.
	auto ch = c.find("children");
	if (ch != c.end() && ch->is_array())
		for (const json& sub : *ch)
			BuildControlNode(node, sub, maps, nextId, childTableId);
}

// Build the whole FormData blob for a form node with a "controls" tree.
// Returns an empty buffer when there are no controls (caller leaves FormData
// empty -> auto-layout, the MVP-A behaviour).
wxMemoryBuffer BuildFormData(const json& f, const wxString& formName, const AttrMaps& maps) {
	auto it = f.find("controls");
	if (it == f.end() || !it->is_array() || it->empty())
		return wxMemoryBuffer();

	auto root = std::make_shared<ibDataNode>();
	root->SetValue(wxT("ControlId"), (s32)kFormRootId);   // form's own control id (1)
	root->SetValue(wxT("Name"), formName);
	root->SetValue(wxT("Expanded"), true);

	int nextId = kFormRootId + 1;   // children start after the form
	for (const json& c : *it)
		BuildControlNode(*root, c, maps, nextId, /*tableId*/ 0);

	return ibValueMetaObjectFormBase::FormNodeToBlob(ibDataValue::Child(root));
}

// Create form child metaobjects under an owner (catalog/document/register).
// Form name + module (BSL already translated to VES upstream). When the form JSON
// carries a "controls" tree (MVP-B), replicate it into the FormData blob so the
// 1C layout is preserved; otherwise FormData stays empty and OES auto-builds the
// layout from the object's attributes on open (MVP-A). Form type has no public
// setter; it stays default.
bool AddForms(ibMetaDataConfigurationFile& cfg, ibValueMetaObject* owner,
              const json& node, wxString& err) {
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
			const wxMemoryBuffer formData = BuildFormData(f, name, maps);
			if (!formData.IsEmpty())
				form->SetFormData(formData);
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
	if (!AddForms(cfg, obj, node, err))
		return false;
	return true;
}

// Register: dimensions, resources, attributes, object/manager modules.
bool FillRegister(ibMetaDataConfigurationFile& cfg, ibValueMetaObject* obj,
                  const json& node, const RefMap& refMap, wxString& err) {
	if (!AddTypedChildren(cfg, obj, node, "dimensions", g_metaDimensionCLSID, refMap, err)) return false;
	if (!AddTypedChildren(cfg, obj, node, "resources",  g_metaResourceCLSID,  refMap, err)) return false;
	if (!AddTypedChildren(cfg, obj, node, "attributes", g_metaAttributeCLSID, refMap, err)) return false;
	ApplyObjectModules(obj, node);
	if (!AddForms(cfg, obj, node, err))
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

	RefMap refMap;
	std::vector<std::pair<ibValueMetaObject*, const json*>> records;  // catalogs + documents
	std::vector<std::pair<ibValueMetaObject*, const json*>> infoRegs;
	std::vector<std::pair<ibValueMetaObject*, const json*>> accumRegs;
	std::vector<std::pair<ibValueMetaObject*, const json*>> constants;

	// ---- Pass 1: create all objects (so references resolve) ----
	if (!CreateObjects(cfg, root, spec, "catalogs",  g_metaCatalogCLSID,  wxT("Catalog"),  refMap, records, err)) return false;
	if (!CreateObjects(cfg, root, spec, "documents", g_metaDocumentCLSID, wxT("Document"), refMap, records, err)) return false;
	if (!CreateObjects(cfg, root, spec, "informationRegisters",  g_metaInformationRegisterCLSID,  wxEmptyString, refMap, infoRegs,  err)) return false;
	if (!CreateObjects(cfg, root, spec, "accumulationRegisters", g_metaAccumulationRegisterCLSID, wxEmptyString, refMap, accumRegs, err)) return false;
	if (!CreateObjects(cfg, root, spec, "constants", g_metaConstantCLSID, wxEmptyString, refMap, constants, err)) return false;

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
