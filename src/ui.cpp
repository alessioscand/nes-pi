/*
 * nes-pi - menus (304x224, the layout of neo-pi)
 */
#include "fontdata.h"
#include "ui.h"
#include <circle/font.h>
#include <circle/util.h>
#include <circle/timer.h>

#define OX	UI_SRC_X
#define OY	UI_SRC_Y
#define ARGB(c)	(0xFF000000u | (c))

CUI::CUI (void)
:	m_bFont (FALSE)
{
	// list font: the console's own (sfix.sfix); until it is loaded, the menu font
	memset (m_Font, 0, sizeof m_Font);
	for (unsigned c = 32; c < 128; c++)
	{
		memcpy (m_Font[c], g_MenuFont[c - 32], 8);
	}
	memset (m_Buf, 0, sizeof m_Buf);
	memset (m_Snapshot, 0, sizeof m_Snapshot);
}

void CUI::SetFont (const u8 *pSFix, size_t nSize)
{
	// Fix-layer tiles: 32 bytes, columns ordered 0x10, 0x18, 0x00, 0x08,
	// low nibble = left pixel. In the BIOS font colour 1 is the glyph.
	if (pSFix == 0 || nSize < 128 * 32)
	{
		return;
	}
	static const unsigned Col[4] = {0x10, 0x18, 0x00, 0x08};
	for (unsigned c = 32; c < 128; c++)
	{
		const u8 *t = pSFix + c * 32;
		for (unsigned y = 0; y < 8; y++)
		{
			u8 nRow = 0;
			for (unsigned p = 0; p < 4; p++)
			{
				u8 b = t[Col[p] + y];
				if ((b & 0x0F) == 1) nRow |= 0x80 >> (p * 2);
				if ((b >> 4) == 1)   nRow |= 0x80 >> (p * 2 + 1);
			}
			m_Font[c][y] = nRow;
		}
	}
	m_bFont = TRUE;
}

void CUI::Pixel (int x, int y, u32 c)
{
	if (x >= 0 && x < UI_W && y >= 0 && y < UI_H)
	{
		m_Buf[(y + OY) * UI_SRC_PITCH + x + OX] = ARGB (c);
	}
}

void CUI::Fill (int x, int y, int w, int h, u32 c)
{
	for (int j = y; j < y + h; j++)
	{
		for (int i = x; i < x + w; i++)
		{
			Pixel (i, j, c);
		}
	}
}

// nes-pi logo, 30x15: N "NES", P "PI", W border, . plate, space = transparent
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
#define LOGO_W		30
#define LOGO_H		15

// Static logo on the game list: in colour, twice the size, above the list
#define LOGO_SCALE	2
#define LOGO_Y		7		// in the band above the list (y 0-40)
#define LOGO_GOLD	0xF8B800u	// NES palette $28
#define LOGO_CRIMSON	UI_DANGER	// NES palette $15

// Background: a checkerboard of 16-pixel squares in black and grey,
// scrolling diagonally (down-right) at 30 pixels per second, the same motion
// as neo-pi's background; the pattern repeats every 32 pixels
#define BG_CELL		16
#define BG_DARK		0x000000u
#define BG_LIGHT	0x3C3C3Cu
#define BG_US_PER_PX	33333		// 30 pixels per second

// Screens without a game behind them. bFreeze: keep the last position (the
// loading screen is redrawn at irregular times, moving it would look jerky)
void CUI::Background (boolean bFreeze)
{
	static unsigned s_nOff;
	if (!bFreeze)
	{
		// time based, so it moves at the same speed whatever draws the frames
		s_nOff = (unsigned) (CTimer::GetClockTicks64 () / BG_US_PER_PX) % (2 * BG_CELL);
	}
	const u32 Color[2] = {ARGB (BG_DARK), ARGB (BG_LIGHT)};
	for (int y = 0; y < UI_H; y++)
	{
		u32 *p = &m_Buf[(y + OY) * UI_SRC_PITCH + OX];
		const unsigned yy = ((unsigned) y + 2 * BG_CELL - s_nOff) / BG_CELL;
		for (int x = 0; x < UI_W; x++)
		{
			const unsigned xx = ((unsigned) x + 2 * BG_CELL - s_nOff) / BG_CELL;
			p[x] = Color[(xx + yy) & 1];
		}
	}
}

void CUI::Logo (void)
{
	const int x0 = (UI_W - LOGO_W * LOGO_SCALE) / 2;
	for (int ly = 0; ly < LOGO_H; ly++)
	{
		for (int lx = 0; lx < LOGO_W; lx++)
		{
			u32 c;
			switch (s_Logo[ly][lx])
			{
			case 'N': c = LOGO_GOLD; break;
			case 'P': c = LOGO_CRIMSON; break;
			case 'W': c = UI_WHITE; break;
			case '.': c = 0x000000; break;
			default:  continue;
			}
			Fill (x0 + lx * LOGO_SCALE, LOGO_Y + ly * LOGO_SCALE, LOGO_SCALE, LOGO_SCALE, c);
		}
	}
}

void CUI::Panel (int x0, int y0, int x1, int y1)
{
	Fill (x0, y0, x1 - x0 + 1, y1 - y0 + 1, UI_FRAME2);
	Fill (x0 + 1, y0 + 1, x1 - x0 - 1, y1 - y0 - 1, UI_FRAME1);
	Fill (x0 + 3, y0 + 3, x1 - x0 - 5, y1 - y0 - 5, UI_FRAME2);
	Fill (x0 + 4, y0 + 4, x1 - x0 - 7, y1 - y0 - 7, UI_PANEL);
}

// Menu font (Matinee, D.Guard) by default; bListFont = the console font of the game list
void CUI::Text (int x, int y, const char *s, u32 c, unsigned nTall, boolean bListFont)
{
	for (unsigned i = 0; s[i] != '\0'; i++)
	{
		unsigned ch = (unsigned char) s[i];
		if (ch < 32 || ch >= 128)
		{
			ch = '?';
		}
		if (!bListFont && ch >= 'a' && ch <= 'z')
		{
			ch -= 32;		// menu font: capitals only (its lower case is hard to read)
		}
		for (int pass = 0; pass < 2; pass++)		// shadow, then glyph
		{
			int dx = pass == 0 ? 1 : 0;
			u32 col = pass == 0 ? 0x000000 : c;
			for (unsigned r = 0; r < 8; r++)
			{
				u8 nRow = bListFont ? m_Font[ch][r] : g_MenuFont[ch - 32][r];
				for (unsigned k = 0; k < 8; k++)
				{
					if (nRow & (0x80 >> k))
					{
						for (unsigned t = 0; t < nTall; t++)
						{
							Pixel (x + i * 8 + k + dx, y + r * nTall + t + dx, col);
						}
					}
				}
			}
		}
	}
}

void CUI::CText (int y, const char *s, u32 c, unsigned nTall, boolean bListFont)
{
	Text ((UI_W - (int) strlen (s) * 8) / 2, y, s, c, nTall, bListFont);
}

// Neo Geo buttons: 13 px disc in the button colour with a 5x7 letter drawn
// for it (exactly centred), as on the controller
static const u32 ButtonColor[4] = {0xDC2828, 0xF0C81E, 0x28B446, 0x325AE6};	// A red B yellow C green D blue
static const u32 ButtonInk[4]   = {0xFFFFFF, 0x281E00, 0xFFFFFF, 0xFFFFFF};
static const char *ButtonGlyph[4][7] =
{
	{".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"},
	{"####.", "#...#", "#...#", "####.", "#...#", "#...#", "####."},
	{".###.", "#...#", "#....", "#....", "#....", "#...#", ".###."},
	{"####.", "#...#", "#...#", "#...#", "#...#", "#...#", "####."}
};

void CUI::ButtonIcon (int x, int y, char cButton)
{
	if (cButton == 'S')
	{
		Text (x, y + 3, "SELECT", UI_VALUE);
		return;
	}
	unsigned b = (unsigned) (cButton - 'A');
	if (b > 3)
	{
		return;
	}
	// a 13 x 13 disc drawn by hand, with an even 1-pixel black rim
	static const char *const Disc[13] =
	{
		"....#####....",
		"..##ooooo##..",
		".#ooooooooo#.",
		".#ooooooooo#.",
		"#ooooooooooo#",
		"#ooooooooooo#",
		"#ooooooooooo#",
		"#ooooooooooo#",
		"#ooooooooooo#",
		".#ooooooooo#.",
		".#ooooooooo#.",
		"..##ooooo##..",
		"....#####....",
	};
	for (int j = 0; j < 13; j++)
	{
		for (int i = 0; i < 13; i++)
		{
			char c = Disc[j][i];
			if (c != '.')
			{
				Pixel (x + i, y + j, c == '#' ? 0x000000 : ButtonColor[b]);
			}
		}
	}
	for (int r = 0; r < 7; r++)
	{
		for (int k = 0; k < 5; k++)
		{
			if (ButtonGlyph[b][r][k] == '#')
			{
				Pixel (x + 4 + k, y + 3 + r, ButtonInk[b]);
			}
		}
	}
}

u32 CUI::ButtonColorOf (char cButton)
{
	unsigned b = (unsigned) (cButton - 'A');
	return b <= 3 ? ButtonColor[b] : UI_WHITE;
}

u32 CUI::Dim (u32 c, unsigned nPercent)
{
	return ((((c >> 16) & 0xFF) * nPercent / 100) << 16)
	     | ((((c >> 8) & 0xFF) * nPercent / 100) << 8)
	     | (((c & 0xFF) * nPercent / 100));
}

// Hint bar: button followed by its action
static int HintIconWidth (char c)	{ return c == 'S' ? 6 * 8 + 4 : 13; }	// 'S' = the word SELECT

void CUI::Buttons (int y, const TButtonHint *pHints, unsigned nHints)
{
	const int nGap = 24;
	int nW = 0;
	for (unsigned i = 0; i < nHints; i++)
	{
		nW += HintIconWidth (pHints[i].cButton) + 4 + (int) strlen (pHints[i].pText) * 8 + (i ? nGap : 0);
	}
	int x = (UI_W - nW) / 2;
	for (unsigned i = 0; i < nHints; i++)
	{
		char c = pHints[i].cButton;
		ButtonIcon (x, y, c);
		int w = HintIconWidth (c);
		Text (x + w + 4, y + 3, pHints[i].pText, 0xD2D2D2);
		x += w + 4 + (int) strlen (pHints[i].pText) * 8 + nGap;
	}
}

// The paused game (darkened snapshot) or, with no game loaded, the pattern
void CUI::Backdrop (boolean bOverGame)
{
	if (bOverGame)
	{
		for (int y = 0; y < UI_H; y++)
		{
			memcpy (&m_Buf[(y + OY) * UI_SRC_PITCH + OX], &m_Snapshot[y * UI_W], UI_W * 4);
		}
	}
	else
	{
		Background ();
	}
}

void CUI::DrawList (const TGameEntry *pGames, unsigned nCount, unsigned nSel, const char *pMessage,
		    boolean bOverGame)
{
	Backdrop (bOverGame);
	Logo ();					// the big logo: only on the game list
	// y 0..40 reserved for the logo (empty for now)
	Panel (12, 44, UI_W - 13, 190);

	Fill (17, 104, UI_W - 34, 23, UI_BAND);
	Fill (17, 104, UI_W - 34, 1, UI_FRAME2);
	Fill (17, 126, UI_W - 34, 1, UI_FRAME2);

	for (int k = -4; k <= 4; k++)
	{
		int i = (int) nSel + k;
		if (i < 0 || i >= (int) nCount)
		{
			continue;
		}
		char Name[40];
		strncpy (Name, pGames[i].Name, 34);
		Name[34] = '\0';
		if (k == 0)
		{
			CText (107, Name, UI_WHITE, 2, TRUE);
		}
		else
		{
			int y = 115 + k * 12 + (k > 0 ? 8 : -8) - 4;
			int nFade = 100 - (k < 0 ? -k : k) * 18;
			if (nFade < 35) nFade = 35;
			CText (y, Name, Dim (UI_DIM, (unsigned) nFade), 1, TRUE);
		}
	}

	char Count[16];
	{
		unsigned a = nSel + 1, b = nCount;
		// "nn/mm" without printf
		char *p = Count;
		if (a >= 100) *p++ = '0' + (a / 100) % 10;
		if (a >= 10) *p++ = '0' + (a / 10) % 10;
		*p++ = '0' + a % 10;
		*p++ = '/';
		if (b >= 100) *p++ = '0' + (b / 100) % 10;
		if (b >= 10) *p++ = '0' + (b / 10) % 10;
		*p++ = '0' + b % 10;
		*p = '\0';
	}
	Text (UI_W - 18 - (int) strlen (Count) * 8, 178, Count, 0x7A8290);

	if (pMessage && pMessage[0])
	{
		CText (32, pMessage, UI_ERR);
	}
	static const TButtonHint Hints[2] = {{'A', "START GAME"}, {'S', "OPTIONS"}};
	Buttons (198, Hints, 2);
}

void CUI::SnapshotGame (const u32 *pGameBuf)
{
	for (int y = 0; y < UI_H; y++)
	{
		const u32 *s = pGameBuf + (y + OY) * UI_SRC_PITCH + OX;
		for (int x = 0; x < UI_W; x++)
		{
			u32 p = s[x];
			u32 r = ((p >> 16) & 0xFF) * 30 / 100;
			u32 g = ((p >> 8) & 0xFF) * 30 / 100;
			u32 b = (p & 0xFF) * 30 / 100;
			m_Snapshot[y * UI_W + x] = ARGB ((r << 16) | (g << 8) | b);
		}
	}
}

void CUI::DrawPanelMenu (boolean bOverGame, const char *pTitle,
			 const TMenuItem *pItems, unsigned nItems, unsigned nSel,
			 const char *pMessage, u32 nMessageColor,
			 const TButtonHint *pHints, unsigned nHints, const char *pFooter)
{
	if (bOverGame)
	{
		for (int y = 0; y < UI_H; y++)
		{
			memcpy (&m_Buf[(y + OY) * UI_SRC_PITCH + OX], &m_Snapshot[y * UI_W], UI_W * 4);
		}
	}
	else
	{
		Background ();
	}

	const int nStep = nItems > 8 ? 12 : 15;		// long lists (controls) a bit tighter
	int nInner = 22 + (int) nItems * nStep + 22;		// title + items + message line
	int y0 = (190 - nInner) / 2 + 4;
	if (y0 < 4) y0 = 4;
	int x0 = 44, x1 = UI_W - 45;
	Panel (x0, y0, x1, y0 + nInner + 8);

	// at most 24 characters fit inside the frame: cut long names at a word
	char Title[34];
	strncpy (Title, pTitle, 33);
	Title[33] = '\0';
	if (strlen (Title) > 24)
	{
		Title[24] = '\0';
		for (int i = 23; i > 8; i--)
		{
			if (Title[i] == ' ')
			{
				Title[i] = '\0';
				break;
			}
		}
		size_t n = strlen (Title);
		while (n > 0 && (Title[n - 1] == ' ' || Title[n - 1] == '-' || Title[n - 1] == ':'))
		{
			Title[--n] = '\0';
		}
	}
	CText (y0 + 10, Title, UI_TITLE);
	Fill (x0 + 8, y0 + 22, x1 - x0 - 15, 1, UI_FRAME2);

	for (unsigned n = 0; n < nItems; n++)
	{
		int y = y0 + 32 + (int) n * nStep;
		boolean bSel = n == nSel;
		if (bSel)
		{
			Fill (x0 + 8, y - 3, x1 - x0 - 15, 13, UI_BAND);
			for (int t = 0; t < 4; t++)		// cursor triangle
			{
				Fill (x0 + 12 + t, y + t, 1, 8 - 2 * t, UI_CURSOR);
			}
		}
		u32 nColor = pItems[n].bDisabled ? Dim (UI_DIM, 55) : (bSel ? UI_WHITE : UI_DIM);
		if (!pItems[n].bDisabled && strcmp (pItems[n].pLabel, "POWER OFF") == 0)
		{
			nColor = bSel ? UI_WHITE : UI_DANGER;	// crimson; white on the crimson band
		}
		Text (x0 + 22, y, pItems[n].pLabel, nColor);
		if (pItems[n].pValue)
		{
			int nLen = (int) strlen (pItems[n].pValue);
			u32 nValColor = pItems[n].bDisabled ? Dim (UI_DIM, 45) : (bSel ? UI_VALUE : Dim (UI_DIM, 75));
			Text (x1 - 10 - nLen * 8, y, pItems[n].pValue, nValColor);
		}
	}

	if (pMessage && pMessage[0])
	{
		Text (x0 + 22, y0 + 32 + (int) nItems * nStep + 4, pMessage, nMessageColor);
	}

	if (pFooter && pFooter[0])
	{
		char Footer[40];
		strncpy (Footer, pFooter, 37);
		Footer[37] = '\0';
		CText (y0 + nInner + 14, Footer, Dim (UI_DIM, 60));
	}

	Buttons (209, pHints, nHints);
}

void CUI::DrawBox (const char *pLine1, const char *pLine2, const TButtonHint *pHint)
{
	// darken what is underneath, so the box stands out from the menu
	for (int y = 0; y < UI_H; y++)
	{
		u32 *p = &m_Buf[(y + OY) * UI_SRC_PITCH + OX];
		for (int x = 0; x < UI_W; x++)
		{
			p[x] = ARGB (Dim (p[x] & 0xFFFFFF, 40));
		}
	}
	Panel (64, 80, UI_W - 65, 150);
	if (pLine1) CText (92, pLine1, UI_WHITE);
	if (pLine2) CText (110, pLine2, UI_DIM);
	if (pHint) Buttons (126, pHint, 1);
}

void CUI::DrawBlack (void)
{
	for (int y = 0; y < UI_H; y++)
	{
		for (int x = 0; x < UI_W; x++)
		{
			m_Buf[(y + OY) * UI_SRC_PITCH + x + OX] = 0xFF000000u;
		}
	}
}

void CUI::DrawLoading (const char *pName, unsigned nPercent)
{
	Background (TRUE);

	// long titles go on two lines of up to 26 characters, split at a space
	const unsigned nMax = 26;
	char Line1[34], Line2[34];
	size_t nLen = strlen (pName);
	Line2[0] = '\0';
	if (nLen <= nMax)
	{
		strncpy (Line1, pName, sizeof Line1 - 1);
		Line1[sizeof Line1 - 1] = '\0';
	}
	else
	{
		unsigned nCut = nMax;
		while (nCut > 0 && pName[nCut] != ' ')
		{
			nCut--;
		}
		if (nCut == 0)
		{
			nCut = nMax;			// no space: split inside the word
		}
		memcpy (Line1, pName, nCut);
		Line1[nCut] = '\0';
		const char *pRest = pName + nCut;
		while (*pRest == ' ')
		{
			pRest++;
		}
		strncpy (Line2, pRest, nMax);
		Line2[nMax] = '\0';
	}

	int dy = Line2[0] ? 6 : 0;			// the box grows by 6 lines up and down
	Panel (32, 84 - dy, UI_W - 33, 150 + dy);
	CText (98 - dy, "LOADING", UI_TITLE);
	CText (112 - dy, Line1, UI_WHITE);
	if (Line2[0])
	{
		CText (124 - dy, Line2, UI_WHITE);
	}
	int nBarW = UI_W - 96;
	int yBar = 130 + dy;
	Fill (48, yBar, nBarW, 8, UI_FRAME2);
	Fill (49, yBar + 1, nBarW - 2, 6, UI_PANEL);
	Fill (49, yBar + 1, (nBarW - 2) * (int) (nPercent > 100 ? 100 : nPercent) / 100, 6, UI_TITLE);
}

void CUI::DrawMessage (const char *pTitle, const char *pLine1, const char *pLine2)
{
	Background ();
	Panel (16, 70, UI_W - 17, 160);

	// the lines centred vertically in the panel (inside 74-156): 22 px from
	// the title to the first line below it, 16 px between the other two
	const int nY0 = 70, nY1 = 160, nGlyph = 8;
	int nH = nGlyph;
	if (pLine1) nH += 22;
	if (pLine2) nH += pLine1 ? 16 : 22;
	int y = (nY0 + nY1) / 2 - nH / 2;
	CText (y, pTitle, UI_ERR);
	y += 22;
	if (pLine1)
	{
		CText (y, pLine1, UI_WHITE);
		y += 16;
	}
	if (pLine2) CText (y, pLine2, UI_DIM);
}

// ---- controls set-up -------------------------------------------------------

void CUI::DrawMapping (boolean bOverGame, const char *pTitle, int nStep,
		       const char *const *pValues, const char *pFooter)
{
	if (bOverGame)
	{
		for (int y = 0; y < UI_H; y++)
		{
			memcpy (&m_Buf[(y + OY) * UI_SRC_PITCH + OX], &m_Snapshot[y * UI_W], UI_W * 4);
		}
	}
	else
	{
		Background ();
	}

	const int x0 = 44, x1 = UI_W - 45;
	Panel (x0, 12, x1, 196);
	CText (22, pTitle, UI_TITLE);
	Fill (x0 + 8, 34, x1 - x0 - 15, 1, UI_FRAME2);

	// the request, big and in the middle: PRESS A / PRESS START ...
	static const char *Names[4] = {"A", "B", "START", "SELECT"};
	if (nStep >= 0 && nStep < 4)
	{
		const char *pName = Names[nStep];
		int nLen = 6 + (int) strlen (pName);
		int x = (UI_W - nLen * 8) / 2;
		Text (x, 46, "PRESS ", UI_WHITE, 2);
		Text (x + 48, 46, pName, nStep < 2 ? ButtonColorOf ((char) ('A' + nStep)) : UI_VALUE, 2);
	}
	else
	{
		CText (46, "SAVED", UI_OK, 2);
	}

	static const char *Labels[4] = {0, 0, "START", "SELECT"};
	for (int i = 0; i < 4; i++)
	{
		int y = 84 + i * 22;
		if (i == nStep)
		{
			Fill (x0 + 8, y - 1, x1 - x0 - 15, 15, UI_BAND);
		}
		if (i < 2)
		{
			ButtonIcon (x0 + 20, y, (char) ('A' + i));
		}
		else
		{
			Text (x0 + 20, y + 3, Labels[i], i == nStep ? UI_WHITE : UI_DIM);
		}
		const char *pValue = pValues[i];
		if (i == nStep)
		{
			pValue = "?";
		}
		if (pValue && pValue[0])
		{
			Text (x1 - 10 - (int) strlen (pValue) * 8, y + 3, pValue,
			      i == nStep ? UI_VALUE : Dim (UI_DIM, 85));
		}
	}

	if (pFooter && pFooter[0])
	{
		CText (174, pFooter, Dim (UI_DIM, 60));
	}
}

// ---- credits -----------------------------------------------------------------

void CUI::DrawCredits (const char *pVersion)
{
	for (int y = 0; y < UI_H; y++)
	{
		memcpy (&m_Buf[(y + OY) * UI_SRC_PITCH + OX], &m_Snapshot[y * UI_W], UI_W * 4);
	}

	const int x0 = 18, x1 = UI_W - 19, y0 = 6;
	Panel (x0, y0, x1, y0 + 187);
	CText (y0 + 10, "CREDITS", UI_TITLE);
	Fill (x0 + 8, y0 + 22, x1 - x0 - 15, 1, UI_FRAME2);

	char Title[32] = "NES-PI ";
	strncat (Title, pVersion, sizeof Title - 8);
	CText (y0 + 28, Title, UI_WHITE, 2, TRUE);
	CText (y0 + 48, "BY A.SCANDEREBECH", UI_VALUE);
	Fill (x0 + 8, y0 + 62, x1 - x0 - 15, 1, UI_FRAME2);

	static const char *Rows[8][2] =
	{
		{"CORE",   "NESTOPIA - M.FREIJ"},
		{"",       "R.CARMICHAEL AND OTHERS"},
		{"C++",    "NEWLIB, LIBSTDC++"},
		{"ZIP",    "MINIZ - R.GELDREICH"},
		{"SYSTEM", "CIRCLE - R.STANGE"},
		{"NTSC",   "BLARGG - S.GREEN"},
		{"FONT",   "MATINEE - D.GUARD"},
		{"",       ""}
	};
	for (int i = 0; i < 8; i++)
	{
		int y = y0 + 68 + i * 11;
		Text (x0 + 12, y, Rows[i][0], Dim (UI_DIM, 80));
		Text (x0 + 12 + 7 * 8, y, Rows[i][1], 0xEBEBEB);
	}
	Fill (x0 + 8, y0 + 157, x1 - x0 - 15, 1, UI_FRAME2);
	CText (y0 + 162, "NES IS A TRADEMARK OF NINTENDO.", 0x828796);
	CText (y0 + 173, "GAMES NOT INCLUDED.", 0x828796);

	static const TButtonHint Close = {'B', "CLOSE"};
	Buttons (201, &Close, 1);
}
