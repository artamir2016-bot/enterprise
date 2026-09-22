////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : constants manager
////////////////////////////////////////////////////////////////////////////

#include "constantManager.h"


#include "backend/metaData.h"
#include "backend/objCtor.h"

ibClassID ibValueManagerDataObjectConstant::GetClassType() const
{
	const ibMetaData* metaData = m_metaObject->GetMetaData();
	wxASSERT(metaData);
	const ibCtorMetaValueType* clsFactory =
		metaData->GetTypeCtor(m_metaObject, ibCtorObjectMetaType::ibCtorObjectMetaType_Manager);
	wxASSERT(clsFactory);
	return clsFactory->GetClassType();
}

wxString ibValueManagerDataObjectConstant::GetClassName() const
{
	const ibMetaData* metaData = m_metaObject->GetMetaData();
	wxASSERT(metaData);
	const ibCtorMetaValueType* clsFactory =
		metaData->GetTypeCtor(m_metaObject, ibCtorObjectMetaType::ibCtorObjectMetaType_Manager);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

wxString ibValueManagerDataObjectConstant::GetString() const
{
	const ibMetaData* metaData = m_metaObject->GetMetaData();
	wxASSERT(metaData);
	const ibCtorMetaValueType* clsFactory =
		metaData->GetTypeCtor(m_metaObject, ibCtorObjectMetaType::ibCtorObjectMetaType_Manager);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

enum Func {
	enSet = 0,
	enGet
};

void ibValueManagerDataObjectConstant::FillManagerMethods(ibMemberTable& helper) const
{
	helper.AppendFunc(wxT("Set"), 1, wxT("Set(value : any)"));
	helper.AppendFunc(wxT("Get"), wxT("Get()"));
	// OES-RU: 1C aliases — Константы.X.Установить(v) / .Получить().
	helper.AliasMethod(wxString::FromUTF8("\xD0\xA3\xD1\x81\xD1\x82\xD0\xB0\xD0\xBD\xD0\xBE\xD0\xB2\xD0\xB8\xD1\x82\xD1\x8C"), wxT("Set"));  // Установить
	helper.AliasMethod(wxString::FromUTF8("\xD0\x9F\xD0\xBE\xD0\xBB\xD1\x83\xD1\x87\xD0\xB8\xD1\x82\xD1\x8C"), wxT("Get"));  // Получить
}

bool ibValueManagerDataObjectConstant::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	ibValuePtr<ibValueRecordDataObjectConstant> recordDataObjectValue(m_metaObject->CreateRecordDataObjectValue());

	switch (lMethodNum)
	{
	case enSet:
		recordDataObjectValue->SetConstValue(*paParams[0]);
		return true;
	case enGet:
		pvarRetValue = recordDataObjectValue->GetConstValue();
		return true;
	}

	return false;
}