/*
 * nes-pi - its look: colours, fonts, logo, credits and the grid (theme.h)
 */
#include "project.h"
#include "theme.h"
#include "fonts.h"

// the grid: a checkerboard of 16-pixel squares in black and dark grey
#define BG_CELL		16

// the logo, 30x15 drawn twice as big: N "NES", P "PI", W border, . plate
static const char *const s_Logo[] =
{
	" WWWWWWWWWWWWWWWWWWWWWWWWWWWW ",
	"W............................W",
	"W.NNNNN.NN.NNNNNNNN.NNNNNNNN.W",
	"W.NNNNN.NN.NNNN.....NNNN.....W",
	"W.NNNNNNNN.NNNNNNNN.NNNNNNNN.W",
	"W.NN.NNNNN.NNNN.........NNNN.W",
	"W.NN.NNNNN.NNNNNNNN.NNNNNNNN.W",
	"W............................W",
	"W.PPPPPPPP.PPPP..............W",
	"W.PPPP..PP.PPPP..............W",
	"W.PPPPPPPP.PPPP..............W",
	"W.PPPP.....PPPP..............W",
	"W.PPPP.....PPPP.....WWW..WWW.W",
	"W...................W W..W W.W",
	" WWWWWWWWWWWWWWWWWWWW WWWW WW ",
};

static const char s_LogoKeys[] = "NPW.";
static const u32 s_LogoColors[] = {0xF8B800, 0xE40058, 0xF8F8F8, 0x000000};	// NES palette $28 and $15

static const char *const s_Credits[][2] =
{
	{"CORE",   "NESTOPIA - M.FREIJ"},
	{"",       "R.CARMICHAEL AND OTHERS"},
	{"C++",    "NEWLIB, LIBSTDC++"},
	{"ZIP",    "MINIZ - R.GELDREICH"},
	{"SYSTEM", "CIRCLE - R.STANGE"},
	{"NTSC",   "BLARGG - S.GREEN"},
	{"FONT",   "MATINEE - D.GUARD"}
};

// black panels with a silver frame, a crimson band, gold titles
const TUITheme g_Theme =
{
	"NES-PI",
	0xBCBCBC, 0x5C5C5C, 0x0C0C0C,		// frame, outline, panel
	0xBC0049, 0xF8B800,			// band (crimson at 80%), cursor
	0xF8B800, 0xF8B800, 0x96A0AF,		// title, value, dim
	0xE40058,				// POWER OFF
	g_FontMatinee, g_FontMatinee, TRUE,	// menus, the list (with lower case)
	g_FontMatinee,				// around the picture
	0x7A8290, TRUE,				// the list's bottom row
	s_Logo, sizeof s_Logo / sizeof s_Logo[0], 2, s_LogoKeys, s_LogoColors,
	s_Credits, sizeof s_Credits / sizeof s_Credits[0],
	{BG_CELL, BG_CELL}, 0x000000, 0,	// the moat: an invisible frame a square out
	{0, 0},
	BG_CELL,				// whole squares from the frame's corners
	FALSE,					// thin lines
	0
};


u32 ThemeGrid (int x, int y, int M, int nHalfW, int nHalfH)
{
	(void) nHalfW;
	(void) nHalfH;
	// squares from the moat's bottom right corner; the columns mirrored
	// about its middle, so that both sides match
	const int P = BG_CELL * M, nCols = (PROJECT_WIDTH + 2 * BG_CELL) / BG_CELL;
	int cx = (x + P * 256) / P - 256 + nCols / 2;
	const int cy = (y + P * 256) / P;
	if (cx < 0) cx = -1 - cx;
	return (cx + cy) & 1 ? 0x1A1A1A : 0x000000;
}

u32 ThemeGround (void)
{
	return 0x000000;
}
