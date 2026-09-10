#ifndef __META_DOCUMENT_JOURNAL_OBJECT_H__
#define __META_DOCUMENT_JOURNAL_OBJECT_H__

// A document journal — a configuration-level metaobject that aggregates several document types into
// one list. It carries no table and no module of its own: it is a declaration of WHICH documents it
// registers, held as an ibMetaDescription of their metaIDs.
//
// LIFECYCLE. Plain ibValueMetaObject forwarding — there is no inner module to drive.

#include "metaObject.h"   // ibValueMetaObject
#include "backend/propertyManager/property/propertyRecord.h"   // ibPropertyRecord + ibMetaDescription

class BACKEND_API ibValueMetaObjectDocumentJournal : public ibValueMetaObject {
public:

	ibValueMetaObjectDocumentJournal(const wxString& name = wxEmptyString, const wxString& synonym = wxEmptyString, const wxString& comment = wxEmptyString);

	// The document types this journal aggregates — an ibMetaDescription of their metaIDs.
	ibMetaDescription& GetRegisteredDocuments() const { return m_propertyRegisteredDocuments->GetValueAsMetaDesc(); }

	// Hosts no children.
	virtual ibClassID ResolveChild(const ibClassID&) const override { return 0; }

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	//lifecycle — plain forwarding (no inner module)
	virtual bool OnCreateMetaObject(ibMetaData* metaData, int flags);
	virtual bool OnLoadMetaObject(ibMetaData* metaData);
	virtual bool OnSaveMetaObject(int flags);
	virtual bool OnDeleteMetaObject();

	virtual bool OnBeforeRunMetaObject(int flags);
	virtual bool OnAfterRunMetaObject(int flags);
	virtual bool OnBeforeCloseMetaObject();
	virtual bool OnAfterCloseMetaObject();

protected:

	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

private:

	ibPropertyCategory* m_categoryData = ibPropertyObject::CreatePropertyCategory(wxT("Data"), _("Data"));
	ibPropertyRecord*   m_propertyRegisteredDocuments = ibPropertyObject::CreateProperty<ibPropertyRecord>(m_categoryData, wxT("RegisteredDocuments"), _("Registered documents"));

	friend class ibMetaData;
};

#endif // !__META_DOCUMENT_JOURNAL_OBJECT_H__
