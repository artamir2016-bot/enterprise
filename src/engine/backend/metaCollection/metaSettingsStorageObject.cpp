////////////////////////////////////////////////////////////////////////////
//	Description : settings storage — a config-level metaobject owning one manager module
////////////////////////////////////////////////////////////////////////////

#include "metaSettingsStorageObject.h"

#include "backend/metaData.h"

//***********************************************************************
//*                        Settings storage object                      *
//***********************************************************************

ibValueMetaObjectSettingsStorage::ibValueMetaObjectSettingsStorage(const wxString& name, const wxString& synonym, const wxString& comment)
	: ibValueMetaObject(name, synonym, comment)
{
}

//***********************************************************************
//*        lifecycle — forward to the inner module                      *
//***********************************************************************

bool ibValueMetaObjectSettingsStorage::OnCreateMetaObject(ibMetaData* metaData, int flags)
{
	if (!ibValueMetaObject::OnCreateMetaObject(metaData, flags))
		return false;
	return (*m_propertyStorageModule)->OnCreateMetaObject(metaData, flags);
}

bool ibValueMetaObjectSettingsStorage::OnLoadMetaObject(ibMetaData* metaData)
{
	if (!(*m_propertyStorageModule)->OnLoadMetaObject(metaData))
		return false;
	return ibValueMetaObject::OnLoadMetaObject(metaData);
}

bool ibValueMetaObjectSettingsStorage::OnSaveMetaObject(int flags)
{
	if (!(*m_propertyStorageModule)->OnSaveMetaObject(flags))
		return false;
	return ibValueMetaObject::OnSaveMetaObject(flags);
}

bool ibValueMetaObjectSettingsStorage::OnDeleteMetaObject()
{
	if (!(*m_propertyStorageModule)->OnDeleteMetaObject())
		return false;
	return ibValueMetaObject::OnDeleteMetaObject();
}

bool ibValueMetaObjectSettingsStorage::OnBeforeRunMetaObject(int flags)
{
	if (!(*m_propertyStorageModule)->OnBeforeRunMetaObject(flags))
		return false;
	return ibValueMetaObject::OnBeforeRunMetaObject(flags);
}

bool ibValueMetaObjectSettingsStorage::OnAfterRunMetaObject(int flags)
{
	if (!(*m_propertyStorageModule)->OnAfterRunMetaObject(flags))
		return false;
	return ibValueMetaObject::OnAfterRunMetaObject(flags);
}

bool ibValueMetaObjectSettingsStorage::OnBeforeCloseMetaObject()
{
	if (!(*m_propertyStorageModule)->OnBeforeCloseMetaObject())
		return false;
	return ibValueMetaObject::OnBeforeCloseMetaObject();
}

bool ibValueMetaObjectSettingsStorage::OnAfterCloseMetaObject()
{
	if (!(*m_propertyStorageModule)->OnAfterCloseMetaObject())
		return false;
	return ibValueMetaObject::OnAfterCloseMetaObject();
}

//***********************************************************************
//*                          load & save from DB                        *
//***********************************************************************

bool ibValueMetaObjectSettingsStorage::ReadData(const ibDataNode& node)
{
	if (!ibValueMetaObject::ReadData(node))
		return false;

	m_propertyStorageModule->SetNodeValue(node.GetProperty(m_propertyStorageModule->GetName()));
	return true;
}

bool ibValueMetaObjectSettingsStorage::WriteData(ibDataNode& node) const
{
	if (!ibValueMetaObject::WriteData(node))
		return false;

	node.SetProperty(m_propertyStorageModule->GetName(), m_propertyStorageModule->GetNodeValue());
	return true;
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_TYPE_REGISTER(ibValueMetaObjectSettingsStorage, "SettingsStorage", g_metaSettingsStorageCLSID);
