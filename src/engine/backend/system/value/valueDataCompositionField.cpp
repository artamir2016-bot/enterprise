////////////////////////////////////////////////////////////////////////////
//	Description : data-composition field -- a field addressed by its path
////////////////////////////////////////////////////////////////////////////

#include "valueDataCompositionField.h"
#include "backend/stringUtils.h"

//////////////////////////////////////////////////////////////////////

ibValueDataCompositionField::ibValueDataCompositionField()
	: ibValueStaticMembers(ibValueTypes::TYPE_VALUE)
{
}

ibValueDataCompositionField::ibValueDataCompositionField(const wxString& field)
	: ibValueStaticMembers(ibValueTypes::TYPE_VALUE), m_field(field)
{
}

bool ibValueDataCompositionField::Init(ibValue** paParams, const long lSizeArray)
{
	// Новый ПолеКомпоновкиДанных(ИмяПоля) — the path is required; an empty ctor yields an empty field.
	if (lSizeArray < 1)
		return true;
	m_field = paParams[0]->GetString();
	return true;
}

enum
{
	eField
};

void ibValueDataCompositionField_BindNames(ibValue::ibMemberTable& helper, const ibValue* /*ctx*/)
{
	helper.AppendProp(wxT("Field"));
}

bool ibValueDataCompositionField::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	switch (lPropNum)
	{
	case eField:
		m_field = varPropVal.GetString();
		return true;
	}
	return false;
}

bool ibValueDataCompositionField::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	switch (lPropNum)
	{
	case eField:
		pvarPropVal = m_field;
		return true;
	}
	return false;
}

bool ibValueDataCompositionField::CompareValueEQ(const ibValue& cParam) const
{
	ibValueDataCompositionField* other = dynamic_cast<ibValueDataCompositionField*>(cParam.GetRef());
	if (other != nullptr)
		return stringUtils::CompareString(m_field, other->m_field);
	return false;
}

bool ibValueDataCompositionField::CompareValueNE(const ibValue& cParam) const
{
	return !CompareValueEQ(cParam);
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

VALUE_TYPE_REGISTER(ibValueDataCompositionField, "DataCompositionField");
