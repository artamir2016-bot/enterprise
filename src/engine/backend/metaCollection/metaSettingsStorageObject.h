#ifndef __META_SETTINGS_STORAGE_OBJECT_H__
#define __META_SETTINGS_STORAGE_OBJECT_H__

// A settings storage — a configuration-level metaobject that owns ONE manager module and nothing
// else. It carries no table and no schedule; its whole purpose is to host the module in which a
// configuration's settings save/load/delete logic lives. The module is a MANAGER module (registered
// with the module manager, compiled once with the session's modules), mirroring the scheduled job's
// own choice for exactly the same reason: the entry points are type-level operations.
//
// LIFECYCLE. This object only forwards to the inner module (its own registration on run, withdrawal
// on close); it adds no registration of its own.

#include "metaModuleObject.h"   // ibPropertyInnerModule + ibValueMetaObjectManagerModule

class BACKEND_API ibValueMetaObjectSettingsStorage : public ibValueMetaObject {
public:

	ibValueMetaObjectSettingsStorage(const wxString& name = wxEmptyString, const wxString& synonym = wxEmptyString, const wxString& comment = wxEmptyString);

	// The storage module. A manager module, so it is name-resolvable in a session.
	const ibValueMetaObjectManagerModule* GetStorageModule() const { return m_propertyStorageModule->GetMetaObject(); }

	// Hosts no children — a settings storage is code, nothing else.
	virtual ibClassID ResolveChild(const ibClassID&) const override { return 0; }

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	//lifecycle — forward to the inner module
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

	// The storage module — a MANAGER module (registered, compiled with the session).
	ibPropertyInnerModule<ibValueMetaObjectManagerModule>* m_propertyStorageModule =
		ibPropertyObject::CreateProperty<ibPropertyInnerModule<ibValueMetaObjectManagerModule>>(
			m_categoryContext, wxT("StorageModule"), _("Storage module"));

	friend class ibMetaData;
};

#endif // !__META_SETTINGS_STORAGE_OBJECT_H__
