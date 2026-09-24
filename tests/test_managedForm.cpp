// Managed-form compiler — element tree -> control-tree ibDataNode blob.
// Verifies the compiler emits the SAME node shape the runtime form loads
// (clsids, SizerItem nesting, Source bindings, group orientation). No live
// metadata: bindings are resolved by a stub. See docs/managed-form.md.

#include <gtest/gtest.h>

#include "backend/clsid.h"
#include "backend/serialize/dataBuilder.h"
#include "backend/managedForm/managedElement.h"
#include "backend/managedForm/managedFormCompiler.h"
#include "backend/managedForm/managedFormSerializer.h"

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

// A PictureField compiles to the Picture control (CT_PICT).
TEST(ManagedForm, PictureFieldEmitsPictureControl) {
	ibManagedElement root(ibManagedNodeKind::Group);
	ibManagedElement pic(ibManagedNodeKind::Field, wxT("logo"));
	pic.viewKind = ibFieldViewKind::PictureField;
	root.children = { pic };

	ibDataNode form;
	ibManagedFormCompiler(StubResolver()).Compile(form, root);

	ASSERT_EQ(form.Children().size(), 1u);
	EXPECT_EQ(ControlUnderCell(form.Children()[0])->GetClsid(), control_to_clsid("CT_PICT"));
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

// --- serialization round-trip (1b-1) ----------------------------------------

namespace {

// A representative tree: a titled horizontal head group with two fields of
// different view kinds, a pages group, and a table with columns.
ibManagedElement SampleTree() {
	ibManagedElement root(ibManagedNodeKind::Group);

	ibManagedElement head(ibManagedNodeKind::Group, wxT("head"));
	head.representation = ibGroupRepresentation::TitledBox;
	head.title = wxT("Head");
	head.layout = ibGroupLayout::Horizontal;
	ibManagedElement code(ibManagedNodeKind::Field, wxT("code"));
	code.dataPath = wxT("Code"); code.viewKind = ibFieldViewKind::InputField; code.title = wxT("Code");
	ibManagedElement vat(ibManagedNodeKind::Field, wxT("vat"));
	vat.dataPath = wxT("Vat"); vat.viewKind = ibFieldViewKind::ChoiceField;
	head.children = { code, vat };

	ibManagedElement tab(ibManagedNodeKind::Table, wxT("lines"));
	tab.dataPath = wxT("Goods");
	ibManagedElement c1(ibManagedNodeKind::Column, wxT("item")); c1.dataPath = wxT("Goods.Item");
	ibManagedElement c2(ibManagedNodeKind::Column, wxT("qty"));  c2.dataPath = wxT("Goods.Qty");
	tab.children = { c1, c2 };

	root.children = { head, tab };
	return root;
}

void ExpectSameElement(const ibManagedElement& a, const ibManagedElement& b) {
	EXPECT_EQ(static_cast<int>(a.kind), static_cast<int>(b.kind));
	EXPECT_EQ(a.name, b.name);
	EXPECT_EQ(a.title, b.title);
	EXPECT_EQ(a.dataPath, b.dataPath);
	EXPECT_EQ(static_cast<int>(a.viewKind), static_cast<int>(b.viewKind));
	EXPECT_EQ(static_cast<int>(a.layout), static_cast<int>(b.layout));
	EXPECT_EQ(static_cast<int>(a.representation), static_cast<int>(b.representation));
	EXPECT_EQ(a.commandId, b.commandId);
	ASSERT_EQ(a.children.size(), b.children.size());
	for (std::size_t i = 0; i < a.children.size(); ++i)
		ExpectSameElement(a.children[i], b.children[i]);
}

} // namespace

// Write -> Read reproduces the element tree exactly (every field, full nesting).
TEST(ManagedForm, SerializerRoundTripTree) {
	const ibManagedElement original = SampleTree();
	std::vector<ibManagedAttribute> attrs = {
		{ wxT("Object"), 1, true },
		{ wxT("Helper"), 42, false },
	};

	ibDataNode node;
	ibManagedFormSerializer::Write(node, original, attrs);

	ibManagedElement restored;
	std::vector<ibManagedAttribute> restoredAttrs;
	ibManagedFormSerializer::Read(node, restored, restoredAttrs);

	ExpectSameElement(original, restored);
	ASSERT_EQ(restoredAttrs.size(), 2u);
	EXPECT_EQ(restoredAttrs[0].name, wxT("Object"));
	EXPECT_EQ(restoredAttrs[0].id, 1);
	EXPECT_TRUE(restoredAttrs[0].isMain);
	EXPECT_EQ(restoredAttrs[1].name, wxT("Helper"));
	EXPECT_EQ(restoredAttrs[1].id, 42);
	EXPECT_FALSE(restoredAttrs[1].isMain);
}

// A tree that survived serialization compiles to the SAME control-tree shape as
// the original — the serializer preserves everything the compiler reads.
TEST(ManagedForm, SerializedTreeCompilesIdentically) {
	const ibManagedElement original = SampleTree();

	ibDataNode node;
	ibManagedFormSerializer::Write(node, original, {});
	ibManagedElement restored;
	std::vector<ibManagedAttribute> ignore;
	ibManagedFormSerializer::Read(node, restored, ignore);

	ibDataNode formA, formB;
	ibManagedFormCompiler(StubResolver()).Compile(formA, original);
	ibManagedFormCompiler(StubResolver()).Compile(formB, restored);

	ASSERT_EQ(formA.Children().size(), formB.Children().size());
	// top level: [SizerItem->StaticBox(head), SizerItem->Tablebox(lines)]
	EXPECT_EQ(ControlUnderCell(formA.Children()[0])->GetClsid(),
	          ControlUnderCell(formB.Children()[0])->GetClsid());
	EXPECT_EQ(ControlUnderCell(formA.Children()[1])->GetClsid(),
	          ControlUnderCell(formB.Children()[1])->GetClsid());
	// the table's columns survived (2 CT_TBLC each)
	const ibDataNode* tableA = ControlUnderCell(formA.Children()[1]);
	const ibDataNode* tableB = ControlUnderCell(formB.Children()[1]);
	EXPECT_EQ(tableA->Children().size(), tableB->Children().size());
	EXPECT_EQ(tableB->Children().size(), 2u);
}
