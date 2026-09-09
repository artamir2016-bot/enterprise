////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : calculation register manager
////////////////////////////////////////////////////////////////////////////

#include "calculationRegisterManager.h"
#include "backend/metaData.h"

#include "commonObject.h"


const ibValueMetaObjectCommonModule* ibValueManagerDataObjectCalculationRegister::GetManagerModule() const {
	return m_metaObject->GetManagerModule();
}

enum {
	eCreateRecordSet,
	eCreateRecordManager,
	eCreateRecordKey,
	eGet,
	eGetDisplacement,
	eGetBase,
	eSelect,
	eGetForm,
	eGetRecordForm,
	eGetListForm,
	eGetTemplate,
};

void ibValueManagerDataObjectCalculationRegister::FillManagerMethods(ibMemberTable& helper) const
{
	helper.AppendFunc(wxT("CreateRecordSet"), wxT("CreateRecordSet()"));
	helper.AppendFunc(wxT("CreateRecordManager"), wxT("CreateRecordManager()"));
	helper.AppendFunc(wxT("CreateRecordKey"), wxT("CreateRecordKey()"));
	// TWO ARGUMENTS, because there are two forms: Get(filter) and Get(period, filter).
	helper.AppendFunc(wxT("Get"), 2, wxT("Get(Period, Filter...)"));
	helper.AppendFunc(wxT("GetDisplacement"), 1, wxT("GetDisplacement(Filter...)"));
	helper.AppendFunc(wxT("GetBase"), 2, wxT("GetBase(BaseRegister, Filter...)"));
	helper.AppendFunc(wxT("Select"), wxT("Select()"));
	helper.AppendFunc(wxT("GetForm"), 3, wxT("GetForm(string, owner, guid)"));
	helper.AppendFunc(wxT("GetRecordForm"), 3, wxT("GetRecordForm(string, owner, guid)"));
	helper.AppendFunc(wxT("GetListForm"), 3, wxT("GetListForm(string, owner, guid)"));
	helper.AppendFunc(wxT("GetTemplate"), 1, wxT("GetTemplate(string)"));
}

#include "selector/objectSelector.h"

bool ibValueManagerDataObjectCalculationRegister::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	switch (lMethodNum)
	{
	case eCreateRecordSet:
		pvarRetValue = m_metaObject->CreateRecordSetObjectValue();
		return true;
	case eCreateRecordManager:
		pvarRetValue = m_metaObject->CreateRecordManagerObjectValue();
		return true;
	case eCreateRecordKey:
		pvarRetValue = new ibValueRecordKeyObject(m_metaObject);
		return true;
	case eGet:
		pvarRetValue = lSizeArray > 1 ?
			ibValueManagerDataObjectCalculationRegister::Get(*paParams[0], *paParams[1])
			: lSizeArray > 0 ?
			ibValueManagerDataObjectCalculationRegister::Get(*paParams[0]) :
			ibValueManagerDataObjectCalculationRegister::Get();
		return true;
	case eGetDisplacement:
		pvarRetValue = lSizeArray > 0 ?
			ibValueManagerDataObjectCalculationRegister::GetDisplacement(*paParams[0])
			: ibValueManagerDataObjectCalculationRegister::GetDisplacement();
		return true;
	case eGetBase:
		pvarRetValue = lSizeArray > 1 ?
			ibValueManagerDataObjectCalculationRegister::GetBase(*paParams[0], *paParams[1])
			: lSizeArray > 0 ?
			ibValueManagerDataObjectCalculationRegister::GetBase(*paParams[0])
			: ibValue();
		return true;
	case eSelect:
		pvarRetValue = new ibValueSelectorRegisterDataObject(m_metaObject);
		return true;
	case eGetForm:
	{
		ibValueGuid* guidVal = lSizeArray > 2 ? paParams[2]->ConvertToType<ibValueGuid>() : nullptr;
		pvarRetValue = m_metaObject->GetGenericForm(paParams[0]->GetString(),
			lSizeArray > 1 ? paParams[1]->ConvertToType<ibBackendControlFrame>() : nullptr,
			guidVal ? ((ibGuid)*guidVal) : ibGuid());
		return true;
	}
	case eGetRecordForm:
	{
		ibValueRecordKeyObject* keyVal = lSizeArray > 2 ? paParams[2]->ConvertToType<ibValueRecordKeyObject>() : nullptr;
		pvarRetValue = m_metaObject->GetRecordForm(paParams[0]->GetString(),
			lSizeArray > 1 ? paParams[1]->ConvertToType<ibBackendControlFrame>() : nullptr,
			keyVal ? keyVal->GetUniqueKey() : wxNullUniquePairKey);
		return true;
	}
	case eGetListForm:
	{
		ibValueGuid* guidVal = lSizeArray > 2 ? paParams[2]->ConvertToType<ibValueGuid>() : nullptr;
		pvarRetValue = m_metaObject->GetListForm(paParams[0]->GetString(),
			lSizeArray > 1 ? paParams[1]->ConvertToType<ibBackendControlFrame>() : nullptr,
			guidVal ? ((ibGuid)*guidVal) : ibGuid());
		return true;
	}
	case eGetTemplate:
		pvarRetValue = m_metaObject->GetTemplate(paParams[0]->GetString());
		return true;
	}

	return ibValueManagerDataObject::CallAsFunc(lMethodNum, pvarRetValue, paParams, lSizeArray);
}
