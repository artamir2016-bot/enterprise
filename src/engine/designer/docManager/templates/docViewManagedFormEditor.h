#ifndef __DOC_VIEW_MANAGED_FORM_EDITOR_H__
#define __DOC_VIEW_MANAGED_FORM_EDITOR_H__

// ----------------------------------------------------------------------------
// Designer document/view for a MANAGED form (ibValueMetaObjectManagedForm).
// Registered for g_metaManagedFormCLSID in docManager.cpp. Thin: it hosts the
// element-tree editor (ibManagedFormEditor); there is no code page in the MVP.
// ----------------------------------------------------------------------------

#include "docManager/docManager.h"
#include "win/editor/managedFormEditor/managedFormEditor.h"

class ibManagedFormEditView : public ibMetaView {
public:

	ibManagedFormEditView() : ibMetaView(), m_editor(nullptr) {}

	ibManagedFormEditor* GetEditor() const { return m_editor; }

	virtual bool OnCreate(ibDocument* doc, long flags) override;
	virtual void OnUpdate(ibView* sender, wxObject* hint) override;
	virtual bool OnClose(bool deleteWindow = true) override;

private:

	ibManagedFormEditor* m_editor;

	wxDECLARE_DYNAMIC_CLASS(ibManagedFormEditView);
};

class ibManagedFormEditDocument : public ibMetaDocument {
public:

	ibManagedFormEditDocument() : ibMetaDocument() {}

	ibManagedFormEditor* GetEditor() const;

	virtual bool OnSaveDocument(const wxString& filename) override;

	wxDECLARE_NO_COPY_CLASS(ibManagedFormEditDocument);
	wxDECLARE_DYNAMIC_CLASS(ibManagedFormEditDocument);
};

#endif // __DOC_VIEW_MANAGED_FORM_EDITOR_H__
