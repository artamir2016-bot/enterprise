#include "managedFormEditor.h"
#include "managedElementProperty.h"

#include <wx/sizer.h>
#include <wx/button.h>
#include <wx/menu.h>
#include <wx/statline.h>
#include <wx/log.h>

#include "backend/metaCollection/genericData.h"                   // ibValueMetaObjectGenericData
#include "backend/metaCollection/attribute/metaAttributeObject.h" // ibValueMetaObjectAttribute
#include "backend/metaCollection/partial/commonObject.h"          // hierarchy ref Code/Description
#include "backend/metaCollection/metaObject.h"                    // g_metaTableRefCLSID

#include "frontend/docView/docView.h"
#include "frontend/mainFrame/objinspect/objinspect.h"
#include "frontend/visualView/ctrl/form.h"
#include "backend/metaCollection/metaFormObject.h"

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
	ID_MFE_TEST,
	ID_MFE_TREE,
};

wxBEGIN_EVENT_TABLE(ibManagedFormEditor, wxPanel)
EVT_BUTTON(ID_MFE_DELETE,    ibManagedFormEditor::OnDelete)
EVT_BUTTON(ID_MFE_UP,        ibManagedFormEditor::OnMoveUp)
EVT_BUTTON(ID_MFE_DOWN,      ibManagedFormEditor::OnMoveDown)
EVT_BUTTON(ID_MFE_TEST,      ibManagedFormEditor::OnTestForm)
EVT_MENU(ID_MFE_DELETE,      ibManagedFormEditor::OnDelete)
EVT_MENU(ID_MFE_UP,          ibManagedFormEditor::OnMoveUp)
EVT_MENU(ID_MFE_DOWN,        ibManagedFormEditor::OnMoveDown)
// Every Add-* id (buttons AND menu items) funnels into OnAddElement.
EVT_COMMAND_RANGE(ID_MFE_ADD_GROUP, ID_MFE_ADD__LAST, wxEVT_BUTTON, ibManagedFormEditor::OnAddElement)
EVT_COMMAND_RANGE(ID_MFE_ADD_GROUP, ID_MFE_ADD__LAST, wxEVT_MENU,   ibManagedFormEditor::OnAddElement)
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
	// Common kinds on the toolbar; the full set (Column / Pages / Page / Decoration) lives
	// in the tree context menu so the bar stays readable.
	wxBoxSizer* bar = new wxBoxSizer(wxHORIZONTAL);
	bar->Add(new wxButton(this, ID_MFE_ADD_GROUP,  _("Add group")),  0, wxALL, 2);
	bar->Add(new wxButton(this, ID_MFE_ADD_FIELD,  _("Add field")),  0, wxALL, 2);
	bar->Add(new wxButton(this, ID_MFE_ADD_TABLE,  _("Add table")),  0, wxALL, 2);
	bar->Add(new wxButton(this, ID_MFE_ADD_BUTTON, _("Add button")), 0, wxALL, 2);
	bar->Add(new wxStaticLine(this, wxID_ANY, wxDefaultPosition, wxSize(2, -1), wxLI_VERTICAL), 0, wxEXPAND | wxALL, 2);
	bar->Add(new wxButton(this, ID_MFE_DELETE,     _("Delete")),     0, wxALL, 2);
	bar->Add(new wxButton(this, ID_MFE_UP,         _("Up")),         0, wxALL, 2);
	bar->Add(new wxButton(this, ID_MFE_DOWN,       _("Down")),       0, wxALL, 2);
	bar->AddStretchSpacer(1);
	bar->Add(new wxButton(this, ID_MFE_TEST,       _("Test form")),  0, wxALL, 2);
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
	m_document->Modify(true);
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
	std::swap(parent->children[idx], parent->children[newIdx]);
	RebuildTree();
	m_document->Modify(true);
}

void ibManagedFormEditor::OnAddElement(wxCommandEvent& event)
{
	const ibManagedNodeKind kind = KindForAddId(event.GetId());
	ibManagedElement* created = AddElement(kind);
	if (created != nullptr && event.GetId() == ID_MFE_ADD_PAGES) {
		// "Add pages" creates a Group that presents as a notebook.
		created->representation = ibGroupRepresentation::Pages;
		RebuildTree();
		m_document->Modify(true);
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
		m_document->Modify(true);
}

std::vector<wxString> ibManagedFormEditor::AvailableBindings() const
{
	// The owner object's attributes + "Section.Column" paths — the same set the compiler's
	// resolver accepts (see CompileElementsToFormData). An object managed form binds through
	// the owner; a managed form with no business owner offers nothing.
	std::vector<wxString> out;
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
