#include "widgets.h"

/* XPM — an editable drop-down glyph */
static const char* s_combobox_xpm[] = {
	"16 16 4 1",
	"  c None",
	". c #716F64",
	"o c #FFFFFF",
	"X c #66CC33",
	"                ",
	"                ",
	" .............. ",
	" .oooooooooo.X. ",
	" .oooooooooo.X. ",
	" .oooooooooo... ",
	" .oooooooooo.X. ",
	" .oooooooooo.X. ",
	" .............. ",
	"                ",
	" .............. ",
	" .oooooooooooo. ",
	" .oooooooooooo. ",
	" .............. ",
	"                ",
	"                "
};

wxIcon ibValueComboBox::GetIcon() const
{
	return wxIcon(s_combobox_xpm);
}

wxIcon ibValueComboBox::GetIconGroup()
{
	return wxIcon(s_combobox_xpm);
}
