#include "managedElementProperty.h"
#include "managedFormEditor.h"

#include <algorithm>

// Field/Column only: the view kind. Group only: layout + representation. Everything
// else (Name/Title) applies to all kinds; DataPath applies to the bound kinds.
ibManagedElementProperty::ibManagedElementProperty(ibManagedElement* element, ibManagedFormEditor* editor)
	: ibPropertyObject(), m_element(element), m_editor(editor)
{
	m_cat = CreatePropertyCategory(wxT("Element"), _("Element"));

	m_propName     = CreateProperty<ibPropertyString>(m_cat, wxT("Name"),     _("Name"),     m_element->name);
	m_propTitle    = CreateProperty<ibPropertyString>(m_cat, wxT("Title"),    _("Title"),    m_element->title);

	// DataPath is a dropdown of the owner's attributes (+ a "(not bound)" entry). Build the
	// ordered list now so the initial selected id matches the element's current binding.
	if (m_editor != nullptr)
		m_bindings = m_editor->AvailableBindings();
	// If the element already binds to something not in the owner set (imported / stale), keep it
	// visible by appending it — so editing another property never silently drops the binding.
	if (!m_element->dataPath.IsEmpty()
		&& std::find(m_bindings.begin(), m_bindings.end(), m_element->dataPath) == m_bindings.end())
		m_bindings.push_back(m_element->dataPath);
	long initialBinding = -1;   // -1 => "(not bound)"
	for (size_t i = 0; i < m_bindings.size(); ++i)
		if (m_bindings[i] == m_element->dataPath) { initialBinding = (long)i; break; }
	m_propDataPath = CreateProperty<ibPropertyList>(m_cat, wxT("DataPath"), _("Data path"),
		&ibManagedElementProperty::FillBindings, initialBinding);

	m_propViewKind = CreateProperty<ibPropertyList>(m_cat, wxT("ViewKind"), _("View kind"),
		&ibManagedElementProperty::FillViewKind, (long)m_element->viewKind);
	m_propLayout   = CreateProperty<ibPropertyList>(m_cat, wxT("Layout"), _("Layout"),
		&ibManagedElementProperty::FillLayout, (long)m_element->layout);
	m_propRepr     = CreateProperty<ibPropertyList>(m_cat, wxT("Representation"), _("Representation"),
		&ibManagedElementProperty::FillRepresentation, (long)m_element->representation);
}

bool ibManagedElementProperty::IsEditable() const
{
	return m_editor != nullptr && m_editor->IsEditable();
}

bool ibManagedElementProperty::FillViewKind(ibPropertyList* prop)
{
	prop->AppendItem(_("Auto"),            (int)ibFieldViewKind::Auto,            wxNullBitmap);
	prop->AppendItem(_("Input field"),     (int)ibFieldViewKind::InputField,      wxNullBitmap);
	prop->AppendItem(_("Label field"),     (int)ibFieldViewKind::LabelField,      wxNullBitmap);
	prop->AppendItem(_("Check box"),       (int)ibFieldViewKind::CheckBoxField,   wxNullBitmap);
	prop->AppendItem(_("Radio button"),    (int)ibFieldViewKind::RadioField,      wxNullBitmap);
	prop->AppendItem(_("Picture"),         (int)ibFieldViewKind::PictureField,    wxNullBitmap);
	prop->AppendItem(_("Choice list"),     (int)ibFieldViewKind::ChoiceField,     wxNullBitmap);
	prop->AppendItem(_("Combo box"),       (int)ibFieldViewKind::ComboBoxField,   wxNullBitmap);
	prop->AppendItem(_("Reference field"), (int)ibFieldViewKind::ReferenceField,  wxNullBitmap);
	return true;
}

bool ibManagedElementProperty::FillLayout(ibPropertyList* prop)
{
	prop->AppendItem(_("Vertical"),   (int)ibGroupLayout::Vertical,   wxNullBitmap);
	prop->AppendItem(_("Horizontal"), (int)ibGroupLayout::Horizontal, wxNullBitmap);
	return true;
}

bool ibManagedElementProperty::FillRepresentation(ibPropertyList* prop)
{
	prop->AppendItem(_("Plain"),      (int)ibGroupRepresentation::Plain,     wxNullBitmap);
	prop->AppendItem(_("Titled box"), (int)ibGroupRepresentation::TitledBox, wxNullBitmap);
	prop->AppendItem(_("Pages"),      (int)ibGroupRepresentation::Pages,     wxNullBitmap);
	return true;
}

bool ibManagedElementProperty::FillBindings(ibPropertyList* prop)
{
	prop->AppendItem(_("(not bound)"), -1, wxNullBitmap);
	for (size_t i = 0; i < m_bindings.size(); ++i)
		prop->AppendItem(m_bindings[i], (int)i, wxNullBitmap);
	return true;
}

wxString ibManagedElementProperty::BindingNameForId(long id) const
{
	if (id < 0 || id >= (long)m_bindings.size())
		return wxEmptyString;   // "(not bound)" or out of range
	return m_bindings[(size_t)id];
}

void ibManagedElementProperty::OnPropertyChanged(ibProperty* property, const wxVariant& oldValue, const wxVariant& newValue)
{
	if (m_element == nullptr || property == nullptr)
		return;

	const wxString name = property->GetName();
	if (name == wxT("Name"))
		m_element->name = newValue.GetString();
	else if (name == wxT("Title"))
		m_element->title = newValue.GetString();
	else if (name == wxT("DataPath"))
		m_element->dataPath = BindingNameForId(newValue.GetLong());
	else if (name == wxT("ViewKind"))
		m_element->viewKind = (ibFieldViewKind)newValue.GetLong();
	else if (name == wxT("Layout"))
		m_element->layout = (ibGroupLayout)newValue.GetLong();
	else if (name == wxT("Representation"))
		m_element->representation = (ibGroupRepresentation)newValue.GetLong();
	else
		return;

	if (m_editor != nullptr)
		m_editor->OnElementChanged(m_element);
}

void ibManagedElementProperty::OnPropertyRefresh()
{
	if (m_element == nullptr)
		return;

	const bool isGroup  = (m_element->kind == ibManagedNodeKind::Group) || (m_element->kind == ibManagedNodeKind::Page);
	const bool isField  = (m_element->kind == ibManagedNodeKind::Field) || (m_element->kind == ibManagedNodeKind::Column);
	const bool isBound  = isField || (m_element->kind == ibManagedNodeKind::Table);

	// View kind only makes sense for a drawn field/column.
	HideProperty(m_propViewKind, !isField);
	// Layout / representation only for a container group.
	HideProperty(m_propLayout, !isGroup);
	HideProperty(m_propRepr,   !isGroup);
	// A binding path only for bound nodes.
	HideProperty(m_propDataPath, !isBound);
}
