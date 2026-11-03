/*
 * nes-pi - HDMI output (see video.h)
 */
#include "fontdata.h"
#include "video.h"
#include "fbmap.h"
#include <circle/bcmpropertytags.h>
#include <circle/font.h>
#include <circle/util.h>

#define PROPTAG_RELEASE_BUFFER	0x00048001
#define PROPTAG_GET_PIXEL_ORDER	0x00040006	// 0 = BGR, 1 = RGB

#define MAX_LINE	(PIC_MAX_W * 8)		// longest scaled line we support

CVideoOut::CVideoOut (void)
:	m_pFrameBuffer (0),
	m_nDispW (0),
	m_nDispH (0),
	m_nWidth (0),
	m_nHeight (0),
	m_nPitchPx (0),
	m_pBase (0),
	m_nBack (1),
	m_bGameMode (FALSE),
	m_bRacing (FALSE),
	m_nTextScale (1),
	m_nScale (1),
	m_nK (1),
	m_nM (1),
	m_nFilter (FILTER_NONE),
	m_nGameX (0),
	m_nGameY (0),
	m_nStatusY (0),
	m_nStatusScale (1),
	m_nTopY (0),
	m_pLine (0),
	m_pDark (0),

	m_nPixelOrder (1),
	m_nWCPages (0),
	m_bRLow (TRUE)
{
	m_Src.nW = PIC_MAX_W;		// until SetSource: the menus' layout (ui.h)
	m_Src.nH = 224;
	m_Src.nPitch = 320;
	m_Src.nX = 8;
	m_Src.nY = 24;
	m_Src.bHalfX = 0;
}

CVideoOut::~CVideoOut (void)
{
	delete m_pFrameBuffer;
	delete [] m_pLine;
	delete [] m_pDark;
}

boolean CVideoOut::Allocate (unsigned nWidth, unsigned nHeight)
{
	if (m_pFrameBuffer != 0)
	{
		CBcmPropertyTags Tags;
		TPropertyTagSimple Release;
		Tags.GetTag (PROPTAG_RELEASE_BUFFER, &Release, sizeof Release);	// may be unsupported
		delete m_pFrameBuffer;
	}

	m_pFrameBuffer = new CBcmFrameBuffer (nWidth, nHeight, 32, nWidth, nHeight * 2, 0, TRUE);
	if (   m_pFrameBuffer == 0
	    || !m_pFrameBuffer->Initialize ())
	{
		return FALSE;
	}

	m_nWidth   = m_pFrameBuffer->GetWidth ();
	m_nHeight  = m_pFrameBuffer->GetHeight ();
	m_nPitchPx = m_pFrameBuffer->GetPitch () / 4;
	m_pBase    = (u32 *) (uintptr) m_pFrameBuffer->GetBuffer ();
	if (   m_pBase == 0
	    || m_nWidth != nWidth || m_nHeight != nHeight
	    || m_pFrameBuffer->GetVirtHeight () < 2 * m_nHeight)
	{
		return FALSE;
	}

	// Byte order of the pixels: QEMU reports RGB (1, red in the low byte),
	// the Pi 4 firmware (framebuffer_swap=1 by default) reports BGR (0, red
	// in the high byte, the same layout as Geolith's buffer)
	CBcmPropertyTags Tags;
	TPropertyTagSimple Order;
	if (Tags.GetTag (PROPTAG_GET_PIXEL_ORDER, &Order, sizeof Order))
	{
		m_nPixelOrder = Order.nValue;
	}
	m_bRLow = m_nPixelOrder != 0;

	// write-combining: stores merged into bursts instead of one bus write each
	m_nWCPages = FbWriteCombine ((uintptr) m_pBase, (size_t) m_nPitchPx * 4 * m_pFrameBuffer->GetVirtHeight ());

	Clear (0, RGB (0, 0, 0));
	Clear (1, RGB (0, 0, 0));
	ShowBuffer (0);
	return TRUE;
}

boolean CVideoOut::Initialize (void)
{
	// current HDMI mode (chosen by the firmware from config.txt / EDID)
	CBcmPropertyTags Tags;
	TPropertyTagDisplayDimensions Dim;
	if (   Tags.GetTag (PROPTAG_GET_DISPLAY_DIMENSIONS, &Dim, sizeof Dim)
	    && Dim.nWidth >= 640 && Dim.nWidth <= 4096
	    && Dim.nHeight >= 480 && Dim.nHeight <= 2160)
	{
		m_nDispW = Dim.nWidth;
		m_nDispH = Dim.nHeight;
	}
	else
	{
		m_nDispW = 640;
		m_nDispH = 480;
	}

	m_pLine = new u32[MAX_LINE];
	m_pDark = new u32[MAX_LINE];
	if (m_pLine == 0 || m_pDark == 0)
	{
		return FALSE;
	}

	return SetConsoleMode ();
}

boolean CVideoOut::SetConsoleMode (void)
{
	m_bGameMode = FALSE;
	m_nTextScale = m_nDispH >= 900 ? 2 : 1;
	return Allocate (m_nDispW, m_nDispH);
}

boolean CVideoOut::SetGameMode (unsigned nFilter, boolean bAllowHVS, boolean bWide)
{
	m_nFilter = nFilter < FILTER_COUNT ? nFilter : FILTER_NONE;


	// total integer scale S: the largest picture must fit
	unsigned sx = m_nDispW / PIC_MAX_W, sy = m_nDispH / PIC_MAX_H;
	m_nScale = sx < sy ? sx : sy;
	if (m_nScale < 1) m_nScale = 1;
	if (m_nScale > 8) m_nScale = 8;

	// HVS factor k: largest divisor of S that divides the display exactly;
	// with a filter the CPU must draw at least 2x (m >= 2)
	m_nK = 1;
	if (bAllowHVS)
	{
		for (unsigned k = m_nScale; k >= 2; k--)
		{
			if (   m_nScale % k == 0
			    && m_nDispW % k == 0 && m_nDispH % k == 0
			    && ((m_nFilter == FILTER_NONE && !bWide) || m_nScale / k >= 2))
			{
				m_nK = k;
				break;
			}
		}
	}
	m_nM = m_nScale / m_nK;

	unsigned nFbW = m_nDispW / m_nK, nFbH = m_nDispH / m_nK;
	if (!Allocate (nFbW, nFbH))
	{
		// fall back to CPU scaling at the full resolution
		m_nK = 1;
		m_nM = m_nScale;
		if (!Allocate (m_nDispW, m_nDispH))
		{
			return FALSE;
		}
	}

	UpdateRect ();
	m_bGameMode = TRUE;
	return TRUE;
}

void CVideoOut::UpdateRect (void)
{
	unsigned nGameW = m_Src.bHalfX ? m_Src.nW * m_nM / 2 : m_Src.nW * m_nM;
	unsigned nGameH = m_Src.nH * m_nM;
	m_nGameX = m_nWidth > nGameW ? (m_nWidth - nGameW) / 2 : 0;
	m_nGameY = m_nHeight > nGameH ? (m_nHeight - nGameH) / 2 : 0;

	// text in the black bands: 8x8 menu font, 16 px high on screen where possible
	m_nStatusScale = m_nK >= 2 ? 1 : 2;
	unsigned nTextH = 8 * m_nStatusScale + m_nStatusScale;		// + shadow
	unsigned nBottom = m_nHeight - (m_nGameY + nGameH);
	m_nStatusY = m_nGameY + nGameH + (nBottom > nTextH ? (nBottom - nTextH) / 2 : 0);
	m_nTopY = m_nGameY > nTextH ? (m_nGameY - nTextH) / 2 : 0;
}

void CVideoOut::SetSource (const TVideoSource &Source)
{
	if (   Source.nW == m_Src.nW && Source.nH == m_Src.nH && Source.nPitch == m_Src.nPitch
	    && Source.nX == m_Src.nX && Source.nY == m_Src.nY && Source.bHalfX == m_Src.bHalfX)
	{
		return;
	}
	boolean bSize = Source.nW != m_Src.nW || Source.nH != m_Src.nH || Source.bHalfX != m_Src.bHalfX;
	m_Src = Source;
	if (m_bGameMode)
	{
		UpdateRect ();
		if (bSize)
		{
			Clear (0, RGB (0, 0, 0));
			Clear (1, RGB (0, 0, 0));
		}
	}
}

unsigned CVideoOut::GetColumns (void) const
{
	return m_nWidth / (m_Font.GetCharWidth () * m_nTextScale);
}

unsigned CVideoOut::GetRows (void) const
{
	return m_nHeight / (m_Font.GetCharHeight () * m_nTextScale);
}

void CVideoOut::Clear (unsigned nBuffer, u32 nColor)
{
	nColor = Native (nColor);
	for (unsigned y = 0; y < m_nHeight; y++)
	{
		u32 *p = Row (nBuffer, y);
		for (unsigned x = 0; x < m_nWidth; x++)
		{
			p[x] = nColor;
		}
	}
}

void CVideoOut::ClearRows (unsigned nBuffer, unsigned nFirstRow, unsigned nRows, u32 nColor)
{
	nColor = Native (nColor);
	unsigned nCharH = m_Font.GetCharHeight () * m_nTextScale;
	unsigned y0 = nFirstRow * nCharH;
	unsigned y1 = (nFirstRow + nRows) * nCharH;
	if (y1 > m_nHeight)
	{
		y1 = m_nHeight;
	}
	for (unsigned y = y0; y < y1; y++)
	{
		u32 *p = Row (nBuffer, y);
		for (unsigned x = 0; x < m_nWidth; x++)
		{
			p[x] = nColor;
		}
	}
}

void CVideoOut::DrawChar (unsigned nBuffer, unsigned nX, unsigned nY, char ch, u32 nFG, u32 nBG,
			  unsigned nTextScale, const CCharGenerator &Font)
{
	nFG = Native (nFG);
	nBG = Native (nBG);
	unsigned nCharW = Font.GetCharWidth ();
	unsigned nCharH = Font.GetCharHeight ();
	if (   nX + nCharW * nTextScale > m_nWidth
	    || nY + nCharH * nTextScale > m_nHeight)
	{
		return;
	}

	for (unsigned cy = 0; cy < nCharH; cy++)
	{
		CCharGenerator::TPixelLine Line = Font.GetPixelLine (ch, cy);
		for (unsigned sy = 0; sy < nTextScale; sy++)
		{
			u32 *p = Row (nBuffer, nY + cy * nTextScale + sy) + nX;
			for (unsigned cx = 0; cx < nCharW; cx++)
			{
				u32 nColor = Font.GetPixel (cx, Line) ? nFG : nBG;
				for (unsigned sx = 0; sx < nTextScale; sx++)
				{
					*p++ = nColor;
				}
			}
		}
	}
}

void CVideoOut::DrawText (unsigned nBuffer, unsigned nCol, unsigned nRow, const char *pText,
			  u32 nFG, u32 nBG)
{
	unsigned nCharW = m_Font.GetCharWidth () * m_nTextScale;
	unsigned nCharH = m_Font.GetCharHeight () * m_nTextScale;
	for (unsigned i = 0; pText[i] != '\0' && nCol + i < GetColumns (); i++)
	{
		DrawChar (nBuffer, (nCol + i) * nCharW, nRow * nCharH, pText[i], nFG, nBG,
			  m_nTextScale, m_Font);
	}
}

void CVideoOut::DrawGame (const u32 *pSource)
{
	for (unsigned y = 0; y < m_Src.nH; y++)
	{
		DrawGameLineTo (m_nBack, pSource, y, m_pLine, m_pDark);
	}
}

void CVideoOut::DrawGameLineTo (unsigned nBuffer, const u32 *pSource, unsigned y, u32 *pLine, u32 *pDark)
{
	const unsigned M = m_nM;
	const unsigned nW = m_Src.nW;
	const unsigned nGameW = m_Src.bHalfX ? nW * M / 2 : nW * M;

	// scanlines: bottom rows of each group darker (2 of 4 at 4x, 1 of 2 or 3)
	const unsigned nDarkRows = m_nFilter != FILTER_NONE ? (M >= 4 ? M / 2 : (M >= 2 ? 1 : 0)) : 0;

	// expand one source line horizontally into cached memory, converting
	// Geolith's XRGB8888 (R high) to the framebuffer order if needed
	const u32 *pSrc = pSource + (m_Src.nY + y) * m_Src.nPitch + m_Src.nX;
	u32 *pDst = pLine;
	if (m_Src.bHalfX)
	{
		// the TV filter's lines (already soft): M/2 times wide; with an odd M
		// (720p) each output pixel blends the two source pixels it falls on
		const u32 nStep = (2u << 16) / M;			// source pixels per output pixel, 16.16
		u32 nPos = 0;
		for (unsigned x = 0; x < nGameW; x++, nPos += nStep)
		{
			unsigned i = nPos >> 16;
			u32 p = pSrc[i];
			if ((M & 1) && i + 1 < nW)
			{
				u32 q = pSrc[i + 1], f = (nPos >> 8) & 0xFF;	// weight of q, 0-255
				u32 rb = (((p & 0xFF00FFu) * (256 - f) + (q & 0xFF00FFu) * f) >> 8) & 0xFF00FFu;
				u32 g = (((p & 0x00FF00u) * (256 - f) + (q & 0x00FF00u) * f) >> 8) & 0x00FF00u;
				p = rb | g;
			}
			*pDst++ = m_bRLow ? 0xFF000000u | (p & 0x0000FF00u) | ((p >> 16) & 0xFFu) | ((p & 0xFFu) << 16)
					  : 0xFF000000u | p;
		}
	}
	else if (m_bRLow)
	{
		for (unsigned x = 0; x < nW; x++)
		{
			u32 p = pSrc[x];
			u32 c = 0xFF000000u | (p & 0x0000FF00u) | ((p >> 16) & 0xFFu) | ((p & 0xFFu) << 16);
			for (unsigned s = 0; s < M; s++)
			{
				*pDst++ = c;
			}
		}
	}
	else
	{
		for (unsigned x = 0; x < nW; x++)
		{
			u32 c = 0xFF000000u | pSrc[x];
			for (unsigned s = 0; s < M; s++)
			{
				*pDst++ = c;
			}
		}
	}

	unsigned nDstY = m_nGameY + y * M;

	if (nDarkRows)
	{
		// shifts and masks only (75% = 50% + 25%)
		const u32 *pL = pLine;
		u32 *pD = pDark;
		switch (m_nFilter)
		{
		case FILTER_SCAN_LIGHT:
			for (unsigned x = 0; x < nGameW; x++)
			{
				u32 c = pL[x];
				pD[x] = 0xFF000000u | (((c >> 1) & 0x007F7F7Fu) + ((c >> 2) & 0x003F3F3Fu));
			}
			break;
		case FILTER_SCAN_STRONG:
			for (unsigned x = 0; x < nGameW; x++)
			{
				pD[x] = 0xFF000000u | ((pL[x] >> 2) & 0x003F3F3Fu);
			}
			break;
		default:
			for (unsigned x = 0; x < nGameW; x++)
			{
				pD[x] = 0xFF000000u | ((pL[x] >> 1) & 0x007F7F7Fu);
			}
			break;
		}
	}
	for (unsigned s = 0; s < M; s++)
	{
		const u32 *pSrcLine = s >= M - nDarkRows ? pDark : pLine;
		memcpy (Row (nBuffer, nDstY + s) + m_nGameX, pSrcLine, nGameW * 4);
	}
	FbFlush ();			// the line is in memory before the HVS reads it
}

// One centred line in the top or bottom black band (nSpans = 0 clears it)
void CVideoOut::DrawBandText (boolean bTop, const TTextSpan *pSpans, unsigned nSpans)
{
	const unsigned S = m_nStatusScale;
	const unsigned nY = bTop ? m_nTopY : m_nStatusY;
	const unsigned nH = 9 * S;					// glyph + shadow
	const unsigned nBuf = m_bRacing ? m_nBack ^ 1 : m_nBack;

	for (unsigned y = nY; y < nY + nH && y < m_nHeight; y++)
	{
		u32 *p = Row (nBuf, y);
		if (y >= m_nGameY && y < m_nHeight - m_nGameY)
		{
			// never inside the picture: clear only the side bands
			continue;
		}
		for (unsigned x = 0; x < m_nWidth; x++)
		{
			p[x] = 0xFF000000u;
		}
	}

	unsigned nChars = 0;
	for (unsigned i = 0; i < nSpans; i++)
	{
		nChars += strlen (pSpans[i].pText);
	}
	unsigned nW = nChars * 8 * S;
	if (nSpans == 0 || nW > m_nWidth)
	{
		return;
	}

	unsigned x = (m_nWidth - nW) / 2;
	for (unsigned i = 0; i < nSpans; i++)
	{
		u32 c0 = pSpans[i].nColor;			// 0xRRGGBB, like the UI colours
		u32 nColor = Native (0xFF000000u | ((c0 >> 16) & 0xFF) | (c0 & 0xFF00) | ((c0 & 0xFF) << 16));
		for (const char *s = pSpans[i].pText; *s; s++, x += 8 * S)
		{
			unsigned ch = (unsigned char) *s;
			if (ch < 32 || ch > 127)
			{
				continue;
			}
			if (ch >= 'a' && ch <= 'z')
			{
				ch -= 32;		// capitals only, like the menus
			}
			for (int pass = 0; pass < 2; pass++)		// shadow, then glyph
			{
				u32 c = pass == 0 ? 0xFF000000u : nColor;
				unsigned o = pass == 0 ? S : 0;
				for (unsigned r = 0; r < 8; r++)
				{
					unsigned char nRow = g_MenuFont[ch - 32][r];
					for (unsigned k = 0; k < 8; k++)
					{
						if (!(nRow & (0x80 >> k)))
						{
							continue;
						}
						for (unsigned sy = 0; sy < S; sy++)
						{
							unsigned yy = nY + r * S + sy + o;
							if (yy >= m_nHeight) continue;
							u32 *p = Row (nBuf, yy) + x + k * S + o;
							for (unsigned sx = 0; sx < S; sx++)
							{
								p[sx] = c;
							}
						}
					}
				}
			}
		}
	}
}

void CVideoOut::ShowBuffer (unsigned nBuffer)
{
	m_pFrameBuffer->SetVirtualOffset (0, nBuffer * m_nHeight);
	m_nBack = nBuffer ^ 1;
}

boolean CVideoOut::Flip (void)
{
	boolean bOK = m_pFrameBuffer->SetVirtualOffset (0, m_nBack * m_nHeight);
	m_nBack ^= 1;
	return bOK;
}

boolean CVideoOut::WaitForVSync (void)
{
	return m_pFrameBuffer->WaitForVerticalSync ();
}
