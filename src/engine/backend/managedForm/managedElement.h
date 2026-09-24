#ifndef __MANAGED_ELEMENT_H__
#define __MANAGED_ELEMENT_H__

// -----------------------------------------------------------------------------
// The managed-form element model — a declarative form tree (1C «управляемая
// форма»): Group / Field / Table / Column / Button / Decoration nodes bound to
// form attributes, with NO geometry. This is the SOURCE model; the compiler
// (managedFormCompiler.h) turns it into the existing control-tree ibDataNode
// blob that the runtime form already loads, so desktop + web rendering come for
// free. See docs/managed-form.md.
//
// Backend, GUI-free on purpose: the compiler is backend code so wenterprise-server
// (web) builds it too.
// -----------------------------------------------------------------------------

#include <vector>
#include <wx/string.h>

#include "backend/backend_core.h"   // ibMetaID, ibSourceId

// What a node IS.
enum class ibManagedNodeKind {
	Group,        // a container — box / titled box / pages, by representation below
	Page,         // one page inside a Pages group
	Field,        // a bound field; its control is chosen by ViewKind
	Table,        // a bound tabular section (tablebox)
	Column,       // a table column (a field inside a Table)
	Button,       // a command button
	Decoration,   // an unbound label / picture
};

// 1C ВидПоля — how a bound field DRAWS, chosen declaratively instead of by the
// source type. `Auto` keeps the current type-driven choice (the default).
enum class ibFieldViewKind {
	Auto = 0,
	InputField,     // text box (+ select button for references)
	LabelField,     // read-only presentation
	CheckBoxField,
	RadioField,
	PictureField,
	ChoiceField,    // drop-down list (CT_CHOI)
	ComboBoxField,  // editable drop-down (CT_CMBB)
	ReferenceField, // an input for a reference value — text box WITH a select button
};

// A Group's child stacking axis.
enum class ibGroupLayout {
	Vertical,
	Horizontal,
};

// How a Group presents itself.
enum class ibGroupRepresentation {
	Plain,     // a layout-only box (CT_BSZR)
	TitledBox, // a bordered, captioned frame (CT_SSZER)
	Pages,     // a notebook (CT_NTBK) whose children are Pages (CT_NTPG)
};

// One node of the managed form tree. A plain aggregate — no behaviour, so it
// serialises and copies trivially and the compiler is the only thing that reads
// its meaning.
struct ibManagedElement {
	ibManagedNodeKind     kind = ibManagedNodeKind::Group;
	wxString              name;        // element name (designer identity)
	wxString              title;       // caption (group title, field label, button text)

	// Binding — the name of the form attribute (or attribute.field) this node
	// draws. Empty for Decoration and layout-only Groups. Resolved to a metaId
	// hop path by the compiler's resolver.
	wxString              dataPath;

	// Field / Column: how it draws.
	ibFieldViewKind       viewKind = ibFieldViewKind::Auto;

	// Group: stacking axis + presentation.
	ibGroupLayout         layout = ibGroupLayout::Vertical;
	ibGroupRepresentation representation = ibGroupRepresentation::Plain;

	// Button: the form command it runs (its form-unique command id).
	ibMetaID              commandId = wxNOT_FOUND;

	std::vector<ibManagedElement> children;

	ibManagedElement() = default;
	ibManagedElement(ibManagedNodeKind k, const wxString& n = wxEmptyString)
		: kind(k), name(n) {}
};

// A form attribute declaration — the data the form binds to (1C «реквизит
// формы»). The MAIN attribute (isMain, conventionally id 1) is what an object
// form's fields hop through.
struct ibManagedAttribute {
	wxString  name;
	ibMetaID  id      = wxNOT_FOUND;   // the attribute's own metaId (assigned by the owner)
	bool      isMain  = false;
};

#endif
