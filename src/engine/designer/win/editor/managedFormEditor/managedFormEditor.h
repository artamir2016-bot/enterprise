#ifndef __MANAGED_FORM_EDITOR_H__
#define __MANAGED_FORM_EDITOR_H__

// ----------------------------------------------------------------------------
// ibManagedFormEditor — the Designer editor widget for a MANAGED form
// (ibValueMetaObjectManagedForm). It edits the declarative ELEMENT TREE
// (ibManagedElement), the form's source of truth, NOT the compiled control
// tree. Layout: a small toolbar (Add Group / Add Field / Delete / Up / Down /
// Test form) over a wxTreeCtrl of the element tree. The selected element edits
// in the shared objectInspector via ibManagedElementProperty. "Test form"
// compiles the tree to the existing control-tree blob and ShowForm()s it (the
// proven runtime path). An embedded live canvas is a later increment.
//
// The editor holds a WORKING COPY of the element tree, so closing without
// saving leaves the metaobject untouched; Save writes the copy back.
// ----------------------------------------------------------------------------

#include <wx/panel.h>
#include <wx/treectrl.h>

#include <memory>
#include <vector>

#include "backend/managedForm/managedElement.h"

class ibMetaDocument;
class ibValueMetaObjectManagedForm;
class ibValueForm;
class ibManagedElementProperty;

class ibManagedFormEditor : public wxPanel {
public:

	ibManagedFormEditor(ibMetaDocument* document, wxWindow* parent, wxWindowID id = wxID_ANY);
	virtual ~ibManagedFormEditor();

	// Load the element tree from the metaobject into the working copy + tree.
	bool LoadForm();
	// Write the working copy back onto the metaobject + mark the document modified.
	bool SaveForm();

	// Editability gate — read from the managed metaobject (read-only config => false).
	bool IsEditable() const;

	// Called by the property adapter after an element property edit: relabel the tree
	// node + mark modified. (No preview recompile here — preview is on demand.)
	void OnElementChanged(ibManagedElement* element);

	// The binding targets the owner object offers: attribute names + "Section.Column"
	// paths (mirrors the owner walk in CompileElementsToFormData). Drives the DataPath
	// picker in the property adapter. Empty when there is no resolvable owner.
	std::vector<wxString> AvailableBindings() const;

private:

	// Tree item payload: a raw pointer into the working copy's element tree.
	// Valid only between full rebuilds (vector reallocation invalidates it), so any
	// STRUCTURAL edit rebuilds the whole tree before the next interaction.
	class ElementItemData : public wxTreeItemData {
	public:
		explicit ElementItemData(ibManagedElement* el) : m_element(el) {}
		ibManagedElement* m_element;
	};

	void BuildToolbar(wxSizer* sizer);
	void RebuildTree();
	void AddTreeNode(const wxTreeItemId& parentItem, ibManagedElement* el);
	wxString ElementLabel(const ibManagedElement* el) const;

	ibManagedElement* SelectedElement() const;
	// Locate a node's parent vector + index by pointer identity. Returns false for the root.
	bool FindParent(ibManagedElement& root, const ibManagedElement* target,
		ibManagedElement*& outParent, size_t& outIndex) const;
	// The nearest ancestor-or-self of `node` whose kind is `kind` (nullptr if none).
	ibManagedElement* FindAncestorOfKind(ibManagedElement* node, ibManagedNodeKind kind) const;
	wxString UniqueName(const wxString& base) const;
	static wxString NamePrefixFor(ibManagedNodeKind kind);

	// Toolbar / context handlers.
	void OnAddElement(wxCommandEvent& event);   // all Add-* buttons/menu items map here
	void OnDelete(wxCommandEvent& event);
	void OnMoveUp(wxCommandEvent& event);
	void OnMoveDown(wxCommandEvent& event);
	void OnTestForm(wxCommandEvent& event);

	void OnTreeSelChanged(wxTreeEvent& event);
	void OnTreeContextMenu(wxTreeEvent& event);

	// Add a node of `kind` at the right place for the current selection, honouring the
	// containment rules (Column only inside a Table, Page only inside a Pages group,
	// everything else inside a Group/Page/root). Returns the created node (stable until the
	// next structural edit), or nullptr + a status message if the kind cannot be placed.
	ibManagedElement* AddElement(ibManagedNodeKind kind);
	// Resolve where a new node of `kind` goes: the container vector to push into. Returns
	// nullptr with a reason if the current selection forbids it.
	ibManagedElement* ResolveInsertParent(ibManagedNodeKind kind, wxString& reason) const;
	void MoveSelected(int dir);   // -1 up, +1 down
	void PreviewForm();

	ibMetaDocument*               m_document = nullptr;
	ibValueMetaObjectManagedForm* m_managed  = nullptr;

	// Working copy (the edited source of truth until Save).
	ibManagedElement                m_root;
	std::vector<ibManagedAttribute> m_attrs;

	wxTreeCtrl* m_tree = nullptr;

	// One live adapter at a time; rebuilt per selection, never handed dangling.
	std::unique_ptr<ibManagedElementProperty> m_adapter;

	// The last ShowForm()ed preview form (ref-held; dropped on replace/close).
	ibValueForm* m_previewForm = nullptr;

	wxDECLARE_EVENT_TABLE();
};

#endif // __MANAGED_FORM_EDITOR_H__
