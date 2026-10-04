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

class wxSplitterWindow;
class wxNotebook;
class wxListCtrl;
class wxListEvent;
class ibVisualHost;

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

	// Capture the current element tree as an undo point. Call BEFORE a mutation (the
	// property adapter calls this before it writes an edit; structural ops call it
	// before they change the tree). Clears the redo stack.
	void PushUndoSnapshot();

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
	// Build the 3-pane body: [elements tree | Attributes/Commands/Parameters notebook] over a
	// preview pane. Returns the top-level window to add under the toolbar.
	wxWindow* BuildBody(wxWindow* parent);
	void RebuildDataPanels();   // refill Attributes / Commands / Parameters from the working copy
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

	void OnUndo(wxCommandEvent& event);
	void OnRedo(wxCommandEvent& event);
	void Undo();
	void Redo();
	// Drop the inspector's hold on the current adapter (used before restoring a snapshot,
	// which invalidates every element pointer the adapter may hold).
	void ClearSelectionBinding();

	void OnTreeSelChanged(wxTreeEvent& event);
	void OnTreeContextMenu(wxTreeEvent& event);

	// Add a node of `kind` at the right place for the current selection, honouring the
	// containment rules (Column only inside a Table, Page only inside a Pages group,
	// everything else inside a Group/Page/root). Returns the created node (stable until the
	// next structural edit), or nullptr + a status message if the kind cannot be placed.
	// Flush the working copy onto the metaobject AND mark the document modified. Called after
	// every edit so the in-memory metaobject always reflects the tree — a config save / update
	// then persists it without depending on the editor-doc save firing first.
	void MarkDirty();

	ibManagedElement* AddElement(ibManagedNodeKind kind);
	// Resolve where a new node of `kind` goes: the container vector to push into. Returns
	// nullptr with a reason if the current selection forbids it.
	ibManagedElement* ResolveInsertParent(ibManagedNodeKind kind, wxString& reason) const;
	void MoveSelected(int dir);   // -1 up, +1 down
	void PreviewForm();           // open the compiled form in a MODAL window (the "Test form" button)
	// Rebuild the EMBEDDED preview in the bottom pane from the current element tree. Coalesced via
	// CallAfter so a burst of edits recompiles once. No-op if there's no owner / empty tree.
	void RefreshPreview();
	void DoRefreshPreview();

	ibMetaDocument*               m_document = nullptr;
	ibValueMetaObjectManagedForm* m_managed  = nullptr;

	// Working copy (the edited source of truth until Save).
	ibManagedElement                m_root;
	std::vector<ibManagedAttribute> m_attrs;

	// Undo/redo = whole-tree snapshots (the tree is small; this is simple and correct).
	std::vector<ibManagedElement> m_undo;
	std::vector<ibManagedElement> m_redo;
	static constexpr size_t kMaxUndo = 100;

	wxTreeCtrl* m_tree = nullptr;

	// 3-pane body (1C-style). The data notebook lists the form's data on the right; the preview
	// pane hosts the compiled form at the bottom (filled in a later increment).
	wxSplitterWindow* m_outerSplit = nullptr;   // body (top) over preview (bottom)
	wxSplitterWindow* m_topSplit   = nullptr;   // tree (left) | data notebook (right)
	wxNotebook*       m_dataBook   = nullptr;
	wxListCtrl*       m_attrList   = nullptr;    // Реквизиты
	wxListCtrl*       m_cmdList    = nullptr;    // Команды
	wxListCtrl*       m_paramList  = nullptr;    // Параметры
	wxWindow*         m_previewPane = nullptr;   // bottom container
	ibVisualHost*     m_previewHost = nullptr;   // embedded live-form host inside m_previewPane
	bool              m_refreshQueued = false;   // coalesce preview rebuilds

	// One live adapter at a time; rebuilt per selection, never handed dangling.
	std::unique_ptr<ibManagedElementProperty> m_adapter;

	// The form rendered in the EMBEDDED preview pane (ref-held; dropped on rebuild/close).
	ibValueForm* m_previewForm = nullptr;
	// The form opened by the MODAL "Test form" button — a separate object (one ibValueForm cannot
	// be both embedded and shown as a window).
	ibValueForm* m_testForm = nullptr;

	wxDECLARE_EVENT_TABLE();
};

#endif // __MANAGED_FORM_EDITOR_H__
