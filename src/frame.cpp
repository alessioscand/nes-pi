/*
 * nes-pi - the emulated frame (see frame.h)
 */
#include "frame.h"
#include "racing.h"
#include "ui.h"
#include "tvcore.h"
#include "smp.h"
#include <circle/util.h>

TPicture g_Pic = {TRUE, NTSC_OFF, TRUE, NES_PALETTE_NESTOPIA, FALSE, {0}, 0};

u32 g_VideoBuf[NES_W * NES_H] __attribute__ ((aligned (64)));
u16 g_IdxBuf[NES_W * NES_H];
u32 g_NtscBuf[NTSC_OUT_W * NES_H] __attribute__ ((aligned (64)));

#define UI_NTSC_W	COMP_OUT_WIDTH (UI_W)
static u32 s_UINtsc[UI_NTSC_W * UI_H] __attribute__ ((aligned (64)));

TVideoSource g_NesSource = {NES_W, NES_H - 2 * OVERSCAN_LINES, NES_W, 0, OVERSCAN_LINES, 0};
// the TV filter's 602-pixel lines are shown half as wide per pixel (bHalfX),
// which gives a 4:3 picture at the same height
TVideoSource g_NtscSource = {NTSC_OUT_W, NES_H - 2 * OVERSCAN_LINES, NTSC_OUT_W, 0, OVERSCAN_LINES, 1};
const TVideoSource g_UISource = {UI_W, UI_H, UI_SRC_PITCH, UI_SRC_X, UI_SRC_Y, 0};
static const TVideoSource s_UINtscSource = {UI_NTSC_W, UI_H, UI_NTSC_W, 0, 0, 1};

const TVideoSource &FrameSource (void)	{ return g_Pic.nTvMode != NTSC_OFF ? g_NtscSource : g_NesSource; }
const u32 *FrameBuffer (void)		{ return g_Pic.nTvMode != NTSC_OFF ? g_NtscBuf : g_VideoBuf; }

int16_t g_AudioBuf[2 * (AUDIO_FRAME + 64)] __attribute__ ((aligned (64)));
static int16_t s_AudioMono[AUDIO_FRAME + 64];

volatile unsigned g_Pad[2] = {0, 0};
volatile boolean g_bRaceFrame = FALSE;

// the TV filter's looks (blargg's controls): S-VIDEO as he made it,
// COMPOSITE with every artifact at its strongest, RF the aerial cable on
// grandma's TV; all a little less saturated
static const TCompLook s_Looks[4] =
{
	//  artifacts fringing  bleed  sharpness resolution saturation
	{0, 0, 0, 0, 0, 0},				// OFF: not used
	{-1.0f, -1.0f, 0.0f,  0.2f,  0.2f, -0.20f},	// S-VIDEO
	{ 1.0f,  1.0f, 0.0f,  0.0f,  0.0f, -0.20f},	// COMPOSITE
	{ 1.0f,  1.0f, 1.0f, -0.5f, -0.5f, -0.35f},	// RF
};
static void *s_pTables = 0;		// the filter's tables (composite_init)
static int s_nTablesMode = NTSC_OFF;	// the look they were built for

static unsigned s_nTvBurst = 0;		// colour burst phase of the frame (0-2)
static CScreen *s_pVideo = 0;
static u32 s_Line[PIC_MAX_W * 8], s_Dark[PIC_MAX_W * 8];	// core 0's scratch lines
static u32 s_TvLine[2][PIC_MAX_W * 8], s_TvDark[2][PIC_MAX_W * 8];	// the TV filter's two sides

static void NtscLine (const void *pSource, unsigned nPhase, u32 *pOut)
{
	composite_line (s_pTables, (const unsigned *) pSource, NES_W, nPhase, pOut);
}

// a filtered line (core 1, or the caller without it)
static void TvLineOut (unsigned nRow, const u32 *pFiltered, boolean bRace, unsigned nSide)
{
	s_pVideo->DrawGameLine (g_NtscBuf, nRow, s_TvLine[nSide], s_TvDark[nSide]);
	if (bRace)
	{
		RaceRowDone (nRow);
	}
}

// each line, as soon as the PPU finishes it; then the next line waits for its
// turn against the HDMI output (beam racing)
static void NesLineHook (int nLine, const uint16_t *pPixels)
{
	const int nTop = (int) g_NesSource.nY, nRows = (int) g_NesSource.nH;	// lines shown
	if (nLine >= 0 && nLine < NES_H)
	{
		const u32 *pPal = nes_palette ();
		u32 *pDst = &g_VideoBuf[nLine * NES_W];		// black sides with overscan
		memcpy (&g_IdxBuf[nLine * NES_W], pPixels, NES_W * 2);
		unsigned x0 = 0, x1 = NES_W;
		if (g_Pic.bOverscan)
		{
			x0 = OVERSCAN_SIDE;
			x1 = NES_W - OVERSCAN_SIDE;
			for (unsigned x = 0; x < OVERSCAN_SIDE; x++)
			{
				pDst[x] = pDst[NES_W - 1 - x] = 0;
			}
		}
		for (unsigned x = x0; x < x1; x++)
		{
			pDst[x] = pPal[pPixels[x] & 511];
		}
		int nRow = nLine - nTop;
		if (nRow >= 0 && nRow < nRows)
		{
			if (g_Pic.nTvMode != NTSC_OFF)
			{
				// the black sides go into the filter too: they blend in like on a TV
				TvPost (pDst, (s_nTvBurst + (unsigned) nLine) % COMP_PHASES, (unsigned) nRow,
					g_bRaceFrame, &g_NtscBuf[nLine * NTSC_OUT_W]);
			}
			else
			{
				s_pVideo->DrawGameLine (g_VideoBuf, (unsigned) nRow, s_Line, s_Dark);
				if (g_bRaceFrame)
				{
					RaceRowDone ((unsigned) nRow);
				}
			}
		}
	}
	int nNext = nLine + 1 - nTop;
	if (g_bRaceFrame && nNext >= 0 && nNext < nRows)
	{
		RaceRowNext ((unsigned) nNext);
	}
}

void FrameSetup (CScreen *pVideo)
{
	s_pVideo = pVideo;
	s_pTables = new u8[composite_size ()];
	nes_set_line_hook (NesLineHook);
	TvSetOutput (TvLineOut);
}

void FrameSetTvMode (int nMode)
{
	TvSync ();
	if (nMode != NTSC_OFF && nMode != s_nTablesMode && s_pTables)
	{
		composite_init (s_pTables, &s_Looks[nMode & 3]);
		s_nTablesMode = nMode;
		TvSetFilter (NtscLine);
	}
}

const TVideoSource &FrameUISource (void)
{
	return g_Pic.nTvMode != NTSC_OFF && s_pTables ? s_UINtscSource : g_UISource;
}

// a fixed colour burst phase for each line: the dots do not crawl on still text
const u32 *FrameUIBuffer (const u32 *pUI)
{
	if (g_Pic.nTvMode == NTSC_OFF || !s_pTables)
	{
		return pUI;
	}
	for (unsigned y = 0; y < UI_H; y++)
	{
		composite_line (s_pTables, pUI + (UI_SRC_Y + y) * UI_SRC_PITCH + UI_SRC_X, UI_W,
				y % COMP_PHASES, &s_UINtsc[y * UI_NTSC_W]);
	}
	return s_UINtsc;
}

// a frame of lines through the TV filter, not drawn (start-up check, in LOG.TXT)
unsigned FrameTvTimeUs (unsigned nLines)
{
	static u32 Out[NTSC_OUT_W];
	static u32 Line[NES_W];
	for (unsigned x = 0; x < NES_W; x++)
	{
		Line[x] = x * 0x0B3A57u & 0xFFFFFF;	// many colours, like a busy scene
	}
	TvSetOutput (0);
	u64 t0 = Ticks ();
	for (unsigned i = 0; i < nLines; i++)
	{
		TvPost (Line, i % COMP_PHASES, i, FALSE, Out);
	}
	TvSync ();
	u64 t1 = Ticks ();
	TvSetOutput (TvLineOut);
	return (unsigned) ((t1 - t0) * 1000000 / TicksFreq ());
}

// vertical blank (the game reads the pads) then the 240 lines, each shown as
// it is finished; audio to stereo
void FrameRun (void)
{
	nes_frame (g_Pad[0], g_Pad[1], s_AudioMono, AUDIO_FRAME);
	if (g_Pic.nTvMode != NTSC_OFF)
	{
		TvSync ();			// every line filtered and on the screen
		// the phase steps through all three values: the colour artifacts crawl
		// along the edges even on a still picture (the classic dot crawl;
		// stronger than a real NES, which alternates only two)
		s_nTvBurst = (s_nTvBurst + 1) % COMP_PHASES;
	}
	for (unsigned i = 0; i < AUDIO_FRAME; i++)
	{
		g_AudioBuf[2*i] = g_AudioBuf[2*i+1] = s_AudioMono[i];
	}
}
