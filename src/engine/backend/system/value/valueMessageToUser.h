#ifndef __VALUE_MESSAGE_TO_USER_H__
#define __VALUE_MESSAGE_TO_USER_H__

#include "backend/compiler/value.h"

void ibValueMessageToUser_BindNames(ibValue::ibMemberTable& helper, const ibValue* ctx);

// A USER MESSAGE — 1C `СообщениеПользователю` (MessageToUser).
//
// The object a script builds to report something to the user, then delivers with .Сообщить():
//   М = Новый СообщениеПользователю; М.Текст = "…"; М.Поле = "Реквизит"; М.Сообщить();
// It carries the text plus the addressing fields (Поле / КлючДанных / Путь) that let a UI bind the
// message to a form control; the platform's message sink only needs the text today, the rest are
// stored so imported modules that set them compile and round-trip.
class BACKEND_API ibValueMessageToUser : public ibValueStaticMembers<&ibValueMessageToUser_BindNames>
{
public:

	wxString m_text;       // Текст       — the message body
	wxString m_field;      // Поле        — form attribute name to attach to
	wxString m_dataKey;    // КлючДанных   — object/record key the message is about
	wxString m_dataPath;   // Путь        — data path to the addressed field

public:

	ibValueMessageToUser();
	virtual ~ibValueMessageToUser() {}

	// Новый СообщениеПользователю() — no arguments (fields are set afterwards).
	virtual bool Init(ibValue** paParams, const long lSizeArray) override;

	virtual wxString GetString() const override { return m_text; }
	virtual bool IsEmpty() const override { return m_text.IsEmpty(); }

	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal) override;
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal) override;

	// .Сообщить() / .Message() — deliver the text to the session's message sink.
	virtual bool CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray) override;
};

#endif // __VALUE_MESSAGE_TO_USER_H__
