
#include "widgets.h"
#include "backend/serialize/dataBuilder.h"   // ibDataNode (control -> node)
#include "backend/compiler/procUnit.h"
#ifdef OES_USE_WEB
#include "frontend/web/webWindow.h"
#endif


//****************************************************************************
//*                             ComboBox                                     *
//****************************************************************************

ibValueComboBox::ibValueComboBox() : ibValueWindow()
{
}

// The Items property holds the drop-down list, one entry per line.
std::vector<wxString> ibValueComboBox::GetItems() const
{
	std::vector<wxString> out;
	const wxString raw = m_propertyItems->GetValueAsString();
	wxString line;
	for (wxUniChar ch : raw) {
		if (ch == wxT('\n') || ch == wxT('\r')) {
			if (!line.IsEmpty()) out.push_back(line);
			line.clear();
		} else {
			line += ch;
		}
	}
	if (!line.IsEmpty()) out.push_back(line);
	return out;
}

// A combo commits either a list pick (wxEVT_COMBOBOX) or typed text
// (wxEVT_TEXT) — both carry the new text as GetString().
void ibValueComboBox::OnSelectionCommitted(wxCommandEvent& event)
{
	m_propertyValue->SetValue(event.GetString());
}

wxObject* ibValueComboBox::Create(ibFrontendWindow* wxparent, ibVisualHost* visualHost)
{
	(void)visualHost;
#ifdef OES_USE_WEB
	(void)wxparent;
	auto* combobox = new ibWebComboBox(GetControlID());
#else
	auto* combobox = new wxComboBox(wxparent, wxID_ANY, wxEmptyString,
		wxDefaultPosition, wxDefaultSize);
	combobox->Bind(wxEVT_TEXT, &ibValueComboBox::OnSelectionCommitted, this);
#endif
	combobox->Bind(wxEVT_COMBOBOX, &ibValueComboBox::OnSelectionCommitted, this);
	return combobox;
}

void ibValueComboBox::OnCreated(wxObject* wxobject, ibFrontendWindow* wxparent, ibVisualHost* visualHost, bool firstCreated)
{
}

void ibValueComboBox::Update(wxObject* wxobject, ibVisualHost* visualHost)
{
	(void)visualHost;
	const std::vector<wxString> items = GetItems();
	const wxString value = GetValueText();
#ifdef OES_USE_WEB
	ibWebComboBox* combobox = static_cast<ibWebComboBox*>(wxobject);
	combobox->SetItems(items);
	combobox->SetValue(value);
#else
	wxComboBox* combobox = dynamic_cast<wxComboBox*>(wxobject);
	if (combobox != nullptr) {
		combobox->Clear();
		for (const wxString& it : items)
			combobox->Append(it);
		combobox->SetValue(value);   // editable field — set text, not just a list index
	}
#endif
	UpdateWindow(combobox);
}

void ibValueComboBox::Cleanup(wxObject* obj, ibVisualHost* visualHost)
{
}

//*******************************************************************
//*								 Data		                        *
//*******************************************************************

bool ibValueComboBox::ReadData(const ibDataNode& node)
{
	m_propertyItems->SetNodeValue(node.GetProperty(m_propertyItems->GetName()));
	m_propertyValue->SetNodeValue(node.GetProperty(m_propertyValue->GetName()));
	return ibValueWindow::ReadData(node);
}

bool ibValueComboBox::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyItems->GetName(), m_propertyItems->GetNodeValue());
	node.SetProperty(m_propertyValue->GetName(), m_propertyValue->GetNodeValue());
	return ibValueWindow::WriteData(node);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

CONTROL_TYPE_REGISTER(ibValueComboBox, "Combobox", "Widget", control_to_clsid("CT_CMBB"));
