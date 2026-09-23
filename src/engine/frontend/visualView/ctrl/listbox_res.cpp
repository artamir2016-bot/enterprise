#include "widgets.h"

/* XPM — a stacked-rows list glyph */
static const char* s_listbox_xpm[] = {
	"16 16 4 1",
	"  c None",
	". c #716F64",
	"o c #FFFFFF",
	"X c #99CCFF",
	"                ",
	" .............. ",
	" .oooooooooooo. ",
	" .XXXXXXXXXXXX. ",
	" .oooooooooooo. ",
	" .oooooooooooo. ",
	" .oooooooooooo. ",
	" .oooooooooooo. ",
	" .oooooooooooo. ",
	" .oooooooooooo. ",
	" .oooooooooooo. ",
	" .oooooooooooo. ",
	" .oooooooooooo. ",
	" .............. ",
	"                ",
	"                "
};

wxIcon ibValueListBox::GetIcon() const
{
	return wxIcon(s_listbox_xpm);
}

wxIcon ibValueListBox::GetIconGroup()
{
	return wxIcon(s_listbox_xpm);
}
