/*
 * A project's look (src/theme.cpp): colours, fonts, logo, credits and the
 * background ("grid")
 */
#ifndef COMMON_THEME_H
#define COMMON_THEME_H

#include <circle/types.h>

struct TUITheme
{
	const char	*pName;			// "S16-PI": the credits' title

	// colours, 0xRRGGBB
	u32		 nFrame1;		// the panels' frame
	u32		 nFrame2;		// its outline, the separators
	u32		 nPanel;		// inside a panel
	u32		 nBand;			// the selected line
	u32		 nCursor;		// the triangle on it
	u32		 nTitle;		// panel titles, LOADING, the bar
	u32		 nValue;		// values, the scroll arrows, the author
	u32		 nDim;			// labels, names not selected
	u32		 nPower;		// POWER OFF, CLEAR MEMORY

	// fonts: 96 characters from 0x20, 8x8 (fonts.h)
	const unsigned char (*pMenuFont)[8];
	const unsigned char (*pListFont)[8];	// the game list's titles
	boolean		 bListLower;		// it has lower case (else capitals)
	const unsigned char (*pInfoFont)[8];	// around the picture: title,
						// buttons, status line, messages

	// the game list's bottom row (set and region or maker, the counter)
	u32		 nListInfo;		// its colour
	boolean		 bListInfoInListFont;	// in pListFont (else pMenuFont)

	// the logo above the game list: one string per row, one character per
	// pixel; pLogoKeys[i] is drawn in pLogoColors[i], anything else is clear
	const char *const *pLogo;
	unsigned	 nLogoRows;
	unsigned	 nLogoScale;		// 1 or 2
	const char	*pLogoKeys;
	const u32	*pLogoColors;

	// credits: up to 10 rows "WHAT", "WHO" (the trademarks: README, LICENSES)
	const char *const (*pCredits)[2];
	unsigned	 nCredits;

	// around a game, [0] 1080p, [1] 720p: a frame nFrame game pixels out (0:
	// none, the grid's dither), the grid lit near it (nFrameColour, back in
	// nFrameGlow game pixels); the side text nSideShift game pixels further
	int		 nFrame[2];
	u32		 nFrameColour;
	int		 nFrameGlow;
	int		 nSideShift[2];
	// a fabric: the grid's step in game pixels, through the frame's corners
	// (0: from the screen's centre)
	int		 nFabric;
	// lines a game pixel wide (FALSE: thin)
	boolean		 bPixel;
	// without a frame: whole squares of nSquare game pixels (lines from the
	// screen's centre), a square's width clear around the picture (0: dither)
	int		 nSquare;
	// 1080p: the frame this many game pixels further out, the grid still
	// through the corners it had (0: the frame on the grid)
	int		 nFramePad;
};

extern const TUITheme g_Theme;

// The grid (0xRRGGBB) at point x, y from the screen's centre (with nFabric:
// from the frame's bottom right corner), M points per game pixel; nHalfW,
// nHalfH: half the screen. ThemeGround: its empty colour.
u32 ThemeGrid (int x, int y, int M, int nHalfW, int nHalfH);
u32 ThemeGround (void);

#endif
