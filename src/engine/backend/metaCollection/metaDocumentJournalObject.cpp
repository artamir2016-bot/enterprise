////////////////////////////////////////////////////////////////////////////
//	Description : document journal — aggregates several document types into one list
////////////////////////////////////////////////////////////////////////////

#include "metaDocumentJournalObject.h"

#include "backend/metaData.h"

//***********************************************************************
//*                        Document journal object                      *
//***********************************************************************

ibValueMetaObjectDocumentJournal::ibValueMetaObjectDocumentJournal(const wxString& name, const wxString& synonym, const wxString& comment)
	: ibValueMetaObject(name, synonym, comment)
{
}

//***********************************************************************
//*        lifecycle — plain forwarding (no inner module)               *
//***********************************************************************

bool ibValueMetaObjectDocumentJournal::OnCreateMetaObject(ibMetaData* metaData, int flags)
{
	return ibValueMetaObject::OnCreateMetaObject(metaData, flags);
}

bool ibValueMetaObjectDocumentJournal::OnLoadMetaObject(ibMetaData* metaData)
{
	return ibValueMetaObject::OnLoadMetaObject(metaData);
}

bool ibValueMetaObjectDocumentJournal::OnSaveMetaObject(int flags)
{
	return ibValueMetaObject::OnSaveMetaObject(flags);
}

bool ibValueMetaObjectDocumentJournal::OnDeleteMetaObject()
{
	return ibValueMetaObject::OnDeleteMetaObject();
}

bool ibValueMetaObjectDocumentJournal::OnBeforeRunMetaObject(int flags)
{
	return ibValueMetaObject::OnBeforeRunMetaObject(flags);
}

bool ibValueMetaObjectDocumentJournal::OnAfterRunMetaObject(int flags)
{
	return ibValueMetaObject::OnAfterRunMetaObject(flags);
}

bool ibValueMetaObjectDocumentJournal::OnBeforeCloseMetaObject()
{
	return ibValueMetaObject::OnBeforeCloseMetaObject();
}

bool ibValueMetaObjectDocumentJournal::OnAfterCloseMetaObject()
{
	return ibValueMetaObject::OnAfterCloseMetaObject();
}

//***********************************************************************
//*                          load & save from DB                        *
//***********************************************************************

bool ibValueMetaObjectDocumentJournal::ReadData(const ibDataNode& node)
{
	if (!ibValueMetaObject::ReadData(node))
		return false;

	m_propertyRegisteredDocuments->SetNodeValue(node.GetProperty(m_propertyRegisteredDocuments->GetName()));
	return true;
}

bool ibValueMetaObjectDocumentJournal::WriteData(ibDataNode& node) const
{
	if (!ibValueMetaObject::WriteData(node))
		return false;

	node.SetProperty(m_propertyRegisteredDocuments->GetName(), m_propertyRegisteredDocuments->GetNodeValue());
	return true;
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_TYPE_REGISTER(ibValueMetaObjectDocumentJournal, "DocumentJournal", g_metaDocumentJournalCLSID);
