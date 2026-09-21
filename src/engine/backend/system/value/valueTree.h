#ifndef __VALUE_TREE_H__
#define __VALUE_TREE_H__

#include "backend/compiler/value.h"

#include <map>
#include <vector>

// ДеревоЗначений (ValueTree) — an in-memory hierarchical table: a set of named
// COLUMNS and a tree of ROWS, each row carrying a value per column and its own
// child rows. The 1C-idiomatic shape: `Дерево.Колонки.Добавить("X")`,
// `Стр = Дерево.Строки.Добавить()`, `Стр.X = 1`, `Подстр = Стр.Строки.Добавить()`.
//
// Deliberately standalone (NOT the form-bound ibValueModelTable machinery): a
// value tree is a computation structure, so it is built on the plain per-instance
// dynamic-member base. Columns surface on each row as NAMED properties
// (row.ColumnName) via the row's own member table, rebuilt when columns change.

constexpr ibClassID g_valueTreeCLSID = value_to_clsid("VL_TREE");

class ibValueTree;
class ibValueTreeRow;
class ibValueTreeRowCollection;

// --- a single column ---------------------------------------------------------
void ibValueTreeColumn_BindNames(ibValue::ibMemberTable& helper, const ibValue* ctx);

class BACKEND_API ibValueTreeColumn : public ibValueStaticMembers<&ibValueTreeColumn_BindNames> {
	enum Prop { enName = 0, enTitle, enValueType, enWidth };
public:
	ibValueTreeColumn() : ibValueStaticMembers(ibValueTypes::TYPE_VALUE) {}
	ibValueTreeColumn(const wxString& name, const ibValue& valueType, const wxString& title, int width)
		: ibValueStaticMembers(ibValueTypes::TYPE_VALUE), m_name(name), m_valueType(valueType),
		  m_title(title.IsEmpty() ? name : title), m_width(width) {}

	const wxString& GetColumnName() const { return m_name; }
	const wxString& GetColumnTitle() const { return m_title; }

	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal) override;
	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal) override;

private:
	wxString m_name;
	ibValue  m_valueType;
	wxString m_title;
	int      m_width = 0;
};

// --- the columns collection --------------------------------------------------
void ibValueTreeColumnCollection_BindNames(ibValue::ibMemberTable& helper, const ibValue* ctx);

class BACKEND_API ibValueTreeColumnCollection : public ibValueStaticMembers<&ibValueTreeColumnCollection_BindNames> {
	enum Func { enAdd = 0, enCount, enFind, enGet, enDelete, enIndexOf, enClear };
public:
	explicit ibValueTreeColumnCollection(ibValueTree* owner = nullptr)
		: ibValueStaticMembers(ibValueTypes::TYPE_VALUE), m_owner(owner) {}
	virtual ~ibValueTreeColumnCollection() {}

	virtual bool IsEmpty() const override { return m_columns.empty(); }

	ibValueTreeColumn* Add(const wxString& name, const ibValue& valueType, const wxString& title, int width);
	unsigned int Count() const { return (unsigned int)m_columns.size(); }
	ibValueTreeColumn* Get(unsigned int idx) const { return idx < m_columns.size() ? &*m_columns[idx] : nullptr; }
	ibValueTreeColumn* Find(const wxString& name) const;
	long IndexOf(const wxString& name) const;
	void Delete(unsigned int idx);
	void Clear();

	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray) override;
	virtual bool GetAt(const ibValue& varKeyValue, ibValue& pvarValue) override;

	virtual std::shared_ptr<ibValueIteratorState> CreateIterator() override;

private:
	ibValueTree* m_owner;   // non-owning — the tree owns this collection
	std::vector<ibValuePtr<ibValueTreeColumn>> m_columns;
};

// --- one row (holds a value per column + its own child rows) -----------------
class BACKEND_API ibValueTreeRow : public ibValueDynamicMembers {
	// Fixed members numbered high so they never collide with column props (0..n-1).
	enum { kRows = 1000000, kParent, kOwner };
	enum Func { enLevel = 0 };
public:
	ibValueTreeRow(ibValueTree* tree, ibValueTreeRow* parent);
	virtual ~ibValueTreeRow() {}

	void FillMembers(ibMemberTable& helper) const;

	ibValueTreeRow* GetParentRow() const { return m_parent; }
	ibValueTree*    GetOwnerTree() const { return m_tree; }
	unsigned int    GetLevel() const;
	ibValueTreeRowCollection* GetChildRows() const;   // defined in .cpp (collection is incomplete here)

	// Refresh the named-column members when the tree's columns change.
	void InvalidateMembers() { m_members.Invalidate(); }

	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal) override;
	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal) override;
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray) override;

private:
	ibValueTree*     m_tree;    // non-owning
	ibValueTreeRow*  m_parent;  // non-owning (nullptr = top-level)
	std::map<wxString, ibValue> m_values;   // key = lowercased column name
	ibValuePtr<ibValueTreeRowCollection> m_children;
};

// --- a collection of rows (the tree's root set, or a row's children) ---------
void ibValueTreeRowCollection_BindNames(ibValue::ibMemberTable& helper, const ibValue* ctx);

class BACKEND_API ibValueTreeRowCollection : public ibValueStaticMembers<&ibValueTreeRowCollection_BindNames> {
	enum Func { enAdd = 0, enInsert, enCount, enGet, enIndexOf, enClear };
public:
	ibValueTreeRowCollection(ibValueTree* tree = nullptr, ibValueTreeRow* parentRow = nullptr)
		: ibValueStaticMembers(ibValueTypes::TYPE_VALUE), m_tree(tree), m_parentRow(parentRow) {}
	virtual ~ibValueTreeRowCollection() {}

	virtual bool IsEmpty() const override { return m_rows.empty(); }

	ibValueTreeRow* Add();
	ibValueTreeRow* Insert(unsigned int idx);
	unsigned int Count() const { return (unsigned int)m_rows.size(); }
	ibValueTreeRow* Get(unsigned int idx) const { return idx < m_rows.size() ? &*m_rows[idx] : nullptr; }
	long IndexOf(const ibValueTreeRow* row) const;
	void Clear() { m_rows.clear(); }

	// Rebuild the named-column members of every row in this collection (recursive).
	void InvalidateRowMembers();

	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray) override;
	virtual bool GetAt(const ibValue& varKeyValue, ibValue& pvarValue) override;

	virtual std::shared_ptr<ibValueIteratorState> CreateIterator() override;

private:
	ibValueTree*    m_tree;       // non-owning
	ibValueTreeRow* m_parentRow;  // non-owning (nullptr = the tree's root set)
	std::vector<ibValuePtr<ibValueTreeRow>> m_rows;
};

// --- the tree ----------------------------------------------------------------
class BACKEND_API ibValueTree : public ibValueDynamicMembers {
	enum { kColumns = 0, kRows };
public:
	ibValueTree();
	virtual ~ibValueTree() {}

	virtual bool Init() override;
	virtual bool Init(ibValue** paParams, const long lSizeArray) override;

	void FillMembers(ibMemberTable& helper) const;

	ibValueTreeColumnCollection* GetColumns() const { return &*m_columns; }
	ibValueTreeRowCollection*    GetRows() const { return &*m_rows; }

	// Called by the columns collection when a column is added/removed: every row
	// must resurface its named-column members.
	void OnColumnsChanged();

	virtual bool IsEmpty() const override;
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal) override;

private:
	ibValuePtr<ibValueTreeColumnCollection> m_columns;
	ibValuePtr<ibValueTreeRowCollection>    m_rows;
};

#endif
