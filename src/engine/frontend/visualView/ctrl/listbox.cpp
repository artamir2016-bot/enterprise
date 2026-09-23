
#include "widgets.h"
#include "backend/serialize/dataBuilder.h"   // ibDataNode (control -> node)
#include "backend/compiler/procUnit.h"
#ifdef OES_USE_WEB
#include "frontend/web/webWindow.h"
#endif


//****************************************************************************
//*                             ListBox                                      *
//****************************************************************************

ibValueListBox::ibValueListBox() : ibValueWindow()
{
}

// The Items property holds the list, one entry per line.
std::vector<wxString> ibValueListBox::GetItems() const
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

void ibValueListBox::OnSelectionCommitted(wxCommandEvent& event)
{
	m_propertyValue->SetValue(event.GetString());
}

wxObject* ibValueListBox::Create(ibFrontendWindow* wxparent, ibVisualHost* visualHost)
{
	(void)visualHost;
#ifdef OES_USE_WEB
	(void)wxparent;
	auto* listbox = new ibWebListBox(GetControlID());
#else
	auto* listbox = new wxListBox(wxparent, wxID_ANY, wxDefaultPosition,
		wxDefaultSize, 0, nullptr);
#endif
	listbox->Bind(wxEVT_LISTBOX, &ibValueListBox::OnSelectionCommitted, this);
	return listbox;
}

void ibValueListBox::OnCreated(wxObject* wxobject, ibFrontendWindow* wxparent, ibVisualHost* visualHost, bool firstCreated)
{
}

void ibValueListBox::Update(wxObject* wxobject, ibVisualHost* visualHost)
{
	(void)visualHost;
	const std::vector<wxString> items = GetItems();
	const wxString value = GetValueText();
#ifdef OES_USE_WEB
	ibWebListBox* listbox = static_cast<ibWebListBox*>(wxobject);
	listbox->SetItems(items);
	listbox->SetValue(value);
#else
	wxListBox* listbox = dynamic_cast<wxListBox*>(wxobject);
	if (listbox != nullptr) {
		listbox->Clear();
		for (const wxString& it : items)
			listbox->Append(it);
		if (!value.IsEmpty())
			listbox->SetStringSelection(value);
	}
#endif
	UpdateWindow(listbox);
}

void ibValueListBox::Cleanup(wxObject* obj, ibVisualHost* visualHost)
{
}

//*******************************************************************
//*								Data	                            *
//*******************************************************************

bool ibValueListBox::ReadData(const ibDataNode& node)
{
	m_propertyItems->SetNodeValue(node.GetProperty(m_propertyItems->GetName()));
	m_propertyValue->SetNodeValue(node.GetProperty(m_propertyValue->GetName()));
	return ibValueWindow::ReadData(node);
}

bool ibValueListBox::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyItems->GetName(), m_propertyItems->GetNodeValue());
	node.SetProperty(m_propertyValue->GetName(), m_propertyValue->GetNodeValue());
	return ibValueWindow::WriteData(node);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

CONTROL_TYPE_REGISTER(ibValueListBox, "Listbox", "Widget", control_to_clsid("CT_LSTB"));
