/*
 * The screen the kernel draws on (game, menus, console); CVideoOut
 * (video.cpp) is HDMI, another output would be another implementation.
 */
#ifndef COMMON_SCREEN_H
#define COMMON_SCREEN_H

#include <circle/types.h>
#include "project.h"

// The game's buffer and the visible picture inside it (project.h)
#define GAME_SRC_PITCH   PROJECT_SRC_PITCH
#define GAME_SRC_LINES   PROJECT_SRC_LINES
#define GAME_SRC_X       PROJECT_SRC_X
#define GAME_SRC_Y       PROJECT_SRC_Y
#define GAME_W           PROJECT_WIDTH
#define GAME_H           PROJECT_HEIGHT

// The menus' layout (ui.h) and the canvas they are drawn on: the picture, or
// more if the menus are larger (nes-pi), centred on it; the game's buffer
// has room for it around the picture
#define UI_W             304
#define UI_H             224
#define CANVAS_W         (GAME_W > UI_W ? GAME_W : UI_W)
#define CANVAS_H         (GAME_H > UI_H ? GAME_H : UI_H)
#define CANVAS_PAD_X     ((CANVAS_W - GAME_W) / 2)
#define CANVAS_PAD_Y     ((CANVAS_H - GAME_H) / 2)
#define CANVAS_WIDER     (CANVAS_W > GAME_W || CANVAS_H > GAME_H)

// TV FILTER, in the menu's order
#define FILTER_NONE	0
#define FILTER_FAST	1	// the bottom half of each game line's rows at 50%
#define FILTER_RGB	2	// crt-hyllian-fast (crtfx.h)
#define FILTER_CRT	3	// crt-pi (crtfx.h)
#define FILTER_COMPOSITE 4	// blargg's NTSC filter (tvcore.h)
#define FILTER_COUNT	5
#define FILTER_IS_SHADER(f) ((f) == FILTER_RGB || (f) == FILTER_CRT)

// What is around the picture: with a game, OPTIONS > BEZEL; with none, the grid
// a pixel of the menus where the screen's background shows (DrawGame: with no
// filter; on the canvas around the picture, always)
#define GRID_HOLE	0x00010203u

#define BEZEL_GRID	1	// the background (theme.h), behind the menus too; else black
#define BEZEL_INFO	2	// the game's title and buttons (where there is room)
				// OPTIONS > BEZEL: OFF, TEXT (INFO), GRID, FULL (both)

// Logical colours (R in the low byte); the screen converts them if the
// framebuffer stores red in the high byte (Pi 4)
#define RGB(r, g, b)     (0xFF000000u | ((u32)(b) << 16) | ((u32)(g) << 8) | (u32)(r))

class CScreen
{
public:
	virtual ~CScreen (void) {}

	virtual boolean Initialize (void) = 0;		// console mode

	// Game mode for a filter; bAllowHVS = FALSE forces CPU scaling
	virtual boolean SetGameMode (unsigned nFilter, boolean bAllowHVS) = 0;
	virtual boolean SetConsoleMode (void) = 0;
	virtual boolean IsGameMode (void) const = 0;

	virtual unsigned GetWidth (void) const = 0;	// output resolution
	virtual unsigned GetHeight (void) const = 0;
	virtual unsigned GetScale (void) const = 0;	// total integer scale
	virtual unsigned GetHVSFactor (void) const = 0;	// part done by the HVS (1 = CPU only)
	virtual unsigned GetCPUScale (void) const = 0;	// part drawn by the CPU
	virtual unsigned GetFilter (void) const = 0;
	virtual unsigned GetFBWidth (void) const = 0;	// framebuffer before the HVS
	virtual unsigned GetFBHeight (void) const = 0;
	virtual unsigned GetGameY (void) const = 0;	// first game line in the framebuffer

	// Beam racing: game lines and band text go straight into the buffer being
	// shown (no flip); the UI (DrawGame) still uses the back buffer
	virtual void SetRacing (boolean bOn) = 0;

	// Console text (character cells)
	virtual unsigned GetRows (void) const = 0;
	virtual void Clear (unsigned nBuffer, u32 nColor) = 0;
	virtual void DrawText (unsigned nBuffer, unsigned nCol, unsigned nRow, const char *pText,
			       u32 nFG, u32 nBG) = 0;
	virtual void ClearRows (unsigned nBuffer, unsigned nFirstRow, unsigned nRows, u32 nColor) = 0;

	// The visible GAME_W x GAME_H area into the back buffer; bFilter = FALSE:
	// no TV filter (the menus with no game behind them)
	// bCanvas: the menus' whole canvas, beyond the picture if it is wider
	virtual void DrawGame (const u32 *pSource, boolean bFilter = TRUE, boolean bCanvas = FALSE) = 0;
	// one line of it, with the caller's scratch lines (GAME_W * 8 pixels each):
	// called by core 1 as each line is finished
	virtual void DrawGameLine (const u32 *pSource, unsigned y, u32 *pLine, u32 *pDark) = 0;
	// COMPOSITE: one filtered line (nNtscW pixels 0x00RRGGBB) stretched to the
	// game width (COMP_OUT_WIDTH (GAME_W) pixels: GAME_W; a longer line, the menus'
	// canvas: as much wider); pLine = scratch (CANVAS_W * 8)
	virtual void DrawNtscLine (unsigned y, const u32 *pNtsc, unsigned nNtscW, u32 *pLine) = 0;

	// The border around the picture: nMode = BEZEL_GRID and / or BEZEL_INFO
	// (0: black). Drawn in both buffers when the mode or nId (the game)
	// changes; SetGameMode forgets it
	struct TBezelInfo
	{
		const char *pTitle;			// "GOLDEN AXE"
		const char *pMaker;			// "SEGA 1989"
		const char *pButtons[6];		// buttons A..F, 0 after the last,
							// "" = not used
	};
	virtual void SetBezel (unsigned nMode, const TBezelInfo *pInfo, unsigned nId) = 0;

	// Text beside the picture ('\n': new line, long lines wrap): left column
	// right-aligned, bottom-aligned (status); right column top-aligned
	// (messages); too narrow (4:3): the bands below / above. No spans: clear.
	struct TTextSpan { const char *pText; u32 nColor; };	// colour: 0xRRGGBB
	virtual void DrawSideText (boolean bRight, const TTextSpan *pSpans, unsigned nSpans) = 0;

	// for the log
	virtual unsigned GetPixelOrder (void) const = 0;
	virtual unsigned GetWCPages (void) const = 0;	// framebuffer pages write-combining
	virtual boolean IsRedLow (void) const = 0;

	virtual void ShowBuffer (unsigned nBuffer) = 0;	// show a buffer now
	virtual boolean Flip (void) = 0;		// show the back buffer, swap roles
	virtual boolean WaitForVSync (void) = 0;
};

#endif
