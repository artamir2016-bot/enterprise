////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : calculationRegister action
////////////////////////////////////////////////////////////////////////////

#include "calculationRegister.h"

enum
{
	eDefActionAndClose = 1,
	eSave,
	eCopy,
};

ibValueRecordManagerObjectCalculationRegister::ibStandardCommandSet ibValueRecordManagerObjectCalculationRegister::GetStandardCommands(const ibFormID &formType)
{
	ibStandardCommandSet registerActions(this);

	registerActions.AddAction(wxT("SaveAndClose"), _("Save and close"), g_picSaveCLSID, true, eDefActionAndClose);
	registerActions.AddAction(wxT("Save"), _("Save"), g_picSaveCLSID, true, eSave);
	registerActions.AddAction(wxT("Clone"), _("Clone"), g_picCloneCLSID, true, eCopy);

	return registerActions;
}

void ibValueRecordManagerObjectCalculationRegister::CallAsAction(const ibActionID &action, ibBackendValueForm* srcForm)
{
	switch (action)
	{
	case eDefActionAndClose:
		if (WriteRegister())
			srcForm->CloseForm();
		break;
	case eSave: WriteRegister();
		break;
	case eCopy: CopyRegister(true);
		break;
	}
}
