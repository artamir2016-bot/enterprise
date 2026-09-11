#ifndef _ACCUMULATION_REGISTER_ENUM_H__
#define _ACCUMULATION_REGISTER_ENUM_H__

enum ibRegisterType {
	eBalances,
	eTurnovers
};

enum ibRecordType {
	eExpense,
	eReceipt
};

#pragma region enumeration
#include "backend/compiler/enumUnit.h"
class ibValueEnumAccumulationRegisterType : public ibValueEnumeration<ibRegisterType> {
	public:
	ibValueEnumAccumulationRegisterType() : ibValueEnumeration() {}
	//ibValueEnumAccumulationRegisterType(ibRegisterType mode) : ibValueEnumeration(mode) {}

	virtual void CreateEnumeration() {
		AddEnumeration(ibRegisterType::eBalances, wxT("Balances"), _("Balances"));
		AddEnumeration(ibRegisterType::eTurnovers, wxT("Turnovers"), _("Turnovers"));
	}
};
class ibValueEnumAccumulationRegisterRecordType : public ibValueEnumeration<ibRecordType> {
	public:
	static ibValue CreateDefEnumValue() {
		return ibValue::CreateEnumObject<ibValueEnumAccumulationRegisterRecordType>(ibRecordType::eExpense);
	}

	ibValueEnumAccumulationRegisterRecordType() : ibValueEnumeration() {}
	//ibValueEnumAccumulationRegisterRecordType(ibRecordType recordType) : ibValueEnumeration(recordType) {}

	virtual void CreateEnumeration() {
		AddEnumeration(eExpense, wxT("Expense"), _("Expense"));
		AddEnumeration(eReceipt, wxT("Receipt"), _("Receipt"));
		// 1C Russian value names so imported modules resolve ВидДвиженияНакопления.Расход / .Приход.
		// No UTF-8 BOM here and the build sets no /utf-8 -> byte-escape via FromUTF8.
		AddEnumAlias(eExpense, wxString::FromUTF8("\xD0\xA0\xD0\xB0\xD1\x81\xD1\x85\xD0\xBE\xD0\xB4"));   // Расход
		AddEnumAlias(eReceipt, wxString::FromUTF8("\xD0\x9F\xD1\x80\xD0\xB8\xD1\x85\xD0\xBE\xD0\xB4"));   // Приход
	}
};
constexpr ibClassID g_enumRecordTypeCLSID = enum_to_clsid("EN_RETP");
#pragma endregion 

#endif
