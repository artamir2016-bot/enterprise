#include "calculationRegister.h"
#include "backend/serialize/dataBuilder.h"
#include "backend/system/value/valueDynamicList.h"   // ibValueDynamicList — the standard list migrates onto the universal dynamic list
#include "backend/metaData.h"
#include "backend/moduleManager/moduleManager.h"

//***********************************************************************
//*                         metaData                                    *
//***********************************************************************


/////////////////////////////////////////////////////////////////////////

ibValueMetaObjectCalculationRegister::ibValueMetaObjectCalculationRegister() : ibValueMetaObjectRegisterData(),
m_metaRecordManager(new ibValueMetaObjectRecordManager())
{
	//set default proc
	(*m_propertyObjectModule)->SetDefaultProcedure(wxT("BeforeWrite"), ibContentHelper::eProcedureHelper, { wxT("Cancel") });
	(*m_propertyObjectModule)->SetDefaultProcedure(wxT("OnWrite"), ibContentHelper::eProcedureHelper, { wxT("Cancel") });
}

ibValueMetaObjectCalculationRegister::~ibValueMetaObjectCalculationRegister()
{
	wxDELETE(m_metaRecordManager);
}

ibValueMetaObjectFormBase* ibValueMetaObjectCalculationRegister::GetDefaultFormByID(const ibFormID& id) const
{
	if (id == eFormRecord && m_propertyDefFormRecord->GetValueAsInteger() != wxNOT_FOUND) {
		return FindFormObjectByFilter(m_propertyDefFormRecord->GetValueAsInteger());
	}
	else if (id == eFormList && m_propertyDefFormList->GetValueAsInteger() != wxNOT_FOUND) {
		return FindFormObjectByFilter(m_propertyDefFormList->GetValueAsInteger());
	}

	return nullptr;
}

#pragma region _form_builder_h_
ibBackendValueForm* ibValueMetaObjectCalculationRegister::GetRecordForm(const wxString& strFormName, ibBackendControlFrame* ownerControl, const ibUniqueKey& formGuid) const
{
	return ibValueMetaObjectGenericData::CreateAndBuildForm(
		strFormName,
		ibValueMetaObjectCalculationRegister::eFormRecord,
		ownerControl, CreateRecordManagerObjectValue(),
		formGuid
	);
}

ibBackendValueForm* ibValueMetaObjectCalculationRegister::GetListForm(const wxString& strFormName, ibBackendControlFrame* ownerControl, const ibUniqueKey& formGuid) const
{
	return ibValueMetaObjectGenericData::CreateAndBuildForm(
		strFormName,
		ibValueMetaObjectCalculationRegister::eFormList,
		ownerControl, ibCreateList(GetQueryable(), HasPeriod() ? GetRegisterPeriod() : nullptr),   // migrated onto the universal dynamic list
		formGuid
	);
}
#pragma endregion

//***************************************************************************
//*                       Save & load metaData                              *
//***************************************************************************

bool ibValueMetaObjectCalculationRegister::WriteData(ibDataNode& node) const
{
	node.SetValue(m_propertyDefFormRecord->GetName(), GetGuidByID(m_propertyDefFormRecord->GetValueAsInteger()).str());
	node.SetValue(m_propertyDefFormList->GetName(), GetGuidByID(m_propertyDefFormList->GetValueAsInteger()).str());

	node.SetProperty(m_propertyPeriodicity->GetName(), m_propertyPeriodicity->GetNodeValue());
	node.SetProperty(m_propertyUseActionPeriod->GetName(), m_propertyUseActionPeriod->GetNodeValue());

	// Action-period standard attributes — persist their identity (metaID) so column names stay stable.
	node.SetProperty(m_propertyAttributeActionPeriodStart->GetName(),  m_propertyAttributeActionPeriodStart->GetNodeValue());
	node.SetProperty(m_propertyAttributeActionPeriodEnd->GetName(),    m_propertyAttributeActionPeriodEnd->GetNodeValue());
	node.SetProperty(m_propertyAttributeRegistrationPeriod->GetName(), m_propertyAttributeRegistrationPeriod->GetNodeValue());
	node.SetProperty(m_propertyUseBasePeriod->GetName(), m_propertyUseBasePeriod->GetNodeValue());
	node.SetProperty(m_propertyAttributeBasePeriodStart->GetName(), m_propertyAttributeBasePeriodStart->GetNodeValue());
	node.SetProperty(m_propertyAttributeBasePeriodEnd->GetName(),   m_propertyAttributeBasePeriodEnd->GetNodeValue());
	node.SetProperty(m_propertyAttributeActualActionPeriodStart->GetName(), m_propertyAttributeActualActionPeriodStart->GetNodeValue());
	node.SetProperty(m_propertyAttributeActualActionPeriodEnd->GetName(),   m_propertyAttributeActualActionPeriodEnd->GetNodeValue());

	node.SetProperty(m_propertyObjectModule->GetName(), m_propertyObjectModule->GetNodeValue());
	node.SetProperty(m_propertyManagerModule->GetName(), m_propertyManagerModule->GetNodeValue());

	return ibValueMetaObjectRegisterData::WriteData(node);
}

bool ibValueMetaObjectCalculationRegister::ReadData(const ibDataNode& node)
{
	m_propertyDefFormRecord->SetValue(GetIdByGuid(node.GetValue<wxString>(m_propertyDefFormRecord->GetName())));
	m_propertyDefFormList->SetValue(GetIdByGuid(node.GetValue<wxString>(m_propertyDefFormList->GetName())));

	m_propertyPeriodicity->SetNodeValue(node.GetProperty(m_propertyPeriodicity->GetName()));
	m_propertyUseActionPeriod->SetNodeValue(node.GetProperty(m_propertyUseActionPeriod->GetName()));

	m_propertyAttributeActionPeriodStart->SetNodeValue(node.GetProperty(m_propertyAttributeActionPeriodStart->GetName()));
	m_propertyAttributeActionPeriodEnd->SetNodeValue(node.GetProperty(m_propertyAttributeActionPeriodEnd->GetName()));
	m_propertyAttributeRegistrationPeriod->SetNodeValue(node.GetProperty(m_propertyAttributeRegistrationPeriod->GetName()));
	m_propertyUseBasePeriod->SetNodeValue(node.GetProperty(m_propertyUseBasePeriod->GetName()));
	m_propertyAttributeBasePeriodStart->SetNodeValue(node.GetProperty(m_propertyAttributeBasePeriodStart->GetName()));
	m_propertyAttributeBasePeriodEnd->SetNodeValue(node.GetProperty(m_propertyAttributeBasePeriodEnd->GetName()));
	m_propertyAttributeActualActionPeriodStart->SetNodeValue(node.GetProperty(m_propertyAttributeActualActionPeriodStart->GetName()));
	m_propertyAttributeActualActionPeriodEnd->SetNodeValue(node.GetProperty(m_propertyAttributeActualActionPeriodEnd->GetName()));

	m_propertyObjectModule->SetNodeValue(node.GetProperty(m_propertyObjectModule->GetName()));
	m_propertyManagerModule->SetNodeValue(node.GetProperty(m_propertyManagerModule->GetName()));

	return ibValueMetaObjectRegisterData::ReadData(node);
}

//***********************************************************************
//*                           read & save events                        *
//***********************************************************************

#include "backend/appData.h"

bool ibValueMetaObjectCalculationRegister::OnCreateMetaObject(ibMetaData* metaData, int flags)
{
	if (!ibValueMetaObjectRegisterData::OnCreateMetaObject(metaData, flags))
		return false;

	return (*m_propertyAttributeActionPeriodStart)->OnCreateMetaObject(metaData, flags) &&
		(*m_propertyAttributeActionPeriodEnd)->OnCreateMetaObject(metaData, flags) &&
		(*m_propertyAttributeRegistrationPeriod)->OnCreateMetaObject(metaData, flags) &&
		(*m_propertyAttributeBasePeriodStart)->OnCreateMetaObject(metaData, flags) &&
		(*m_propertyAttributeBasePeriodEnd)->OnCreateMetaObject(metaData, flags) &&
		(*m_propertyAttributeActualActionPeriodStart)->OnCreateMetaObject(metaData, flags) &&
		(*m_propertyAttributeActualActionPeriodEnd)->OnCreateMetaObject(metaData, flags) &&
		(*m_propertyManagerModule)->OnCreateMetaObject(metaData, flags) &&
		(*m_propertyObjectModule)->OnCreateMetaObject(metaData, flags);
}

bool ibValueMetaObjectCalculationRegister::OnLoadMetaObject(ibMetaData* metaData)
{
	if (!(*m_propertyManagerModule)->OnLoadMetaObject(metaData))
		return false;

	if (!(*m_propertyObjectModule)->OnLoadMetaObject(metaData))
		return false;

	if (!(*m_propertyAttributeActionPeriodStart)->OnLoadMetaObject(metaData)) return false;
	if (!(*m_propertyAttributeActionPeriodEnd)->OnLoadMetaObject(metaData)) return false;
	if (!(*m_propertyAttributeRegistrationPeriod)->OnLoadMetaObject(metaData)) return false;
	if (!(*m_propertyAttributeBasePeriodStart)->OnLoadMetaObject(metaData)) return false;
	if (!(*m_propertyAttributeBasePeriodEnd)->OnLoadMetaObject(metaData)) return false;
	if (!(*m_propertyAttributeActualActionPeriodStart)->OnLoadMetaObject(metaData)) return false;
	if (!(*m_propertyAttributeActualActionPeriodEnd)->OnLoadMetaObject(metaData)) return false;

	return ibValueMetaObjectRegisterData::OnLoadMetaObject(metaData);
}

bool ibValueMetaObjectCalculationRegister::OnSaveMetaObject(int flags)
{
	if (!(*m_propertyManagerModule)->OnSaveMetaObject(flags))
		return false;

	if (!(*m_propertyObjectModule)->OnSaveMetaObject(flags))
		return false;

	if (!(*m_propertyAttributeActionPeriodStart)->OnSaveMetaObject(flags)) return false;
	if (!(*m_propertyAttributeActionPeriodEnd)->OnSaveMetaObject(flags)) return false;
	if (!(*m_propertyAttributeRegistrationPeriod)->OnSaveMetaObject(flags)) return false;
	if (!(*m_propertyAttributeBasePeriodStart)->OnSaveMetaObject(flags)) return false;
	if (!(*m_propertyAttributeBasePeriodEnd)->OnSaveMetaObject(flags)) return false;
	if (!(*m_propertyAttributeActualActionPeriodStart)->OnSaveMetaObject(flags)) return false;
	if (!(*m_propertyAttributeActualActionPeriodEnd)->OnSaveMetaObject(flags)) return false;

	// A calculation register is always subordinate to a recorder, but at IMPORT (or before the posting
	// documents are linked) the recorder type is legitimately empty. The base treats that as a WARNING,
	// not a refusal (commonObject.cpp OnSaveMetaObject) — the register is created and simply cannot be
	// written to until a recorder appears. Delegate instead of refusing here.
	return ibValueMetaObjectRegisterData::OnSaveMetaObject(flags);
}

bool ibValueMetaObjectCalculationRegister::OnDeleteMetaObject()
{
	if (!(*m_propertyManagerModule)->OnDeleteMetaObject())
		return false;

	if (!(*m_propertyObjectModule)->OnDeleteMetaObject())
		return false;

	if (!(*m_propertyAttributeActionPeriodStart)->OnDeleteMetaObject()) return false;
	if (!(*m_propertyAttributeActionPeriodEnd)->OnDeleteMetaObject()) return false;
	if (!(*m_propertyAttributeRegistrationPeriod)->OnDeleteMetaObject()) return false;
	if (!(*m_propertyAttributeBasePeriodStart)->OnDeleteMetaObject()) return false;
	if (!(*m_propertyAttributeBasePeriodEnd)->OnDeleteMetaObject()) return false;
	if (!(*m_propertyAttributeActualActionPeriodStart)->OnDeleteMetaObject()) return false;
	if (!(*m_propertyAttributeActualActionPeriodEnd)->OnDeleteMetaObject()) return false;

	return ibValueMetaObjectRegisterData::OnDeleteMetaObject();
}

bool ibValueMetaObjectCalculationRegister::OnReloadMetaObject()
{
	if (auto* cc = m_metaData->GetCompileCache()) {

		ibValueRecordSetObjectCalculationRegister* recordSet = nullptr;
		if (cc->FindCompileModule(m_propertyObjectModule->GetMetaObject(), recordSet)) {
			if (!recordSet->InitializeObject())
				return false;
		}

		ibValueRecordManagerObjectCalculationRegister* recordManager = nullptr;
		if (cc->FindCompileModule(m_metaRecordManager, recordManager)) {
			if (!recordManager->InitializeObject())
				return false;
		}
	}

	return true;
}

#include "backend/objCtor.h"

bool ibValueMetaObjectCalculationRegister::OnBeforeRunMetaObject(int flags)
{
	if (!(*m_propertyManagerModule)->OnBeforeRunMetaObject(flags))
		return false;

	if (!(*m_propertyObjectModule)->OnBeforeRunMetaObject(flags))
		return false;

	registerSelection();

	return ibValueMetaObjectRegisterData::OnBeforeRunMetaObject(flags);
}

bool ibValueMetaObjectCalculationRegister::OnAfterRunMetaObject(int flags)
{
	if (!(*m_propertyManagerModule)->OnAfterRunMetaObject(flags))
		return false;

	if (!(*m_propertyObjectModule)->OnAfterRunMetaObject(flags))
		return false;


	if (auto* cc = m_metaData->GetCompileCache()) {

		if (ibValueMetaObjectRegisterData::OnAfterRunMetaObject(flags)) {

			if (!cc->AddCompileModule(m_metaRecordManager, [this]() -> ibValue* { return CreateRecordManagerObjectValue(); }))
				return false;

			if (!cc->AddCompileModule(m_propertyObjectModule->GetMetaObject(), [this]() -> ibValue* { return CreateRecordSetObjectValue(); }))
				return false;

			return true;
		}
	}

	return ibValueMetaObjectRegisterData::OnAfterRunMetaObject(flags);
}

bool ibValueMetaObjectCalculationRegister::OnBeforeCloseMetaObject()
{
	if (!(*m_propertyManagerModule)->OnBeforeCloseMetaObject())
		return false;

	if (!(*m_propertyObjectModule)->OnBeforeCloseMetaObject())
		return false;


	if (auto* cc = m_metaData->GetCompileCache()) {

		if (ibValueMetaObjectRegisterData::OnBeforeCloseMetaObject()) {

			cc->RemoveCompileModule(m_metaRecordManager);

			cc->RemoveCompileModule(m_propertyObjectModule->GetMetaObject());

			return true;
		}
	}

	return ibValueMetaObjectRegisterData::OnBeforeCloseMetaObject();
}

bool ibValueMetaObjectCalculationRegister::OnAfterCloseMetaObject()
{
	if (!(*m_propertyManagerModule)->OnAfterCloseMetaObject())
		return false;

	if (!(*m_propertyObjectModule)->OnAfterCloseMetaObject())
		return false;

	unregisterSelection();

	return ibValueMetaObjectRegisterData::OnAfterCloseMetaObject();
}

//***********************************************************************
//*                             form events                             *
//***********************************************************************

void ibValueMetaObjectCalculationRegister::OnCreateFormObject(ibValueMetaObjectFormBase* metaForm)
{
	if (metaForm->GetTypeForm() == ibValueMetaObjectCalculationRegister::eFormRecord
		&& m_propertyDefFormRecord->GetValueAsInteger() == wxNOT_FOUND)
	{
		m_propertyDefFormRecord->SetValue(metaForm->GetMetaID());
	}
	else if (metaForm->GetTypeForm() == ibValueMetaObjectCalculationRegister::eFormList
		&& m_propertyDefFormList->GetValueAsInteger() == wxNOT_FOUND)
	{
		m_propertyDefFormList->SetValue(metaForm->GetMetaID());
	}
}

void ibValueMetaObjectCalculationRegister::OnRemoveMetaForm(ibValueMetaObjectFormBase* metaForm)
{
	if (metaForm->GetTypeForm() == ibValueMetaObjectCalculationRegister::eFormRecord
		&& m_propertyDefFormRecord->GetValueAsInteger() == metaForm->GetMetaID())
	{
		m_propertyDefFormRecord->SetValue(wxNOT_FOUND);
	}
	else if (metaForm->GetTypeForm() == ibValueMetaObjectCalculationRegister::eFormList
		&& m_propertyDefFormList->GetValueAsInteger() == metaForm->GetMetaID())
	{
		m_propertyDefFormList->SetValue(wxNOT_FOUND);
	}
}

#include "calculationRegisterManager.h"

ibValueManagerDataObject* ibValueMetaObjectCalculationRegister::CreateManagerDataObjectValue() const
{
	return new ibValueManagerDataObjectCalculationRegister(this);
}

ibValueRecordSetObject* ibValueMetaObjectCalculationRegister::CreateRecordSetObjectRegValue(const ibUniqueKeyPair& uniqueKey) const
{
	if (auto* cc = m_metaData->GetCompileCache()) {
		ibValueRecordSetObject* pDataRef = nullptr;
		if (!cc->FindCompileModule(m_propertyObjectModule->GetMetaObject(), pDataRef)) {
			return new ibValueRecordSetObjectCalculationRegister(this, uniqueKey);
		}
		return pDataRef;
	}

	return new ibValueRecordSetObjectCalculationRegister(this, uniqueKey);
}

ibValueRecordManagerObject* ibValueMetaObjectCalculationRegister::CreateRecordManagerObjectRegValue(const ibUniqueKeyPair& uniqueKey) const
{
	if (auto* cc = m_metaData->GetCompileCache()) {
		ibValueRecordManagerObject* pDataRef = nullptr;
		if (!cc->FindCompileModule(m_metaRecordManager, pDataRef)) {
			return new ibValueRecordManagerObjectCalculationRegister(this, uniqueKey);
		}
		return pDataRef;
	}
	return new ibValueRecordManagerObjectCalculationRegister(this, uniqueKey);
}

ibSourceDataObject* ibValueMetaObjectCalculationRegister::CreateSourceObject(const ibValueMetaObjectFormBase* metaObject) const
{
	switch (metaObject->GetTypeForm())
	{
	case eFormRecord:
		return CreateRecordManagerObjectValue();
	case eFormList:
		return ibCreateList(GetQueryable(), HasPeriod() ? GetRegisterPeriod() : nullptr);   // migrated onto the universal dynamic list
	}

	return nullptr;
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

SYSTEM_TYPE_REGISTER(ibValueMetaObjectCalculationRegister::ibValueMetaObjectRecordManager, "CalculationRecordManager", system_to_clsid("MT_CRCM"));
METADATA_TYPE_REGISTER(ibValueMetaObjectCalculationRegister, "CalculationRegister", g_metaCalculationRegisterCLSID);
