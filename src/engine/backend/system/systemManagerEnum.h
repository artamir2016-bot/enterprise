#ifndef _SYSTEMOBJECTS_ENUMS_H__
#define _SYSTEMOBJECTS_ENUMS_H__

#include "systemEnum.h"
#include "backend/compiler/enumUnit.h"

class ibValueEnumStatusMessage : public ibValueEnumeration<ibStatusMessage> {
	public:
	ibValueEnumStatusMessage() : ibValueEnumeration() {}
	//ibValueEnumStatusMessage(ibStatusMessage status) : ibValueEnumeration(status) {}

	virtual void CreateEnumeration() {
		AddEnumeration(ibStatusMessage::ibStatusMessage_Information, wxT("Information"), _("Information"));
		AddEnumeration(ibStatusMessage::ibStatusMessage_Warning, wxT("Warning"), _("Warning"));
		AddEnumeration(ibStatusMessage::ibStatusMessage_Error, wxT("Error"), _("Error"));
		// OES-RU: 1C СтатусСообщения member names, mapped onto the three severities.
		// Byte-escaped UTF-8 (no BOM, no /utf-8). Обычное/Информация/БезСтатуса → Information;
		// Внимание/Важное → Warning; ОченьВажное → Error.
		AddEnumAlias(ibStatusMessage::ibStatusMessage_Information, wxString::FromUTF8("\xD0\x98\xD0\xBD\xD1\x84\xD0\xBE\xD1\x80\xD0\xBC\xD0\xB0\xD1\x86\xD0\xB8\xD1\x8F"));                 // Информация
		AddEnumAlias(ibStatusMessage::ibStatusMessage_Information, wxString::FromUTF8("\xD0\x9E\xD0\xB1\xD1\x8B\xD1\x87\xD0\xBD\xD0\xBE\xD0\xB5"));                                         // Обычное
		AddEnumAlias(ibStatusMessage::ibStatusMessage_Information, wxString::FromUTF8("\xD0\x91\xD0\xB5\xD0\xB7\xD0\xA1\xD1\x82\xD0\xB0\xD1\x82\xD1\x83\xD1\x81\xD0\xB0"));                 // БезСтатуса
		AddEnumAlias(ibStatusMessage::ibStatusMessage_Warning,     wxString::FromUTF8("\xD0\x92\xD0\xBD\xD0\xB8\xD0\xBC\xD0\xB0\xD0\xBD\xD0\xB8\xD0\xB5"));                                 // Внимание
		AddEnumAlias(ibStatusMessage::ibStatusMessage_Warning,     wxString::FromUTF8("\xD0\x92\xD0\xB0\xD0\xB6\xD0\xBD\xD0\xBE\xD0\xB5"));                                                 // Важное
		AddEnumAlias(ibStatusMessage::ibStatusMessage_Error,       wxString::FromUTF8("\xD0\x9E\xD1\x87\xD0\xB5\xD0\xBD\xD1\x8C\xD0\x92\xD0\xB0\xD0\xB6\xD0\xBD\xD0\xBE\xD0\xB5"));         // ОченьВажное
	}
};

class ibValueEnumQuestionMode : public ibValueEnumeration<ibQuestionMode> {
	public:
	ibValueEnumQuestionMode() : ibValueEnumeration() {}
	//ibValueEnumQuestionMode(ibQuestionMode mode) : ibValueEnumeration(mode) {}

	virtual void CreateEnumeration() {
		AddEnumeration(ibQuestionMode::ibQuestionMode_YesNo, wxT("YesNo"), _("Yes or no"));
		AddEnumeration(ibQuestionMode::ibQuestionMode_YesNoCancel, wxT("YesNoCancel"), _("Yes or no or cancel"));
		AddEnumeration(ibQuestionMode::ibQuestionMode_OK, wxT("Ok"), _("Ok"));
		AddEnumeration(ibQuestionMode::ibQuestionMode_OKCancel, wxT("OkCancel"), _("Ok or cancel"));
		// OES-RU: 1C РежимДиалогаВопрос member names. Byte-escaped UTF-8 (no BOM, no /utf-8).
		AddEnumAlias(ibQuestionMode::ibQuestionMode_YesNo,       wxString::FromUTF8("\xD0\x94\xD0\xB0\xD0\x9D\xD0\xB5\xD1\x82"));                                                 // ДаНет
		AddEnumAlias(ibQuestionMode::ibQuestionMode_YesNoCancel, wxString::FromUTF8("\xD0\x94\xD0\xB0\xD0\x9D\xD0\xB5\xD1\x82\xD0\x9E\xD1\x82\xD0\xBC\xD0\xB5\xD0\xBD\xD0\xB0")); // ДаНетОтмена
		AddEnumAlias(ibQuestionMode::ibQuestionMode_OK,          wxString::FromUTF8("\xD0\x9E\xD0\x9A"));                                                                         // ОК
		AddEnumAlias(ibQuestionMode::ibQuestionMode_OKCancel,    wxString::FromUTF8("\xD0\x9E\xD0\x9A\xD0\x9E\xD1\x82\xD0\xBC\xD0\xB5\xD0\xBD\xD0\xB0"));                         // ОКОтмена
	}
};

class ibValueEnumQuestionReturnCode : public ibValueEnumeration<ibQuestionReturnCode> {
	public:
	ibValueEnumQuestionReturnCode() : ibValueEnumeration() {}
	//ibValueEnumQuestionReturnCode(ibQuestionReturnCode code) : ibValueEnumeration(code) {}

	virtual void CreateEnumeration() {
		AddEnumeration(ibQuestionReturnCode::ibQuestionReturnCode_Yes, wxT("Yes"), _("Yes"));
		AddEnumeration(ibQuestionReturnCode::ibQuestionReturnCode_No, wxT("No"), _("Yes"));
		AddEnumeration(ibQuestionReturnCode::ibQuestionReturnCode_OK, wxT("Ok"), _("Ok"));
		AddEnumeration(ibQuestionReturnCode::ibQuestionReturnCode_Cancel, wxT("Cancel"), _("Cancel"));
	}
};

class ibValueEnumRoundMode : public ibValueEnumeration<ibRoundMode> {
	public:
	ibValueEnumRoundMode() : ibValueEnumeration() {}
	//ibValueEnumRoundMode(ibRoundMode mode) : ibValueEnumeration(mode) {}

	virtual void CreateEnumeration() {
		AddEnumeration(ibRoundMode::ibRoundMode_Round15as10, wxT("Round15as10"), _("Round 15 as 10"));
		AddEnumeration(ibRoundMode::ibRoundMode_Round15as20, wxT("Round15as20"), _("Round 15 as 20"));
	}
};

class ibValueChars : public ibValueEnumeration<ibChars> {
	public:
	ibValueChars() : ibValueEnumeration() {}
	//ibValueChars(ibChars c) : ibValueEnumeration(c) {}

	virtual void CreateEnumeration() {
		AddEnumeration(ibChars::eCR, wxT("CR"));
		AddEnumeration(ibChars::eFF, wxT("FF"));
		AddEnumeration(ibChars::eLF, wxT("LF"));
		AddEnumeration(ibChars::eNBSp, wxT("NBSp"));
		AddEnumeration(ibChars::eTab, wxT("Tab"));
		AddEnumeration(ibChars::eVTab, wxT("VTab"));
	}

	virtual wxString GetDescription(ibChars val) const {
		return (char)val;
	}
};

class ibValueEnumEventLogLevel : public ibValueEnumeration<ibEventLogLevel> {
	public:
	ibValueEnumEventLogLevel() : ibValueEnumeration() {}

	virtual void CreateEnumeration() {
		AddEnumeration(ibEventLogLevel::ibEventLogLevel_Information, wxT("Information"), _("Information"));
		AddEnumeration(ibEventLogLevel::ibEventLogLevel_Error, wxT("Error"), _("Error"));
		AddEnumeration(ibEventLogLevel::ibEventLogLevel_Warning, wxT("Warning"), _("Warning"));
		AddEnumeration(ibEventLogLevel::ibEventLogLevel_Note, wxT("Note"), _("Note"));
		// OES-RU: 1C УровеньЖурналаРегистрации member names. Byte-escaped UTF-8 (no BOM, no /utf-8).
		AddEnumAlias(ibEventLogLevel::ibEventLogLevel_Information, wxString::FromUTF8("\xD0\x98\xD0\xBD\xD1\x84\xD0\xBE\xD1\x80\xD0\xBC\xD0\xB0\xD1\x86\xD0\xB8\xD1\x8F"));                                 // Информация
		AddEnumAlias(ibEventLogLevel::ibEventLogLevel_Error,       wxString::FromUTF8("\xD0\x9E\xD1\x88\xD0\xB8\xD0\xB1\xD0\xBA\xD0\xB0"));                                                                 // Ошибка
		AddEnumAlias(ibEventLogLevel::ibEventLogLevel_Warning,     wxString::FromUTF8("\xD0\x9F\xD1\x80\xD0\xB5\xD0\xB4\xD1\x83\xD0\xBF\xD1\x80\xD0\xB5\xD0\xB6\xD0\xB4\xD0\xB5\xD0\xBD\xD0\xB8\xD0\xB5")); // Предупреждение
		AddEnumAlias(ibEventLogLevel::ibEventLogLevel_Note,        wxString::FromUTF8("\xD0\x9F\xD1\x80\xD0\xB8\xD0\xBC\xD0\xB5\xD1\x87\xD0\xB0\xD0\xBD\xD0\xB8\xD0\xB5"));                                 // Примечание
	}
};

#endif