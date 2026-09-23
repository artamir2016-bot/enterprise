
#include "widgets.h"
#include "backend/serialize/dataBuilder.h"   // ibDataNode (control -> node)
#include "backend/compiler/procUnit.h"
#ifdef OES_USE_WEB
#include "frontend/web/webWindow.h"
#endif


//****************************************************************************
//*                             Choice                                       *
//****************************************************************************

ibValueChoice::ibValueChoice() : ibValueWindow()
{
}

// The Items property holds the choice list, one entry per line. Split on
// CR/LF, dropping empty lines — mirrors the wxChoice item vector and the web
// <option> list from one serialised string.
std::vector<wxString> ibValueChoice::GetItems() const
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

// A selection (native wxEVT_CHOICE on desktop, synthesised on web) is stored
// back into the Value property so it survives the next host rebuild.
void ibValueChoice::OnSelectionCommitted(wxCommandEvent& event)
{
	m_propertyValue->SetValue(event.GetString());
}

wxObject* ibValueChoice::Create(ibFrontendWindow* wxparent, ibVisualHost* visualHost)
{
	(void)visualHost;
#ifdef OES_USE_WEB
	(void)wxparent;
	auto* choice = new ibWebChoice(GetControlID());
#else
	auto* choice = new wxChoice(wxparent, wxID_ANY, wxDefaultPosition, wxDefaultSize);
#endif
	choice->Bind(wxEVT_CHOICE, &ibValueChoice::OnSelectionCommitted, this);
	return choice;
}

void ibValueChoice::OnCreated(wxObject* wxobject, ibFrontendWindow* wxparent, ibVisualHost* visualHost, bool firstCreated)
{
}

void ibValueChoice::Update(wxObject* wxobject, ibVisualHost* visualHost)
{
	(void)visualHost;
	const std::vector<wxString> items = GetItems();
	const wxString value = GetValueText();
#ifdef OES_USE_WEB
	ibWebChoice* choice = static_cast<ibWebChoice*>(wxobject);
	choice->SetItems(items);
	choice->SetValue(value);
#else
	wxChoice* choice = dynamic_cast<wxChoice*>(wxobject);
	if (choice != nullptr) {
		choice->Clear();
		for (const wxString& it : items)
			choice->Append(it);
		if (!value.IsEmpty())
			choice->SetStringSelection(value);
	}
#endif
	UpdateWindow(choice);
}

void ibValueChoice::Cleanup(wxObject* obj, ibVisualHost* visualHost)
{
}

//*******************************************************************
//*								Data	                            *
//*******************************************************************

bool ibValueChoice::ReadData(const ibDataNode& node)
{
	m_propertyItems->SetNodeValue(node.GetProperty(m_propertyItems->GetName()));
	m_propertyValue->SetNodeValue(node.GetProperty(m_propertyValue->GetName()));
	return ibValueWindow::ReadData(node);
}

bool ibValueChoice::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyItems->GetName(), m_propertyItems->GetNodeValue());
	node.SetProperty(m_propertyValue->GetName(), m_propertyValue->GetNodeValue());
	return ibValueWindow::WriteData(node);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

CONTROL_TYPE_REGISTER(ibValueChoice, "Choice", "Widget", control_to_clsid("CT_CHOI"));
