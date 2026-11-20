/*
 * The menus, the same in every project: drawn like the game (its size,
 * XRGB8888), laid out on UI_W x UI_H (304x224, screen.h) in the middle; the
 * look from theme.h.
 */
#ifndef COMMON_UI_H
#define COMMON_UI_H

#include <circle/types.h>
#include "screen.h"
#include "theme.h"

#define UI_ELLIPSIS	"\x1F"	// "..." in one character (names cut short)

// Colours for messages and the status line, the same everywhere (0xRRGGBB)
#define UI_WHITE	0xF8F8F8u
#define UI_GREY		0xA0A0AAu
#define UI_OK		0x6EBE6Eu
#define UI_WARN		0xF0C828u
#define UI_ERR		0xFF6E6Eu
#define UI_VALUE	(g_Theme.nValue)

// OPTIONS > BEZEL: the next value (nDir +1 / -1: OFF, TEXT, GRID, FULL) and
// its name ("FULL" in the menu, "full" in the .cfg and the log)
unsigned BezelNext (unsigned nBezel, int nDir);
const char *BezelName (unsigned nBezel, boolean bMenu);

struct TMenuItem
{
	const char	*pLabel;
	const char	*pValue;	// 0 = none
	boolean		 bDisabled;
};

struct TButtonHint
{
	char		 cButton;	// 'A' 'B' 'C' 'D', 'S' = SELECT (written)
	const char	*pText;
};

struct TGameEntry
{
	char	Name[64];	// the title in the list (a long one scrolls)
	char	File[64];	// the file on the card
	int	nGame;		// the project's own index (-1: none)
	char	Info[40];	// under the list: "goldnaxe  SET 6 US"
};

class CUI
{
public:
	CUI (void);

	u32 *GetBuffer (void)	{ return m_Buf; }
	// the last menu is over the grid (no game behind): shown with no filter
	boolean ShowsGrid (void) const	{ return m_bGrid; }
	// the logo above the game list (not on a CRT: it would leave the title
	// safe area)
	void SetLogo (boolean bShow)	{ m_bLogo = bShow; }

	// another font for the game list's titles (neo-pi: the BIOS's own)
	void SetListFont (const unsigned char (*pFont)[8], boolean bLower);

	// bOverGame: a game is loaded, the list goes over it, paused
	// (SnapshotGame); nTicks: frames since the selection changed (a long
	// title scrolls)
	void DrawList (const TGameEntry *pGames, unsigned nCount, unsigned nSel,
		       boolean bOverGame, unsigned nTicks);
	// the picture behind the next menus, darkened: call when they open
	void SnapshotGame (const u32 *pGameBuf);
	// A panel with a title and rows. bWide: as wide as the list (DIP
	// SWITCHES); nRows: its size in rows; bMoreAbove / Below: arrows
	void DrawPanelMenu (boolean bOverGame, const char *pTitle,
			    const TMenuItem *pItems, unsigned nItems, unsigned nSel,
			    const char *pMessage, u32 nMessageColor,
			    const TButtonHint *pHints, unsigned nHints, const char *pFooter,
			    boolean bWide = FALSE, unsigned nRows = 0,
			    boolean bMoreAbove = FALSE, boolean bMoreBelow = FALSE);
	// a small box alone, over the paused game or the background
	void DrawBox (boolean bOverGame, const char *pLine1, const char *pLine2, const TButtonHint *pHint);
	// CONTROLS: nStep = the slot being asked (A B C D START SELECT, then E F
	// with 6 buttons); one past the last = saved
	void DrawMapping (boolean bOverGame, const char *pTitle, int nStep,
			  const char *const *pValues, const char *pFooter);
	void DrawLoading (const char *pName, unsigned nPercent);
	void DrawCredits (const char *pVersion);	// over the paused game
	void DrawMessage (const char *pTitle, const char *pLine1, const char *pLine2);
	void DrawBlack (void);
	void DrawEmpty (void);			// the background alone (start-up)

	static u32 Dim (u32 c, unsigned nPercent);

private:
	void Pixel (int x, int y, u32 c);
	void Fill (int x, int y, int w, int h, u32 c);
	void Background (void);
	void Backdrop (boolean bOverGame);
	void Logo (int y);
	void Panel (int x0, int y0, int x1, int y1);
	void Separator (int x0, int x1, int y);
	void Text (int x, int y, const char *s, u32 c, unsigned nTall = 1, boolean bList = FALSE);
	void CText (int y, const char *s, u32 c, unsigned nTall = 1, boolean bList = FALSE);
	void Buttons (int y, const TButtonHint *pHints, unsigned nHints);
	void ButtonIcon (int x, int y, char cButton);
	static u32 ButtonColorOf (char cButton);

private:
	u32	m_Buf[GAME_SRC_PITCH * GAME_SRC_LINES] __attribute__ ((aligned (64)));
	u32	m_Snapshot[GAME_SRC_PITCH * GAME_SRC_LINES];
	const unsigned char (*m_pListFont)[8];
	boolean	m_bListLower;
	boolean	m_bGrid;
	boolean	m_bLogo;
	int	m_nClipX0, m_nClipX1;	// Pixel draws only inside (a scrolling title)
};

#endif
