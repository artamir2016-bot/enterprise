////////////////////////////////////////////////////////////////////////////
//	Description : СистемнаяИнформация (SystemInfo) — read-only platform facts
////////////////////////////////////////////////////////////////////////////

#include "valueSystemInfo.h"

// Cyrillic identifiers as UTF-8 byte escapes — this TU is compiled WITHOUT /utf-8,
// so a raw literal would be mis-encoded. Same convention as valueTree.cpp.
#define RU(bytes) wxString::FromUTF8(bytes)
static const char* const kRu_AppVersion   = "\xD0\x92\xD0\xB5\xD1\x80\xD1\x81\xD0\xB8\xD1\x8F\xD0\x9F\xD1\x80\xD0\xB8\xD0\xBB\xD0\xBE\xD0\xB6\xD0\xB5\xD0\xBD\xD0\xB8\xD1\x8F"; // ВерсияПриложения
static const char* const kRu_Version      = "\xD0\x92\xD0\xB5\xD1\x80\xD1\x81\xD0\xB8\xD1\x8F"; // Версия
static const char* const kRu_PlatformType = "\xD0\xA2\xD0\xB8\xD0\xBF\xD0\x9F\xD0\xBB\xD0\xB0\xD1\x82\xD1\x84\xD0\xBE\xD1\x80\xD0\xBC\xD1\x8B"; // ТипПлатформы
static const char* const kRu_RAM          = "\xD0\x9E\xD0\xBF\xD0\xB5\xD1\x80\xD0\xB0\xD1\x82\xD0\xB8\xD0\xB2\xD0\xBD\xD0\xB0\xD1\x8F\xD0\x9F\xD0\xB0\xD0\xBC\xD1\x8F\xD1\x82\xD1\x8C"; // ОперативнаяПамять
static const char* const kRu_ComputerName = "\xD0\x98\xD0\xBC\xD1\x8F\xD0\x9A\xD0\xBE\xD0\xBC\xD0\xBF\xD1\x8C\xD1\x8E\xD1\x82\xD0\xB5\xD1\x80\xD0\xB0"; // ИмяКомпьютера
static const char* const kRu_OSVersion    = "\xD0\x92\xD0\xB5\xD1\x80\xD1\x81\xD0\xB8\xD1\x8F\xD0\x9E\xD0\xA1"; // ВерсияОС
static const char* const kRu_ComputerID   = "\xD0\x98\xD0\xB4\xD0\xB5\xD0\xBD\xD1\x82\xD0\xB8\xD1\x84\xD0\xB8\xD0\xBA\xD0\xB0\xD1\x82\xD0\xBE\xD1\x80\xD0\x9A\xD0\xBE\xD0\xBC\xD0\xBF\xD1\x8C\xD1\x8E\xD1\x82\xD0\xB5\xD1\x80\xD0\xB0"; // ИдентификаторКомпьютера

void ibValueSystemInfo_BindNames(ibValue::ibMemberTable& helper, const ibValue* /*ctx*/)
{
	// English first (positions 0..6), then Russian aliases in the same order (7..13).
	helper.AppendProp(wxT("AppVersion"),   true, false, 0L);   // 0
	helper.AppendProp(wxT("Version"),      true, false, 0L);   // 1
	helper.AppendProp(wxT("PlatformType"), true, false, 0L);   // 2
	helper.AppendProp(wxT("RAM"),          true, false, 0L);   // 3
	helper.AppendProp(wxT("ComputerName"), true, false, 0L);   // 4
	helper.AppendProp(wxT("OSVersion"),    true, false, 0L);   // 5
	helper.AppendProp(wxT("ComputerID"),   true, false, 0L);   // 6
	helper.AppendProp(RU(kRu_AppVersion),   true, false, 0L);  // 7
	helper.AppendProp(RU(kRu_Version),      true, false, 0L);  // 8
	helper.AppendProp(RU(kRu_PlatformType), true, false, 0L);  // 9
	helper.AppendProp(RU(kRu_RAM),          true, false, 0L);  // 10
	helper.AppendProp(RU(kRu_ComputerName), true, false, 0L);  // 11
	helper.AppendProp(RU(kRu_OSVersion),    true, false, 0L);  // 12
	helper.AppendProp(RU(kRu_ComputerID),   true, false, 0L);  // 13
}

bool ibValueSystemInfo::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	switch (lPropNum % enPropCount) {
	case enAppVersion:   pvarPropVal = ibValue(wxT("1.0.0.0")); return true;
	case enVersion:      pvarPropVal = ibValue(wxT("8.3.20.0")); return true;
	case enPlatformType: pvarPropVal = ibValue(); return true;   // enum not modelled yet
	case enRAM:          pvarPropVal = ibValue(ibNumber(0)); return true;
	case enComputerName: pvarPropVal = ibValue(wxEmptyString); return true;
	case enOSVersion:    pvarPropVal = ibValue(wxEmptyString); return true;
	case enComputerID:   pvarPropVal = ibValue(wxEmptyString); return true;
	}
	return false;
}

//======================================================================
//  Registration
//======================================================================
#include "backend/compiler/typeCtor.h"

VALUE_TYPE_REGISTER(ibValueSystemInfo, "SystemInfo", g_valueSystemInfoCLSID);
