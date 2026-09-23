#ifndef __MANAGED_FORM_COMPILER_H__
#define __MANAGED_FORM_COMPILER_H__

// -----------------------------------------------------------------------------
// The managed-form compiler: element tree (managedElement.h) -> the control-tree
// ibDataNode blob the runtime form already loads (the shape metadataConfigSpec.cpp
// emits). Group -> box / titled box / notebook; Field -> the control chosen by
// its ViewKind; Table -> tablebox + columns; Button -> button bound to a command.
//
// Bindings are resolved by a caller-supplied Resolver (dataPath -> metaId hop
// path), so the compiler is decoupled from live metadata and testable on its own.
// See docs/managed-form.md §4.
// -----------------------------------------------------------------------------

#include <functional>
#include <vector>

#include <wx/string.h>

#include "backend/backend_core.h"   // ibSourceId, ibMetaID
#include "managedElement.h"

class ibDataNode;

class BACKEND_API ibManagedFormCompiler {
public:
	// dataPath -> the ordered metaId hop path a control's Source binds through
	// (e.g. {mainAttrId, fieldMetaId} for an object-form field). An empty result
	// means "unbound" — the control is emitted without a Source.
	using Resolver = std::function<std::vector<ibSourceId>(const wxString& dataPath)>;

	explicit ibManagedFormCompiler(Resolver resolve) : m_resolve(std::move(resolve)) {}

	// Compile the element tree into `formRoot` (the form's control-tree root
	// node). Children of `root` become the form's top-level controls. Returns the
	// number of control nodes emitted (excludes SizerItem wrappers).
	int Compile(ibDataNode& formRoot, const ibManagedElement& root);

private:
	// Emit one element (and its subtree) under `parent`. `sizerable` says the
	// parent lays children out through a box (so each child rides a SizerItem);
	// a Notebook/Table parent adds pages/columns directly. `tableId` is the
	// enclosing tablebox section metaId (wxNOT_FOUND otherwise).
	void EmitElement(ibDataNode& parent, const ibManagedElement& el,
		bool sizerable, ibMetaID tableId);

	void EmitGroup(ibDataNode& parent, const ibManagedElement& el, bool sizerable);
	void EmitField(ibDataNode& parent, const ibManagedElement& el, bool sizerable, ibMetaID tableId);

	// Wrap a control in a SizerItem cell; returns the cell (the control is added
	// as ITS child). `expand` fills the cross axis.
	ibDataNode& AddSizerItem(ibDataNode& parent, bool expand);

	// The control clsid a field's ViewKind maps to (Auto -> text box for now;
	// the type-driven Auto rule needs live metadata and lands with runtime open).
	static ibClassID ClsidForViewKind(ibFieldViewKind viewKind);

	Resolver m_resolve;
	int      m_nextId = 2;   // 1 is the form root's own id; controls start at 2
	int      m_count  = 0;
};

#endif
