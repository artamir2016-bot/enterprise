////////////////////////////////////////////////////////////////////////////
//	Description : event subscription — binds a common-module handler to a source event
////////////////////////////////////////////////////////////////////////////

#include "metaEventSubscriptionObject.h"

#include "backend/metaData.h"

//***********************************************************************
//*                        Event subscription object                    *
//***********************************************************************

ibValueMetaObjectEventSubscription::ibValueMetaObjectEventSubscription(const wxString& name, const wxString& synonym, const wxString& comment)
	: ibValueMetaObject(name, synonym, comment)
{
}

//***********************************************************************
//*        lifecycle — plain forwarding (no inner module)               *
//***********************************************************************

bool ibValueMetaObjectEventSubscription::OnCreateMetaObject(ibMetaData* metaData, int flags)
{
	return ibValueMetaObject::OnCreateMetaObject(metaData, flags);
}

bool ibValueMetaObjectEventSubscription::OnLoadMetaObject(ibMetaData* metaData)
{
	return ibValueMetaObject::OnLoadMetaObject(metaData);
}

bool ibValueMetaObjectEventSubscription::OnSaveMetaObject(int flags)
{
	return ibValueMetaObject::OnSaveMetaObject(flags);
}

bool ibValueMetaObjectEventSubscription::OnDeleteMetaObject()
{
	return ibValueMetaObject::OnDeleteMetaObject();
}

bool ibValueMetaObjectEventSubscription::OnBeforeRunMetaObject(int flags)
{
	return ibValueMetaObject::OnBeforeRunMetaObject(flags);
}

bool ibValueMetaObjectEventSubscription::OnAfterRunMetaObject(int flags)
{
	return ibValueMetaObject::OnAfterRunMetaObject(flags);
}

bool ibValueMetaObjectEventSubscription::OnBeforeCloseMetaObject()
{
	return ibValueMetaObject::OnBeforeCloseMetaObject();
}

bool ibValueMetaObjectEventSubscription::OnAfterCloseMetaObject()
{
	return ibValueMetaObject::OnAfterCloseMetaObject();
}

//***********************************************************************
//*                          load & save from DB                        *
//***********************************************************************

bool ibValueMetaObjectEventSubscription::ReadData(const ibDataNode& node)
{
	if (!ibValueMetaObject::ReadData(node))
		return false;

	m_propertySource->SetNodeValue(node.GetProperty(m_propertySource->GetName()));
	m_propertyEvent->SetNodeValue(node.GetProperty(m_propertyEvent->GetName()));
	m_propertyHandler->SetNodeValue(node.GetProperty(m_propertyHandler->GetName()));
	return true;
}

bool ibValueMetaObjectEventSubscription::WriteData(ibDataNode& node) const
{
	if (!ibValueMetaObject::WriteData(node))
		return false;

	node.SetProperty(m_propertySource->GetName(),  m_propertySource->GetNodeValue());
	node.SetProperty(m_propertyEvent->GetName(),   m_propertyEvent->GetNodeValue());
	node.SetProperty(m_propertyHandler->GetName(), m_propertyHandler->GetNodeValue());
	return true;
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_TYPE_REGISTER(ibValueMetaObjectEventSubscription, "EventSubscription", g_metaEventSubscriptionCLSID);
