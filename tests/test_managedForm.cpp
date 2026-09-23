// Managed-form compiler — element tree -> control-tree ibDataNode blob.
// Verifies the compiler emits the SAME node shape the runtime form loads
// (clsids, SizerItem nesting, Source bindings, group orientation). No live
// metadata: bindings are resolved by a stub. See docs/managed-form.md.

#include <gtest/gtest.h>

#include "backend/clsid.h"
#include "backend/serialize/dataBuilder.h"
#include "backend/managedForm/managedElement.h"
#include "backend/managedForm/managedFormCompiler.h"

namespace {

const ibClassID CT_TXTC = control_to_clsid("CT_TXTC");
const ibClassID CT_CHKB = control_to_clsid("CT_CHKB");
const ibClassID CT_CHOI = control_to_clsid("CT_CHOI");
const ibClassID CT_STTX = control_to_clsid("CT_STTX");
const ibClassID CT_BSZR = control_to_clsid("CT_BSZR");
const ibClassID CT_SSZER = control_to_clsid("CT_SSZER");
const ibClassID CT_SIZR = control_to_clsid("CT_SIZR");
const ibClassID CT_TABL = control_to_clsid("CT_TABL");
const ibClassID CT_TBLC = control_to_clsid("CT_TBLC");
const ibClassID CT_BUTN = control_to_clsid("CT_BUTN");
const ibClassID CT_NTBK = control_to_clsid("CT_NTBK");
const ibClassID CT_NTPG = control_to_clsid("CT_NTPG");

// A stub resolver: main attribute is id 1; each named field gets a distinct
// synthetic field metaId so a two-hop {1, fieldId} path is produced.
ibManagedFormCompiler::Resolver StubResolver() {
	return [](const wxString& dataPath) -> std::vector<ibSourceId> {
		if (dataPath.IsEmpty()) return {};
		int fieldId = 100 + static_cast<int>(dataPath.length());  // deterministic, non-zero
		return { 1, fieldId };
	};
}

// The single control node a sizerable child is wrapped in: SizerItem -> control.
const ibDataNode* ControlUnderCell(const ibDataNode& cell) {
	EXPECT_EQ(cell.GetClsid(), CT_SIZR);
	if (cell.Children().empty()) return nullptr;
	return &cell.Children().front();
}

} // namespace

// A plain group of two fields: root -> [SizerItem->Textctrl, SizerItem->Checkbox].
TEST(ManagedForm, PlainGroupTwoFields_NestingAndClsids) {
	ibManagedElement root(ibManagedNodeKind::Group);
	ibManagedElement f1(ibManagedNodeKind::Field, wxT("f1"));
	f1.dataPath = wxT("Description");
	f1.viewKind = ibFieldViewKind::Auto;          // -> textctrl
	ibManagedElement f2(ibManagedNodeKind::Field, wxT("f2"));
	f2.dataPath = wxT("Posted");
	f2.viewKind = ibFieldViewKind::CheckBoxField; // -> checkbox
	root.children = { f1, f2 };

	ibDataNode form;
	ibManagedFormCompiler comp(StubResolver());
	const int emitted = comp.Compile(form, root);

	EXPECT_EQ(emitted, 2);
	ASSERT_EQ(form.Children().size(), 2u);        // two SizerItem cells

	const ibDataNode* c1 = ControlUnderCell(form.Children()[0]);
	ASSERT_NE(c1, nullptr);
	EXPECT_EQ(c1->GetClsid(), CT_TXTC);
	EXPECT_EQ(c1->GetValue<wxString>(wxT("Name")), wxT("f1"));
	EXPECT_NE(c1->FindProperty(wxT("Source")), nullptr);   // bound

	const ibDataNode* c2 = ControlUnderCell(form.Children()[1]);
	ASSERT_NE(c2, nullptr);
	EXPECT_EQ(c2->GetClsid(), CT_CHKB);
	EXPECT_EQ(c2->GetValue<wxString>(wxT("Name")), wxT("f2"));
}

// ViewKind drives the control clsid.
TEST(ManagedForm, ViewKindSelectsControl) {
	ibManagedElement root(ibManagedNodeKind::Group);
	ibManagedElement choice(ibManagedNodeKind::Field, wxT("vat"));
	choice.dataPath = wxT("VatRate");
	choice.viewKind = ibFieldViewKind::ChoiceField;
	ibManagedElement label(ibManagedNodeKind::Field, wxT("note"));
	label.viewKind = ibFieldViewKind::LabelField;   // unbound label field
	root.children = { choice, label };

	ibDataNode form;
	ibManagedFormCompiler comp(StubResolver());
	comp.Compile(form, root);

	ASSERT_EQ(form.Children().size(), 2u);
	EXPECT_EQ(ControlUnderCell(form.Children()[0])->GetClsid(), CT_CHOI);
	EXPECT_EQ(ControlUnderCell(form.Children()[1])->GetClsid(), CT_STTX);
}

// A titled subgroup becomes a Staticboxsizer carrying its Title + Orient, and
// lays ITS fields out sizerable.
TEST(ManagedForm, TitledHorizontalSubgroup) {
	ibManagedElement root(ibManagedNodeKind::Group);
	ibManagedElement grp(ibManagedNodeKind::Group, wxT("head"));
	grp.representation = ibGroupRepresentation::TitledBox;
	grp.title = wxT("Header");
	grp.layout = ibGroupLayout::Horizontal;
	ibManagedElement fa(ibManagedNodeKind::Field, wxT("a")); fa.dataPath = wxT("Code");
	ibManagedElement fb(ibManagedNodeKind::Field, wxT("b")); fb.dataPath = wxT("Date");
	grp.children = { fa, fb };
	root.children = { grp };

	ibDataNode form;
	ibManagedFormCompiler comp(StubResolver());
	comp.Compile(form, root);

	ASSERT_EQ(form.Children().size(), 1u);
	const ibDataNode* box = ControlUnderCell(form.Children()[0]);
	ASSERT_NE(box, nullptr);
	EXPECT_EQ(box->GetClsid(), CT_SSZER);
	EXPECT_EQ(box->GetProp<wxString>(wxT("Title")), wxT("Header"));
	EXPECT_NE(box->FindProperty(wxT("Orient")), nullptr);
	ASSERT_EQ(box->Children().size(), 2u);   // two SizerItem cells inside the box
	EXPECT_EQ(ControlUnderCell(box->Children()[0])->GetClsid(), CT_TXTC);
	EXPECT_EQ(ControlUnderCell(box->Children()[1])->GetClsid(), CT_TXTC);
}

// A pages group -> Notebook with NotebookPage children, each holding its fields.
TEST(ManagedForm, PagesGroupBecomesNotebook) {
	ibManagedElement root(ibManagedNodeKind::Group);
	ibManagedElement pages(ibManagedNodeKind::Group, wxT("pages"));
	pages.representation = ibGroupRepresentation::Pages;
	ibManagedElement p1(ibManagedNodeKind::Page, wxT("p1")); p1.title = wxT("Main");
	ibManagedElement pf(ibManagedNodeKind::Field, wxT("x")); pf.dataPath = wxT("Code");
	p1.children = { pf };
	pages.children = { p1 };
	root.children = { pages };

	ibDataNode form;
	ibManagedFormCompiler comp(StubResolver());
	comp.Compile(form, root);

	ASSERT_EQ(form.Children().size(), 1u);
	const ibDataNode* nb = ControlUnderCell(form.Children()[0]);
	ASSERT_NE(nb, nullptr);
	EXPECT_EQ(nb->GetClsid(), CT_NTBK);
	ASSERT_EQ(nb->Children().size(), 1u);
	const ibDataNode& page = nb->Children().front();
	EXPECT_EQ(page.GetClsid(), CT_NTPG);
	ASSERT_EQ(page.Children().size(), 1u);          // one SizerItem cell in the page
	EXPECT_EQ(ControlUnderCell(page.Children()[0])->GetClsid(), CT_TXTC);
}

// A table -> tablebox with columns attached DIRECTLY (not wrapped in cells).
TEST(ManagedForm, TableWithColumns) {
	ibManagedElement root(ibManagedNodeKind::Group);
	ibManagedElement tab(ibManagedNodeKind::Table, wxT("lines"));
	tab.dataPath = wxT("Goods");
	ibManagedElement c1(ibManagedNodeKind::Column, wxT("nom")); c1.dataPath = wxT("Goods.Item");
	ibManagedElement c2(ibManagedNodeKind::Column, wxT("qty")); c2.dataPath = wxT("Goods.Qty");
	tab.children = { c1, c2 };
	root.children = { tab };

	ibDataNode form;
	ibManagedFormCompiler comp(StubResolver());
	comp.Compile(form, root);

	ASSERT_EQ(form.Children().size(), 1u);
	const ibDataNode* table = ControlUnderCell(form.Children()[0]);
	ASSERT_NE(table, nullptr);
	EXPECT_EQ(table->GetClsid(), CT_TABL);
	ASSERT_EQ(table->Children().size(), 2u);        // columns attach directly
	EXPECT_EQ(table->Children()[0].GetClsid(), CT_TBLC);
	EXPECT_EQ(table->Children()[1].GetClsid(), CT_TBLC);
}

// A button -> Button control (unbound Source; command binding lands with runtime open).
TEST(ManagedForm, ButtonEmitsButtonControl) {
	ibManagedElement root(ibManagedNodeKind::Group);
	ibManagedElement btn(ibManagedNodeKind::Button, wxT("ok"));
	btn.title = wxT("OK");
	root.children = { btn };

	ibDataNode form;
	ibManagedFormCompiler comp(StubResolver());
	comp.Compile(form, root);

	ASSERT_EQ(form.Children().size(), 1u);
	const ibDataNode* b = ControlUnderCell(form.Children()[0]);
	ASSERT_NE(b, nullptr);
	EXPECT_EQ(b->GetClsid(), CT_BUTN);
	EXPECT_EQ(b->GetProp<wxString>(wxT("Title")), wxT("OK"));
}
