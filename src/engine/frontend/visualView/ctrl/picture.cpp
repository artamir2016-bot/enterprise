
#include "widgets.h"
#include "backend/serialize/dataBuilder.h"   // ibDataNode (control -> node)
#include "backend/compiler/procUnit.h"
#ifdef OES_USE_WEB
#include "frontend/web/webWindow.h"
#include "backend/backend_picture.h"
#else
#include <wx/statbmp.h>
#endif


//****************************************************************************
//*                             Picture                                      *
//****************************************************************************

ibValuePicture::ibValuePicture() : ibValueWindow()
{
}

wxObject* ibValuePicture::Create(ibFrontendWindow* wxparent, ibVisualHost* visualHost)
{
	(void)visualHost;
#ifdef OES_USE_WEB
	(void)wxparent;
	return new ibWebPicture(GetControlID());
#else
	return new wxStaticBitmap(wxparent, wxID_ANY, wxNullBitmap);
#endif
}

void ibValuePicture::OnCreated(wxObject* wxobject, ibFrontendWindow* wxparent, ibVisualHost* visualHost, bool firstCreated)
{
}

void ibValuePicture::Update(wxObject* wxobject, ibVisualHost* visualHost)
{
	(void)visualHost;
	const wxBitmap bmp = m_propertyPicture->GetValueAsBitmap();
#ifdef OES_USE_WEB
	ibWebPicture* picture = static_cast<ibWebPicture*>(wxobject);
	wxString uri;
	if (bmp.IsOk()) {
		const wxString b64 = ibBackendPicture::CreateBase64Image(bmp.ConvertToImage());
		if (!b64.IsEmpty())
			uri = wxT("data:image/png;base64,") + b64;
	}
	picture->SetPictureDataUri(uri);
	UpdateWindow(picture);
#else
	wxStaticBitmap* picture = dynamic_cast<wxStaticBitmap*>(wxobject);
	if (picture != nullptr)
		picture->SetBitmap(bmp.IsOk() ? bmp : wxNullBitmap);
	UpdateWindow(picture);
#endif
}

void ibValuePicture::Cleanup(wxObject* obj, ibVisualHost* visualHost)
{
}

//*******************************************************************
//*								Data                                *
//*******************************************************************

bool ibValuePicture::ReadData(const ibDataNode& node)
{
	m_propertyPicture->SetNodeValue(node.GetProperty(m_propertyPicture->GetName()));
	m_propertySource->SetNodeValue(node.GetProperty(m_propertySource->GetName()));
	return ibValueWindow::ReadData(node);
}

bool ibValuePicture::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyPicture->GetName(), m_propertyPicture->GetNodeValue());
	node.SetProperty(m_propertySource->GetName(), m_propertySource->GetNodeValue());
	return ibValueWindow::WriteData(node);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

CONTROL_TYPE_REGISTER(ibValuePicture, "Picture", "Widget", control_to_clsid("CT_PICT"));
