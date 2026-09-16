#ifndef __VALUE_LIST_OF_VALUES_H__
#define __VALUE_LIST_OF_VALUES_H__

#include "backend/compiler/value.h"
#include "backend/system/value/valueArray.h"   // ibValueArray — UnloadValues() result

void ibValueListItem_BindNames(ibValue::ibMemberTable& helper, const ibValue* ctx);
void ibValueListOfValues_BindNames(ibValue::ibMemberTable& helper, const ibValue* ctx);

// ONE ITEM of a value list — 1C ЭлементСпискаЗначений.
//   .Значение (Value) — the value it carries
//   .Представление (Presentation) — the text shown for it
//   .Пометка (Check) — a boolean mark (checkbox lists)
class BACKEND_API ibValueListItem : public ibValueStaticMembers<&ibValueListItem_BindNames>
{
public:
	ibValue  m_value;
	wxString m_presentation;
	bool     m_check = false;

	ibValueListItem() : ibValueStaticMembers(ibValueTypes::TYPE_VALUE) {}

	virtual wxString GetString() const override { return m_presentation.IsEmpty() ? m_value.GetString() : m_presentation; }
	virtual bool IsEmpty() const override { return m_value.IsEmpty() && m_presentation.IsEmpty(); }

	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal) override;
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal) override;
};

// A VALUE LIST — 1C СписокЗначений: an ordered list of (value, presentation, check) items with
// selection-style helpers. Constructed as `Новый СписокЗначений`, walked with For Each, filled with
// .Добавить(Значение, Представление). Backs picker dialogs, radio groups and multi-choice fields.
class BACKEND_API ibValueListOfValues : public ibValueStaticMembers<&ibValueListOfValues_BindNames>
{
public:
	std::vector<ibValuePtr<ibValueListItem>> m_items;

	ibValueListOfValues() : ibValueStaticMembers(ibValueTypes::TYPE_VALUE) {}

	virtual bool Init(ibValue** paParams, const long lSizeArray) override;

	virtual bool IsEmpty() const override { return m_items.empty(); }
	virtual wxString GetString() const override { return wxT("ListOfValues"); }

	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray) override;
	virtual bool CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray) override;

	// For Each item In list — yields the items.
	virtual std::shared_ptr<ibValueIteratorState> CreateIterator() override;

	// Helper: append a new item, return it (as a reffer value).
	ibValue AddItem(const ibValue& value, const wxString& presentation, bool check);
};

#endif // __VALUE_LIST_OF_VALUES_H__
