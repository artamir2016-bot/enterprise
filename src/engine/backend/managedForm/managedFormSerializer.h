#ifndef __MANAGED_FORM_SERIALIZER_H__
#define __MANAGED_FORM_SERIALIZER_H__

// -----------------------------------------------------------------------------
// Serialise the managed-form element tree (managedElement.h) to / from an
// ibDataNode — the format-agnostic tree every metaobject uses. This is what the
// ManagedForm metatype's ReadData / WriteData ride on; the compiled control-tree
// FormData is a derived cache and is NOT stored here (see docs/managed-form.md §6).
// Backend, GUI-free.
// -----------------------------------------------------------------------------

#include <vector>

#include "backend/backend_core.h"
#include "managedElement.h"

class ibDataNode;

class BACKEND_API ibManagedFormSerializer {
public:
	// Write the element tree + the form-attribute list under `node` (two named
	// child sub-nodes: "Root" and "Attrs").
	static void Write(ibDataNode& node, const ibManagedElement& root,
		const std::vector<ibManagedAttribute>& attrs);

	// Read them back. Missing sub-nodes leave the outputs default (an old blob
	// with no attributes reads as an empty list, never an error).
	static void Read(const ibDataNode& node, ibManagedElement& root,
		std::vector<ibManagedAttribute>& attrs);

private:
	static void WriteElement(ibDataNode& node, const ibManagedElement& el);
	static void ReadElement(const ibDataNode& node, ibManagedElement& el);
};

#endif
