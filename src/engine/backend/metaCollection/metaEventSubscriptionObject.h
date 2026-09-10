#ifndef __META_EVENT_SUBSCRIPTION_OBJECT_H__
#define __META_EVENT_SUBSCRIPTION_OBJECT_H__

// An event subscription — a configuration-level metaobject that binds a common-module handler to an
// event of one or more source metaobjects. It carries no module and no table of its own: it is a
// pure declaration of three things — WHICH objects (the source), WHICH event, and WHAT handler runs.
//
// LIFECYCLE. Plain ibValueMetaObject forwarding — there is no inner module to drive.

#include "metaObject.h"   // ibValueMetaObject
#include "backend/propertyManager/property/propertyRecord.h"   // ibPropertyRecord + ibMetaDescription
#include "backend/propertyManager/property/propertyString.h"   // ibPropertyString

class BACKEND_API ibValueMetaObjectEventSubscription : public ibValueMetaObject {
public:

	ibValueMetaObjectEventSubscription(const wxString& name = wxEmptyString, const wxString& synonym = wxEmptyString, const wxString& comment = wxEmptyString);

	// The source metaobjects the subscription listens to — an ibMetaDescription of metaIDs.
	ibMetaDescription& GetSourceDescription() const { return m_propertySource->GetValueAsMetaDesc(); }

	// The event name (e.g. "BeforeWrite").
	wxString GetEvent() const { return m_propertyEvent->GetValueAsString(); }

	// The handler, in "CommonModule.X.Method" form.
	wxString GetHandler() const { return m_propertyHandler->GetValueAsString(); }
	void SetHandler(const wxString& handler) { m_propertyHandler->SetValue(handler); }

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

	ibPropertyCategory* m_categoryEvent = ibPropertyObject::CreatePropertyCategory(wxT("Event"), _("Event"));
	ibPropertyRecord*   m_propertySource = ibPropertyObject::CreateProperty<ibPropertyRecord>(m_categoryEvent, wxT("Source"), _("Source"));
	ibPropertyString*   m_propertyEvent = ibPropertyObject::CreateProperty<ibPropertyString>(m_categoryEvent, wxT("Event"), _("Event"), wxEmptyString);
	ibPropertyString*   m_propertyHandler = ibPropertyObject::CreateProperty<ibPropertyString>(m_categoryEvent, wxT("Handler"), _("Handler"), wxEmptyString);

	friend class ibMetaData;
};

#endif // !__META_EVENT_SUBSCRIPTION_OBJECT_H__
