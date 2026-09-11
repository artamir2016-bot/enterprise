////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : enum factory 
////////////////////////////////////////////////////////////////////////////

#include "enumFactory.h"
#include "backend/propertyManager/propertyManager.h"

//*********************************************************************************************************
//*                                   Singleton class "enumFactory"                                       *
//*********************************************************************************************************

ibValueEnumFactory::ibValueEnumFactory() :
	ibValueDynamicMembers(ibValueTypes::TYPE_VALUE, true)
{
	m_members.Bind(this, &ibValueEnumFactory::FillMembers);
}

ibValueEnumFactory::~ibValueEnumFactory() {
}

// Russian aliases for SYSTEM enum TYPE names, so imported 1C modules (kept verbatim) reach them by
// their 1C name (ВидДвиженияНакопления -> AccumulationRecordType). Value names are aliased on the enum
// itself (AddEnumAlias). No UTF-8 BOM here and the build sets no /utf-8, so keys are byte-escaped via
// FromUTF8. A pair whose English target is not a registered enum is ignored.
static const std::vector<std::pair<wxString, wxString>>& EnumTypeRuAliases()
{
	static const std::vector<std::pair<wxString, wxString>> aliases = {
		{ wxString::FromUTF8("\xD0\x92\xD0\xB8\xD0\xB4\xD0\x94\xD0\xB2\xD0\xB8\xD0\xB6\xD0\xB5\xD0\xBD\xD0\xB8\xD1\x8F\xD0\x9D\xD0\xB0\xD0\xBA\xD0\xBE\xD0\xBF\xD0\xBB\xD0\xB5\xD0\xBD\xD0\xB8\xD1\x8F"),  // ВидДвиженияНакопления
		  wxT("AccumulationRecordType") },
	};
	return aliases;
}

void ibValueEnumFactory::FillMembers(ibMemberTable& helper) const
{
	for (auto& ctor : ibValue::GetListCtorsByType(ibCtorObjectType_object_enum)) {
		helper.AppendProp(ctor->GetClassName());
	}
	for (const auto& a : EnumTypeRuAliases())
		if (ibValue::IsRegisterCtor(a.second))
			helper.AppendProp(a.first);
}

bool ibValueEnumFactory::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	wxString strEnumeration = GetPropName(lPropNum);
	for (const auto& a : EnumTypeRuAliases())
		if (a.first == strEnumeration) { strEnumeration = a.second; break; }
	if (!ibValue::IsRegisterCtor(strEnumeration))
		return false;
	pvarPropVal = ibValue::CreateObject(strEnumeration);
	return true;
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

CONTEXT_TYPE_REGISTER(ibValueEnumFactory, "EnumManager", context_to_clsid("CO_ENMR"));
