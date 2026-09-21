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
