#include "widgets.h"

/* XPM — a small picture/landscape glyph */
static const char* s_picture_xpm[] = {
	"16 16 5 1",
	"  c None",
	". c #716F64",
	"o c #FFFFFF",
	"X c #66CC33",
	"O c #FFD24D",
	"................",
	".oooooooooooooo.",
	".oooooOOOoooooo.",
	".ooooOOOOOooooo.",
	".oooooOOOoooooo.",
	".oooooooooooooo.",
	".oooooooooooooo.",
	".ooooooooooXXoo.",
	".oooooooooXXXXo.",
	".ooooooXXXXXXXX.",
	".oooooXXXXXXXXX.",
	".ooooXXXXXXXXXX.",
	".oooXXXXXXXXXXX.",
	".ooXXXXXXXXXXXX.",
	".oXXXXXXXXXXXXX.",
	"................"
};

wxIcon ibValuePicture::GetIcon() const
{
	return wxIcon(s_picture_xpm);
}

wxIcon ibValuePicture::GetIconGroup()
{
	return wxIcon(s_picture_xpm);
}
