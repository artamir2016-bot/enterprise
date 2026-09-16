#ifndef __DOCUMENT_ENUM_H__
#define __DOCUMENT_ENUM_H__

enum ibDocumentWriteMode {
	ibDocumentWriteMode_Posting,
	ibDocumentWriteMode_UndoPosting,
	ibDocumentWriteMode_Write
};

enum ibDocumentPostingMode {
	ibDocumentPostingMode_RealTime,
	ibDocumentPostingMode_Regular
};

#pragma region enumeration
#include "backend/compiler/enumUnit.h"

class ibValueEnumDocumentWriteMode : public ibValueEnumeration<ibDocumentWriteMode> {
	public:
	ibValueEnumDocumentWriteMode() : ibValueEnumeration() {}
	//ibValueEnumDocumentWriteMode(ibDocumentWriteMode mode) : ibValueEnumeration(mode) {}

	virtual void CreateEnumeration() {
		AddEnumeration(ibDocumentWriteMode::ibDocumentWriteMode_Posting, wxT("Posting"), _("Posting"));
		AddEnumeration(ibDocumentWriteMode::ibDocumentWriteMode_UndoPosting, wxT("UndoPosting"), _("Undo posting"));
		AddEnumeration(ibDocumentWriteMode::ibDocumentWriteMode_Write, wxT("Write"), _("Write"));
		// OES-RU: 1C РежимЗаписиДокумента member names. Byte-escaped UTF-8 (no BOM, no /utf-8).
		AddEnumAlias(ibDocumentWriteMode::ibDocumentWriteMode_Posting,     wxString::FromUTF8("\xD0\x9F\xD1\x80\xD0\xBE\xD0\xB2\xD0\xB5\xD0\xB4\xD0\xB5\xD0\xBD\xD0\xB8\xD0\xB5"));                                 // Проведение
		AddEnumAlias(ibDocumentWriteMode::ibDocumentWriteMode_UndoPosting, wxString::FromUTF8("\xD0\x9E\xD1\x82\xD0\xBC\xD0\xB5\xD0\xBD\xD0\xB0\xD0\x9F\xD1\x80\xD0\xBE\xD0\xB2\xD0\xB5\xD0\xB4\xD0\xB5\xD0\xBD\xD0\xB8\xD1\x8F")); // ОтменаПроведения
		AddEnumAlias(ibDocumentWriteMode::ibDocumentWriteMode_Write,       wxString::FromUTF8("\xD0\x97\xD0\xB0\xD0\xBF\xD0\xB8\xD1\x81\xD1\x8C"));                                                                 // Запись
	}
};
class ibValueEnumDocumentPostingMode : public ibValueEnumeration<ibDocumentPostingMode> {
	public:
	ibValueEnumDocumentPostingMode() : ibValueEnumeration() {}
	//ibValueEnumDocumentPostingMode(ibDocumentPostingMode mode) : ibValueEnumeration(mode) {}

	virtual void CreateEnumeration() {
		AddEnumeration(ibDocumentPostingMode::ibDocumentPostingMode_RealTime, wxT("RealTime"), _("Real time"));
		AddEnumeration(ibDocumentPostingMode::ibDocumentPostingMode_Regular, wxT("Regular"), _("Regular"));
	}
};
#pragma endregion 

#endif // ! _DOCUMENT_EMUN_H_
