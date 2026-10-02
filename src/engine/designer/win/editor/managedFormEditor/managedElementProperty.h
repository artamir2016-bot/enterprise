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

private:

	ibManagedElement*    m_element;
	ibManagedFormEditor* m_editor;

	ibPropertyCategory* m_cat          = nullptr;
	ibPropertyString*   m_propName     = nullptr;
	ibPropertyString*   m_propTitle    = nullptr;
	ibPropertyString*   m_propDataPath = nullptr;
	ibPropertyList*     m_propViewKind = nullptr;
	ibPropertyList*     m_propLayout   = nullptr;
	ibPropertyList*     m_propRepr     = nullptr;
};

#endif // __MANAGED_ELEMENT_PROPERTY_H__
