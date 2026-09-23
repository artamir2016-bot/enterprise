#ifndef _SYSTEM_ENUMS_H__
#define _SYSTEM_ENUMS_H__

enum ibStatusMessage
{
	ibStatusMessage_Information = 1,
	ibStatusMessage_Warning,
	ibStatusMessage_Error
};

enum ibQuestionMode
{
	ibQuestionMode_YesNo = 1,
	ibQuestionMode_YesNoCancel,
	ibQuestionMode_OK,
	ibQuestionMode_OKCancel
};

enum ibQuestionReturnCode
{
	ibQuestionReturnCode_Yes = 1,
	ibQuestionReturnCode_No,
	ibQuestionReturnCode_OK,
	ibQuestionReturnCode_Cancel,
	// 1C DialogReturnCode also carries the message-box buttons (Abort/Retry/Ignore/Skip).
	ibQuestionReturnCode_Abort,
	ibQuestionReturnCode_Retry,
	ibQuestionReturnCode_Ignore,
	ibQuestionReturnCode_Skip
};

enum ibRoundMode
{
	ibRoundMode_Round15as10 = 1,
	ibRoundMode_Round15as20
};

enum ibChars {
	eCR = 13,
	eFF = 12,
	eLF = 10,
	eNBSp = 160,
	eTab = 9,
	eVTab = 11,
};

// УровеньЖурналаРегистрации — the severity a script writes to the event log with.
enum ibEventLogLevel
{
	ibEventLogLevel_Information = 1,
	ibEventLogLevel_Error,
	ibEventLogLevel_Warning,
	ibEventLogLevel_Note
};

// ВыравниваниеКнопокКоманднойПанели — how a command bar aligns its buttons.
enum ibCommandBarButtonsAlignment
{
	ibCommandBarButtonsAlignment_Auto = 1,
	ibCommandBarButtonsAlignment_Left,
	ibCommandBarButtonsAlignment_Right
};

// ТипКнопкиКоманднойПанели — the kind of a command-bar button.
enum ibCommandBarButtonType
{
	ibCommandBarButtonType_Action = 1,
	ibCommandBarButtonType_Separator,
	ibCommandBarButtonType_Submenu,
	ibCommandBarButtonType_CheckBox,
	ibCommandBarButtonType_Break
};

// ОтображениеКнопкиКоманднойПанели — how a command-bar button is shown.
enum ibCommandBarButtonRepresentation
{
	ibCommandBarButtonRepresentation_Auto = 1,
	ibCommandBarButtonRepresentation_Picture,
	ibCommandBarButtonRepresentation_Text,
	ibCommandBarButtonRepresentation_PictureAndText
};

#endif