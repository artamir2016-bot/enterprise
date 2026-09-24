#include "managedFormCompiler.h"

#include "backend/clsid.h"                     // control_to_clsid
#include "backend/serialize/dataBuilder.h"     // ibDataNode / ibDataValue
#include "backend/sourceDescription.h"         // ibSourceDescription(Memory)

namespace {

// Control clsids — the SAME strings the frontend registers and metadataConfigSpec
// emits (widgets.h / tableBox.h / notebook.h / boxsizer.cpp), computed the same way.
const ibClassID kCtrlText      = control_to_clsid("CT_TXTC");   // Textctrl
const ibClassID kCtrlStatic    = control_to_clsid("CT_STTX");   // Statictext
const ibClassID kCtrlCheckbox  = control_to_clsid("CT_CHKB");   // Checkbox
const ibClassID kCtrlRadio     = control_to_clsid("CT_RDBT");   // Radiobutton
const ibClassID kCtrlChoice    = control_to_clsid("CT_CHOI");   // Choice
const ibClassID kCtrlCombo     = control_to_clsid("CT_CMBB");   // Combobox
const ibClassID kCtrlTable     = control_to_clsid("CT_TABL");   // Tablebox
const ibClassID kCtrlColumn    = control_to_clsid("CT_TBLC");   // TableboxColumn
const ibClassID kCtrlButton    = control_to_clsid("CT_BUTN");   // Button
const ibClassID kCtrlBox       = control_to_clsid("CT_BSZR");   // Boxsizer
const ibClassID kCtrlStaticBox = control_to_clsid("CT_SSZER");  // Staticboxsizer
const ibClassID kCtrlSizerItem = control_to_clsid("CT_SIZR");   // SizerItem
const ibClassID kCtrlNotebook  = control_to_clsid("CT_NTBK");   // Notebook
const ibClassID kCtrlPage      = control_to_clsid("CT_NTPG");   // NotebookPage
const ibClassID kCtrlPicture   = control_to_clsid("CT_PICT");   // Picture

// wxOrientation raw values (wx/defs.h, stable ABI) — this backend TU stays
// GUI-header-free, so they are hardcoded (mirrors metadataConfigSpec.cpp).
const int kOrientHorizontal = 4;   // wxHORIZONTAL
const int kOrientVertical   = 8;   // wxVERTICAL
const int kStretchExpand    = 0x2000;  // wxEXPAND

ibDataValue MakeSource(const std::vector<ibSourceId>& hops) {
	ibSourceDescription desc;
	for (ibSourceId id : hops)
		desc.AppendSource(id);
	ibDataValue value;
	ibSourceDescriptionMemory::WriteNode(value, desc);
	return value;
}

} // namespace

ibClassID ibManagedFormCompiler::ClsidForViewKind(ibFieldViewKind viewKind) {
	switch (viewKind) {
	case ibFieldViewKind::CheckBoxField: return kCtrlCheckbox;
	case ibFieldViewKind::RadioField:    return kCtrlRadio;
	case ibFieldViewKind::LabelField:    return kCtrlStatic;
	case ibFieldViewKind::ChoiceField:   return kCtrlChoice;
	case ibFieldViewKind::ComboBoxField: return kCtrlCombo;
	case ibFieldViewKind::PictureField:  return kCtrlPicture; // the Picture control (CT_PICT)
	case ibFieldViewKind::ReferenceField: return kCtrlText;  // text box + a select button (set in EmitField)
	case ibFieldViewKind::InputField:    return kCtrlText;
	case ibFieldViewKind::Auto:
	default:                             return kCtrlText;    // the type-driven Auto rule needs live metadata (runtime-open increment)
	}
}

ibDataNode& ibManagedFormCompiler::AddSizerItem(ibDataNode& parent, bool expand) {
	const int id = m_nextId++;
	ibDataNode& si = parent.AddChild(kCtrlSizerItem, id);
	si.SetValue(wxT("ControlId"), (s32)id);
	si.SetValue(wxT("Name"), wxString());   // system control — the designer hides it
	si.SetValue(wxT("Expanded"), true);
	if (expand) {
		si.SetProp<s32>(wxT("BorderSize"), 0);
		si.SetProperty(wxT("Stretch"), ibDataValue::Int(kStretchExpand));
	}
	return si;
}

int ibManagedFormCompiler::Compile(ibDataNode& formRoot, const ibManagedElement& root) {
	m_nextId = 2;
	m_count  = 0;
	// The root is a Group by convention — its children are the form's top-level
	// controls, laid out through the form's own (vertical) box.
	for (const ibManagedElement& child : root.children)
		EmitElement(formRoot, child, /*sizerable*/ true, /*tableId*/ wxNOT_FOUND);
	return m_count;
}

void ibManagedFormCompiler::EmitElement(ibDataNode& parent, const ibManagedElement& el,
	bool sizerable, ibMetaID tableId) {
	switch (el.kind) {
	case ibManagedNodeKind::Group:
	case ibManagedNodeKind::Page:
		EmitGroup(parent, el, sizerable);
		break;
	case ibManagedNodeKind::Field:
		EmitField(parent, el, sizerable, tableId);
		break;
	case ibManagedNodeKind::Column: {
		// A table column is a CT_TBLC node attached DIRECTLY to the tablebox
		// (never wrapped in a SizerItem). Its Source is the column's own hop path.
		const int id = m_nextId++;
		ibDataNode& col = parent.AddChild(control_to_clsid("CT_TBLC"), id);
		col.SetValue(wxT("ControlId"), (s32)id);
		col.SetValue(wxT("Name"), el.name);
		if (!el.title.IsEmpty())
			col.SetProp<wxString>(wxT("Title"), el.title);
		const std::vector<ibSourceId> hops = m_resolve ? m_resolve(el.dataPath) : std::vector<ibSourceId>();
		if (!hops.empty())
			col.SetProperty(wxT("Source"), MakeSource(hops));
		m_count++;
		break;
	}
	case ibManagedNodeKind::Decoration: {
		ibDataNode* host = &parent;
		if (sizerable) host = &AddSizerItem(parent, /*expand*/ false);
		const int id = m_nextId++;
		ibDataNode& node = host->AddChild(kCtrlStatic, id);
		node.SetValue(wxT("ControlId"), (s32)id);
		node.SetValue(wxT("Name"), el.name);
		node.SetProp<wxString>(wxT("Title"), el.title);
		m_count++;
		break;
	}
	case ibManagedNodeKind::Button: {
		ibDataNode* host = &parent;
		if (sizerable) host = &AddSizerItem(parent, /*expand*/ false);
		const int id = m_nextId++;
		ibDataNode& node = host->AddChild(kCtrlButton, id);
		node.SetValue(wxT("ControlId"), (s32)id);
		node.SetValue(wxT("Name"), el.name);
		node.SetProp<wxString>(wxT("Title"), el.title);
		m_count++;
		break;
	}
	case ibManagedNodeKind::Table: {
		ibDataNode* host = &parent;
		if (sizerable) host = &AddSizerItem(parent, /*expand*/ true);
		const int id = m_nextId++;
		ibDataNode& node = host->AddChild(kCtrlTable, id);
		node.SetValue(wxT("ControlId"), (s32)id);
		node.SetValue(wxT("Name"), el.name);
		node.SetProp<wxString>(wxT("Title"), el.title);
		const std::vector<ibSourceId> hops = m_resolve ? m_resolve(el.dataPath) : std::vector<ibSourceId>();
		const ibMetaID sectionId = hops.empty() ? wxNOT_FOUND : hops.back();
		if (!hops.empty())
			node.SetProperty(wxT("Source"), MakeSource(hops));
		// Columns attach DIRECTLY to the tablebox (not sizerable).
		for (const ibManagedElement& col : el.children)
			EmitElement(node, col, /*sizerable*/ false, sectionId);
		m_count++;
		break;
	}
	}
}

void ibManagedFormCompiler::EmitGroup(ibDataNode& parent, const ibManagedElement& el, bool sizerable) {
	// An empty group has nothing to lay out — drop it rather than emit a stray box.
	if (el.children.empty())
		return;

	const bool pages  = (el.representation == ibGroupRepresentation::Pages);
	const bool titled = (el.representation == ibGroupRepresentation::TitledBox) && !el.title.IsEmpty();
	const int orient  = (el.layout == ibGroupLayout::Horizontal) ? kOrientHorizontal : kOrientVertical;

	ibDataNode* boxParent = &parent;
	if (sizerable)
		boxParent = &AddSizerItem(parent, /*expand*/ true);

	const int id = m_nextId++;
	const ibClassID clsid = pages ? kCtrlNotebook : (titled ? kCtrlStaticBox : kCtrlBox);
	ibDataNode& box = boxParent->AddChild(clsid, id);
	box.SetValue(wxT("ControlId"), (s32)id);
	box.SetValue(wxT("Name"), el.name);
	if (titled)
		box.SetProp<wxString>(wxT("Title"), el.title);
	if (!pages)
		box.SetProperty(wxT("Orient"), ibDataValue::Int(orient));
	m_count++;

	// A Pages group holds Page children DIRECTLY (each Page is a notebook page
	// that then lays ITS children out through a box). A plain/titled box lays
	// its children out sizerable.
	if (pages) {
		for (const ibManagedElement& page : el.children) {
			const int pid = m_nextId++;
			ibDataNode& pageNode = box.AddChild(kCtrlPage, pid);
			pageNode.SetValue(wxT("ControlId"), (s32)pid);
			pageNode.SetValue(wxT("Name"), page.name);
			pageNode.SetProp<wxString>(wxT("Title"), page.title);
			m_count++;
			for (const ibManagedElement& child : page.children)
				EmitElement(pageNode, child, /*sizerable*/ true, wxNOT_FOUND);
		}
	} else {
		for (const ibManagedElement& child : el.children)
			EmitElement(box, child, /*sizerable*/ true, wxNOT_FOUND);
	}
}

void ibManagedFormCompiler::EmitField(ibDataNode& parent, const ibManagedElement& el,
	bool sizerable, ibMetaID tableId) {
	(void)tableId;
	ibDataNode* host = &parent;
	if (sizerable) host = &AddSizerItem(parent, /*expand*/ false);
	// Auto resolves against the bound value's type (checkbox for a boolean, etc.);
	// an explicit ViewKind is honoured as-is.
	ibFieldViewKind kind = el.viewKind;
	if (kind == ibFieldViewKind::Auto && m_autoKind)
		kind = m_autoKind(el.dataPath);
	const int id = m_nextId++;
	ibDataNode& node = host->AddChild(ClsidForViewKind(kind), id);
	node.SetValue(wxT("ControlId"), (s32)id);
	node.SetValue(wxT("Name"), el.name);
	if (!el.title.IsEmpty())
		node.SetProp<wxString>(wxT("Title"), el.title);
	const std::vector<ibSourceId> hops = m_resolve ? m_resolve(el.dataPath) : std::vector<ibSourceId>();
	if (!hops.empty())
		node.SetProperty(wxT("Source"), MakeSource(hops));
	// Text-box side buttons: a reference input carries a select button (open the
	// value's choice), a plain input carries none. The textctrl property defaults
	// to TRUE, so a primitive input MUST turn it off explicitly.
	if (kind == ibFieldViewKind::ReferenceField) {
		node.SetProperty(wxT("ButtonSelect"), ibDataValue::Bool(true));
		node.SetProperty(wxT("ButtonClear"),  ibDataValue::Bool(true));
	}
	else if (kind == ibFieldViewKind::InputField) {
		node.SetProperty(wxT("ButtonSelect"), ibDataValue::Bool(false));
		node.SetProperty(wxT("ButtonClear"),  ibDataValue::Bool(false));
	}
	m_count++;
}
