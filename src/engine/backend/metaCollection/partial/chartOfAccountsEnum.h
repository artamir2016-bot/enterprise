#ifndef _CHART_OF_ACCOUNTS_ENUM_H__
#define _CHART_OF_ACCOUNTS_ENUM_H__

enum ibAccountType {
	eActive,
	ePassive,
	eActivePassive
};

#pragma region enumeration
#include "backend/compiler/enumUnit.h"
class ibValueEnumAccountType : public ibValueEnumeration<ibAccountType> {
	public:
	static ibValue CreateDefEnumValue() {
		return ibValue::CreateEnumObject<ibValueEnumAccountType>(ibAccountType::eActive);
	}

	ibValueEnumAccountType() : ibValueEnumeration() {}

	virtual void CreateEnumeration() {
		AddEnumeration(ibAccountType::eActive, wxT("Active"), _("Active"));
		AddEnumeration(ibAccountType::ePassive, wxT("Passive"), _("Passive"));
		AddEnumeration(ibAccountType::eActivePassive, wxT("ActivePassive"), _("Active/Passive"));
		// OES-RU: 1C ВидСчета member names. Byte-escaped UTF-8 (no BOM, no /utf-8).
		AddEnumAlias(ibAccountType::eActive,        wxString::FromUTF8("\xD0\x90\xD0\xBA\xD1\x82\xD0\xB8\xD0\xB2\xD0\xBD\xD1\x8B\xD0\xB9"));                                         // Активный
		AddEnumAlias(ibAccountType::ePassive,       wxString::FromUTF8("\xD0\x9F\xD0\xB0\xD1\x81\xD1\x81\xD0\xB8\xD0\xB2\xD0\xBD\xD1\x8B\xD0\xB9"));                                 // Пассивный
		AddEnumAlias(ibAccountType::eActivePassive, wxString::FromUTF8("\xD0\x90\xD0\xBA\xD1\x82\xD0\xB8\xD0\xB2\xD0\xBD\xD0\xBE\xD0\x9F\xD0\xB0\xD1\x81\xD1\x81\xD0\xB8\xD0\xB2\xD0\xBD\xD1\x8B\xD0\xB9")); // АктивноПассивный
	}
};
constexpr ibClassID g_enumAccountTypeCLSID = enum_to_clsid("EN_ACTP");
#pragma endregion

#endif
