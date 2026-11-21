/*
 * HDMI output
 *
 * Console mode: the framebuffer at the HDMI resolution. Game mode: integer
 * scaling S = m * k, m by the CPU, k by the HVS (square pixels; k = 1 if no
 * useful factor). TV FILTER while drawing, no added lag: FAST here, RGB,
 * CRT and COMPOSITE on core 2 (crtfx, tvcore).
 * The border (BEZEL): black or the theme's grid, with a game its title and
 * buttons (1080p), drawn once. Status and messages in the side columns,
 * drawn by core 0 only when they change, over the border only.
 */
#ifndef COMMON_VIDEO_H
#define COMMON_VIDEO_H

#include "screen.h"
#include "smp.h"
#include <circle/bcmframebuffer.h>
#include <circle/chargenerator.h>

class CVideoOut final : public CScreen
{
public:
	CVideoOut (void);
	~CVideoOut (void);

	boolean Initialize (void) override;

	boolean SetGameMode (unsigned nFilter, boolean bAllowHVS) override;
	boolean SetConsoleMode (void) override;
	boolean IsGameMode (void) const override	{ return m_bGameMode; }

	unsigned GetWidth (void) const override		{ return m_nDispW; }
	unsigned GetHeight (void) const override	{ return m_nDispH; }
	unsigned GetScale (void) const override		{ return m_nScale; }	// S
	unsigned GetHVSFactor (void) const override	{ return m_nK; }	// k
	unsigned GetCPUScale (void) const override	{ return m_nM; }	// m
	unsigned GetFilter (void) const override	{ return m_nFilter; }
	unsigned GetFBWidth (void) const override	{ return m_nWidth; }
	unsigned GetFBHeight (void) const override	{ return m_nHeight; }
	unsigned GetGameY (void) const override		{ return m_nGameY; }

	void SetRacing (boolean bOn) override		{ SharedStore (m_bRacing, bOn); }

	unsigned GetRows (void) const override;
	void Clear (unsigned nBuffer, u32 nColor) override;
	void DrawText (unsigned nBuffer, unsigned nCol, unsigned nRow, const char *pText,
		       u32 nFG, u32 nBG) override;
	void ClearRows (unsigned nBuffer, unsigned nFirstRow, unsigned nRows, u32 nColor) override;

	void DrawGame (const u32 *pSource, boolean bFilter = TRUE, boolean bCanvas = FALSE) override;
	void DrawGameLine (const u32 *pSource, unsigned y, u32 *pLine, u32 *pDark) override
	{
		DrawGameLineTo (SharedLoad (m_bRacing) ? m_nBack ^ 1 : m_nBack, pSource, y, pLine, pDark, TRUE, FALSE);
	}
	void DrawNtscLine (unsigned y, const u32 *pNtsc, unsigned nNtscW, u32 *pLine) override;
	void SetBezel (unsigned nMode, const TBezelInfo *pInfo, unsigned nId) override;
	void DrawSideText (boolean bRight, const TTextSpan *pSpans, unsigned nSpans) override;

	unsigned GetPixelOrder (void) const override	{ return m_nPixelOrder; }
	unsigned GetWCPages (void) const override	{ return m_nWCPages; }
	boolean IsRedLow (void) const override		{ return m_bRLow; }

	void ShowBuffer (unsigned nBuffer) override;
	boolean Flip (void) override;
	boolean WaitForVSync (void) override;

private:
	unsigned GetColumns (void) const;
	boolean Allocate (unsigned nWidth, unsigned nHeight);
	void DrawGameLineTo (unsigned nBuffer, const u32 *pSource, unsigned y, u32 *pLine, u32 *pDark,
			     boolean bFilter, boolean bCanvas = FALSE);
	void RestoreCanvas (unsigned nBuf);		// the border where the canvas was
	u32 *Row (unsigned nBuffer, unsigned nY) const
	{
		return m_pBase + (nBuffer * m_nHeight + nY) * m_nPitchPx;
	}
	void DrawChar (unsigned nBuffer, unsigned nX, unsigned nY, char ch, u32 nFG, u32 nBG,
		       unsigned nTextScale, const CCharGenerator &Font);
	// the border
	struct TTarget { u32 *pBase; unsigned nPitch, nW, nH; };	// where text goes
	void BgPoint (unsigned *pX, unsigned *pY) const;
	int GameDistance (unsigned x, unsigned y, int *pGX, int *pGY) const;
	u32 ToFb (u32 c) const;					// 0xRRGGBB -> framebuffer
	u32 GridColour (unsigned x, unsigned y) const;		// 0xRRGGBB
	u32 GridPixel (unsigned x, unsigned y) const;		// framebuffer order
	u32 FramePixel (unsigned x, unsigned y, int d) const;
	u32 DitherPixel (unsigned x, unsigned y, int d, int gx, int gy) const;
	u32 SquarePixel (unsigned x, unsigned y) const;
	u32 BgPixel (unsigned x, unsigned y) const;		// framebuffer order
	void FillBorder (unsigned nBuf);
	void DrawInfoLines (unsigned nBuf);
	unsigned DrawButtons (const TTarget &T, unsigned x, unsigned y);
	// the theme's pInfoFont, scale m_nU, hard shadow; nColor 0xRRGGBB;
	// returns the next x
	unsigned InfoWidth (const char *pText) const;
	unsigned DrawInfoText (const TTarget &T, unsigned x, unsigned y, const char *pText, u32 nColor);

private:
	CBcmFrameBuffer *m_pFrameBuffer;
	CCharGenerator	 m_Font;		// console font (8x16)

	unsigned m_nDispW, m_nDispH;		// HDMI mode
	unsigned m_nWidth, m_nHeight;		// current framebuffer
	unsigned m_nPitchPx;
	u32	*m_pBase;
	unsigned m_nBack;
	boolean	 m_bGameMode;
	boolean	 m_bRacing;			// read by core 1

	unsigned m_nTextScale;			// console text scale (1 or 2)
	unsigned m_nScale;			// S
	unsigned m_nK;				// HVS factor
	unsigned m_nM;				// CPU draw scale
	unsigned m_nFilter;
	unsigned m_nGameX, m_nGameY;		// game rectangle in the framebuffer
	unsigned m_nU;				// text scale (2 screen pixels per font pixel)
	boolean	 m_bSide;			// status and messages in the side columns
	boolean	 m_bBands;			// room for the title and the buttons
	unsigned m_nGap;			// text from the picture and the screen edge
	unsigned m_nSideGap;			// the columns' text from the picture
	unsigned m_nBgK;			// the background's points: framebuffer
	unsigned m_nBgM;			// pixels each (1, or m_nM with bPixel),
						// points to a game pixel (m_nM, or 1)
	int	 m_nFrame;			// the theme's frame, game pixels (0: none)
	int	 m_nFrameR, m_nFrameB;		// its lines, background points from the
	int	 m_nFrameL, m_nFrameT;		// screen's centre (left, top: minus)
	int	 m_nGridX0, m_nGridY0;		// where ThemeGrid counts from
	unsigned m_nColTop, m_nColBot;		// the columns' text: between these lines

	// the border
	unsigned m_nBezel;			// BEZEL_GRID, BEZEL_INFO (0: black)
	unsigned m_nBezelId;
	boolean	 m_bBezelDrawn;			// in both buffers
	TBezelInfo m_Info;
	struct TRect { unsigned x0, y0, x1, y1; };	// x1, y1 excluded; empty: x1 = 0
	TRect	 m_Text[2][2];			// [buffer][right column]: text drawn there
	u32	 m_nTextSum[2][2];		// and what it says (0 = nothing)
	u32	*m_pBlock;			// a column's text, prepared before the copy
	u32	*m_pGridPic;			// the grid where the picture goes (the UI's GRID_HOLE)
	u32	*m_pCanvasPic;			// CANVAS_WIDER: the border under the canvas
	boolean	 m_bCanvasDrawn;		// and the canvas drawn over it

	u32 Native (u32 c) const		// RGB() (R low) -> framebuffer order
	{
		return m_bRLow ? c : (c & 0xFF00FF00u) | ((c >> 16) & 0xFFu) | ((c & 0xFFu) << 16);
	}

	u32	*m_pLine;			// one scaled game line (cached memory)
	u32	*m_pDark;			// the same line at 50% (FAST)
	unsigned m_nPixelOrder;			// reported by the firmware
	unsigned m_nWCPages;
	boolean	 m_bRLow;			// red in the low byte of a pixel
};

#endif
