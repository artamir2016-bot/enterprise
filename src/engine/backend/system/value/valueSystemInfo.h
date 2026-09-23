#ifndef __VALUE_SYSTEM_INFO_H__
#define __VALUE_SYSTEM_INFO_H__

#include "backend/compiler/value.h"

// СистемнаяИнформация (SystemInfo) — a New-able value exposing read-only facts
// about the running platform/host. 1C-idiomatic use:
//   СистИнфо = Новый СистемнаяИнформация;
//   Верс = СистИнфо.ВерсияПриложения;
//
// Interim scope: the property NAMES exist so imported BSP modules compile and run;
// the values are best-effort stubs (platform version string, host name; RAM/OS
// left empty). ТипПлатформы returns an empty value until the platform-type enum
// is modelled — comparisons still compile.

constexpr ibClassID g_valueSystemInfoCLSID = value_to_clsid("VL_SYSI");

void ibValueSystemInfo_BindNames(ibValue::ibMemberTable& helper, const ibValue* ctx);

class BACKEND_API ibValueSystemInfo : public ibValueStaticMembers<&ibValueSystemInfo_BindNames> {
	// English props first (positions 0..6), Russian aliases next (7..13) in the SAME
	// order; dispatch is position % 7 (FindProp returns the append position).
	enum Prop { enAppVersion = 0, enVersion, enPlatformType, enRAM,
	            enComputerName, enOSVersion, enComputerID, enPropCount };
public:
	ibValueSystemInfo() : ibValueStaticMembers(ibValueTypes::TYPE_VALUE) {}

	virtual bool Init() override { return true; }
	virtual bool Init(ibValue** paParams, const long lSizeArray) override { return true; }

	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal) override;
};

// ---------------------------------------------------------------------------
// Interim EMPTY New-able stubs for platform types OES does not model yet. They
// exist so imported modules that construct them compile and LOAD (their other,
// unrelated functions become available) — the objects carry no behaviour.
// ---------------------------------------------------------------------------

// ПостроительОтчета (ReportBuilder) — legacy report-building object. Stub: constructs,
// no members/methods (report building is not implemented).
constexpr ibClassID g_valueReportBuilderCLSID = value_to_clsid("VL_RPBL");
void ibValueReportBuilder_BindNames(ibValue::ibMemberTable& helper, const ibValue* ctx);
class BACKEND_API ibValueReportBuilder : public ibValueStaticMembers<&ibValueReportBuilder_BindNames> {
public:
	ibValueReportBuilder() : ibValueStaticMembers(ibValueTypes::TYPE_VALUE) {}
	virtual bool Init() override { return true; }
	virtual bool Init(ibValue** paParams, const long lSizeArray) override { return true; }
};

// СжатиеДанных (DataCompression) — zip/deflate helper. Stub: constructs, no members.
constexpr ibClassID g_valueDataCompressionCLSID = value_to_clsid("VL_DCMP");
void ibValueDataCompression_BindNames(ibValue::ibMemberTable& helper, const ibValue* ctx);
class BACKEND_API ibValueDataCompression : public ibValueStaticMembers<&ibValueDataCompression_BindNames> {
public:
	ibValueDataCompression() : ibValueStaticMembers(ibValueTypes::TYPE_VALUE) {}
	virtual bool Init() override { return true; }
	virtual bool Init(ibValue** paParams, const long lSizeArray) override { return true; }
};

// ХранилищеЗначения (ValueStorage) — wraps an arbitrary value so it can be held/passed
// as an opaque box; Получить()/Get() returns the wrapped value. A real minimal type
// (holds the value in memory); DB-blob (de)serialization + compression are later work.
// Construct: Новый ХранилищеЗначения(Значение [, СжатиеДанных]).
constexpr ibClassID g_valueStorageCLSID = value_to_clsid("VL_VSTG");
void ibValueStorage_BindNames(ibValue::ibMemberTable& helper, const ibValue* ctx);
class BACKEND_API ibValueStorage : public ibValueStaticMembers<&ibValueStorage_BindNames> {
	enum Func { enGet = 0 };
public:
	ibValueStorage() : ibValueStaticMembers(ibValueTypes::TYPE_VALUE) {}

	virtual bool Init() override { return true; }
	virtual bool Init(ibValue** paParams, const long lSizeArray) override {
		if (lSizeArray > 0 && paParams && paParams[0]) m_value = *paParams[0];
		return true;
	}

	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray) override;

private:
	ibValue m_value;
};

#endif
