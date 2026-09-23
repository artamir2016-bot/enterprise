////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : system objects 
////////////////////////////////////////////////////////////////////////////

#include "systemManagerEnum.h"


//add new enumeration
ENUM_TYPE_REGISTER(ibValueEnumStatusMessage, "StatusMessage", enum_to_clsid("EN_STMS"));
ENUM_TYPE_REGISTER(ibValueEnumQuestionMode, "QuestionMode", enum_to_clsid("EN_QSMD"));
ENUM_TYPE_REGISTER(ibValueEnumQuestionReturnCode, "QuestionReturnCode", enum_to_clsid("EN_QSRC"));
ENUM_TYPE_REGISTER(ibValueEnumRoundMode, "RoundMode", enum_to_clsid("EN_ROMO"));

ENUM_TYPE_REGISTER(ibValueChars, "Chars", enum_to_clsid("EN_CHAR"));
ENUM_TYPE_REGISTER(ibValueEnumEventLogLevel, "EventLogLevel", enum_to_clsid("EN_ELLV"));
ENUM_TYPE_REGISTER(ibValueEnumAllowedLength, "AllowedLength", enum_to_clsid("EN_ALEN"));
ENUM_TYPE_REGISTER(ibValueEnumCommandBarButtonsAlignment, "CommandBarButtonsAlignment", enum_to_clsid("EN_CBBA"));
ENUM_TYPE_REGISTER(ibValueEnumCommandBarButtonType, "CommandBarButtonType", enum_to_clsid("EN_CBBT"));
ENUM_TYPE_REGISTER(ibValueEnumCommandBarButtonRepresentation, "CommandBarButtonRepresentation", enum_to_clsid("EN_CBBR"));
ENUM_TYPE_REGISTER(ibValueEnumReportBuilderDimensionType, "ReportBuilderDimensionType", enum_to_clsid("EN_RBDT"));
