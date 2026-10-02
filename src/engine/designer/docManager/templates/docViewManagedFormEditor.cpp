#include "docViewManagedFormEditor.h"

wxIMPLEMENT_DYNAMIC_CLASS(ibManagedFormEditView, ibMetaView);
wxIMPLEMENT_DYNAMIC_CLASS(ibManagedFormEditDocument, ibMetaDocument);

// ----------------------------------------------------------------------------
// ibManagedFormEditView
// ----------------------------------------------------------------------------

bool ibManagedFormEditView::OnCreate(ibDocument* docBase, long flags)
{
	ibMetaDocument* doc = GetDocument();
	m_editor = new ibManagedFormEditor(doc, m_viewFrame, wxID_ANY);

	if (!m_editor->LoadForm()) {
		// Still open the (empty) editor — a managed form with no elements is valid.
	}
	return ibView::OnCreate(docBase, flags);
}

void ibManagedFormEditView::OnUpdate(ibView* sender, wxObject* hint)
{
	if (m_editor != nullptr)
		m_editor->LoadForm();
}

bool ibManagedFormEditView::OnClose(bool deleteWindow)
{
	if (deleteWindow) {
		GetFrame()->Destroy();
		SetFrame(nullptr);
	}

	if (ibMetaView::OnClose(deleteWindow)) {
		if (m_editor != nullptr) {
			m_editor->Destroy();
			m_editor = nullptr;
		}
		return true;
	}
	return false;
}

// ----------------------------------------------------------------------------
// ibManagedFormEditDocument
// ----------------------------------------------------------------------------

ibManagedFormEditor* ibManagedFormEditDocument::GetEditor() const
{
	ibView* view = GetFirstView();
	return view != nullptr ? wxDynamicCast(view, ibManagedFormEditView)->GetEditor() : nullptr;
}

bool ibManagedFormEditDocument::OnSaveDocument(const wxString& filename)
{
	// Copy the edited element tree back onto the metaobject; the actual byte-write
	// happens when the configuration is saved (WriteData serializes the Elements node).
	ibManagedFormEditor* editor = GetEditor();
	if (editor != nullptr)
		editor->SaveForm();
	return ibMetaDocument::OnSaveDocument(filename);
}
