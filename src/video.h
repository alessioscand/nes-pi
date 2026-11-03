/*
 * nes-pi - HDMI output
 *
 * Console mode: framebuffer at the HDMI resolution (boot text, errors).
 * Game mode: integer scaling of a picture (the menus at 304x224, the NES at
 * 256x240: the same scale for both, only the rectangle changes), split between
 * the CPU (draw scale m) and the hardware scaler HVS (factor k), S = m * k:
 *   - the framebuffer is (display / k), so the HVS enlarges it exactly k times
 *     with the same factor in both directions (square pixels, no stretching);
 *   - black borders are drawn by us inside the framebuffer;
 *   - if the display is not divisible by any useful k, k = 1 (CPU only).
 * Filters (applied while drawing, no extra buffering, no added lag):
 *   scanlines in three strengths (dark rows at 75%, 50% or 25%).
 */
#ifndef VIDEO_H
#define VIDEO_H

#include <circle/bcmframebuffer.h>
#include <circle/chargenerator.h>
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

// Logical colors (R in the low byte); CVideoOut converts them if the
// framebuffer stores red in the high byte (Pi 4)
#define RGB(r, g, b)     (0xFF000000u | ((u32)(b) << 16) | ((u32)(g) << 8) | (u32)(r))

class CVideoOut
{
public:
	CVideoOut (void);
	~CVideoOut (void);

	boolean Initialize (void);		// console mode

	// Game mode for a filter; bAllowHVS = FALSE forces CPU scaling; bWide: the
	// TV filter is on (pictures with bHalfX: the CPU must draw at least 2x)
	boolean SetGameMode (unsigned nFilter, boolean bAllowHVS, boolean bWide = FALSE);
	// Which picture comes next (menus or game): if it changes, the picture
	// rectangle is recomputed and both buffers are cleared
	void SetSource (const TVideoSource &Source);
	boolean SetConsoleMode (void);
	boolean IsGameMode (void) const		{ return m_bGameMode; }

	unsigned GetWidth (void) const		{ return m_nDispW; }	// HDMI resolution
	unsigned GetHeight (void) const		{ return m_nDispH; }
	unsigned GetScale (void) const		{ return m_nScale; }	// total integer scale S
	unsigned GetHVSFactor (void) const	{ return m_nK; }	// k (1 = CPU only)
	unsigned GetFilter (void) const		{ return m_nFilter; }
	unsigned GetFBWidth (void) const	{ return m_nWidth; }	// framebuffer before the HVS
	unsigned GetFBHeight (void) const	{ return m_nHeight; }
	unsigned GetGameY (void) const		{ return m_nGameY; }	// first game line in the framebuffer
	unsigned GetCPUScale (void) const	{ return m_nM; }	// m

	// Beam racing: game lines and band text go straight into the buffer being
	// shown (no flip); the UI (DrawGame) still uses the back buffer
	void SetRacing (boolean bOn)		{ m_bRacing = bOn; }
	boolean IsRacing (void) const		{ return m_bRacing; }

	// Text grid (character cells) of the console
	unsigned GetColumns (void) const;
	unsigned GetRows (void) const;
	void Clear (unsigned nBuffer, u32 nColor);
	void DrawText (unsigned nBuffer, unsigned nCol, unsigned nRow, const char *pText,
		       u32 nFG, u32 nBG);
	void ClearRows (unsigned nBuffer, unsigned nFirstRow, unsigned nRows, u32 nColor);

	// Game: draws the whole picture into the back buffer
	void DrawGame (const u32 *pSource);
	// one line of the picture into the back buffer (the shown one while
	// racing), with the caller's scratch lines (PIC_MAX_W * 8 pixels each)
	void DrawGameLine (const u32 *pSource, unsigned y, u32 *pLine, u32 *pDark)
	{
		DrawGameLineTo (m_bRacing ? m_nBack ^ 1 : m_nBack, pSource, y, pLine, pDark);
	}
	// One centred line of text in the top or bottom black band (no spans = clear)
	struct TTextSpan { const char *pText; u32 nColor; };	// colour: 0xRRGGBB
	void DrawBandText (boolean bTop, const TTextSpan *pSpans, unsigned nSpans);

	// Colors: RGB() values are converted to the framebuffer byte order
	unsigned GetPixelOrder (void) const	{ return m_nPixelOrder; }
	unsigned GetWCPages (void) const	{ return m_nWCPages; }	// framebuffer pages write-combining
	boolean IsRedLow (void) const		{ return m_bRLow; }

	unsigned GetBackBuffer (void) const	{ return m_nBack; }
	void ShowBuffer (unsigned nBuffer);	// show a buffer now
	boolean Flip (void);			// show back buffer, swap roles
	boolean WaitForVSync (void);

private:
	boolean Allocate (unsigned nWidth, unsigned nHeight);
	void UpdateRect (void);
	void DrawGameLineTo (unsigned nBuffer, const u32 *pSource, unsigned y, u32 *pLine, u32 *pDark);
	u32 *Row (unsigned nBuffer, unsigned nY) const
	{
		return m_pBase + (nBuffer * m_nHeight + nY) * m_nPitchPx;
	}
	void DrawChar (unsigned nBuffer, unsigned nX, unsigned nY, char ch, u32 nFG, u32 nBG,
		       unsigned nTextScale, const CCharGenerator &Font);

private:
	CBcmFrameBuffer *m_pFrameBuffer;
	CCharGenerator	 m_Font;		// console font (8x16)

	unsigned m_nDispW, m_nDispH;		// HDMI mode
	unsigned m_nWidth, m_nHeight;		// current framebuffer
	unsigned m_nPitchPx;
	u32	*m_pBase;
	unsigned m_nBack;
	boolean	 m_bGameMode;
	volatile boolean m_bRacing;

	unsigned m_nTextScale;			// console text scale (1 or 2)
	unsigned m_nScale;			// S
	unsigned m_nK;				// HVS factor
	unsigned m_nM;				// CPU draw scale
	unsigned m_nFilter;
	TVideoSource m_Src;			// current picture
	unsigned m_nGameX, m_nGameY;		// picture rectangle in the framebuffer
	unsigned m_nStatusY;
	unsigned m_nStatusScale;
	unsigned m_nTopY;

	u32 Native (u32 c) const		// RGB() (R low) -> framebuffer order
	{
		return m_bRLow ? c : (c & 0xFF00FF00u) | ((c >> 16) & 0xFFu) | ((c & 0xFFu) << 16);
	}

	u32	*m_pLine;			// one scaled game line (cached memory)
	u32	*m_pDark;			// the same line darkened (scanlines)
	unsigned m_nPixelOrder;			// reported by the firmware
	unsigned m_nWCPages;
	boolean	 m_bRLow;			// red in the low byte of a pixel
};

#endif
