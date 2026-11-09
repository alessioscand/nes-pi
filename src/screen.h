/*
 * nes-pi - the screen: what the kernel draws the game, the menus and the
 * console on
 *
 * The kernel uses only this interface. CVideoOut (video.cpp) is the HDMI
 * screen; another output (a CRT on the composite jack) would be another
 * implementation, in its own files, chosen in kernel.h.
 */
#ifndef SCREEN_H
#define SCREEN_H

#include <circle/types.h>

// the largest picture shown (the scale is chosen so that it fits)
#define PIC_MAX_W	304
#define PIC_MAX_H	240

// where the picture is in the buffer handed to DrawGame / DrawGameLine
struct TVideoSource
{
	unsigned nW, nH;		// visible picture
	unsigned nPitch;		// pixels per buffer line
	unsigned nX, nY;		// top left of the picture in the buffer
	unsigned bHalfX;		// 1: half the horizontal scale (the TV filter's
					// 602-pixel lines: 4:3 at the same height)
};

#define FILTER_NONE	0
#define FILTER_SCAN_LIGHT  1	// dark rows at 75%
#define FILTER_SCAN_MEDIUM 2	// dark rows at 50%
#define FILTER_SCAN_STRONG 3	// dark rows at 25%
#define FILTER_COUNT	4

// Logical colours (R in the low byte); the screen converts them if the
// framebuffer stores red in the high byte (Pi 4)
#define RGB(r, g, b)     (0xFF000000u | ((u32)(b) << 16) | ((u32)(g) << 8) | (u32)(r))

class CScreen
{
public:
	virtual ~CScreen (void) {}

	virtual boolean Initialize (void) = 0;		// console mode

	// Game mode for a filter; bAllowHVS = FALSE forces CPU scaling; bWide: the
	// TV filter is on (pictures with bHalfX: the CPU must draw at least 2x)
	virtual boolean SetGameMode (unsigned nFilter, boolean bAllowHVS, boolean bWide = FALSE) = 0;
	// Which picture comes next (menus or game): if it changes, the picture
	// rectangle is recomputed and both buffers are cleared
	virtual void SetSource (const TVideoSource &Source) = 0;
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

	// The whole picture into the back buffer; bScanlines = FALSE: menus, always sharp
	virtual void DrawGame (const u32 *pSource, boolean bScanlines = TRUE) = 0;
	// one line of it, with the caller's scratch lines (PIC_MAX_W * 8 pixels
	// each): called as each line is finished (the buffer shown while racing)
	virtual void DrawGameLine (const u32 *pSource, unsigned y, u32 *pLine, u32 *pDark) = 0;

	// One centred line of text in the top or bottom black band (no spans = clear)
	struct TTextSpan { const char *pText; u32 nColor; };	// colour: 0xRRGGBB
	virtual void DrawBandText (boolean bTop, const TTextSpan *pSpans, unsigned nSpans) = 0;

	// for the log
	virtual unsigned GetPixelOrder (void) const = 0;
	virtual unsigned GetWCPages (void) const = 0;	// framebuffer pages write-combining
	virtual boolean IsRedLow (void) const = 0;

	virtual void ShowBuffer (unsigned nBuffer) = 0;	// show a buffer now
	virtual boolean Flip (void) = 0;		// show the back buffer, swap roles
	virtual boolean WaitForVSync (void) = 0;
};

#endif
