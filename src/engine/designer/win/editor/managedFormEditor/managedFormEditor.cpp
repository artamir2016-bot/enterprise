#include "managedFormEditor.h"
#include "managedElementProperty.h"

#include <wx/sizer.h>
#include <wx/button.h>
#include <wx/menu.h>
#include <wx/statline.h>
#include <wx/log.h>
#include <wx/accel.h>
#include <wx/wrapsizer.h>
#include <wx/splitter.h>
#include <wx/notebook.h>
#include <wx/listctrl.h>
#include <wx/stattext.h>
#include <wx/panel.h>
#include <wx/treebase.h>   // wxTreeEvent / wxTreeItemData
#include <wx/textdlg.h>    // wxGetTextFromUser — new form attribute name
#include <wx/choicdlg.h>   // wxGetSingleChoiceIndex — attribute type

#include "frontend/win/ctrls/treelistctrl.h"   // ibTreeListCtrl — multi-column Реквизиты tree

#include "backend/metaCollection/genericData.h"                   // ibValueMetaObjectGenericData
#include "backend/metaCollection/attribute/metaAttributeObject.h" // ibValueMetaObjectAttribute
#include "backend/metaCollection/partial/commonObject.h"          // hierarchy ref Code/Description
#include "backend/metaCollection/metaObject.h"                    // g_metaTableRefCLSID

#include "frontend/docView/docView.h"
#include "frontend/mainFrame/objinspect/objinspect.h"
#include "frontend/visualView/ctrl/form.h"
#include "frontend/visualView/ctrl/frame.h"       // ibValueFrame — FindControlByName / GetControlName
#include "frontend/visualView/visualHost.h"      // ibVisualHost — embedded preview host
#include "win/editor/codeEditor/codeEditorDesigner.h"  // ibCodeEditorDesigner — Module tab
#include "backend/metaCollection/metaFormObject.h"

namespace {
// A minimal, read-only ibVisualHost for the embedded preview: it renders whatever ibValueForm the
// editor hands it, with no document / source-object coupling (unlike ibVisualHostClient, whose
// SetCaption dereferences a document). GetValueForm reads the editor's current preview form through
// a slot pointer, so the editor can swap the form and rebuild without re-creating the host.
class ibManagedPreviewHost : public ibVisualHost {
public:
	ibManagedPreviewHost(wxWindow* parent, ibValueForm** formSlot)
		: ibVisualHost(parent, wxID_ANY), m_formSlot(formSlot) {}

	virtual ibValueForm* GetValueForm() const override { return m_formSlot ? *m_formSlot : nullptr; }
	virtual ibFrontendWindow* GetParentBackgroundWindow() const override
		{ return const_cast<ibManagedPreviewHost*>(this); }
	virtual ibFrontendWindow* GetBackgroundWindow() const override { return GetContentWindow(); }

protected:
	virtual void SetCaption(const wxString&) override {}   // no title bar in the preview pane

private:
	ibValueForm** m_formSlot = nullptr;
};

// Payload on a bindable Реквизиты node: the dataPath to bind and which kind to create on activate.
class ibBindingData : public wxTreeItemData {
public:
	ibBindingData(const wxString& path, const wxString& title, ibManagedNodeKind kind,
		bool isFormAttr = false)
		: m_path(path), m_title(title), m_kind(kind), m_isFormAttr(isFormAttr) {}
	wxString          m_path;
	wxString          m_title;
	ibManagedNodeKind m_kind;
	bool              m_isFormAttr;   // a form-own attribute (deletable), vs an owner field/column
};

// Best-effort readable type for an attribute (primitives by value-type; reference/composite folds
// to "Reference"). Good enough for the Тип column; an exact "CatalogRef.X" presentation is a later
// refinement.
wxString AttrTypeString(ibValueMetaObjectAttribute* a)
{
	if (a == nullptr) return wxEmptyString;
	if (a->ContainType(ibValueTypes::TYPE_BOOLEAN)) return _("Boolean");
	if (a->ContainType(ibValueTypes::TYPE_NUMBER))  return _("Number");
	if (a->ContainType(ibValueTypes::TYPE_STRING))  return _("String");
	if (a->ContainType(ibValueTypes::TYPE_DATE))    return _("Date");
	return _("Reference");
}
}

// The Add-* command ids are CONTIGUOUS so one handler can map id -> node kind by offset.
enum {
	ID_MFE_ADD_GROUP = wxID_HIGHEST + 5100,
	ID_MFE_ADD_FIELD,
	ID_MFE_ADD_TABLE,
	ID_MFE_ADD_COLUMN,
	ID_MFE_ADD_BUTTON,
	ID_MFE_ADD_PAGES,     // a Group whose representation is Pages (a notebook)
	ID_MFE_ADD_PAGE,      // a Page inside a Pages group
	ID_MFE_ADD_DECOR,
	ID_MFE_ADD__LAST = ID_MFE_ADD_DECOR,
	ID_MFE_DELETE,
	ID_MFE_UP,
	ID_MFE_DOWN,
	ID_MFE_UNDO,
	ID_MFE_REDO,
	ID_MFE_TEST,
	ID_MFE_TREE,
	ID_MFE_ADD_ATTR,
	ID_MFE_DEL_ATTR,
};

wxBEGIN_EVENT_TABLE(ibManagedFormEditor, wxPanel)
EVT_BUTTON(ID_MFE_DELETE,    ibManagedFormEditor::OnDelete)
EVT_BUTTON(ID_MFE_UP,        ibManagedFormEditor::OnMoveUp)
EVT_BUTTON(ID_MFE_DOWN,      ibManagedFormEditor::OnMoveDown)
EVT_BUTTON(ID_MFE_TEST,      ibManagedFormEditor::OnTestForm)
EVT_BUTTON(ID_MFE_UNDO,      ibManagedFormEditor::OnUndo)
EVT_BUTTON(ID_MFE_REDO,      ibManagedFormEditor::OnRedo)
EVT_MENU(ID_MFE_UNDO,        ibManagedFormEditor::OnUndo)
EVT_MENU(ID_MFE_REDO,        ibManagedFormEditor::OnRedo)
EVT_MENU(ID_MFE_DELETE,      ibManagedFormEditor::OnDelete)
EVT_MENU(ID_MFE_UP,          ibManagedFormEditor::OnMoveUp)
EVT_MENU(ID_MFE_DOWN,        ibManagedFormEditor::OnMoveDown)
// Every Add-* id (buttons AND menu items) funnels into OnAddElement.
EVT_COMMAND_RANGE(ID_MFE_ADD_GROUP, ID_MFE_ADD__LAST, wxEVT_BUTTON, ibManagedFormEditor::OnAddElement)
EVT_COMMAND_RANGE(ID_MFE_ADD_GROUP, ID_MFE_ADD__LAST, wxEVT_MENU,   ibManagedFormEditor::OnAddElement)
EVT_BUTTON(ID_MFE_ADD_ATTR,  ibManagedFormEditor::OnAddAttribute)
EVT_BUTTON(ID_MFE_DEL_ATTR,  ibManagedFormEditor::OnDeleteAttribute)
EVT_TREE_SEL_CHANGED(ID_MFE_TREE, ibManagedFormEditor::OnTreeSelChanged)
EVT_TREE_ITEM_MENU(ID_MFE_TREE,   ibManagedFormEditor::OnTreeContextMenu)
wxEND_EVENT_TABLE()

// Map an Add-* command id to the node kind it creates. ID_MFE_ADD_PAGES maps to Group
// (the caller stamps representation = Pages).
static ibManagedNodeKind KindForAddId(int id)
{
	switch (id) {
	case ID_MFE_ADD_GROUP:  return ibManagedNodeKind::Group;
	case ID_MFE_ADD_FIELD:  return ibManagedNodeKind::Field;
	case ID_MFE_ADD_TABLE:  return ibManagedNodeKind::Table;
	case ID_MFE_ADD_COLUMN: return ibManagedNodeKind::Column;
	case ID_MFE_ADD_BUTTON: return ibManagedNodeKind::Button;
	case ID_MFE_ADD_PAGES:  return ibManagedNodeKind::Group;   // representation stamped by caller
	case ID_MFE_ADD_PAGE:   return ibManagedNodeKind::Page;
	case ID_MFE_ADD_DECOR:  return ibManagedNodeKind::Decoration;
	default:                return ibManagedNodeKind::Group;
	}
}

ibManagedFormEditor::ibManagedFormEditor(ibMetaDocument* document, wxWindow* parent, wxWindowID id)
	: wxPanel(parent, id), m_document(document)
{
	// Top-level Form / Module notebook (1C-style). The Form page holds the toolbar + 3-pane body;
	// the Module page is the form-module code editor.
	wxBoxSizer* sizerMain = new wxBoxSizer(wxVERTICAL);
	m_mainBook = new wxNotebook(this, wxID_ANY);

	wxPanel* formPage = new wxPanel(m_mainBook, wxID_ANY);
	wxBoxSizer* formSizer = new wxBoxSizer(wxVERTICAL);
	BuildToolbar(formPage, formSizer);
	formSizer->Add(BuildBody(formPage), 1, wxEXPAND | wxALL, 2);
	formPage->SetSizer(formSizer);

	m_codeEditor = new ibCodeEditorDesigner(m_document, m_mainBook, wxID_ANY);

	m_mainBook->AddPage(formPage,     _("Form"),   true);
	m_mainBook->AddPage(m_codeEditor, _("Module"));

	sizerMain->Add(m_mainBook, 1, wxEXPAND);
	SetSizer(sizerMain);

	// Ctrl+Z / Ctrl+Y undo-redo (and Ctrl+Shift+Z as a common redo alias).
	wxAcceleratorEntry entries[3];
	entries[0].Set(wxACCEL_CTRL,               (int)'Z', ID_MFE_UNDO);
	entries[1].Set(wxACCEL_CTRL,               (int)'Y', ID_MFE_REDO);
	entries[2].Set(wxACCEL_CTRL | wxACCEL_SHIFT,(int)'Z', ID_MFE_REDO);
	SetAcceleratorTable(wxAcceleratorTable(3, entries));
}

ibManagedFormEditor::~ibManagedFormEditor()
{
	// Drop the inspector's hold on our adapter BEFORE the adapter dies.
	if (objectInspector != nullptr)
		objectInspector->SelectObject(nullptr);
	m_adapter.reset();
	// Tear the embedded host's controls down before dropping its form (the host is destroyed with
	// the panel; GetValueForm() reads m_previewForm, which we null here so late teardown is safe).
	if (m_previewHost != nullptr)
		m_previewHost->ClearVisualHost();
	if (m_previewForm != nullptr) {
		m_previewForm->DecrRef();
		m_previewForm = nullptr;
	}
	if (m_testForm != nullptr) {
		m_testForm->DecrRef();
		m_testForm = nullptr;
	}
}

void ibManagedFormEditor::BuildToolbar(wxWindow* parent, wxSizer* sizer)
{
	// A WRAP sizer: when the editor pane is narrow the buttons flow onto a second row
	// instead of collapsing to zero width (which would make them unclickable). Common
	// kinds live here; the full set (Column / Pages / Page / Decoration) is in the tree
	// context menu. Buttons parent to the Form page; their command events still propagate up
	// to this editor's event table.
	auto mkBtn = [parent](int id, const wxString& label, const wxString& name) {
		return new wxButton(parent, id, label, wxDefaultPosition, wxDefaultSize, 0,
			wxDefaultValidator, name);
	};
	wxWrapSizer* bar = new wxWrapSizer(wxHORIZONTAL);
	// Unique control names on each button so automation can target them without a mouse
	// (via the test agent's pressButton); no UI effect.
	bar->Add(mkBtn(ID_MFE_ADD_GROUP,  _("Add group"),  wxT("mfeAddGroup")),  0, wxALL, 2);
	bar->Add(mkBtn(ID_MFE_ADD_FIELD,  _("Add field"),  wxT("mfeAddField")),  0, wxALL, 2);
	bar->Add(mkBtn(ID_MFE_ADD_TABLE,  _("Add table"),  wxT("mfeAddTable")),  0, wxALL, 2);
	bar->Add(mkBtn(ID_MFE_ADD_BUTTON, _("Add button"), wxT("mfeAddButton")), 0, wxALL, 2);
	bar->Add(mkBtn(ID_MFE_DELETE,     _("Delete"),     wxT("mfeDelete")),    0, wxALL, 2);
	bar->Add(mkBtn(ID_MFE_UP,         _("Up"),         wxT("mfeUp")),        0, wxALL, 2);
	bar->Add(mkBtn(ID_MFE_DOWN,       _("Down"),       wxT("mfeDown")),      0, wxALL, 2);
	bar->Add(mkBtn(ID_MFE_UNDO, _("Undo"), wxT("mfeUndo")), 0, wxALL, 2);
	bar->Add(mkBtn(ID_MFE_REDO, _("Redo"), wxT("mfeRedo")), 0, wxALL, 2);
	bar->Add(mkBtn(ID_MFE_TEST, _("Test form"), wxT("mfeTest")), 0, wxALL, 2);
	sizer->Add(bar, 0, wxEXPAND);
}

wxWindow* ibManagedFormEditor::BuildBody(wxWindow* parent)
{
	// Outer split: the editing body (top) over the preview pane (bottom).
	m_outerSplit = new wxSplitterWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxSP_3D | wxSP_LIVE_UPDATE);
	m_outerSplit->SetSashGravity(1.0);      // the body absorbs vertical growth; preview keeps height
	m_outerSplit->SetMinimumPaneSize(48);

	// Top split: element tree (left) | data notebook (right).
	m_topSplit = new wxSplitterWindow(m_outerSplit, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxSP_3D | wxSP_LIVE_UPDATE);
	m_topSplit->SetSashGravity(0.6);
	m_topSplit->SetMinimumPaneSize(140);

	m_tree = new wxTreeCtrl(m_topSplit, ID_MFE_TREE, wxDefaultPosition, wxDefaultSize,
		wxTR_HIDE_ROOT | wxTR_HAS_BUTTONS | wxTR_LINES_AT_ROOT | wxTR_SINGLE | wxTR_DEFAULT_STYLE);

	// Data notebook: Attributes / Commands / Parameters (the form's data, 1C «Реквизиты»).
	m_dataBook = new wxNotebook(m_topSplit, wxID_ANY);

	// Реквизиты page: a small toolbar (add / delete a form-own attribute) over a hierarchical
	// multi-column tree. Object expands into the owner's attributes / tabular sections (sections
	// into their columns), with a Тип column; form-own attributes list below.
	wxPanel* attrPage = new wxPanel(m_dataBook, wxID_ANY);
	wxBoxSizer* attrSizer = new wxBoxSizer(wxVERTICAL);
	wxBoxSizer* attrBar   = new wxBoxSizer(wxHORIZONTAL);
	attrBar->Add(new wxButton(attrPage, ID_MFE_ADD_ATTR, _("Add attribute"),
		wxDefaultPosition, wxDefaultSize, 0, wxDefaultValidator, wxT("mfeAddAttr")), 0, wxALL, 1);
	attrBar->Add(new wxButton(attrPage, ID_MFE_DEL_ATTR, _("Delete attribute"),
		wxDefaultPosition, wxDefaultSize, 0, wxDefaultValidator, wxT("mfeDelAttr")), 0, wxALL, 1);
	attrSizer->Add(attrBar, 0, wxEXPAND);

	// Pass an explicit validator + name: the ctor's default name arg references an unexported
	// symbol (ibTreeListCtrlNameStr), so spelling it out keeps the link clean.
	m_attrTree = new ibTreeListCtrl(attrPage, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxTR_DEFAULT_STYLE | wxTR_HIDE_ROOT | wxTR_FULL_ROW_HIGHLIGHT,
		wxDefaultValidator, wxT("mfeAttrTree"));
	m_attrTree->AddColumn(_("Attribute"), 190, wxALIGN_LEFT);
	m_attrTree->AddColumn(_("Type"),      200, wxALIGN_LEFT);
	m_attrTree->Bind(wxEVT_COMMAND_TREE_ITEM_ACTIVATED, &ibManagedFormEditor::OnAttrActivated, this);
	attrSizer->Add(m_attrTree, 1, wxEXPAND);
	attrPage->SetSizer(attrSizer);

	m_cmdList = new wxListCtrl(m_dataBook, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxLC_REPORT | wxLC_SINGLE_SEL);
	m_cmdList->AppendColumn(_("Command"), wxLIST_FORMAT_LEFT, 220);

	m_paramList = new wxListCtrl(m_dataBook, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxLC_REPORT | wxLC_SINGLE_SEL);
	m_paramList->AppendColumn(_("Parameter"), wxLIST_FORMAT_LEFT, 150);
	m_paramList->AppendColumn(_("Type"),      wxLIST_FORMAT_LEFT, 170);

	m_dataBook->AddPage(attrPage,    _("Attributes"), true);
	m_dataBook->AddPage(m_cmdList,   _("Commands"));
	m_dataBook->AddPage(m_paramList, _("Parameters"));

	m_topSplit->SplitVertically(m_tree, m_dataBook, 300);

	// Preview pane: an embedded live-form host that renders the compiled element tree.
	m_previewPane = new wxPanel(m_outerSplit, wxID_ANY);
	m_previewHost = new ibManagedPreviewHost(m_previewPane, &m_previewForm);
	{
		wxBoxSizer* ps = new wxBoxSizer(wxVERTICAL);
		ps->Add(m_previewHost, 1, wxEXPAND | wxALL, 2);
		m_previewPane->SetSizer(ps);
	}

	m_outerSplit->SplitHorizontally(m_topSplit, m_previewPane, -200);  // ~200px preview strip
	return m_outerSplit;
}

bool ibManagedFormEditor::IsEditable() const
{
	return m_managed != nullptr && m_managed->IsEditable();
}

bool ibManagedFormEditor::LoadForm()
{
	if (m_document == nullptr)
		return false;
	m_managed = m_document->GetMetaObject() != nullptr
		? m_document->GetMetaObject()->ConvertToType<ibValueMetaObjectManagedForm>()
		: nullptr;
	if (m_managed == nullptr)
		return false;

	m_root  = m_managed->GetElementRoot();
	m_attrs = m_managed->GetElementAttrs();
	RebuildTree();
	RebuildDataPanels();
	// Load the form module's code into the Module tab (the form IS an ibValueMetaObjectModuleBase,
	// so the editor binds to it through the document).
	if (m_codeEditor != nullptr)
		m_codeEditor->LoadModule();
	return true;
}

void ibManagedFormEditor::RebuildDataPanels()
{
	if (m_attrTree == nullptr)
		return;

	m_attrTree->DeleteRoot();
	wxTreeItemId root = m_attrTree->AddRoot(wxT("root"));

	auto* owner = m_managed != nullptr
		? dynamic_cast<ibValueMetaObjectGenericData*>(m_managed->GetParent()) : nullptr;

	// The main "Object" attribute, typed to the owning business object (as the compiler emits it),
	// expands into the owner's fields and tabular sections — double-clicking a leaf binds a field.
	if (owner != nullptr) {
		wxTreeItemId objItem = m_attrTree->AppendItem(root, wxT("Object"));
		m_attrTree->SetItemText(objItem, 1, owner->GetName());
		m_attrTree->SetItemBold(objItem, true);

		if (auto* h = dynamic_cast<ibValueMetaObjectRecordDataHierarchyMutableRef*>(owner)) {
			if (auto* code = dynamic_cast<ibValueMetaObjectAttribute*>(h->GetDataCode())) {
				wxTreeItemId it = m_attrTree->AppendItem(objItem, wxT("Code"), -1, -1,
					new ibBindingData(wxT("Code"), wxT("Code"), ibManagedNodeKind::Field));
				m_attrTree->SetItemText(it, 1, AttrTypeString(code));
			}
			if (auto* desc = dynamic_cast<ibValueMetaObjectAttribute*>(h->GetDataDescription())) {
				wxTreeItemId it = m_attrTree->AppendItem(objItem, wxT("Description"), -1, -1,
					new ibBindingData(wxT("Description"), wxT("Description"), ibManagedNodeKind::Field));
				m_attrTree->SetItemText(it, 1, AttrTypeString(desc));
			}
		}
		for (unsigned int i = 0; i < owner->GetChildCount(); ++i) {
			ibValueMetaObject* child = owner->GetChild(i);
			if (child == nullptr) continue;
			if (auto* a = dynamic_cast<ibValueMetaObjectAttribute*>(child)) {
				wxTreeItemId it = m_attrTree->AppendItem(objItem, a->GetName(), -1, -1,
					new ibBindingData(a->GetName(), a->GetName(), ibManagedNodeKind::Field));
				m_attrTree->SetItemText(it, 1, AttrTypeString(a));
			}
			else if (child->GetClassType() == g_metaTableRefCLSID) {
				const wxString section = child->GetName();
				wxTreeItemId secItem = m_attrTree->AppendItem(objItem, section, -1, -1,
					new ibBindingData(section, section, ibManagedNodeKind::Table));
				m_attrTree->SetItemText(secItem, 1, _("ValueTable"));
				for (unsigned int j = 0; j < child->GetChildCount(); ++j) {
					if (auto* col = dynamic_cast<ibValueMetaObjectAttribute*>(child->GetChild(j))) {
						const wxString path = section + wxT(".") + col->GetName();
						wxTreeItemId it = m_attrTree->AppendItem(secItem, col->GetName(), -1, -1,
							new ibBindingData(path, col->GetName(), ibManagedNodeKind::Column));
						m_attrTree->SetItemText(it, 1, AttrTypeString(col));
					}
				}
			}
		}
		m_attrTree->Expand(objItem);
	}

	// The form's own attributes (session-lived — they exist while the form is open). Deletable.
	for (const ibManagedAttribute& a : m_attrs) {
		wxTreeItemId it = m_attrTree->AppendItem(root, a.name, -1, -1,
			new ibBindingData(a.name, a.name, ibManagedNodeKind::Field, /*isFormAttr*/ true));
		wxString tn;
		switch (a.type) {
		case ibValueTypes::TYPE_NUMBER:  tn = _("Number");  break;
		case ibValueTypes::TYPE_BOOLEAN: tn = _("Boolean"); break;
		case ibValueTypes::TYPE_DATE:    tn = _("Date");    break;
		default:                         tn = _("String");  break;
		}
		m_attrTree->SetItemText(it, 1, tn);
	}

	m_cmdList->DeleteAllItems();     // Commands — read-only placeholder (mirrors the 1C tab)
	m_paramList->DeleteAllItems();   // Parameters — placeholder
}

void ibManagedFormEditor::OnAttrActivated(wxTreeEvent& event)
{
	if (!IsEditable())
		return;
	auto* data = dynamic_cast<ibBindingData*>(m_attrTree->GetItemData(event.GetItem()));
	if (data == nullptr)
		return;   // a container row (Object / a section) — nothing to bind directly
	AddBoundElement(data->m_path, data->m_title, data->m_kind);
}

ibManagedElement* ibManagedFormEditor::AddBoundElement(const wxString& dataPath,
	const wxString& title, ibManagedNodeKind kind)
{
	// Columns can only live in a Table; for a double-click bind, fall back to a Field so the node
	// is always placeable (a column dragged onto a table is handled by the drag path later).
	if (kind == ibManagedNodeKind::Column)
		kind = ibManagedNodeKind::Field;

	ibManagedElement* node = AddElement(kind);   // honours containment rules + undo snapshot
	if (node == nullptr)
		return nullptr;
	node->dataPath = dataPath;
	node->title    = title;
	RebuildTree();        // relabel + refresh preview with the binding applied
	MarkDirty();
	return node;
}

void ibManagedFormEditor::OnAddAttribute(wxCommandEvent&)
{
	if (!IsEditable())
		return;
	const wxString name = wxGetTextFromUser(_("Attribute name:"), _("New form attribute"),
		UniqueName(wxT("Attribute")), this);
	if (name.IsEmpty())
		return;

	const wxArrayString kinds{ _("String"), _("Number"), _("Boolean"), _("Date") };
	const int sel = wxGetSingleChoiceIndex(_("Type:"), _("New form attribute"), kinds, this);
	if (sel < 0)
		return;
	static const ibValueTypes typeOf[] = {
		ibValueTypes::TYPE_STRING, ibValueTypes::TYPE_NUMBER,
		ibValueTypes::TYPE_BOOLEAN, ibValueTypes::TYPE_DATE };

	PushUndoSnapshot();
	ibManagedAttribute attr;
	attr.name   = name;
	attr.isMain = false;
	attr.type   = typeOf[sel];
	m_attrs.push_back(attr);

	RebuildDataPanels();
	MarkDirty();          // flush to the metaobject
	RefreshPreview();     // the new attribute can now be bound / shown
}

void ibManagedFormEditor::OnDeleteAttribute(wxCommandEvent&)
{
	if (!IsEditable() || m_attrTree == nullptr)
		return;
	auto* data = dynamic_cast<ibBindingData*>(m_attrTree->GetItemData(m_attrTree->GetSelection()));
	if (data == nullptr || !data->m_isFormAttr) {
		wxLogStatus(wxT("%s"), _("Select a form attribute to delete (owner fields cannot be removed here)."));
		return;
	}
	PushUndoSnapshot();
	for (auto it = m_attrs.begin(); it != m_attrs.end(); ++it) {
		if (it->name == data->m_path) { m_attrs.erase(it); break; }
	}
	RebuildDataPanels();
	MarkDirty();
	RefreshPreview();
}

bool ibManagedFormEditor::SaveForm()
{
	if (m_managed == nullptr)
		return false;
	m_managed->SetElementTree(m_root, m_attrs);
	if (m_codeEditor != nullptr)
		m_codeEditor->SaveModule();   // flush the Module tab's code onto the form module
	return true;
}

wxString ibManagedFormEditor::ElementLabel(const ibManagedElement* el) const
{
	wxString kind;
	switch (el->kind) {
	case ibManagedNodeKind::Group:      kind = _("Group"); break;
	case ibManagedNodeKind::Page:       kind = _("Page"); break;
	case ibManagedNodeKind::Field:      kind = _("Field"); break;
	case ibManagedNodeKind::Table:      kind = _("Table"); break;
	case ibManagedNodeKind::Column:     kind = _("Column"); break;
	case ibManagedNodeKind::Button:     kind = _("Button"); break;
	case ibManagedNodeKind::Decoration: kind = _("Decoration"); break;
	default:                            kind = _("Element"); break;
	}
	wxString name = el->name.IsEmpty() ? el->title : el->name;
	if (name.IsEmpty())
		return kind;
	return name + wxT(" : ") + kind;
}

void ibManagedFormEditor::AddTreeNode(const wxTreeItemId& parentItem, ibManagedElement* el)
{
	wxTreeItemId item = m_tree->AppendItem(parentItem, ElementLabel(el), -1, -1, new ElementItemData(el));
	for (ibManagedElement& child : el->children)
		AddTreeNode(item, &child);
}

void ibManagedFormEditor::RebuildTree()
{
	if (m_tree == nullptr)
		return;
	m_tree->DeleteAllItems();
	wxTreeItemId rootItem = m_tree->AddRoot(wxT("root"));
	for (ibManagedElement& child : m_root.children)
		AddTreeNode(rootItem, &child);
	m_tree->ExpandAll();
	RefreshPreview();   // structural edits (add/delete/move/undo/redo) all funnel through here
}

ibManagedElement* ibManagedFormEditor::SelectedElement() const
{
	if (m_tree == nullptr)
		return nullptr;
	wxTreeItemId sel = m_tree->GetSelection();
	if (!sel.IsOk())
		return nullptr;
	ElementItemData* data = dynamic_cast<ElementItemData*>(m_tree->GetItemData(sel));
	return data != nullptr ? data->m_element : nullptr;
}

bool ibManagedFormEditor::FindParent(ibManagedElement& root, const ibManagedElement* target,
	ibManagedElement*& outParent, size_t& outIndex) const
{
	for (size_t i = 0; i < root.children.size(); ++i) {
		if (&root.children[i] == target) {
			outParent = &root;
			outIndex = i;
			return true;
		}
		if (FindParent(root.children[i], target, outParent, outIndex))
			return true;
	}
	return false;
}

wxString ibManagedFormEditor::UniqueName(const wxString& base) const
{
	// Scan the whole tree for the highest "base<N>" and pick N+1.
	int maxN = 0;
	std::vector<const ibManagedElement*> stack{ &m_root };
	while (!stack.empty()) {
		const ibManagedElement* e = stack.back();
		stack.pop_back();
		if (e->name.StartsWith(base)) {
			long n = 0;
			if (e->name.Mid(base.length()).ToLong(&n) && n > maxN)
				maxN = (int)n;
		}
		for (const ibManagedElement& c : e->children)
			stack.push_back(&c);
	}
	return wxString::Format(wxT("%s%d"), base, maxN + 1);
}

wxString ibManagedFormEditor::NamePrefixFor(ibManagedNodeKind kind)
{
	switch (kind) {
	case ibManagedNodeKind::Group:      return wxT("Group");
	case ibManagedNodeKind::Page:       return wxT("Page");
	case ibManagedNodeKind::Field:      return wxT("Field");
	case ibManagedNodeKind::Table:      return wxT("Table");
	case ibManagedNodeKind::Column:     return wxT("Column");
	case ibManagedNodeKind::Button:     return wxT("Button");
	case ibManagedNodeKind::Decoration: return wxT("Decoration");
	default:                            return wxT("Element");
	}
}

ibManagedElement* ibManagedFormEditor::FindAncestorOfKind(ibManagedElement* node, ibManagedNodeKind kind) const
{
	// The element tree is a value tree (no parent pointers), so walk from the root to the
	// node, remembering the last ancestor of `kind`. node itself counts.
	if (node == nullptr)
		return nullptr;
	if (node->kind == kind)
		return node;
	// Search path root -> node.
	struct Walk {
		static ibManagedElement* find(ibManagedElement& cur, const ibManagedElement* target,
			ibManagedNodeKind kind, ibManagedElement* lastMatch) {
			for (ibManagedElement& ch : cur.children) {
				ibManagedElement* lm = (ch.kind == kind) ? &ch : lastMatch;
				if (&ch == target)
					return lm;
				if (ibManagedElement* r = find(ch, target, kind, lm))
					return r;
			}
			return nullptr;
		}
	};
	return Walk::find(const_cast<ibManagedElement&>(m_root), node, kind, nullptr);
}

ibManagedElement* ibManagedFormEditor::ResolveInsertParent(ibManagedNodeKind kind, wxString& reason) const
{
	ibManagedElement* sel = SelectedElement();

	// A Column lives only inside a Table — anchor on the selected Table (or its enclosing one).
	if (kind == ibManagedNodeKind::Column) {
		ibManagedElement* table = FindAncestorOfKind(sel, ibManagedNodeKind::Table);
		if (table == nullptr) {
			reason = _("A column can be added only inside a table. Select a table first.");
			return nullptr;
		}
		return table;
	}

	// A Page lives only inside a Pages group (a Group with representation == Pages).
	if (kind == ibManagedNodeKind::Page) {
		for (ibManagedElement* n = sel; ; ) {
			ibManagedElement* grp = FindAncestorOfKind(n, ibManagedNodeKind::Group);
			if (grp == nullptr)
				break;
			if (grp->representation == ibGroupRepresentation::Pages)
				return grp;
			// keep climbing: look above this group
			ibManagedElement* parent = nullptr; size_t idx = 0;
			if (!FindParent(const_cast<ibManagedElement&>(m_root), grp, parent, idx) || parent == &m_root)
				break;
			n = parent;
		}
		reason = _("A page can be added only inside a Pages group. Select a Pages group first.");
		return nullptr;
	}

	// Everything else (Group / Field / Table / Button / Decoration) goes into the selected
	// container, or as a sibling of the selected leaf, or at the top level. A Column cannot
	// be a sibling of a plain leaf — but non-Column kinds may. Reject placing a non-Column
	// directly inside a Table (tables hold columns only).
	if (sel != nullptr && (sel->kind == ibManagedNodeKind::Group || sel->kind == ibManagedNodeKind::Page))
		return sel;
	if (sel != nullptr) {
		ibManagedElement* parent = nullptr; size_t idx = 0;
		if (FindParent(const_cast<ibManagedElement&>(m_root), sel, parent, idx)) {
			if (parent->kind == ibManagedNodeKind::Table) {
				reason = _("A table can contain only columns.");
				return nullptr;
			}
			return parent;
		}
	}
	return const_cast<ibManagedElement*>(&m_root);
}

ibManagedElement* ibManagedFormEditor::AddElement(ibManagedNodeKind kind)
{
	if (!IsEditable())
		return nullptr;

	wxString reason;
	ibManagedElement* parent = ResolveInsertParent(kind, reason);
	if (parent == nullptr) {
		if (!reason.IsEmpty())
			wxLogStatus(wxT("%s"), reason);
		return nullptr;
	}

	PushUndoSnapshot();

	ibManagedElement node(kind);
	node.name = UniqueName(NamePrefixFor(kind));

	// Insert after the selection when it is a sibling in the same parent, else append.
	ibManagedElement* created = nullptr;
	ibManagedElement* sel = SelectedElement();
	ibManagedElement* selParent = nullptr; size_t selIdx = 0;
	if (sel != nullptr && sel != parent && FindParent(m_root, sel, selParent, selIdx) && selParent == parent) {
		auto it = parent->children.insert(parent->children.begin() + selIdx + 1, node);
		created = &(*it);
	}
	else {
		parent->children.push_back(node);
		created = &parent->children.back();
	}

	RebuildTree();
	MarkDirty();
	return created;
}

void ibManagedFormEditor::MoveSelected(int dir)
{
	if (!IsEditable())
		return;
	ibManagedElement* sel = SelectedElement();
	if (sel == nullptr)
		return;
	ibManagedElement* parent = nullptr; size_t idx = 0;
	if (!FindParent(m_root, sel, parent, idx))
		return;
	const long newIdx = (long)idx + dir;
	if (newIdx < 0 || newIdx >= (long)parent->children.size())
		return;
	PushUndoSnapshot();
	std::swap(parent->children[idx], parent->children[newIdx]);
	RebuildTree();
	MarkDirty();
}

void ibManagedFormEditor::OnAddElement(wxCommandEvent& event)
{
	const ibManagedNodeKind kind = KindForAddId(event.GetId());
	ibManagedElement* created = AddElement(kind);
	if (created != nullptr && event.GetId() == ID_MFE_ADD_PAGES) {
		// "Add pages" creates a Group that presents as a notebook.
		created->representation = ibGroupRepresentation::Pages;
		RebuildTree();
		MarkDirty();
	}
}

void ibManagedFormEditor::OnDelete(wxCommandEvent&)
{
	if (!IsEditable())
		return;
	ibManagedElement* sel = SelectedElement();
	if (sel == nullptr)
		return;
	ibManagedElement* parent = nullptr; size_t idx = 0;
	if (!FindParent(m_root, sel, parent, idx))
		return;
	PushUndoSnapshot();
	// Dropping the node drops the adapter that pointed at it.
	ClearSelectionBinding();
	parent->children.erase(parent->children.begin() + idx);
	RebuildTree();
	MarkDirty();
}

void ibManagedFormEditor::OnMoveUp(wxCommandEvent&)   { MoveSelected(-1); }
void ibManagedFormEditor::OnMoveDown(wxCommandEvent&) { MoveSelected(+1); }
void ibManagedFormEditor::OnTestForm(wxCommandEvent&) { PreviewForm(); }

void ibManagedFormEditor::MarkDirty()
{
	if (m_managed != nullptr)
		m_managed->SetElementTree(m_root, m_attrs);   // keep the metaobject in sync with the edit
	if (m_document != nullptr)
		m_document->Modify(true);
}

void ibManagedFormEditor::ClearSelectionBinding()
{
	if (objectInspector != nullptr)
		objectInspector->SelectObject(nullptr);
	m_adapter.reset();
}

void ibManagedFormEditor::PushUndoSnapshot()
{
	m_undo.push_back(m_root);
	if (m_undo.size() > kMaxUndo)
		m_undo.erase(m_undo.begin());
	m_redo.clear();
}

void ibManagedFormEditor::Undo()
{
	if (m_undo.empty()) {
		wxLogStatus(wxT("%s"), _("Nothing to undo."));
		return;
	}
	// A restore replaces the whole tree, invalidating every element pointer — drop the
	// adapter/inspector binding first, then swap and rebuild.
	ClearSelectionBinding();
	m_redo.push_back(m_root);
	m_root = m_undo.back();
	m_undo.pop_back();
	RebuildTree();
	if (m_document != nullptr)
		MarkDirty();
}

void ibManagedFormEditor::Redo()
{
	if (m_redo.empty()) {
		wxLogStatus(wxT("%s"), _("Nothing to redo."));
		return;
	}
	ClearSelectionBinding();
	m_undo.push_back(m_root);
	m_root = m_redo.back();
	m_redo.pop_back();
	RebuildTree();
	if (m_document != nullptr)
		MarkDirty();
}

void ibManagedFormEditor::OnUndo(wxCommandEvent&) { Undo(); }
void ibManagedFormEditor::OnRedo(wxCommandEvent&) { Redo(); }

void ibManagedFormEditor::OnTreeSelChanged(wxTreeEvent&)
{
	ibManagedElement* el = SelectedElement();
	if (el == nullptr) {
		if (objectInspector != nullptr)
			objectInspector->SelectObject(nullptr);
		m_adapter.reset();
		HighlightInPreview(nullptr);   // clear any preview highlight
		return;
	}
	// Rebuild the adapter for the newly selected element and show it. The Properties pane is
	// created HIDDEN (mainFrameParts: Show(false)) and only appears when ShowInspector() is
	// called — the visual editor / metadata tree do this on select, so the managed-form editor
	// must too, otherwise the palette fills but stays invisible.
	m_adapter = std::make_unique<ibManagedElementProperty>(el, this);
	if (objectInspector != nullptr) {
		objectInspector->SelectObject(m_adapter.get(), true);
		objectInspector->ShowInspector();   // reveal the Properties palette (no-op if already shown)
	}
	HighlightInPreview(el);   // highlight the matching control in the embedded preview
}

void ibManagedFormEditor::OnTreeContextMenu(wxTreeEvent& event)
{
	if (event.GetItem().IsOk())
		m_tree->SelectItem(event.GetItem());

	const bool editable = IsEditable();
	const bool hasSel = SelectedElement() != nullptr;

	wxMenu* add = new wxMenu();
	add->Append(ID_MFE_ADD_GROUP,  _("Group"));
	add->Append(ID_MFE_ADD_PAGES,  _("Pages (notebook)"));
	add->Append(ID_MFE_ADD_PAGE,   _("Page"));
	add->Append(ID_MFE_ADD_FIELD,  _("Field"));
	add->Append(ID_MFE_ADD_TABLE,  _("Table"));
	add->Append(ID_MFE_ADD_COLUMN, _("Column"));
	add->Append(ID_MFE_ADD_BUTTON, _("Button"));
	add->Append(ID_MFE_ADD_DECOR,  _("Decoration"));
	for (int id = ID_MFE_ADD_GROUP; id <= ID_MFE_ADD__LAST; ++id)
		add->Enable(id, editable);

	wxMenu menu;
	menu.AppendSubMenu(add, _("Add"));
	menu.AppendSeparator();
	menu.Append(ID_MFE_DELETE, _("Delete"));
	menu.Append(ID_MFE_UP,     _("Move up"));
	menu.Append(ID_MFE_DOWN,   _("Move down"));
	menu.Enable(ID_MFE_DELETE, editable && hasSel);
	menu.Enable(ID_MFE_UP,     editable && hasSel);
	menu.Enable(ID_MFE_DOWN,   editable && hasSel);

	PopupMenu(&menu);
}

void ibManagedFormEditor::OnElementChanged(ibManagedElement* element)
{
	if (m_tree == nullptr || element == nullptr)
		return;
	// Relabel the matching tree item (structure unchanged → no rebuild).
	wxTreeItemIdValue cookie;
	std::vector<wxTreeItemId> stack;
	wxTreeItemId root = m_tree->GetRootItem();
	if (root.IsOk()) {
		wxTreeItemId child = m_tree->GetFirstChild(root, cookie);
		while (child.IsOk()) { stack.push_back(child); child = m_tree->GetNextChild(root, cookie); }
	}
	while (!stack.empty()) {
		wxTreeItemId item = stack.back();
		stack.pop_back();
		ElementItemData* data = dynamic_cast<ElementItemData*>(m_tree->GetItemData(item));
		if (data != nullptr && data->m_element == element) {
			m_tree->SetItemText(item, ElementLabel(element));
			break;
		}
		wxTreeItemIdValue c2;
		wxTreeItemId ch = m_tree->GetFirstChild(item, c2);
		while (ch.IsOk()) { stack.push_back(ch); ch = m_tree->GetNextChild(item, c2); }
	}
	if (m_document != nullptr)
		MarkDirty();
	RefreshPreview();   // a property edit (title / view kind / binding) changes the rendered form
}

std::vector<wxString> ibManagedFormEditor::AvailableBindings() const
{
	// The owner object's attributes + "Section.Column" paths — the same set the compiler's
	// resolver accepts (see CompileElementsToFormData). An object managed form binds through
	// the owner; a managed form with no business owner offers nothing.
	std::vector<wxString> out;
	// Form-own attributes bind directly by name.
	for (const ibManagedAttribute& a : m_attrs)
		if (!a.name.IsEmpty())
			out.push_back(a.name);

	const auto* owner = m_managed != nullptr
		? dynamic_cast<const ibValueMetaObjectGenericData*>(m_managed->GetParent())
		: nullptr;
	if (owner == nullptr)
		return out;

	// Predefined Code / Description of a hierarchy reference are real attributes.
	if (auto* h = dynamic_cast<const ibValueMetaObjectRecordDataHierarchyMutableRef*>(owner)) {
		if (h->GetDataCode() != nullptr)        out.push_back(wxT("Code"));
		if (h->GetDataDescription() != nullptr) out.push_back(wxT("Description"));
	}
	for (unsigned int i = 0; i < owner->GetChildCount(); ++i) {
		ibValueMetaObject* child = owner->GetChild(i);
		if (child == nullptr)
			continue;
		if (dynamic_cast<ibValueMetaObjectAttribute*>(child) != nullptr) {
			out.push_back(child->GetName());
		}
		else if (child->GetClassType() == g_metaTableRefCLSID) {
			const wxString section = child->GetName();
			for (unsigned int j = 0; j < child->GetChildCount(); ++j)
				if (dynamic_cast<ibValueMetaObjectAttribute*>(child->GetChild(j)) != nullptr)
					out.push_back(section + wxT(".") + child->GetChild(j)->GetName());
		}
	}
	return out;
}

void ibManagedFormEditor::PreviewForm()
{
	// "Test form" — open the compiled form in a MODAL window (full interaction).
	if (m_managed == nullptr)
		return;
	m_managed->SetElementTree(m_root, m_attrs);
	const wxMemoryBuffer blob = m_managed->CompileElementsToFormData();
	if (blob.IsEmpty()) {
		wxLogWarning(_("Managed form: nothing to preview — the element tree is empty or has no owner."));
		return;
	}
	m_managed->SetFormData(blob);

	ibValueForm* form = new ibValueForm(m_managed, nullptr);
	if (!m_managed->LoadFormData(form)) {
		wxDELETE(form);
		wxLogWarning(_("Managed form: could not build the preview form."));
		return;
	}
	form->IncrRef();
	if (m_testForm != nullptr)
		m_testForm->DecrRef();
	m_testForm = form;
	form->ShowForm(static_cast<ibDocument*>(m_document), false);
}

void ibManagedFormEditor::HighlightInPreview(const ibManagedElement* el)
{
	// Restore the previously highlighted window (if it's still alive — it is, unless a rebuild
	// cleared the pointer, which that path does before destroying windows).
	if (m_highlightWin != nullptr) {
		m_highlightWin->SetBackgroundColour(m_highlightOrig);
		m_highlightWin->Refresh();
		m_highlightWin = nullptr;
	}
	if (el == nullptr || el->name.IsEmpty() || m_previewForm == nullptr || m_previewHost == nullptr)
		return;

	// The compiler names each control after its element (managedFormCompiler: SetValue("Name",
	// el.name)), so match by name, then map the ibValueFrame to its rendered wxWindow.
	ibValueFrame* ctrl = m_previewForm->FindControlByName(el->name);
	if (ctrl == nullptr)
		return;
	wxWindow* w = wxDynamicCast(m_previewHost->GetWxObject(ctrl), wxWindow);
	if (w == nullptr)
		return;   // e.g. a group backed by a bare sizer has no window to tint

	m_highlightWin  = w;
	m_highlightOrig = w->GetBackgroundColour();
	w->SetBackgroundColour(wxColour(255, 243, 150));   // soft yellow selection tint
	w->Refresh();
}

void ibManagedFormEditor::RefreshPreview()
{
	// Coalesce a burst of edits into one rebuild on the next event-loop turn.
	if (m_refreshQueued || m_previewHost == nullptr)
		return;
	m_refreshQueued = true;
	CallAfter([this]() { m_refreshQueued = false; DoRefreshPreview(); });
}

void ibManagedFormEditor::DoRefreshPreview()
{
	if (m_previewHost == nullptr || m_managed == nullptr)
		return;

	// The rebuild destroys every preview window — drop the (now-dangling) highlight pointer WITHOUT
	// touching the dead window.
	m_highlightWin = nullptr;

	// Tear down the previous embedded form.
	m_previewHost->ClearVisualHost();
	if (m_previewForm != nullptr) {
		m_previewForm->DecrRef();
		m_previewForm = nullptr;
	}

	m_managed->SetElementTree(m_root, m_attrs);
	const wxMemoryBuffer blob = m_managed->CompileElementsToFormData();
	if (blob.IsEmpty()) {
		m_previewPane->Layout();
		return;   // empty tree / no owner → empty preview, never an error
	}
	m_managed->SetFormData(blob);

	ibValueForm* form = new ibValueForm(m_managed, nullptr);
	if (!m_managed->LoadFormData(form)) {
		wxDELETE(form);
		return;
	}
	form->IncrRef();
	m_previewForm = form;                    // GetValueForm() reads this slot
	m_previewHost->CreateAndUpdateVisualHost();
	m_previewPane->Layout();
	HighlightInPreview(SelectedElement());   // re-light the current selection in the fresh preview
}
