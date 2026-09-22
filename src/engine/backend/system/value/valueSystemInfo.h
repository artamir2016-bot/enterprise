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

#endif
