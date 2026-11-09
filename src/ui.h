/*
 * nes-pi - menus drawn at 304x224 (neo-pi's layout) in their own buffer
 * format as the core (320x264, XRGB8888 with R high), so they go through the
 * same scaling and filters as the game.
 */
#ifndef NESPI_UI_H
#define NESPI_UI_H

#include <circle/types.h>
#include "video.h"

// the menus keep neo-pi's layout: 304x224 inside a 320x264 buffer
#define UI_W		304
#define UI_H		224
#define UI_ELLIPSIS	"\x1F"	// "..." in one character (Text, CText)
#define UI_SRC_PITCH	320
#define UI_SRC_LINES	264
#define UI_SRC_X	8
#define UI_SRC_Y	24

// Palette "silver / blue" (0xRRGGBB)
#define UI_BG1		0x060E1Eu
#define UI_BG2		0x0A1830u
#define UI_FRAME1	0xBCBCBCu
#define UI_FRAME2	0x5C5C5Cu
#define UI_PANEL	0x0C0C0Cu
#define UI_BAND		0xBC0049u	// crimson at 80% (= logo PI)
#define UI_TITLE	0xF8B800u	// gold (= logo NES)
#define UI_VALUE	0xF8B800u
#define UI_DIM		0x96A0AFu
#define UI_INFO		0x7A8290u	// bottom row of the game list
#define UI_WHITE	0xF8F8F8u
#define UI_CURSOR	0xF8B800u	// gold: red would vanish on the band
#define UI_DANGER	0xE40058u	// POWER OFF, logo PI (NES palette $15)
#define UI_OK		0x6EBE6Eu
#define UI_ERR		0xFF6E6Eu
#define UI_WARN		0xF0C828u
#define UI_GREY		0xA0A0AAu

struct TMenuItem
{
	const char	*pLabel;
	const char	*pValue;	// 0 = none
	boolean		 bDisabled;
};

struct TButtonHint
{
	char		 cButton;	// 'A' 'B' 'C' 'D'
	const char	*pText;
};

struct TGameEntry
{
	char	Name[64];	// from the file name
	char	File[64];	// file name in the nespi folder
	char	Info[12];	// "NTSC", "PAL", ...: from the file name, under the list
};

class CUI
{
public:
	CUI (void);

	// sfix.sfix from the user's BIOS: the console's own 8x8 font

	u32 *GetBuffer (void)	{ return m_Buf; }

	// bOverGame: a game is loaded, show it paused behind the list (SnapshotGame);
	// nTicks = frames since the selection changed (scrolls a long title)
	void DrawList (const TGameEntry *pGames, unsigned nCount, unsigned nSel, const char *pMessage,
		       boolean bOverGame, unsigned nTicks);
	// Menu over a darkened copy of the game frame (call SnapshotGame when it opens)
	void SnapshotGame (const u32 *pGameBuf);
	// Generic panel menu: bOverGame = draw over the snapshot, else over the pattern
	void DrawPanelMenu (boolean bOverGame, const char *pTitle,
			    const TMenuItem *pItems, unsigned nItems, unsigned nSel,
			    const char *pMessage, u32 nMessageColor,
			    const TButtonHint *pHints, unsigned nHints, const char *pFooter);
	// Small box drawn over the current buffer contents
	void DrawBox (const char *pLine1, const char *pLine2, const TButtonHint *pHint);
	// Controls set-up: nStep 0-5 = A B C D START SELECT being asked, 6 = saved
	void DrawMapping (boolean bOverGame, const char *pTitle, int nStep,
			  const char *const *pValues, const char *pFooter);
	void DrawLoading (const char *pName, unsigned nPercent);
	// Credits over the darkened game (call SnapshotGame first)
	void DrawCredits (const char *pVersion);
	void DrawMessage (const char *pTitle, const char *pLine1, const char *pLine2);

	static u32 Dim (u32 c, unsigned nPercent);

private:
	void Pixel (int x, int y, u32 c);
	void Fill (int x, int y, int w, int h, u32 c);
	void Background (boolean bFreeze = FALSE);
	void Backdrop (boolean bOverGame);
	void Logo (void);
	void Panel (int x0, int y0, int x1, int y1);
	void Text (int x, int y, const char *s, u32 c, unsigned nTall = 1, boolean bMixedCase = FALSE);
	void CText (int y, const char *s, u32 c, unsigned nTall = 1, boolean bMixedCase = FALSE);
	void Buttons (int y, const TButtonHint *pHints, unsigned nHints);
	void ButtonIcon (int x, int y, char cButton);
	static u32 ButtonColorOf (char cButton);

private:
	u32	m_Buf[UI_SRC_PITCH * UI_SRC_LINES] __attribute__ ((aligned (64)));
	u32	m_Snapshot[UI_W * UI_H];
	int	m_nClipX0, m_nClipX1;	// Pixel draws only from x0 to x1 - 1
};

#endif
