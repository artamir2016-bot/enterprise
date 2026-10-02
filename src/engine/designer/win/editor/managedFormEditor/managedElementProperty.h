#ifndef __MANAGED_ELEMENT_PROPERTY_H__
#define __MANAGED_ELEMENT_PROPERTY_H__

// ----------------------------------------------------------------------------
// ibManagedElementProperty — a property-object ADAPTER over one ibManagedElement
// node of a managed form, so the selected element edits in the shared
// objectInspector exactly like any metaobject/control. The element is a plain
// struct (backend/managedForm/managedElement.h); this wraps it and surfaces
// Name / Title / DataPath / ViewKind / Layout / Representation, hiding the ones
// that do not apply to the node's kind. Edits write straight back into the
// element and tell the editor to relabel + mark modified. Designer-side (wx GUI).
// ----------------------------------------------------------------------------

#include "backend/propertyManager/propertyObject.h"
#include "backend/propertyManager/property/propertyString.h"
#include "backend/propertyManager/property/propertyList.h"
#include "backend/managedForm/managedElement.h"

#include <vector>

class ibManagedFormEditor;

class ibManagedElementProperty : public ibPropertyObject {
public:

	ibManagedElementProperty(ibManagedElement* element, ibManagedFormEditor* editor);

	// ibPropertyObject contract.
	virtual wxString GetClassName() const override { return wxT("ManagedElement"); }
	virtual wxString GetObjectTypeName() const override { return _("Element"); }
	virtual bool IsEditable() const override;

	virtual void OnPropertyChanged(ibProperty* property, const wxVariant& oldValue, const wxVariant& newValue) override;
	virtual void OnPropertyRefresh() override;

	ibManagedElement* GetElement() const { return m_element; }

	// List fill callbacks (ibPropertyList functor form).
	bool FillViewKind(ibPropertyList* prop);
	bool FillLayout(ibPropertyList* prop);
	bool FillRepresentation(ibPropertyList* prop);
	// Fills the DataPath dropdown from the owner object's attributes (editor->AvailableBindings),
	// id == index into m_bindings; id -1 = "(not bound)". A current path absent from the owner
	// set is appended so it still shows and round-trips.
	bool FillBindings(ibPropertyList* prop);

private:

	// Resolve the long id a bindings-list item carries to the dataPath string it represents.
	wxString BindingNameForId(long id) const;

	ibManagedElement*    m_element;
	ibManagedFormEditor* m_editor;

	// The binding targets offered to the DataPath list, in list order (id == index).
	std::vector<wxString> m_bindings;

	ibPropertyCategory* m_cat          = nullptr;
	ibPropertyString*   m_propName     = nullptr;
	ibPropertyString*   m_propTitle    = nullptr;
	ibPropertyList*     m_propDataPath = nullptr;
	ibPropertyList*     m_propViewKind = nullptr;
	ibPropertyList*     m_propLayout   = nullptr;
	ibPropertyList*     m_propRepr     = nullptr;
};

#endif // __MANAGED_ELEMENT_PROPERTY_H__
