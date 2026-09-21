////////////////////////////////////////////////////////////////////////////
//	Description : ДеревоЗначений (ValueTree) — in-memory hierarchical table
////////////////////////////////////////////////////////////////////////////

#include "valueTree.h"

// Cyrillic identifiers as UTF-8 byte escapes — this TU is compiled WITHOUT /utf-8,
// so a raw literal would be mis-encoded. Same convention as backend_type.cpp.
#define RU(bytes) wxString::FromUTF8(bytes)
static const char* const kRu_ValueTree   = "\xD0\x94\xD0\xB5\xD1\x80\xD0\xB5\xD0\xB2\xD0\xBE\xD0\x97\xD0\xBD\xD0\xB0\xD1\x87\xD0\xB5\xD0\xBD\xD0\xB8\xD0\xB9"; // ДеревоЗначений
static const char* const kRu_Columns     = "\xD0\x9A\xD0\xBE\xD0\xBB\xD0\xBE\xD0\xBD\xD0\xBA\xD0\xB8"; // Колонки
static const char* const kRu_Rows        = "\xD0\xA1\xD1\x82\xD1\x80\xD0\xBE\xD0\xBA\xD0\xB8"; // Строки
static const char* const kRu_Add         = "\xD0\x94\xD0\xBE\xD0\xB1\xD0\xB0\xD0\xB2\xD0\xB8\xD1\x82\xD1\x8C"; // Добавить
static const char* const kRu_Count       = "\xD0\x9A\xD0\xBE\xD0\xBB\xD0\xB8\xD1\x87\xD0\xB5\xD1\x81\xD1\x82\xD0\xB2\xD0\xBE"; // Количество
static const char* const kRu_Find        = "\xD0\x9D\xD0\xB0\xD0\xB9\xD1\x82\xD0\xB8"; // Найти
static const char* const kRu_Get         = "\xD0\x9F\xD0\xBE\xD0\xBB\xD1\x83\xD1\x87\xD0\xB8\xD1\x82\xD1\x8C"; // Получить
static const char* const kRu_Delete      = "\xD0\xA3\xD0\xB4\xD0\xB0\xD0\xBB\xD0\xB8\xD1\x82\xD1\x8C"; // Удалить
static const char* const kRu_IndexOf     = "\xD0\x98\xD0\xBD\xD0\xB4\xD0\xB5\xD0\xBA\xD1\x81"; // Индекс
static const char* const kRu_Clear       = "\xD0\x9E\xD1\x87\xD0\xB8\xD1\x81\xD1\x82\xD0\xB8\xD1\x82\xD1\x8C"; // Очистить
static const char* const kRu_Insert      = "\xD0\x92\xD1\x81\xD1\x82\xD0\xB0\xD0\xB2\xD0\xB8\xD1\x82\xD1\x8C"; // Вставить
static const char* const kRu_Parent      = "\xD0\xA0\xD0\xBE\xD0\xB4\xD0\xB8\xD1\x82\xD0\xB5\xD0\xBB\xD1\x8C"; // Родитель
static const char* const kRu_Owner       = "\xD0\x92\xD0\xBB\xD0\xB0\xD0\xB4\xD0\xB5\xD0\xBB\xD0\xB5\xD1\x86"; // Владелец
static const char* const kRu_Level       = "\xD0\xA3\xD1\x80\xD0\xBE\xD0\xB2\xD0\xB5\xD0\xBD\xD1\x8C"; // Уровень
static const char* const kRu_Name        = "\xD0\x98\xD0\xBC\xD1\x8F"; // Имя
static const char* const kRu_Title       = "\xD0\x97\xD0\xB0\xD0\xB3\xD0\xBE\xD0\xBB\xD0\xBE\xD0\xB2\xD0\xBE\xD0\xBA"; // Заголовок
static const char* const kRu_ValueType   = "\xD0\xA2\xD0\xB8\xD0\xBF\xD0\x97\xD0\xBD\xD0\xB0\xD1\x87\xD0\xB5\xD0\xBD\xD0\xB8\xD1\x8F"; // ТипЗначения
static const char* const kRu_Width       = "\xD0\xA8\xD0\xB8\xD1\x80\xD0\xB8\xD0\xBD\xD0\xB0"; // Ширина

//======================================================================
//  Column
//======================================================================
// FindProp returns the APPEND-POSITION index, and GetPropVal/SetPropVal receive that
// position (NOT any explicit number passed to AppendProp). So English names are appended
// first (positions 0..3) and their Russian aliases next (positions 4..7) in the SAME order,
// and dispatch is `position % 4`.
void ibValueTreeColumn_BindNames(ibValue::ibMemberTable& helper, const ibValue* /*ctx*/)
{
	helper.AppendProp(wxT("Name"));       // 0
	helper.AppendProp(wxT("Title"));      // 1
	helper.AppendProp(wxT("ValueType"));  // 2
	helper.AppendProp(wxT("Width"));      // 3
	helper.AppendProp(RU(kRu_Name));      // 4
	helper.AppendProp(RU(kRu_Title));     // 5
	helper.AppendProp(RU(kRu_ValueType)); // 6
	helper.AppendProp(RU(kRu_Width));     // 7
}

bool ibValueTreeColumn::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	switch (lPropNum % 4) {
	case enName:      pvarPropVal = m_name;      return true;
	case enTitle:     pvarPropVal = m_title;     return true;
	case enValueType: pvarPropVal = m_valueType; return true;
	case enWidth:     pvarPropVal = (signed int)m_width; return true;
	}
	return false;
}

bool ibValueTreeColumn::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	switch (lPropNum % 4) {
	case enName:      m_name = varPropVal.GetString();  return true;
	case enTitle:     m_title = varPropVal.GetString(); return true;
	case enValueType: m_valueType = varPropVal;         return true;
	case enWidth:     m_width = varPropVal.GetUInteger(); return true;
	}
	return false;
}

//======================================================================
//  Column collection
//======================================================================
void ibValueTreeColumnCollection_BindNames(ibValue::ibMemberTable& helper, const ibValue* /*ctx*/)
{
	helper.AppendFunc(wxT("Add"), 4, wxT("Add(name, type?, title?, width?)"));
	helper.AppendFunc(wxT("Count"), wxT("Count()"));
	helper.AppendFunc(wxT("Find"), 1, wxT("Find(name)"));
	helper.AppendFunc(wxT("Get"), 1, wxT("Get(index)"));
	helper.AppendFunc(wxT("Delete"), 1, wxT("Delete(index)"));
	helper.AppendFunc(wxT("IndexOf"), 1, wxT("IndexOf(name)"));
	helper.AppendFunc(wxT("Clear"), wxT("Clear()"));

	helper.AliasMethod(RU(kRu_Add), wxT("Add"));
	helper.AliasMethod(RU(kRu_Count), wxT("Count"));
	helper.AliasMethod(RU(kRu_Find), wxT("Find"));
	helper.AliasMethod(RU(kRu_Get), wxT("Get"));
	helper.AliasMethod(RU(kRu_Delete), wxT("Delete"));
	helper.AliasMethod(RU(kRu_IndexOf), wxT("IndexOf"));
	helper.AliasMethod(RU(kRu_Clear), wxT("Clear"));
}

ibValueTreeColumn* ibValueTreeColumnCollection::Add(const wxString& name, const ibValue& valueType, const wxString& title, int width)
{
	ibValuePtr<ibValueTreeColumn> col(new ibValueTreeColumn(name, valueType, title, width));
	m_columns.push_back(col);
	if (m_owner != nullptr)
		m_owner->OnColumnsChanged();
	return &*col;
}

ibValueTreeColumn* ibValueTreeColumnCollection::Find(const wxString& name) const
{
	for (auto& c : m_columns)
		if (c->GetColumnName().CmpNoCase(name) == 0)
			return &*c;
	return nullptr;
}

long ibValueTreeColumnCollection::IndexOf(const wxString& name) const
{
	for (size_t i = 0; i < m_columns.size(); ++i)
		if (m_columns[i]->GetColumnName().CmpNoCase(name) == 0)
			return (long)i;
	return -1;
}

void ibValueTreeColumnCollection::Delete(unsigned int idx)
{
	if (idx >= m_columns.size())
		return;
	m_columns.erase(m_columns.begin() + idx);
	if (m_owner != nullptr)
		m_owner->OnColumnsChanged();
}

void ibValueTreeColumnCollection::Clear()
{
	m_columns.clear();
	if (m_owner != nullptr)
		m_owner->OnColumnsChanged();
}

bool ibValueTreeColumnCollection::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	switch (lMethodNum) {
	case enAdd: {
		const wxString name = (lSizeArray > 0 && paParams[0] != nullptr) ? paParams[0]->GetString() : wxString();
		const ibValue  type = (lSizeArray > 1 && paParams[1] != nullptr) ? *paParams[1] : ibValue();
		const wxString title = (lSizeArray > 2 && paParams[2] != nullptr) ? paParams[2]->GetString() : wxString();
		const int      width = (lSizeArray > 3 && paParams[3] != nullptr) ? paParams[3]->GetUInteger() : 0;
		Add(name, type, title, width);
		pvarRetValue = m_columns.back();
		return true;
	}
	case enCount:
		pvarRetValue = Count();
		return true;
	case enFind: {
		ibValueTreeColumn* c = (lSizeArray > 0) ? Find(paParams[0]->GetString()) : nullptr;
		if (c != nullptr) pvarRetValue = ibValuePtr<ibValueTreeColumn>(c);
		else              pvarRetValue = ibValue();
		return true;
	}
	case enGet: {
		ibValueTreeColumn* c = (lSizeArray > 0) ? Get(paParams[0]->GetUInteger()) : nullptr;
		if (c != nullptr) pvarRetValue = ibValuePtr<ibValueTreeColumn>(c);
		else              pvarRetValue = ibValue();
		return true;
	}
	case enDelete:
		if (lSizeArray > 0) Delete(paParams[0]->GetUInteger());
		return true;
	case enIndexOf:
		pvarRetValue = (signed int)((lSizeArray > 0) ? IndexOf(paParams[0]->GetString()) : -1);
		return true;
	case enClear:
		Clear();
		return true;
	}
	return false;
}

bool ibValueTreeColumnCollection::GetAt(const ibValue& varKeyValue, ibValue& pvarValue)
{
	ibValueTreeColumn* c = nullptr;
	if (varKeyValue.GetType() == ibValueTypes::TYPE_STRING)
		c = Find(varKeyValue.GetString());
	else
		c = Get(varKeyValue.GetUInteger());
	if (c == nullptr)
		return false;
	pvarValue = ibValuePtr<ibValueTreeColumn>(c);
	return true;
}

std::shared_ptr<ibValueIteratorState> ibValueTreeColumnCollection::CreateIterator()
{
	class It : public ibValueIteratorState {
	public:
		explicit It(const std::vector<ibValuePtr<ibValueTreeColumn>>& l) : m_list(l) {}
		bool MoveNext(ibValue& current) override {
			if (m_started) ++m_pos; else m_started = true;
			if (m_pos >= m_list.size()) return false;
			current = m_list[m_pos];
			return true;
		}
		void Reset() override { m_pos = 0; m_started = false; }
	private:
		const std::vector<ibValuePtr<ibValueTreeColumn>>& m_list;
		size_t m_pos = 0; bool m_started = false;
	};
	return std::make_shared<It>(m_columns);
}

//======================================================================
//  Row
//======================================================================
ibValueTreeRow::ibValueTreeRow(ibValueTree* tree, ibValueTreeRow* parent)
	: ibValueDynamicMembers(ibValueTypes::TYPE_VALUE), m_tree(tree), m_parent(parent)
{
	m_children = new ibValueTreeRowCollection(tree, this);
	m_members.Bind(this, &ibValueTreeRow::FillMembers);
}

ibValueTreeRowCollection* ibValueTreeRow::GetChildRows() const { return &*m_children; }

unsigned int ibValueTreeRow::GetLevel() const
{
	unsigned int level = 0;
	for (const ibValueTreeRow* p = m_parent; p != nullptr; p = p->m_parent)
		++level;
	return level;
}

void ibValueTreeRow::FillMembers(ibMemberTable& helper) const
{
	// One property per column (row.ColumnName), positions 0..colCount-1 == column index.
	if (m_tree != nullptr) {
		ibValueTreeColumnCollection* cols = m_tree->GetColumns();
		for (unsigned int i = 0; i < cols->Count(); ++i)
			helper.AppendProp(cols->Get(i)->GetColumnName());
	}
	// Fixed members follow (positions colCount+0..+5), grouped English/Russian.
	helper.AppendProp(wxT("Rows"));      helper.AppendProp(RU(kRu_Rows));      // +0 / +1  → children
	helper.AppendProp(wxT("Parent"));    helper.AppendProp(RU(kRu_Parent));    // +2 / +3  → parent
	helper.AppendProp(wxT("Owner"));     helper.AppendProp(RU(kRu_Owner));     // +4 / +5  → owner
	helper.AppendFunc(wxT("Level"), wxT("Level()"));
	helper.AliasMethod(RU(kRu_Level), wxT("Level"));
}

bool ibValueTreeRow::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	const long colCount = (m_tree != nullptr) ? (long)m_tree->GetColumns()->Count() : 0;
	if (lPropNum >= 0 && lPropNum < colCount) {   // column value by index
		const wxString key = m_tree->GetColumns()->Get(lPropNum)->GetColumnName().Lower();
		auto it = m_values.find(key);
		pvarPropVal = (it != m_values.end()) ? it->second : ibValue();
		return true;
	}
	switch ((lPropNum - colCount) / 2) {   // fixed members: Rows(0), Parent(1), Owner(2)
	case 0:
		pvarPropVal = m_children;
		return true;
	case 1:
		if (m_parent != nullptr)    pvarPropVal = ibValuePtr<ibValueTreeRow>(m_parent);
		else if (m_tree != nullptr) pvarPropVal = ibValuePtr<ibValueTree>(m_tree);
		else                        pvarPropVal = ibValue();
		return true;
	case 2:
		if (m_tree != nullptr) pvarPropVal = ibValuePtr<ibValueTree>(m_tree);
		else                   pvarPropVal = ibValue();
		return true;
	}
	return false;
}

bool ibValueTreeRow::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	const long colCount = (m_tree != nullptr) ? (long)m_tree->GetColumns()->Count() : 0;
	if (lPropNum >= 0 && lPropNum < colCount) {
		const wxString key = m_tree->GetColumns()->Get(lPropNum)->GetColumnName().Lower();
		m_values[key] = varPropVal;
		return true;
	}
	return false;   // Rows / Parent / Owner are read-only
}

bool ibValueTreeRow::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** /*paParams*/, const long /*lSizeArray*/)
{
	if (lMethodNum == enLevel) {
		pvarRetValue = (signed int)GetLevel();
		return true;
	}
	return false;
}

//======================================================================
//  Row collection
//======================================================================
void ibValueTreeRowCollection_BindNames(ibValue::ibMemberTable& helper, const ibValue* /*ctx*/)
{
	helper.AppendFunc(wxT("Add"), wxT("Add()"));
	helper.AppendFunc(wxT("Insert"), 1, wxT("Insert(index)"));
	helper.AppendFunc(wxT("Count"), wxT("Count()"));
	helper.AppendFunc(wxT("Get"), 1, wxT("Get(index)"));
	helper.AppendFunc(wxT("IndexOf"), 1, wxT("IndexOf(row)"));
	helper.AppendFunc(wxT("Clear"), wxT("Clear()"));

	helper.AliasMethod(RU(kRu_Add), wxT("Add"));
	helper.AliasMethod(RU(kRu_Insert), wxT("Insert"));
	helper.AliasMethod(RU(kRu_Count), wxT("Count"));
	helper.AliasMethod(RU(kRu_Get), wxT("Get"));
	helper.AliasMethod(RU(kRu_IndexOf), wxT("IndexOf"));
	helper.AliasMethod(RU(kRu_Clear), wxT("Clear"));
}

ibValueTreeRow* ibValueTreeRowCollection::Add()
{
	ibValuePtr<ibValueTreeRow> row(new ibValueTreeRow(m_tree, m_parentRow));
	m_rows.push_back(row);
	return &*row;
}

ibValueTreeRow* ibValueTreeRowCollection::Insert(unsigned int idx)
{
	if (idx > m_rows.size())
		idx = (unsigned int)m_rows.size();
	ibValuePtr<ibValueTreeRow> row(new ibValueTreeRow(m_tree, m_parentRow));
	m_rows.insert(m_rows.begin() + idx, row);
	return &*row;
}

long ibValueTreeRowCollection::IndexOf(const ibValueTreeRow* row) const
{
	for (size_t i = 0; i < m_rows.size(); ++i)
		if (&*m_rows[i] == row)
			return (long)i;
	return -1;
}

void ibValueTreeRowCollection::InvalidateRowMembers()
{
	for (auto& r : m_rows) {
		r->InvalidateMembers();
		r->GetChildRows()->InvalidateRowMembers();
	}
}

bool ibValueTreeRowCollection::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	switch (lMethodNum) {
	case enAdd:
		Add();
		pvarRetValue = m_rows.back();
		return true;
	case enInsert: {
		const unsigned int idx = (lSizeArray > 0 && paParams[0] != nullptr) ? paParams[0]->GetUInteger() : (unsigned int)m_rows.size();
		Insert(idx);
		// Return the just-inserted row.
		const unsigned int at = (idx > m_rows.size() - 1) ? (unsigned int)m_rows.size() - 1 : idx;
		pvarRetValue = m_rows[at];
		return true;
	}
	case enCount:
		pvarRetValue = Count();
		return true;
	case enGet: {
		ibValueTreeRow* r = (lSizeArray > 0) ? Get(paParams[0]->GetUInteger()) : nullptr;
		if (r != nullptr) pvarRetValue = ibValuePtr<ibValueTreeRow>(r);
		else              pvarRetValue = ibValue();
		return true;
	}
	case enIndexOf: {
		long idx = -1;
		if (lSizeArray > 0 && paParams[0] != nullptr) {
			ibValueTreeRow* r = dynamic_cast<ibValueTreeRow*>(paParams[0]->GetRef());
			if (r != nullptr) idx = IndexOf(r);
		}
		pvarRetValue = (signed int)idx;
		return true;
	}
	case enClear:
		Clear();
		return true;
	}
	return false;
}

bool ibValueTreeRowCollection::GetAt(const ibValue& varKeyValue, ibValue& pvarValue)
{
	ibValueTreeRow* r = Get(varKeyValue.GetUInteger());
	if (r == nullptr)
		return false;
	pvarValue = ibValuePtr<ibValueTreeRow>(r);
	return true;
}

std::shared_ptr<ibValueIteratorState> ibValueTreeRowCollection::CreateIterator()
{
	class It : public ibValueIteratorState {
	public:
		explicit It(const std::vector<ibValuePtr<ibValueTreeRow>>& l) : m_list(l) {}
		bool MoveNext(ibValue& current) override {
			if (m_started) ++m_pos; else m_started = true;
			if (m_pos >= m_list.size()) return false;
			current = m_list[m_pos];
			return true;
		}
		void Reset() override { m_pos = 0; m_started = false; }
	private:
		const std::vector<ibValuePtr<ibValueTreeRow>>& m_list;
		size_t m_pos = 0; bool m_started = false;
	};
	return std::make_shared<It>(m_rows);
}

//======================================================================
//  Tree
//======================================================================
ibValueTree::ibValueTree()
	: ibValueDynamicMembers(ibValueTypes::TYPE_VALUE)
{
	m_columns = new ibValueTreeColumnCollection(this);
	m_rows = new ibValueTreeRowCollection(this, nullptr);
	m_members.Bind(this, &ibValueTree::FillMembers);
}

bool ibValueTree::Init() { return true; }
bool ibValueTree::Init(ibValue** /*paParams*/, const long /*lSizeArray*/) { return true; }

bool ibValueTree::IsEmpty() const
{
	return m_rows->Count() == 0;
}

void ibValueTree::FillMembers(ibMemberTable& helper) const
{
	// English first, then Russian aliases in the same order (dispatch by position % 2).
	helper.AppendProp(wxT("Columns"), true, false, 0L);   // 0
	helper.AppendProp(wxT("Rows"),    true, false, 0L);   // 1
	helper.AppendProp(RU(kRu_Columns), true, false, 0L);  // 2
	helper.AppendProp(RU(kRu_Rows),    true, false, 0L);  // 3
}

void ibValueTree::OnColumnsChanged()
{
	if (m_rows)
		m_rows->InvalidateRowMembers();
}

bool ibValueTree::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	switch (lPropNum % 2) {
	case kColumns: pvarPropVal = m_columns; return true;
	case kRows:    pvarPropVal = m_rows;    return true;
	}
	return false;
}

//======================================================================
//  Registration
//======================================================================
#include "backend/compiler/typeCtor.h"

VALUE_TYPE_REGISTER(ibValueTree, "ValueTree", g_valueTreeCLSID);
SYSTEM_TYPE_REGISTER(ibValueTreeColumn,           "ValueTreeColumn",  system_to_clsid("VL_TRCL"));
SYSTEM_TYPE_REGISTER(ibValueTreeColumnCollection, "ValueTreeColumns", system_to_clsid("VL_TRCC"));
SYSTEM_TYPE_REGISTER(ibValueTreeRow,              "ValueTreeRow",     system_to_clsid("VL_TRRW"));
SYSTEM_TYPE_REGISTER(ibValueTreeRowCollection,    "ValueTreeRows",    system_to_clsid("VL_TRRC"));
