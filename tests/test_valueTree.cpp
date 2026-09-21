// =============================================================================
// OES Enterprise — ibValueTree (ДеревоЗначений) tests
//
// ibValueTree (backend/system/value/valueTree.h) is the script ValueTree value:
// a set of named COLUMNS and a hierarchy of ROWS, each row carrying a value per
// column and its own child rows. Columns surface on each row as NAMED properties
// (row.ColumnName) resolved through the row's per-instance member table.
// Pure (no DB).
// =============================================================================

#include <gtest/gtest.h>
#include "backend/system/value/valueTree.h"
#include "backend/compiler/compileCode.h"
#include "backend/compiler/procUnit.h"
#include "backend/backend_exception.h"

TEST(ValueTree, EmptyByDefault) {
    ibValueTree t;
    EXPECT_TRUE(t.IsEmpty());
    ASSERT_NE(t.GetColumns(), nullptr);
    ASSERT_NE(t.GetRows(), nullptr);
    EXPECT_EQ(t.GetColumns()->Count(), 0u);
    EXPECT_EQ(t.GetRows()->Count(), 0u);
}

TEST(ValueTree, AddColumns) {
    ibValueTree t;
    t.GetColumns()->Add(wxT("Item"), ibValue(), wxString(), 0);
    t.GetColumns()->Add(wxT("Qty"),  ibValue(), wxString(), 0);
    EXPECT_EQ(t.GetColumns()->Count(), 2u);
    EXPECT_NE(t.GetColumns()->Find(wxT("Qty")), nullptr);
    EXPECT_EQ(t.GetColumns()->IndexOf(wxT("Item")), 0);
    EXPECT_EQ(t.GetColumns()->Find(wxT("qty")), t.GetColumns()->Get(1));  // case-insensitive
}

TEST(ValueTree, AddRowsAndColumnValueByName) {
    ibValueTree t;
    t.GetColumns()->Add(wxT("Item"), ibValue(), wxString(), 0);
    t.GetColumns()->Add(wxT("Qty"),  ibValue(), wxString(), 0);

    ibValueTreeRow* row = t.GetRows()->Add();
    ASSERT_NE(row, nullptr);
    EXPECT_EQ(t.GetRows()->Count(), 1u);

    // Column value access by NAME through the row's member table.
    const long numItem = row->FindProp(wxT("Item"));
    const long numQty  = row->FindProp(wxT("Qty"));
    ASSERT_GE(numItem, 0);
    ASSERT_GE(numQty, 0);

    EXPECT_TRUE(row->SetPropVal(numItem, ibValue(wxString(wxT("Milk")))));
    EXPECT_TRUE(row->SetPropVal(numQty,  ibValue(ibNumber(5))));

    ibValue got;
    EXPECT_TRUE(row->GetPropVal(numItem, got));
    EXPECT_EQ(got.GetString(), wxT("Milk"));
    EXPECT_TRUE(row->GetPropVal(numQty, got));
    EXPECT_EQ(got.GetInteger(), 5);
}

TEST(ValueTree, HierarchyLevelAndParent) {
    ibValueTree t;
    t.GetColumns()->Add(wxT("Item"), ibValue(), wxString(), 0);

    ibValueTreeRow* top = t.GetRows()->Add();
    ibValueTreeRow* child = top->GetChildRows()->Add();
    ibValueTreeRow* grand = child->GetChildRows()->Add();

    EXPECT_EQ(top->GetLevel(),   0u);
    EXPECT_EQ(child->GetLevel(), 1u);
    EXPECT_EQ(grand->GetLevel(), 2u);

    EXPECT_EQ(child->GetParentRow(), top);
    EXPECT_EQ(grand->GetParentRow(), child);
    EXPECT_EQ(top->GetParentRow(),   nullptr);   // top-level: no parent row
    EXPECT_EQ(top->GetOwnerTree(),   &t);
}

TEST(ValueTree, ColumnAddedAfterRowStillSurfaces) {
    ibValueTree t;
    ibValueTreeRow* row = t.GetRows()->Add();   // row created BEFORE the column
    EXPECT_EQ(row->FindProp(wxT("Late")), wxNOT_FOUND);

    t.GetColumns()->Add(wxT("Late"), ibValue(), wxString(), 0);   // triggers OnColumnsChanged
    const long num = row->FindProp(wxT("Late"));
    ASSERT_GE(num, 0);
    EXPECT_TRUE(row->SetPropVal(num, ibValue(ibNumber(42))));
    ibValue got;
    EXPECT_TRUE(row->GetPropVal(num, got));
    EXPECT_EQ(got.GetInteger(), 42);
}

TEST(ValueTree, ClearRowsAndColumns) {
    ibValueTree t;
    t.GetColumns()->Add(wxT("A"), ibValue(), wxString(), 0);
    t.GetRows()->Add();
    t.GetRows()->Add();
    EXPECT_EQ(t.GetRows()->Count(), 2u);
    t.GetRows()->Clear();
    EXPECT_EQ(t.GetRows()->Count(), 0u);
    t.GetColumns()->Clear();
    EXPECT_EQ(t.GetColumns()->Count(), 0u);
}

// ===========================================================================
// Runtime through the real compiler + interpreter (headless). Reproduces the
// GUI path: 1C-name translation, ctor, chained collection calls, member access.
// ===========================================================================
namespace {
::testing::AssertionResult RunProg(const wxString& src, ibProcUnit& pu) {
    ibCompileCode cc(wxT("t"), wxT("mem"), false);
    try {
        if (!cc.Compile(src))
            return ::testing::AssertionFailure() << "Compile returned false";
    } catch (const ibBackendException& e) {
        return ::testing::AssertionFailure() << "compile: " << e.GetErrorDescription().ToUTF8().data();
    }
    try {
        pu.Execute(cc.m_cByteCode);
    } catch (const ibBackendException& e) {
        return ::testing::AssertionFailure() << "run: " << e.GetErrorDescription().ToUTF8().data();
    }
    return ::testing::AssertionSuccess();
}
}

// t = Новый ДеревоЗначений;\n  — construction always via the (parsing) Russian keyword,
// so the variants below differ ONLY in the prop/method name language.
#define TREE_NEW "var n public;\nvar t public;\nt = \xD0\x9D\xD0\xBE\xD0\xB2\xD1\x8B\xD0\xB9 \xD0\x94\xD0\xB5\xD1\x80\xD0\xB5\xD0\xB2\xD0\xBE\xD0\x97\xD0\xBD\xD0\xB0\xD1\x87\xD0\xB5\xD0\xBD\xD0\xB8\xD0\xB9;\n"
#define RU_COL  "\xD0\x9A\xD0\xBE\xD0\xBB\xD0\xBE\xD0\xBD\xD0\xBA\xD0\xB8"          // Колонки
#define RU_ADD  "\xD0\x94\xD0\xBE\xD0\xB1\xD0\xB0\xD0\xB2\xD0\xB8\xD1\x82\xD1\x8C" // Добавить

TEST(ValueTreeRuntime, EnglishPropEnglishMethod) {
    ibProcUnit pu;
    EXPECT_TRUE(RunProg(wxString::FromUTF8(TREE_NEW "t.Columns.Add(\"Item\");\n"), pu));
}
TEST(ValueTreeRuntime, RussianPropEnglishMethod) {
    ibProcUnit pu;
    EXPECT_TRUE(RunProg(wxString::FromUTF8(TREE_NEW "t." RU_COL ".Add(\"Item\");\n"), pu));
}
TEST(ValueTreeRuntime, EnglishPropRussianMethod) {
    ibProcUnit pu;
    EXPECT_TRUE(RunProg(wxString::FromUTF8(TREE_NEW "t.Columns." RU_ADD "(\"Item\");\n"), pu));
}
TEST(ValueTreeRuntime, RussianPropRussianMethod) {
    ibProcUnit pu;
    EXPECT_TRUE(RunProg(wxString::FromUTF8(TREE_NEW "t." RU_COL "." RU_ADD "(\"Item\");\n"), pu));
}
