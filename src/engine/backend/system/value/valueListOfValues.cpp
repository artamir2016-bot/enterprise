////////////////////////////////////////////////////////////////////////////
//	Description : value list -- 1C СписокЗначений (ListOfValues) + its item
////////////////////////////////////////////////////////////////////////////

#include "valueListOfValues.h"

//======================================================================
//  ibValueListItem — one (value, presentation, check) row
//======================================================================

enum { eValue, ePresentation, eCheck, eValueRu, ePresentationRu, eCheckRu };

void ibValueListItem_BindNames(ibValue::ibMemberTable& helper, const ibValue* /*ctx*/)
{
	helper.AppendProp(wxT("Value"));
	helper.AppendProp(wxT("Presentation"));
	helper.AppendProp(wxT("Check"));
	helper.AppendProp(wxString::FromUTF8("\xD0\x97\xD0\xBD\xD0\xB0\xD1\x87\xD0\xB5\xD0\xBD\xD0\xB8\xD0\xB5"));                                 // Значение
	helper.AppendProp(wxString::FromUTF8("\xD0\x9F\xD1\x80\xD0\xB5\xD0\xB4\xD1\x81\xD1\x82\xD0\xB0\xD0\xB2\xD0\xBB\xD0\xB5\xD0\xBD\xD0\xB8\xD0\xB5")); // Представление
	helper.AppendProp(wxString::FromUTF8("\xD0\x9F\xD0\xBE\xD0\xBC\xD0\xB5\xD1\x82\xD0\xBA\xD0\xB0"));                                         // Пометка
}

bool ibValueListItem::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	switch (lPropNum)
	{
	case eValue:        case eValueRu:        m_value = varPropVal; return true;
	case ePresentation: case ePresentationRu: m_presentation = varPropVal.GetString(); return true;
	case eCheck:        case eCheckRu:        m_check = varPropVal.GetBoolean(); return true;
	}
	return false;
}

bool ibValueListItem::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	switch (lPropNum)
	{
	case eValue:        case eValueRu:        pvarPropVal = m_value; return true;
	case ePresentation: case ePresentationRu: pvarPropVal = m_presentation; return true;
	case eCheck:        case eCheckRu:        pvarPropVal = m_check; return true;
	}
	return false;
}

//======================================================================
//  ibValueListOfValues — the list
//======================================================================

enum { enAdd, enCount, enGet, enDelete, enClear, enFindByValue, enUnloadValues, enInsert, enIndexOf };

bool ibValueListOfValues::Init(ibValue** /*paParams*/, const long /*lSizeArray*/)
{
	// Новый СписокЗначений() — no args; items are added afterwards.
	return true;
}

void ibValueListOfValues_BindNames(ibValue::ibMemberTable& helper, const ibValue* /*ctx*/)
{
	helper.AppendFunc(wxT("Add"),          4, wxT("Add(value : any, presentation? : string, check? : bool, picture? : any)"));
	helper.AppendFunc(wxT("Count"),           wxT("Count()"));
	helper.AppendFunc(wxT("Get"),          1, wxT("Get(index : number)"));
	helper.AppendFunc(wxT("Delete"),       1, wxT("Delete(indexOrItem : any)"));
	helper.AppendFunc(wxT("Clear"),           wxT("Clear()"));
	helper.AppendFunc(wxT("FindByValue"),  1, wxT("FindByValue(value : any)"));
	helper.AppendFunc(wxT("UnloadValues"),    wxT("UnloadValues()"));
	helper.AppendFunc(wxT("Insert"),       4, wxT("Insert(index : number, value : any, presentation? : string, check? : bool)"));
	helper.AppendFunc(wxT("IndexOf"),      1, wxT("IndexOf(item : any)"));

	// OES-RU: 1C method names. Byte-escaped UTF-8 (no BOM, no /utf-8).
	helper.AliasMethod(wxString::FromUTF8("\xD0\x94\xD0\xBE\xD0\xB1\xD0\xB0\xD0\xB2\xD0\xB8\xD1\x82\xD1\x8C"), wxT("Add"));                                                         // Добавить
	helper.AliasMethod(wxString::FromUTF8("\xD0\x9A\xD0\xBE\xD0\xBB\xD0\xB8\xD1\x87\xD0\xB5\xD1\x81\xD1\x82\xD0\xB2\xD0\xBE"), wxT("Count"));                                     // Количество
	helper.AliasMethod(wxString::FromUTF8("\xD0\x9F\xD0\xBE\xD0\xBB\xD1\x83\xD1\x87\xD0\xB8\xD1\x82\xD1\x8C"), wxT("Get"));                                                       // Получить
	helper.AliasMethod(wxString::FromUTF8("\xD0\xA3\xD0\xB4\xD0\xB0\xD0\xBB\xD0\xB8\xD1\x82\xD1\x8C"), wxT("Delete"));                                                           // Удалить
	helper.AliasMethod(wxString::FromUTF8("\xD0\x9E\xD1\x87\xD0\xB8\xD1\x81\xD1\x82\xD0\xB8\xD1\x82\xD1\x8C"), wxT("Clear"));                                                     // Очистить
	helper.AliasMethod(wxString::FromUTF8("\xD0\x9D\xD0\xB0\xD0\xB9\xD1\x82\xD0\xB8\xD0\x9F\xD0\xBE\xD0\x97\xD0\xBD\xD0\xB0\xD1\x87\xD0\xB5\xD0\xBD\xD0\xB8\xD1\x8E"), wxT("FindByValue")); // НайтиПоЗначению
	helper.AliasMethod(wxString::FromUTF8("\xD0\x92\xD1\x8B\xD0\xB3\xD1\x80\xD1\x83\xD0\xB7\xD0\xB8\xD1\x82\xD1\x8C\xD0\x97\xD0\xBD\xD0\xB0\xD1\x87\xD0\xB5\xD0\xBD\xD0\xB8\xD1\x8F"), wxT("UnloadValues")); // ВыгрузитьЗначения
	helper.AliasMethod(wxString::FromUTF8("\xD0\x92\xD1\x81\xD1\x82\xD0\xB0\xD0\xB2\xD0\xB8\xD1\x82\xD1\x8C"), wxT("Insert"));                                                   // Вставить
	helper.AliasMethod(wxString::FromUTF8("\xD0\x98\xD0\xBD\xD0\xB4\xD0\xB5\xD0\xBA\xD1\x81"), wxT("IndexOf"));                                                                   // Индекс
}

ibValue ibValueListOfValues::AddItem(const ibValue& value, const wxString& presentation, bool check)
{
	ibValuePtr<ibValueListItem> item(new ibValueListItem());
	item->m_value = value;
	item->m_presentation = presentation;
	item->m_check = check;
	m_items.push_back(item);
	return ibValue(&*item);
}

bool ibValueListOfValues::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	switch (lMethodNum)
	{
	case enAdd:
		pvarRetValue = AddItem(
			lSizeArray > 0 ? *paParams[0] : ibValue(),
			lSizeArray > 1 ? paParams[1]->GetString() : wxString(),
			lSizeArray > 2 ? paParams[2]->GetBoolean() : false);
		return true;
	case enCount:
		pvarRetValue = (signed int)m_items.size();
		return true;
	case enGet:
	{
		const long i = lSizeArray > 0 ? paParams[0]->GetInteger() : -1;
		if (i >= 0 && i < (long)m_items.size())
			pvarRetValue = ibValue(&*m_items[i]);
		else
			pvarRetValue = ibValue();
		return true;
	}
	case enFindByValue:
	{
		const ibValue& needle = lSizeArray > 0 ? *paParams[0] : ibValue();
		for (auto& it : m_items)
			if (it->m_value.CompareValueEQ(needle)) { pvarRetValue = ibValue(&*it); return true; }
		pvarRetValue = ibValue();   // Undefined when not found (1C semantics)
		return true;
	}
	case enUnloadValues:
	{
		ibValuePtr<ibValueArray> arr(new ibValueArray());
		for (auto& it : m_items)
			arr->Add(it->m_value);
		pvarRetValue = ibValue(&*arr);
		return true;
	}
	case enInsert:
	{
		const long i = lSizeArray > 0 ? paParams[0]->GetInteger() : 0;
		ibValuePtr<ibValueListItem> item(new ibValueListItem());
		item->m_value        = lSizeArray > 1 ? *paParams[1] : ibValue();
		item->m_presentation = lSizeArray > 2 ? paParams[2]->GetString() : wxString();
		item->m_check        = lSizeArray > 3 ? paParams[3]->GetBoolean() : false;
		const size_t pos = (i < 0) ? 0 : std::min<size_t>((size_t)i, m_items.size());
		m_items.insert(m_items.begin() + pos, item);
		pvarRetValue = ibValue(&*item);
		return true;
	}
	case enIndexOf:
	{
		pvarRetValue = (signed int)-1;
		if (lSizeArray > 0) {
			ibValueListItem* target = dynamic_cast<ibValueListItem*>(paParams[0]->GetRef());
			for (size_t k = 0; k < m_items.size(); ++k)
				if (&*m_items[k] == target) { pvarRetValue = (signed int)k; break; }
		}
		return true;
	}
	case enDelete:
	{
		if (lSizeArray > 0) {
			if (ibValueListItem* target = dynamic_cast<ibValueListItem*>(paParams[0]->GetRef())) {
				for (size_t k = 0; k < m_items.size(); ++k)
					if (&*m_items[k] == target) { m_items.erase(m_items.begin() + k); break; }
			} else {
				const long i = paParams[0]->GetInteger();
				if (i >= 0 && i < (long)m_items.size())
					m_items.erase(m_items.begin() + i);
			}
		}
		pvarRetValue = ibValue();
		return true;
	}
	case enClear:
		m_items.clear();
		pvarRetValue = ibValue();
		return true;
	}
	return false;
}

bool ibValueListOfValues::CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray)
{
	// Every method may be written as a statement (Список.Добавить(x)); forward to the func form
	// and drop the return so the same dispatch serves both call sites.
	ibValue dummy;
	return CallAsFunc(lMethodNum, dummy, paParams, lSizeArray);
}

std::shared_ptr<ibValueIteratorState> ibValueListOfValues::CreateIterator()
{
	class ListIteratorState : public ibValueIteratorState {
	public:
		explicit ListIteratorState(const std::vector<ibValuePtr<ibValueListItem>>& items) : m_items(items) {}
		bool MoveNext(ibValue& current) override {
			if (m_started) ++m_pos; else m_started = true;
			if (m_pos >= m_items.size()) return false;
			current = ibValue(&*m_items[m_pos]);
			return true;
		}
		void Reset() override { m_pos = 0; m_started = false; }
	private:
		const std::vector<ibValuePtr<ibValueListItem>>& m_items;
		size_t m_pos = 0;
		bool m_started = false;
	};
	return std::make_shared<ListIteratorState>(m_items);
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

VALUE_TYPE_REGISTER(ibValueListItem,     "ListItem");
VALUE_TYPE_REGISTER(ibValueListOfValues, "ListOfValues");
