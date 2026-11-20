/*
 * nes-pi - the emulated frame (see frame.h)
 */
#include "frame.h"
#include "racing.h"
#include "tvcore.h"
#include "smp.h"
#include <circle/util.h>

TPicture g_Pic = {TRUE, NTSC_OFF, TRUE, NES_PALETTE_NESTOPIA, FALSE, {0}, 0};

// + 64: the TV filter copies PROJECT_TV_IN_BYTES from the picture's left
// edge, past the last line's end
u32 g_VideoBuf[GAME_SRC_PITCH * GAME_SRC_LINES + 64] __attribute__ ((aligned (64)));
u16 g_IdxBuf[NES_W * NES_H];

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

#define GAME_NTSC_W	COMP_OUT_WIDTH (GAME_W)		// 602
#define CANVAS_NTSC_W	COMP_OUT_WIDTH (CANVAS_W)	// 714
static_assert (CANVAS_NTSC_W == PROJECT_TV_OUT_W, "project.h: PROJECT_TV_OUT_W");
static_assert (CANVAS_W * 4 == PROJECT_TV_IN_BYTES, "project.h: PROJECT_TV_IN_BYTES");

static unsigned s_nTvBurst = 0;		// colour burst phase of the frame (0-2)
static boolean s_bTvCanvas = FALSE;	// the lines queued are the menus' canvas
static CScreen *s_pVideo = 0;
static u32 s_Line[CANVAS_W * 8], s_Dark[CANVAS_W * 8];	// core 0's scratch lines
static u32 s_TvLine[2][CANVAS_W * 8];			// the TV filter's two sides

static void NtscLine (const void *pSource, unsigned nPhase, u32 *pOut)
{
	composite_line (s_pTables, (const unsigned *) pSource, SharedLoad (s_bTvCanvas) ? CANVAS_W : GAME_W,
			nPhase, pOut);
}

// a filtered line (core 2, or the caller without it)
static void TvLineOut (unsigned nRow, const u32 *pFiltered, boolean bRace, unsigned nSide)
{
	s_pVideo->DrawNtscLine (nRow, pFiltered, SharedLoad (s_bTvCanvas) ? CANVAS_NTSC_W : GAME_NTSC_W,
				s_TvLine[nSide]);
	if (bRace)
	{
		RaceRowDone (nRow);
	}
}

static u32 *PictureRow (unsigned nLine)
{
	return &g_VideoBuf[(GAME_SRC_Y + nLine) * GAME_SRC_PITCH + GAME_SRC_X];
}

// one line of the picture from its palette indices; the overscan black
static void ColourLine (unsigned nLine, const u16 *pIdx)
{
	const u32 *pPal = nes_palette ();
	u32 *pDst = PictureRow (nLine);
	if (g_Pic.bOverscan && (nLine < OVERSCAN_LINES || nLine >= NES_H - OVERSCAN_LINES))
	{
		memset (pDst, 0, NES_W * 4);
		return;
	}
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
		pDst[x] = pPal[pIdx[x] & 511];
	}
}

// a finished line to the screen: through the TV filter, or straight
static void LineOut (unsigned nLine, boolean bRace)
{
	if (g_Pic.nTvMode != NTSC_OFF)
	{
		// the black sides go into the filter too: they blend in like on a TV
		TvPost (PictureRow (nLine), (s_nTvBurst + nLine) % COMP_PHASES, nLine, bRace);
	}
	else
	{
		s_pVideo->DrawGameLine (g_VideoBuf, nLine, s_Line, s_Dark);
		if (bRace)
		{
			RaceRowDone (nLine);
		}
	}
}

// each line, as soon as the PPU finishes it; then the next line waits for its
// turn against the HDMI output (beam racing)
static void NesLineHook (int nLine, const uint16_t *pPixels)
{
	if (nLine >= 0 && nLine < NES_H)
	{
		memcpy (&g_IdxBuf[nLine * NES_W], pPixels, NES_W * 2);
		ColourLine ((unsigned) nLine, pPixels);
		LineOut ((unsigned) nLine, g_bRaceFrame);
	}
	const int nNext = nLine + 1;
	if (g_bRaceFrame && nNext >= 0 && nNext < NES_H)
	{
		RaceRowNext ((unsigned) nNext);
	}
}

void FrameSetup (CScreen *pVideo)
{
	static_assert (GAME_W == NES_W && GAME_H == NES_H, "project.h: the NES picture");
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

void FrameRecolour (void)
{
	for (unsigned y = 0; y < NES_H; y++)
	{
		ColourLine (y, &g_IdxBuf[y * NES_W]);
	}
}

void FrameDraw (void)
{
	for (unsigned y = 0; y < NES_H; y++)
	{
		LineOut (y, FALSE);
	}
	if (g_Pic.nTvMode != NTSC_OFF)
	{
		TvSync ();
	}
}

// the menus: the canvas through the same filter as the game, with a fixed
// colour burst phase for each line, so the dots do not crawl on still text
void FrameDrawPicture (u32 *pBuffer, boolean bFilter)
{
	if (g_Pic.nTvMode == NTSC_OFF || !bFilter || !s_pTables)
	{
		s_pVideo->DrawGame (pBuffer, bFilter, TRUE);
		return;
	}
	TvSync ();
	SharedStore (s_bTvCanvas, TRUE);
	for (unsigned y = 0; y < CANVAS_H; y++)
	{
		TvPost (&pBuffer[(GAME_SRC_Y - CANVAS_PAD_Y + y) * GAME_SRC_PITCH + GAME_SRC_X - CANVAS_PAD_X],
			y % COMP_PHASES, y, FALSE);
	}
	TvSync ();
	SharedStore (s_bTvCanvas, FALSE);
}

// a frame of lines through the TV filter, not drawn (start-up check, in LOG.TXT)
unsigned FrameTvTimeUs (unsigned nLines)
{
	static u32 Out[PROJECT_TV_OUT_W];
	static u32 Line[CANVAS_W];
	for (unsigned x = 0; x < CANVAS_W; x++)
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

unsigned FrameMcFailFrame (void)
{
	return 0;
}
