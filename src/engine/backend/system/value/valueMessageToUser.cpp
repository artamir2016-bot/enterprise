////////////////////////////////////////////////////////////////////////////
//	Description : user message -- 1C СообщениеПользователю (MessageToUser)
////////////////////////////////////////////////////////////////////////////

#include "valueMessageToUser.h"
#include "backend/system/systemManager.h"   // ibValueSystemFunction::Message — the session message sink

//////////////////////////////////////////////////////////////////////

ibValueMessageToUser::ibValueMessageToUser()
	: ibValueStaticMembers(ibValueTypes::TYPE_VALUE)
{
}

bool ibValueMessageToUser::Init(ibValue** /*paParams*/, const long /*lSizeArray*/)
{
	// Новый СообщениеПользователю() — no ctor args; the text/fields are assigned after.
	return true;
}

// Props — English canonical names plus their Russian 1C synonyms mapped to the same fields
// (imported modules address .Текст / .Поле / …). Method surface (Сообщить / Message) is added
// via AppendFunc + AliasMethod so an unqualified .Сообщить() call dispatches to enMessage.
enum
{
	eText, eField, eDataKey, eDataPath,
	eTextRu, eFieldRu, eDataKeyRu, eDataPathRu
};
enum { enMessage };

void ibValueMessageToUser_BindNames(ibValue::ibMemberTable& helper, const ibValue* /*ctx*/)
{
	helper.AppendProp(wxT("Text"));
	helper.AppendProp(wxT("Field"));
	helper.AppendProp(wxT("DataKey"));
	helper.AppendProp(wxT("DataPath"));
	helper.AppendProp(wxString::FromUTF8("\xD0\xA2\xD0\xB5\xD0\xBA\xD1\x81\xD1\x82"));                                         // Текст
	helper.AppendProp(wxString::FromUTF8("\xD0\x9F\xD0\xBE\xD0\xBB\xD0\xB5"));                                                 // Поле
	helper.AppendProp(wxString::FromUTF8("\xD0\x9A\xD0\xBB\xD1\x8E\xD1\x87\xD0\x94\xD0\xB0\xD0\xBD\xD0\xBD\xD1\x8B\xD1\x85")); // КлючДанных
	helper.AppendProp(wxString::FromUTF8("\xD0\x9F\xD1\x83\xD1\x82\xD1\x8C"));                                                 // Путь

	helper.AppendFunc(wxT("Message"), wxT("Message()"));                                                                      // .Сообщить()
	helper.AliasMethod(wxString::FromUTF8("\xD0\xA1\xD0\xBE\xD0\xBE\xD0\xB1\xD1\x89\xD0\xB8\xD1\x82\xD1\x8C"), wxT("Message")); // Сообщить
}

bool ibValueMessageToUser::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	switch (lPropNum)
	{
	case eText:     case eTextRu:     m_text     = varPropVal.GetString(); return true;
	case eField:    case eFieldRu:    m_field    = varPropVal.GetString(); return true;
	case eDataKey:  case eDataKeyRu:  m_dataKey  = varPropVal.GetString(); return true;
	case eDataPath: case eDataPathRu: m_dataPath = varPropVal.GetString(); return true;
	}
	return false;
}

bool ibValueMessageToUser::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	switch (lPropNum)
	{
	case eText:     case eTextRu:     pvarPropVal = m_text;     return true;
	case eField:    case eFieldRu:    pvarPropVal = m_field;    return true;
	case eDataKey:  case eDataKeyRu:  pvarPropVal = m_dataKey;  return true;
	case eDataPath: case eDataPathRu: pvarPropVal = m_dataPath; return true;
	}
	return false;
}

bool ibValueMessageToUser::CallAsProc(const long lMethodNum, ibValue** /*paParams*/, const long /*lSizeArray*/)
{
	switch (lMethodNum)
	{
	case enMessage:
		// Deliver to the session's message sink — the same road the global Message() takes.
		ibValueSystemFunction::Message(m_text);
		return true;
	}
	return false;
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

VALUE_TYPE_REGISTER(ibValueMessageToUser, "MessageToUser");
