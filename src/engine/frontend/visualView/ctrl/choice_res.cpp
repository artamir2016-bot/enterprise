#include "widgets.h"

/* XPM — a drop-down list glyph */
static const char* s_choice_xpm[] = {
	"16 16 4 1",
	"  c None",
	". c #716F64",
	"o c #ECE9D8",
	"X c #66CC33",
	"                ",
	" .............. ",
	" .oooooooooo.X. ",
	" .oooooooooo.X. ",
	" .oooooooooo... ",
	" .oooooooooo.X. ",
	" .oooooooooo.X. ",
	" .............. ",
	"                ",
	"      ....      ",
	"     .oooo.     ",
	"     .oooo.     ",
	"     .oooo.     ",
	"      ....      ",
	"                ",
	"                "
};

wxIcon ibValueChoice::GetIcon() const
{
	return wxIcon(s_choice_xpm);
}

wxIcon ibValueChoice::GetIconGroup()
{
	return wxIcon(s_choice_xpm);
}
