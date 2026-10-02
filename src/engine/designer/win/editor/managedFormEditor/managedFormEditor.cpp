#include "managedFormEditor.h"
#include "managedElementProperty.h"

#include <wx/sizer.h>
#include <wx/button.h>
#include <wx/menu.h>

#include "frontend/docView/docView.h"
#include "frontend/mainFrame/objinspect/objinspect.h"
#include "frontend/visualView/ctrl/form.h"
#include "backend/metaCollection/metaFormObject.h"

enum {
	ID_MFE_ADD_GROUP = wxID_HIGHEST + 5100,
	ID_MFE_ADD_FIELD,
	ID_MFE_DELETE,
	ID_MFE_UP,
	ID_MFE_DOWN,
	ID_MFE_TEST,
	ID_MFE_TREE,
};

wxBEGIN_EVENT_TABLE(ibManagedFormEditor, wxPanel)
EVT_BUTTON(ID_MFE_ADD_GROUP, ibManagedFormEditor::OnAddGroup)
EVT_BUTTON(ID_MFE_ADD_FIELD, ibManagedFormEditor::OnAddField)
EVT_BUTTON(ID_MFE_DELETE,    ibManagedFormEditor::OnDelete)
EVT_BUTTON(ID_MFE_UP,        ibManagedFormEditor::OnMoveUp)
EVT_BUTTON(ID_MFE_DOWN,      ibManagedFormEditor::OnMoveDown)
EVT_BUTTON(ID_MFE_TEST,      ibManagedFormEditor::OnTestForm)
EVT_MENU(ID_MFE_ADD_GROUP,   ibManagedFormEditor::OnAddGroup)
EVT_MENU(ID_MFE_ADD_FIELD,   ibManagedFormEditor::OnAddField)
EVT_MENU(ID_MFE_DELETE,      ibManagedFormEditor::OnDelete)
EVT_MENU(ID_MFE_UP,          ibManagedFormEditor::OnMoveUp)
EVT_MENU(ID_MFE_DOWN,        ibManagedFormEditor::OnMoveDown)
EVT_TREE_SEL_CHANGED(ID_MFE_TREE, ibManagedFormEditor::OnTreeSelChanged)
EVT_TREE_ITEM_MENU(ID_MFE_TREE,   ibManagedFormEditor::OnTreeContextMenu)
wxEND_EVENT_TABLE()

ibManagedFormEditor::ibManagedFormEditor(ibMetaDocument* document, wxWindow* parent, wxWindowID id)
	: wxPanel(parent, id), m_document(document)
{
	wxBoxSizer* sizerMain = new wxBoxSizer(wxVERTICAL);
	BuildToolbar(sizerMain);

	m_tree = new wxTreeCtrl(this, ID_MFE_TREE, wxDefaultPosition, wxDefaultSize,
		wxTR_HIDE_ROOT | wxTR_HAS_BUTTONS | wxTR_LINES_AT_ROOT | wxTR_SINGLE | wxTR_DEFAULT_STYLE);
	sizerMain->Add(m_tree, 1, wxEXPAND | wxALL, 2);

	SetSizer(sizerMain);
}

ibManagedFormEditor::~ibManagedFormEditor()
{
	// Drop the inspector's hold on our adapter BEFORE the adapter dies.
	if (objectInspector != nullptr)
		objectInspector->SelectObject(nullptr);
	m_adapter.reset();
	if (m_previewForm != nullptr) {
		m_previewForm->DecrRef();
		m_previewForm = nullptr;
	}
}

void ibManagedFormEditor::BuildToolbar(wxSizer* sizer)
{
	wxBoxSizer* bar = new wxBoxSizer(wxHORIZONTAL);
	bar->Add(new wxButton(this, ID_MFE_ADD_GROUP, _("Add group")), 0, wxALL, 2);
	bar->Add(new wxButton(this, ID_MFE_ADD_FIELD, _("Add field")), 0, wxALL, 2);
	bar->Add(new wxButton(this, ID_MFE_DELETE,    _("Delete")),    0, wxALL, 2);
	bar->Add(new wxButton(this, ID_MFE_UP,        _("Up")),        0, wxALL, 2);
	bar->Add(new wxButton(this, ID_MFE_DOWN,      _("Down")),      0, wxALL, 2);
	bar->AddStretchSpacer(1);
	bar->Add(new wxButton(this, ID_MFE_TEST,      _("Test form")), 0, wxALL, 2);
	sizer->Add(bar, 0, wxEXPAND);
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
	return true;
}

bool ibManagedFormEditor::SaveForm()
{
	if (m_managed == nullptr)
		return false;
	m_managed->SetElementTree(m_root, m_attrs);
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

void ibManagedFormEditor::AddElement(ibManagedNodeKind kind)
{
	if (!IsEditable())
		return;

	ibManagedElement node(kind);
	node.name = UniqueName(kind == ibManagedNodeKind::Group ? wxT("Group") : wxT("Field"));

	ibManagedElement* sel = SelectedElement();
	if (sel != nullptr && (sel->kind == ibManagedNodeKind::Group || sel->kind == ibManagedNodeKind::Page)) {
		// Add as last child of the selected container.
		sel->children.push_back(node);
	}
	else if (sel != nullptr) {
		// Add as sibling after the selected leaf.
		ibManagedElement* parent = nullptr; size_t idx = 0;
		if (FindParent(m_root, sel, parent, idx))
			parent->children.insert(parent->children.begin() + idx + 1, node);
		else
			m_root.children.push_back(node);
	}
	else {
		// Nothing selected → top level.
		m_root.children.push_back(node);
	}

	RebuildTree();
	m_document->Modify(true);
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
	std::swap(parent->children[idx], parent->children[newIdx]);
	RebuildTree();
	m_document->Modify(true);
}

void ibManagedFormEditor::OnAddGroup(wxCommandEvent&) { AddElement(ibManagedNodeKind::Group); }
void ibManagedFormEditor::OnAddField(wxCommandEvent&) { AddElement(ibManagedNodeKind::Field); }

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
	// Dropping the node drops the adapter that pointed at it.
	if (objectInspector != nullptr)
		objectInspector->SelectObject(nullptr);
	m_adapter.reset();
	parent->children.erase(parent->children.begin() + idx);
	RebuildTree();
	m_document->Modify(true);
}

void ibManagedFormEditor::OnMoveUp(wxCommandEvent&)   { MoveSelected(-1); }
void ibManagedFormEditor::OnMoveDown(wxCommandEvent&) { MoveSelected(+1); }
void ibManagedFormEditor::OnTestForm(wxCommandEvent&) { PreviewForm(); }

void ibManagedFormEditor::OnTreeSelChanged(wxTreeEvent&)
{
	ibManagedElement* el = SelectedElement();
	if (el == nullptr) {
		if (objectInspector != nullptr)
			objectInspector->SelectObject(nullptr);
		m_adapter.reset();
		return;
	}
	// Rebuild the adapter for the newly selected element and show it.
	m_adapter = std::make_unique<ibManagedElementProperty>(el, this);
	if (objectInspector != nullptr)
		objectInspector->SelectObject(m_adapter.get(), true);
}

void ibManagedFormEditor::OnTreeContextMenu(wxTreeEvent& event)
{
	if (event.GetItem().IsOk())
		m_tree->SelectItem(event.GetItem());

	wxMenu menu;
	menu.Append(ID_MFE_ADD_GROUP, _("Add group"));
	menu.Append(ID_MFE_ADD_FIELD, _("Add field"));
	menu.AppendSeparator();
	menu.Append(ID_MFE_DELETE, _("Delete"));
	menu.Append(ID_MFE_UP,     _("Move up"));
	menu.Append(ID_MFE_DOWN,   _("Move down"));

	const bool editable = IsEditable();
	menu.Enable(ID_MFE_ADD_GROUP, editable);
	menu.Enable(ID_MFE_ADD_FIELD, editable);
	menu.Enable(ID_MFE_DELETE, editable && SelectedElement() != nullptr);
	menu.Enable(ID_MFE_UP,     editable && SelectedElement() != nullptr);
	menu.Enable(ID_MFE_DOWN,   editable && SelectedElement() != nullptr);

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
		m_document->Modify(true);
}

void ibManagedFormEditor::PreviewForm()
{
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
	if (m_previewForm != nullptr)
		m_previewForm->DecrRef();
	m_previewForm = form;
	form->ShowForm(static_cast<ibDocument*>(m_document), false);
}
