/*
 * nes-pi - kernel: game list, play with USB pads and HDMI
 * audio, in-game menu with save states, automatic diagnostics in LOG.TXT.
 */
#include "kernel.h"
#include "fontdata.h"
#include "neopi.h"
#include "profile.h"
#include "raster.h"
#include "race.h"
#include "nes_core.h"
#include "tvfilter.h"
#include "fbmap.h"
#include <circle/machineinfo.h>
#include <circle/memory.h>
#include <circle/new.h>
#include <circle/util.h>
#include <circle/alloc.h>
#include <circle/bcmwatchdog.h>
#include <stdarg.h>

extern "C" {
int snprintf (char *pBuffer, size_t nSize, const char *pFormat, ...);
int vsnprintf (char *pBuffer, size_t nSize, const char *pFormat, va_list Args);
void *neopi_zip_extract (const void *pZip, size_t nZipSize, const char *pName, size_t *pSize);
void *neopi_png_encode (const void *pRGB, int nWidth, int nHeight, size_t *pSize);
void neopi_png_free (void *p);
}

#define NESPI_DIR	"SD:/nespi"
#define LOG_PATH	NESPI_DIR "/LOG.TXT"
#define BENCH_PATH	NESPI_DIR "/BENCH.TXT"
#define BENCH_FLAG	NESPI_DIR "/BENCH"
#define PROFILE_PATH	NESPI_DIR "/PROFILE.TXT"
#define LAST_PATH	NESPI_DIR "/LAST.TXT"
#define CFG_PATH	NESPI_DIR "/nespi.cfg"
#define PADS_PATH	NESPI_DIR "/pads.cfg"
#define SAVE_DIR	NESPI_DIR "/saves"
#define SHOT_DIR	NESPI_DIR "/shots"

#define SAMPLE_RATE	48000
#define AUDIO_FRAME	(SAMPLE_RATE / 60)	// samples per frame (Nestopia paced at 60 Hz)
#ifndef BENCH_WARMUP
#define BENCH_WARMUP	1800		// 30 s of play before measuring
#endif
#ifndef BENCH_FRAMES
#define BENCH_FRAMES	3600		// then 60 s measured
#endif
#define BENCH_PROFILE_US 200		// profiler: one sample every 200 us
#define BENCH_SLOW_US	16000		// frames from 16 ms on count as slow
#define REPORT_FRAMES	3600		// one statistics line per minute of play

#define COLOR_TEXT	RGB (220, 220, 220)
#define COLOR_TITLE	RGB (255, 200, 0)
#define COLOR_OK	RGB (80, 230, 80)
#define COLOR_ERR	RGB (255, 70, 70)
#define COLOR_DIM	RGB (140, 140, 140)

CKernel *CKernel::s_pThis = 0;

enum { GmSave, GmLoad, GmReset, GmList, GmPower, GmCount };

// ---- emulator buffers and callbacks ---------------------------------------

// the NES picture as 0x00RRGGBB (filled line by line by the line hook), and
// where it sits for the video output; the menus have their own layout
static u32 s_VideoBuf[NES_W * NES_H] __attribute__ ((aligned (64)));
// Overscan: a TV of the time hid about 8 lines at the top and at the bottom,
// and a little of each side, and games leave rubbish there (tiles being
// updated while scrolling, wrong colours at the edges). By default the picture
// is the 224 lines from 8 to 231, with 8 black pixels on each side;
// overscan=off in nespi.cfg shows all 256 x 240.
#define OVERSCAN_LINES	8
#define OVERSCAN_SIDE	8
static boolean s_bOverscan = TRUE;
static TVideoSource s_NesSource = {NES_W, NES_H - 2 * OVERSCAN_LINES, NES_W, 0, OVERSCAN_LINES};

// palette (OPTIONS > PALETTE, palette= in nespi.cfg) and sprite limit
// (OPTIONS > SPRITE FLICKER, sprites=unlimited); nespi/palette.pal = the custom one
#define PALETTE_PATH	NESPI_DIR "/palette.pal"
static int s_nPalette = NES_PALETTE_NESTOPIA;
static boolean s_bUnlimSprites = FALSE;
static u8 s_CustomPal[512 * 3];
static size_t s_nCustomPal = 0;		// 0 = no nespi/palette.pal
static u16 s_IdxBuf[NES_W * NES_H];	// the last picture as palette indices (recolouring)

// TV filter (OPTIONS > TV FILTER, tvfilter= in nespi.cfg): blargg's NTSC
// filter on core 1 (tvfilter.h); its 602-pixel lines are shown half as wide
// per pixel (bHalfX), which gives a 4:3 picture at the same height
static int s_nTvMode = NTSC_OFF;
static u32 s_NtscBuf[NTSC_OUT_W * NES_H] __attribute__ ((aligned (64)));
static TVideoSource s_NtscSource = {NTSC_OUT_W, NES_H - 2 * OVERSCAN_LINES, NTSC_OUT_W, 0, OVERSCAN_LINES, 1};
static unsigned s_nTvBurst = 0;		// colour burst phase of the frame (0-2)
static boolean s_bTvScanlines = TRUE;	// with no TV filter: RGB (light scanlines) or OFF
static const TVideoSource &GameSource (void)	{ return s_nTvMode != NTSC_OFF ? s_NtscSource : s_NesSource; }
static const u32 *GameBuffer (void)		{ return s_nTvMode != NTSC_OFF ? s_NtscBuf : s_VideoBuf; }
static const TVideoSource s_UISource = {UI_W, UI_H, UI_SRC_PITCH, UI_SRC_X, UI_SRC_Y};

static int16_t s_AudioMono[AUDIO_FRAME + 64];
static int16_t s_AudioBuf[2 * (AUDIO_FRAME + 64)] __attribute__ ((aligned (64)));

// pads for the next frame (NES_* bits), written by the game loop
static volatile unsigned s_Pad[2] = {0, 0};

// beam racing (declared early: the line hook uses them)
static void RaceRowDone (unsigned nRow);
static volatile boolean s_bRaceFrame = FALSE;	// this frame is raced
static void RaceRowNext (unsigned nRow);

// Each line, as soon as the PPU finishes it: colours from the palette into
// s_VideoBuf, then to the framebuffer (the one shown while racing) - or, with
// the TV filter, to cores 1-3, which filter it and draw it (TvLineOut); then
// the next line waits for its turn against the HDMI output (beam racing)
static CVideoOut *s_pVideoOut = 0;
static u32 s_Line[PIC_MAX_W * 8], s_Dark[PIC_MAX_W * 8];
static u32 s_TvLine[3][PIC_MAX_W * 8], s_TvDark[3][PIC_MAX_W * 8];	// cores 1-3
static void TvLineOut (unsigned nRow, unsigned nWorker)
{
	s_pVideoOut->DrawGameLine (s_NtscBuf, nRow, s_TvLine[nWorker], s_TvDark[nWorker]);
	if (s_bRaceFrame)
	{
		RaceRowDone (nRow);
	}
}
static void NesLineHook (int nLine, const uint16_t *pPixels)
{
	const int nTop = (int) s_NesSource.nY, nRows = (int) s_NesSource.nH;	// lines shown
	if (nLine >= 0 && nLine < NES_H)
	{
		const u32 *pPal = nes_palette ();
		u32 *pDst = &s_VideoBuf[nLine * NES_W];
		memcpy (&s_IdxBuf[nLine * NES_W], pPixels, NES_W * 2);
		unsigned x0 = 0, x1 = NES_W;
		if (s_bOverscan)
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
			if (s_nTvMode != NTSC_OFF)
			{
				// overscan: black sides go into the filter, so they blend in like on a TV
				static u16 Idx[NES_W];
				memcpy (Idx, pPixels, sizeof Idx);
				if (s_bOverscan)
				{
					for (unsigned x = 0; x < OVERSCAN_SIDE; x++)
					{
						Idx[x] = Idx[NES_W - 1 - x] = 0x0F;
					}
				}
				TvPost (Idx, (s_nTvBurst + (unsigned) nLine) % 3,
					&s_NtscBuf[nLine * NTSC_OUT_W], (unsigned) nRow);
			}
			else
			{
				s_pVideoOut->DrawGameLine (s_VideoBuf, (unsigned) nRow, s_Line, s_Dark);
				if (s_bRaceFrame)
				{
					RaceRowDone ((unsigned) nRow);
				}
			}
		}
	}
	int nNext = nLine + 1 - nTop;
	if (s_bRaceFrame && nNext >= 0 && nNext < nRows)
	{
		RaceRowNext ((unsigned) nNext);
	}
}

// battery-backed RAM: read when a game loads, written when it is unloaded
static char s_BatteryPath[160];
static size_t BatteryLoad (void *pBuffer, size_t nMax)
{
	FIL File;
	UINT nRead = 0;
	if (s_BatteryPath[0] && f_open (&File, s_BatteryPath, FA_READ) == FR_OK)
	{
		f_read (&File, pBuffer, nMax, &nRead);
		f_close (&File);
	}
	return nRead;
}
static void BatterySave (const void *pData, size_t nSize)
{
	FIL File;
	if (s_BatteryPath[0] && f_open (&File, s_BatteryPath, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK)
	{
		UINT nWritten;
		f_write (&File, pData, nSize, &nWritten);
		f_close (&File);
	}
}

// one frame: vertical blank (the game reads the pads) then the 240 lines,
// each shown as it is finished; audio to stereo
static void NesRunFrame (void)
{
	nes_frame (s_Pad[0], s_Pad[1], s_AudioMono, AUDIO_FRAME);
	if (s_nTvMode != NTSC_OFF)
	{
		TvSync ();			// every line filtered and on the screen
		// the phase steps through all three values: the colour artifacts crawl
		// along the edges even on a still picture (the classic dot crawl;
		// stronger than a real NES, which alternates only two)
		s_nTvBurst = (s_nTvBurst + 1) % 3;
	}
	for (unsigned i = 0; i < AUDIO_FRAME; i++)
	{
		s_AudioBuf[2*i] = s_AudioBuf[2*i+1] = s_AudioMono[i];
	}
}

// ---- input lag: frame delay ---------------------------------------------------
//
// Nestopia's frames already start with the vertical blank (the game reads the
// pads there) and then draw every line: the frame shown at the next vsync
// answers the pads read just before it.
//
// Frame delay: the frame starts a few ms after the vsync instead of right
// after it, so the pads are read later and the finished frame waits less
// before the next vsync. framedelay=auto (default) measures how long each
// frame takes and only waits what this game has proved it can afford:
// - the delay starts at 0 and grows slowly (about 17 s time constant) while
//   the frames stay light, never above DELAY_MAX_US;
// - it keeps at least half of the slowest recent frame, and DELAY_GUARD_US,
//   free before the vsync;
// - one heavier frame lowers it at once; a missed vsync with a delay active
//   sets it back to 0 and it starts growing again.
// framedelay=off = no delay; framedelay=N (1-12) = fixed N ms, for testing.
#define FRAME_US		16667
#define DELAY_GUARD_US		3000
#define DELAY_MAX_US		6000
static int s_nDelayCfg = -1;		// -1 auto, 0 off, N ms fixed
static unsigned s_nDelayUs;		// delay before the next frame
static unsigned s_nDelayPeak = FRAME_US;	// slowest recent frame (decays)
static unsigned s_nDelayMisses;		// vsyncs missed with a delay active (auto)
static u64 s_nDelaySum;
static unsigned s_nDelayMin, s_nDelayMax, s_nDelayCount;

static void DelayRestart (void)
{
	s_nDelayPeak = FRAME_US;
	s_nDelayUs = s_nDelayCfg > 0 ? (unsigned) s_nDelayCfg * 1000 : 0;
}

static void DelayStatsReset (void)
{
	s_nDelaySum = 0;
	s_nDelayMin = ~0u;
	s_nDelayMax = s_nDelayCount = s_nDelayMisses = 0;
}

// after each game frame: nWork = time the frame needed (pads to flip done)
static void DelayUpdate (unsigned nWork, boolean bMissed)
{
	s_nDelaySum += s_nDelayUs;
	s_nDelayCount++;
	if (s_nDelayUs < s_nDelayMin) s_nDelayMin = s_nDelayUs;
	if (s_nDelayUs > s_nDelayMax) s_nDelayMax = s_nDelayUs;

	if (s_nDelayCfg >= 0)
	{
		return;				// off or fixed
	}
	if (bMissed && s_nDelayUs > 0)
	{
		s_nDelayMisses++;
		DelayRestart ();
		return;
	}
	s_nDelayPeak -= s_nDelayPeak >> 10;
	if (nWork > s_nDelayPeak)
	{
		s_nDelayPeak = nWork;
	}
	unsigned nFree = s_nDelayPeak / 2 > DELAY_GUARD_US ? s_nDelayPeak / 2 : DELAY_GUARD_US;
	unsigned nNeed = s_nDelayPeak + nFree;
	unsigned nDelay = nNeed < FRAME_US ? FRAME_US - nNeed : 0;
	s_nDelayUs = nDelay < DELAY_MAX_US ? nDelay : DELAY_MAX_US;
}

// ---- beam racing (Pi 4, multicore; see race.h and raster.h) -----------------
static inline u64 ReadTicks (void)
{
	u64 v;
	__asm__ volatile ("isb; mrs %0, cntpct_el0" : "=r" (v));
	return v;
}
static u64 TicksFreq (void)
{
	u64 v;
	__asm__ volatile ("mrs %0, cntfrq_el0" : "=r" (v));
	return v;
}
// HVS registers at bus address 0x7E400000
#if RASPPI == 4
#define HVS_BASE	0xFE400000UL
#define HVS_GEN		5
#else
#define HVS_BASE	0x3F400000UL
#define HVS_GEN		4
#endif
static u32 HvsRead (unsigned nOffset)
{
	return *(volatile u32 *) (uintptr) (HVS_BASE + nOffset);
}
// beamracing= in neopi.cfg: -1 default (on), 0 off, 1 on
static int s_nRaceCfg = -1;
static boolean s_bRaceReady = FALSE;	// calibrated at start-up
static boolean s_bRaceGame = FALSE;	// racing in this game (until it gives up)
static CRaster *s_pRaster = 0;
static CBeamRacer *s_pRacer = 0;

// before each line of a raced frame (line hook): wait for its turn. The time
// spent waiting is added up: it is pacing, not work (LOAD and the timings in
// LOG.TXT leave it out)
static u64 s_nRaceWaitTicks;
static void RaceRowNext (unsigned nRow)
{
	u64 nNow = ReadTicks ();
	s_pRacer->RowArrived (nRow, nNow);
	u64 nRelease = s_pRacer->RowRelease (nRow);
	if ((s64) (nNow - nRelease) < 0)
	{
		u64 nEnd;
		do
		{
			s_pRaster->Sample ();
		}
		while ((s64) ((nEnd = ReadTicks ()) - nRelease) < 0);
		s_nRaceWaitTicks += nEnd - nNow;
	}
	else if ((nRow & 15) == 0)
	{
		s_pRaster->Sample ();
	}
}

// the row is in the framebuffer being shown
static void RaceRowDone (unsigned nRow)
{
	__asm__ volatile ("dsb st" ::: "memory");	// the pixels have left the CPU
	s_pRacer->RowDone (nRow, ReadTicks ());
}

// ---- C interface for the C parts -----------------------------------------

extern "C" void neopi_log (const char *pFormat, ...)
{
	char Buffer[160];
	va_list Args;
	va_start (Args, pFormat);
	vsnprintf (Buffer, sizeof Buffer, pFormat, Args);
	va_end (Args);
	CKernel::Get ()->Log (Buffer, COLOR_TEXT);
}

extern "C" void neopi_fatal (int nCode, const char *pFormat, ...)
{
	char Buffer[160];
	va_list Args;
	va_start (Args, pFormat);
	vsnprintf (Buffer, sizeof Buffer, pFormat, Args);
	va_end (Args);
	CKernel::Get ()->Fatal (nCode, Buffer);
}

// ---- helpers --------------------------------------------------------------

static void FormatMs (char *pOut, size_t nSize, unsigned nMicros)
{
	snprintf (pOut, nSize, "%u.%02u", nMicros / 1000, (nMicros % 1000) / 10);
}

struct TStats
{
	unsigned nAvg, nP99, nMax, nMin;
};

static void ComputeStats (const unsigned *pValues, unsigned nCount, TStats *pStats)
{
	static unsigned Sorted[BENCH_FRAMES];
	u64 nSum = 0;
	for (unsigned i = 0; i < nCount; i++)
	{
		Sorted[i] = pValues[i];
		nSum += pValues[i];
	}
	for (unsigned i = 1; i < nCount; i++)
	{
		unsigned v = Sorted[i];
		int j = (int) i - 1;
		while (j >= 0 && Sorted[j] > v)
		{
			Sorted[j+1] = Sorted[j];
			j--;
		}
		Sorted[j+1] = v;
	}
	pStats->nAvg = (unsigned) (nSum / nCount);
	pStats->nMin = Sorted[0];
	pStats->nMax = Sorted[nCount-1];
	pStats->nP99 = Sorted[(nCount * 99) / 100];
}

// Histogram in 0.1 ms bins (0..50 ms) for per-minute reports
struct THisto
{
	enum { Bins = 500 };
	unsigned Bin[Bins];
	unsigned nCount, nMax;
	u64 nSum;

	unsigned nMin;
	void Reset (void) { memset (Bin, 0, sizeof Bin); nCount = nMax = 0; nMin = ~0u; nSum = 0; }
	void Add (unsigned us)
	{
		if (us < nMin) nMin = us;
		unsigned b = us / 100;
		Bin[b < Bins ? b : Bins - 1]++;
		nCount++;
		nSum += us;
		if (us > nMax) nMax = us;
	}
	unsigned Avg (void) const { return nCount ? (unsigned) (nSum / nCount) : 0; }
	unsigned P99 (void) const
	{
		unsigned nTarget = nCount - nCount / 100, n = 0;
		for (unsigned b = 0; b < Bins; b++)
		{
			n += Bin[b];
			if (n >= nTarget) return b * 100 + 100;
		}
		return nMax;
	}
};

static THisto s_HistEmu, s_HistWork, s_HistPeriod;
static THisto s_HistLag;		// pad report -> start of scan-out of the frame that uses it
static THisto s_HistSlack;		// frame ready (flip) -> vsync
static unsigned s_nOverBudget, s_nMissedVSync, s_nWindowFrames;
static unsigned s_nWindowStart;

static boolean HasNesExtension (const char *pName)
{
	size_t n = strlen (pName);
	if (n < 4)
	{
		return FALSE;
	}
	const char *e = pName + n - 4;
	return    e[0] == '.'
	       && (e[1] == 'n' || e[1] == 'N')
	       && (e[2] == 'e' || e[2] == 'E')
	       && (e[3] == 's' || e[3] == 'S');
}

int CLogSink::Write (const void *pBuffer, size_t nCount)
{
	static char Line[160];
	static unsigned nPos = 0;
	const char *p = (const char *) pBuffer;
	for (size_t i = 0; i < nCount; i++)
	{
		char c = p[i];
		if (c == '\x1b')
		{
			while (i + 1 < nCount && !((p[i+1] >= 'a' && p[i+1] <= 'z') || (p[i+1] >= 'A' && p[i+1] <= 'Z')))
			{
				i++;
			}
			i++;
			continue;
		}
		if (c == '\n' || nPos >= sizeof Line - 1)
		{
			Line[nPos] = '\0';
			if (nPos > 0 && CKernel::Get ())
			{
				CKernel::Get ()->Log (Line, COLOR_DIM);
			}
			nPos = 0;
			continue;
		}
		if (c != '\r')
		{
			Line[nPos++] = c;
		}
	}
	return (int) nCount;
}

// ---- kernel ---------------------------------------------------------------

CKernel::CKernel (void)
:	m_Timer (&m_Interrupt),
	m_Logger (m_Options.GetLogLevel (), &m_Timer),
	m_CPUThrottle (CPUSpeedMaximum),
	m_USBHCI (&m_Interrupt, &m_Timer, TRUE),
	m_EMMC (&m_Interrupt, &m_Timer, &m_ActLED),
	m_pInput (0),
	m_pUI (0),
	m_bVideoOK (FALSE),
	m_bSDOK (FALSE),
	m_bQuiet (TRUE),		// black screen while booting; text only on errors
	m_nRepeat (0),
	m_nLines (0),
	m_pGame (0),
	m_nGameSize (0),
	m_pGames (0),
	m_nGames (0),
	m_nLoaded (-1),
	m_bVSync (TRUE),
	m_bStatusOn (FALSE),
	m_nClearStatus (0),
	m_nShots (0),
	m_nFilter (0),
	m_bHVS (TRUE),
	m_bFilterAllowed (TRUE),
	m_bForceFilters (FALSE)
{
	s_pThis = this;
	m_LastRaw[0] = '\0';
}

CKernel::~CKernel (void)
{
	s_pThis = 0;
}

boolean CKernel::Initialize (void)
{
	m_bVideoOK = m_Video.Initialize ();

	m_Serial.Initialize (115200);

	if (!m_Logger.Initialize (&m_LogSink))
	{
		return FALSE;
	}
	if (!m_Interrupt.Initialize ())
	{
		return FALSE;
	}
	if (!m_Timer.Initialize ())
	{
		return FALSE;
	}

	return TRUE;
}

// ---- console / log --------------------------------------------------------

void CKernel::Log (const char *pLine, u32 nColor)
{
	if (m_nLines > 0 && strcmp (pLine, m_LastRaw) == 0)
	{
		m_nRepeat++;
		unsigned n = (m_nLines - 1) % MaxLines;
		snprintf (m_Lines[n], MaxLineLen, "%s (x%u)", m_LastRaw, m_nRepeat);
		if (!m_bQuiet && m_bVideoOK && !m_Video.IsGameMode () && m_nLines <= m_Video.GetRows ())
		{
			m_Video.DrawText (0, 0, m_nLines - 1, m_Lines[n], nColor, RGB (0, 0, 0));
		}
		return;
	}
	strncpy (m_LastRaw, pLine, MaxLineLen - 1);
	m_LastRaw[MaxLineLen - 1] = '\0';
	m_nRepeat = 1;

	unsigned n = m_nLines % MaxLines;
	strncpy (m_Lines[n], pLine, MaxLineLen - 1);
	m_Lines[n][MaxLineLen - 1] = '\0';
	m_LineColor[n] = nColor;
	m_nLines++;

	if (m_bQuiet)
	{
		return;
	}

	m_Serial.Write (pLine, strlen (pLine));
	m_Serial.Write ("\r\n", 2);

	if (!m_bVideoOK || m_Video.IsGameMode ())	// never write text over the game screen
	{
		return;
	}

	unsigned nRows = m_Video.GetRows ();
	if (m_nLines <= nRows)
	{
		m_Video.DrawText (0, 0, m_nLines - 1, m_Lines[n], nColor, RGB (0, 0, 0));
	}
	else
	{
		RedrawConsole ();
	}
}

void CKernel::RedrawConsole (void)
{
	unsigned nRows = m_Video.GetRows ();
	unsigned nFirst = m_nLines > nRows ? m_nLines - nRows : 0;
	m_Video.ClearRows (0, 0, nRows, RGB (0, 0, 0));
	for (unsigned i = nFirst; i < m_nLines; i++)
	{
		unsigned n = i % MaxLines;
		m_Video.DrawText (0, 0, i - nFirst, m_Lines[n], m_LineColor[n], RGB (0, 0, 0));
	}
}

void CKernel::WriteTextFile (const char *pPath, const char *pText)
{
	if (!m_bSDOK)
	{
		return;
	}
	FIL File;
	if (f_open (&File, pPath, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK)
	{
		return;
	}
	UINT nWritten;
	f_write (&File, pText, strlen (pText), &nWritten);
	f_close (&File);
}

void CKernel::SaveLog (void)
{
	static char Text[MaxLines * (MaxLineLen + 2)];
	unsigned nFirst = m_nLines > MaxLines ? m_nLines - MaxLines : 0;
	size_t nPos = 0;
	for (unsigned i = nFirst; i < m_nLines; i++)
	{
		const char *pLine = m_Lines[i % MaxLines];
		size_t nLen = strlen (pLine);
		memcpy (Text + nPos, pLine, nLen);
		nPos += nLen;
		Text[nPos++] = '\r';
		Text[nPos++] = '\n';
	}
	Text[nPos] = '\0';
	WriteTextFile (LOG_PATH, Text);
}

void CKernel::Fatal (int nCode, const char *pMessage)
{
	if (m_bQuiet || m_Video.IsGameMode ())
	{
		m_bQuiet = FALSE;
		if (m_bVideoOK && m_Video.SetConsoleMode ())
		{
			RedrawConsole ();
		}
	}

	char Line[160];
	snprintf (Line, sizeof Line, "ERROR: %s", pMessage);
	Log (Line, COLOR_ERR);

	switch (nCode)
	{
	case NEOPI_ERR_SD:
		Log ("  Check that the card is FAT32 and that the files are in its root.", COLOR_TEXT);
		break;
	case NEOPI_ERR_GAME:
		Log ("  Copy at least one .nes game into the nespi folder.", COLOR_TEXT);
		break;
	case NEOPI_ERR_MEM:
		Log ("  Not enough memory.", COLOR_TEXT);
		break;
	default:
		break;
	}
	snprintf (Line, sizeof Line, "The green LED blinks %d times: error code %d.", nCode, nCode);
	Log (Line, COLOR_TEXT);

	SaveLog ();

	while (1)
	{
		m_ActLED.Blink (nCode, 250, 350);
		CTimer::SimpleMsDelay (1500);
	}
}

// ---- files ----------------------------------------------------------------

u8 *CKernel::LoadFile (const char *pPath, size_t *pSize, int nHeap, const char *pShowName)
{
	FIL File;
	if (f_open (&File, pPath, FA_READ) != FR_OK)
	{
		return 0;
	}

	size_t nSize = f_size (&File);
	u8 *pBuffer = new (nHeap) u8[nSize + 16];
	if (pBuffer == 0)
	{
		f_close (&File);
		char Msg[160];
		snprintf (Msg, sizeof Msg, "not enough memory for %s (%u MB)",
			  pPath, (unsigned) (nSize >> 20));
		Log (Msg, COLOR_ERR);
		return 0;
	}

	unsigned nStart = CTimer::GetClockTicks ();
	size_t nDone = 0;
	const size_t nChunk = 1 << 20;
	while (nDone < nSize)
	{
		size_t nWant = nSize - nDone < nChunk ? nSize - nDone : nChunk;
		UINT nRead = 0;
		if (   f_read (&File, pBuffer + nDone, nWant, &nRead) != FR_OK
		    || nRead == 0)
		{
			break;
		}
		nDone += nRead;
		if (pShowName != 0 && m_pUI != 0)
		{
			m_pUI->DrawLoading (pShowName, (unsigned) ((u64) nDone * 100 / nSize));
			ShowUIFrame (FALSE);
		}
	}
	f_close (&File);

	if (nDone != nSize)
	{
		delete [] pBuffer;
		return 0;
	}

	unsigned nMs = (CTimer::GetClockTicks () - nStart) / 1000;
	char Msg[180];
	unsigned nSpeed10 = nMs ? (unsigned) ((u64) nSize * 10 * 1000 / nMs / (1024 * 1024)) : 0;
	snprintf (Msg, sizeof Msg, "  read %s: %u KB in %u ms (%u.%u MB/s)", pPath + 4,
		  (unsigned) (nSize >> 10), nMs, nSpeed10 / 10, nSpeed10 % 10);
	Log (Msg, COLOR_OK);

	*pSize = nSize;
	return pBuffer;
}

void CKernel::ScanGames (void)
{
	m_pGames = new TGameEntry[MaxGames];
	m_nGames = 0;

	DIR Dir;
	FILINFO Info;
	if (f_opendir (&Dir, NESPI_DIR) != FR_OK)
	{
		return;
	}
	while (   m_nGames < MaxGames
	       && f_readdir (&Dir, &Info) == FR_OK
	       && Info.fname[0] != '\0')
	{
		if ((Info.fattrib & AM_DIR) || !HasNesExtension (Info.fname)
		    || strlen (Info.fname) >= sizeof m_pGames[0].File)
		{
			continue;
		}

		// the list shows the file name as written: '_' as spaces, no extension
		// and nothing after " (" (region / revision tags)
		TGameEntry &G = m_pGames[m_nGames];
		strcpy (G.File, Info.fname);
		unsigned n = 0;
		size_t nLen = strlen (Info.fname) - 4;
		for (size_t i = 0; i < nLen && n < sizeof G.Name - 1; i++)
		{
			char c = Info.fname[i];
			if (c == '(' || c == '[')
			{
				break;
			}
			if (c == '_') c = ' ';
			if (c < 32 || c > 126) c = ' ';
			G.Name[n++] = c;
		}
		while (n > 0 && G.Name[n-1] == ' ') n--;
		G.Name[n] = '\0';
		if (n == 0)
		{
			strncpy (G.Name, Info.fname, sizeof G.Name - 1);
			G.Name[sizeof G.Name - 1] = '\0';
		}
		m_nGames++;
	}
	f_closedir (&Dir);

	// sort by name, ignoring upper / lower case
	auto Compare = [] (const char *a, const char *b)
	{
		for (;; a++, b++)
		{
			int x = (unsigned char) *a, y = (unsigned char) *b;
			if (x >= 'a' && x <= 'z') x -= 32;
			if (y >= 'a' && y <= 'z') y -= 32;
			if (x != y || x == 0) return x - y;
		}
	};
	for (unsigned i = 1; i < m_nGames; i++)
	{
		TGameEntry t = m_pGames[i];
		int j = (int) i - 1;
		while (j >= 0 && Compare (m_pGames[j].Name, t.Name) > 0)
		{
			m_pGames[j+1] = m_pGames[j];
			j--;
		}
		m_pGames[j+1] = t;
	}
}

void CKernel::GamePath (char *pOut, size_t nSize, const char *pExt) const
{
	char Base[64];
	strcpy (Base, m_pGames[m_nLoaded].File);
	size_t n = strlen (Base);
	if (n > 4) Base[n - 4] = '\0';
	snprintf (pOut, nSize, "%s/%s.%s", SAVE_DIR, Base, pExt);
}

// ---- core -----------------------------------------------------------------

static const char *TvName (int n)
{
	return n == NTSC_SVIDEO ? "S-Video" : n == NTSC_COMPOSITE ? "composite" : n == NTSC_RF ? "RF"
	       : s_bTvScanlines ? "RGB" : "off";
}

static const char *PaletteName (int n)
{
	static const char *Names[4] = {"nestopia", "consumer", "sony", "custom"};
	return Names[n & 3];
}

void CKernel::SetupCoreOnce (void)
{
	s_pVideoOut = &m_Video;
	if (!nes_init (SAMPLE_RATE))
	{
		Fatal (NEOPI_ERR_CORE, nes_last_error ());
	}
	nes_set_battery_io (BatteryLoad, BatterySave);
	nes_set_line_hook (NesLineHook);

	// palette: a custom one in nespi/palette.pal (64 or 512 colours)
	FIL File;
	if (f_open (&File, PALETTE_PATH, FA_READ) == FR_OK)
	{
		UINT nRead = 0;
		f_read (&File, s_CustomPal, sizeof s_CustomPal, &nRead);
		f_close (&File);
		if (nRead == 64 * 3 || nRead == 512 * 3)
		{
			s_nCustomPal = nRead;
			Log (nRead == 64 * 3 ? "Custom palette: nespi/palette.pal, 64 colours"
					     : "Custom palette: nespi/palette.pal, 512 colours", COLOR_TEXT);
		}
		else
		{
			Log ("Custom palette: nespi/palette.pal must be 192 or 1536 bytes, ignored", COLOR_ERR);
		}
	}
	if (s_nPalette == NES_PALETTE_CUSTOM && s_nCustomPal == 0)
	{
		s_nPalette = NES_PALETTE_NESTOPIA;
	}
	ApplyPalette ();
	nes_set_unlimited_sprites (s_bUnlimSprites);

	// TV filter: core 1 does the work (if it starts)
	TvSetOutput (TvLineOut);
	unsigned nWorkers = TvStartCores ();
	TvSetup (NTSC_COMPOSITE);			// the tables, for the timing check
	unsigned nUs = TvBenchmarkUs (NES_H - 2 * OVERSCAN_LINES);
	char Msg[100];
	if (nWorkers)
		snprintf (Msg, sizeof Msg, "TV filter: on core 1, a frame of lines in %u.%u ms",
			  nUs / 1000, (nUs % 1000) / 100);
	else
		snprintf (Msg, sizeof Msg, "TV filter: other cores not started, on core 0 (a frame in %u.%u ms)",
			  nUs / 1000, (nUs % 1000) / 100);
	Log (Msg, nWorkers ? COLOR_TEXT : COLOR_ERR);
	TvSetup (s_nTvMode);
	char Line[100];
	snprintf (Line, sizeof Line, "Settings: TV filter %s, palette %s, sprites %s", TvName (s_nTvMode),
		  PaletteName (s_nPalette), s_bUnlimSprites ? "unlimited" : "8 per line");
	Log (Line, COLOR_TEXT);
}

// the palette to the core, and the last picture recoloured (menus over it)
void CKernel::ApplyPalette (void)
{
	if (!nes_set_palette (s_nPalette, s_CustomPal, s_nCustomPal))
	{
		s_nPalette = NES_PALETTE_NESTOPIA;
		nes_set_palette (s_nPalette, 0, 0);
	}
	const u32 *pPal = nes_palette ();
	for (unsigned y = 0; y < NES_H; y++)
	{
		for (unsigned x = 0; x < NES_W; x++)
		{
			boolean bSide = s_bOverscan && (x < OVERSCAN_SIDE || x >= NES_W - OVERSCAN_SIDE);
			s_VideoBuf[y * NES_W + x] = bSide ? 0 : pPal[s_IdxBuf[y * NES_W + x] & 511];
		}
	}
}

// battery RAM goes to the card when the game is unloaded (game list, power off)
void CKernel::SaveGameData (void)
{
}

void CKernel::UnloadGame (void)
{
	if (m_nLoaded < 0)
	{
		return;
	}
	nes_unload ();
	s_BatteryPath[0] = '\0';
	delete [] m_pGame;
	m_pGame = 0;
	m_nLoaded = -1;
}

boolean CKernel::LoadGame (unsigned nIndex)
{
	UnloadGame ();

	const TGameEntry &G = m_pGames[nIndex];
	char Path[128], Line[180];
	snprintf (Path, sizeof Path, "%s/%s", NESPI_DIR, G.File);
	snprintf (Line, sizeof Line, "Game: %s (%s)", G.Name, G.File);
	Log (Line, COLOR_TITLE);

	m_pGame = LoadFile (Path, &m_nGameSize, HEAP_ANY, m_bQuiet ? G.Name : 0);
	if (m_pGame == 0)
	{
		Log ("  read failed", COLOR_ERR);
		return FALSE;
	}

	m_nLoaded = (int) nIndex;			// GamePath needs it
	GamePath (s_BatteryPath, sizeof s_BatteryPath, "sav");
	// beam racing needs the lines as the frame runs: PPU caught up after every
	// CPU instruction (patches/nestopia-linehook.py); without racing, the lazy PPU
	nes_set_ppu_sync (s_bRaceReady);
	if (!nes_load (m_pGame, m_nGameSize))
	{
		snprintf (Line, sizeof Line, "  %s", nes_last_error ());
		Log (Line, COLOR_ERR);
		s_BatteryPath[0] = '\0';
		delete [] m_pGame;
		m_pGame = 0;
		m_nLoaded = -1;
		return FALSE;
	}
	snprintf (Line, sizeof Line, "  board %s", nes_board_name ());
	Log (Line, COLOR_DIM);
	m_nShots = 0;					// screenshot numbers: next free one
	memset (s_VideoBuf, 0, sizeof s_VideoBuf);

	WriteTextFile (LAST_PATH, G.File);
	return TRUE;
}

boolean CKernel::SaveState (const char *pPath)
{
	size_t nSize = 0;
	void *pState = nes_state_save (&nSize);
	if (pState == 0)
	{
		return FALSE;
	}
	FIL File;
	boolean bOK = FALSE;
	if (f_open (&File, pPath, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK)
	{
		UINT nWritten = 0;
		bOK = f_write (&File, pState, nSize, &nWritten) == FR_OK && nWritten == nSize;
		f_close (&File);
	}
	nes_free (pState);
	return bOK;
}

boolean CKernel::LoadState (const char *pPath)
{
	size_t nSize = 0;
	u8 *pState = LoadFile (pPath, &nSize, HEAP_ANY, 0);
	if (pState == 0)
	{
		return FALSE;
	}
	boolean bOK = nes_state_load (pState, nSize) != 0;
	delete [] pState;
	return bOK;
}

// the NES picture in the menus' layout (304x224): 8 lines off the top and
// bottom (as most TVs did), centred, for the darkened copy behind the menus
void CKernel::SnapshotForUI (void)
{
	static u32 Buf[UI_SRC_PITCH * UI_SRC_LINES];
	memset (Buf, 0, sizeof Buf);
	for (unsigned y = 0; y < UI_H; y++)
	{
		if (s_nTvMode != NTSC_OFF)
		{
			// the filtered picture, as wide as on the screen (602 / 2 pixels)
			const u32 *s = &s_NtscBuf[(y + 8) * NTSC_OUT_W];
			u32 *d = &Buf[(y + UI_SRC_Y) * UI_SRC_PITCH + UI_SRC_X + (UI_W - NTSC_OUT_W / 2) / 2];
			for (unsigned x = 0; x < NTSC_OUT_W / 2; x++)
			{
				u32 a = s[2 * x], b = s[2 * x + 1];
				d[x] = ((a >> 1) & 0x7F7F7Fu) + ((b >> 1) & 0x7F7F7Fu);
			}
			continue;
		}
		const u32 *s = &s_VideoBuf[(y + 8) * NES_W];
		u32 *d = &Buf[(y + UI_SRC_Y) * UI_SRC_PITCH + UI_SRC_X + (UI_W - NES_W) / 2];
		memcpy (d, s, NES_W * 4);
	}
	m_pUI->SnapshotGame (Buf);
}

// ---- video helpers ----------------------------------------------------------

void CKernel::ShowUIFrame (boolean bWait, const char *pTopMessage)
{
	m_Video.SetRacing (FALSE);
	m_Video.SetSource (s_UISource);
	m_Video.DrawGame (m_pUI->GetBuffer ());
	if (pTopMessage && pTopMessage[0])
	{
		// in the black band above the picture, like the in-game messages
		const CVideoOut::TTextSpan Span = {pTopMessage, UI_ERR};
		m_Video.DrawBandText (TRUE, &Span, 1);
	}
	else
	{
		m_Video.DrawBandText (TRUE, 0, 0);	// no status line or message over menus
	}
	m_Video.DrawBandText (FALSE, 0, 0);
	m_Video.Flip ();
	if (!bWait)
	{
		return;
	}
	if (m_bVSync)
	{
		m_Video.WaitForVSync ();
	}
	else
	{
		CTimer::SimpleusDelay (16667);
	}
}

// Select+A: the NES picture (256x240, no filter) as
// nespi/shots/<game>_NNN.png; returns the file name for the message, or 0
const char *CKernel::SaveScreenshot (void)
{
	static char Name[80];
	if (m_nLoaded < 0)
	{
		return 0;
	}
	char Base[64], Path[160];
	strcpy (Base, m_pGames[m_nLoaded].File);
	size_t n = strlen (Base);
	if (n > 4) Base[n - 4] = '\0';

	// next free number (the search starts after the last one used)
	FILINFO Info;
	for (;;)
	{
		if (++m_nShots > 999)
		{
			return 0;
		}
		snprintf (Name, sizeof Name, "%s_%03u.png", Base, m_nShots);
		snprintf (Path, sizeof Path, "%s/%s", SHOT_DIR, Name);
		if (f_stat (Path, &Info) != FR_OK)
		{
			break;
		}
	}

	// the picture as shown (without the hidden overscan lines)
	static u8 RGB[NES_W * NES_H * 3];
	u8 *d = RGB;
	const unsigned nShotH = s_NesSource.nH;
	for (unsigned i = 0; i < NES_W * nShotH; i++)
	{
		u32 c = s_VideoBuf[s_NesSource.nY * NES_W + i];
		*d++ = (u8) (c >> 16);
		*d++ = (u8) (c >> 8);
		*d++ = (u8) c;
	}
	size_t nSize = 0;
	void *pPng = neopi_png_encode (RGB, NES_W, nShotH, &nSize);
	if (pPng == 0)
	{
		return 0;
	}
	FIL File;
	boolean bOK = FALSE;
	if (f_open (&File, Path, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK)
	{
		UINT nWritten = 0;
		bOK = f_write (&File, pPng, nSize, &nWritten) == FR_OK && nWritten == nSize;
		f_close (&File);
	}
	neopi_png_free (pPng);

	char Line[200];
	snprintf (Line, sizeof Line, "  screenshot %s: %s", Name, bOK ? "saved" : "FAILED");
	Log (Line, bOK ? COLOR_TEXT : COLOR_ERR);
	return bOK ? Name : 0;
}

void CKernel::Benchmark (void)
{
	static unsigned Emu[BENCH_FRAMES], Draw[BENCH_FRAMES], Total[BENCH_FRAMES];
	u64 nSamples = 0;

	boolean bQuiet = m_bQuiet;
	ApplyVideoMode ();		// measure the real drawing path (filter, HVS)
	m_Video.SetSource (GameSource ());
	m_bQuiet = TRUE;

	// on screen once per second (outside the timings): the game and, above it,
	// what the benchmark is doing, so it is clear that it is not stuck
	auto Progress = [this] (const char *pPhase, unsigned nFrame, unsigned nFrames)
	{
		char Text[64];
		snprintf (Text, sizeof Text, "BENCHMARK  %s %u/%u S", pPhase,
			  nFrame / 60, nFrames / 60);
		const CVideoOut::TTextSpan Span = {Text, UI_WHITE};
		m_Video.DrawBandText (TRUE, &Span, 1);
		m_Video.Flip ();
	};

	Log ("Benchmark: warm-up", COLOR_TEXT);
	SaveLog ();

	// 1. warm-up: let the BIOS logo and the game's start-up pass
	for (unsigned i = 0; i < BENCH_WARMUP; i++)
	{
		NesRunFrame ();
		if (i % 60 == 0)
		{
			Progress ("WARM-UP", i, BENCH_WARMUP);
		}
	}

	Log ("Benchmark: measuring", COLOR_TEXT);
	SaveLog ();

	// 2. measurement, with the sampling profiler running
	CProfiler *pProfiler = new CProfiler (&m_Interrupt);
	boolean bProfile = pProfiler && pProfiler->Start (BENCH_PROFILE_US);

	enum { Worst = 10 };
	unsigned WorstIdx[Worst];
	unsigned nWorst = 0;
	unsigned nSlow = 0, nOver = 0;
	u32 nAllHash = 2166136261U;

	for (unsigned i = 0; i < BENCH_FRAMES; i++)
	{
		if (bProfile) pProfiler->BeginFrame ();
		unsigned t0 = CTimer::GetClockTicks ();
		NesRunFrame ();			// emulation and drawing go together,
		unsigned t1 = CTimer::GetClockTicks ();	// line by line
		unsigned t2 = t1;
		boolean bSlow = t2 - t0 >= BENCH_SLOW_US;
		if (bProfile) pProfiler->EndFrame (bSlow);

		// hash of every measured frame (outside the timings): the same game and
		// build options must give the same value
		for (unsigned i = 0; i < NES_W * NES_H; i++)
		{
			nAllHash = (nAllHash ^ s_VideoBuf[i]) * 16777619U;
		}
		if (i % 60 == 0)
		{
			Progress ("MEASURING", i, BENCH_FRAMES);
		}

		Emu[i] = t1 - t0;
		Draw[i] = t2 - t1;
		Total[i] = t2 - t0;
		nSamples += AUDIO_FRAME;
		if (bSlow) nSlow++;
		if (Total[i] > 16667) nOver++;

		// keep the indices of the slowest frames, slowest first
		unsigned k;
		if (nWorst < Worst)
		{
			k = nWorst++;
		}
		else if (Total[i] > Total[WorstIdx[Worst - 1]])
		{
			k = Worst - 1;
		}
		else
		{
			continue;
		}
		while (k > 0 && Total[WorstIdx[k-1]] < Total[i])
		{
			WorstIdx[k] = WorstIdx[k-1];
			k--;
		}
		WorstIdx[k] = i;
	}
	if (bProfile) pProfiler->Stop ();

	m_bQuiet = bQuiet;
	m_Video.SetConsoleMode ();
	RedrawConsole ();

	TStats E, D, T;
	ComputeStats (Emu, BENCH_FRAMES, &E);
	ComputeStats (Draw, BENCH_FRAMES, &D);
	ComputeStats (Total, BENCH_FRAMES, &T);

	char a[16], b[16], c[16], d[16];
	char Report[4096];
	size_t nPos = 0;
	char Line[160];

	CMachineInfo *pInfo = CMachineInfo::Get ();
	const unsigned nFrameUs = 16667;
	unsigned nSpeed = T.nAvg ? (unsigned) ((u64) nFrameUs * 100 / T.nAvg) : 0;

	#define REPORT(color) do { Log (Line, color); \
		nPos += snprintf (Report + nPos, sizeof Report - nPos, "%s\r\n", Line); } while (0)

	snprintf (Line, sizeof Line, "nes-pi %s - %s, %u MB, CPU %u MHz, %u C",
		  NEOPI_VERSION, pInfo->GetMachineName (), pInfo->GetRAMSize (),
		  m_CPUThrottle.GetClockRate () / 1000000, m_CPUThrottle.GetTemperature ());
	REPORT (COLOR_TEXT);
	snprintf (Line, sizeof Line, "Game: %s (measured after %u s of play, for %u s)",
		  m_pGames[m_nLoaded].File, BENCH_WARMUP / 60, BENCH_FRAMES / 60);
	REPORT (COLOR_TEXT);
	char Tag[8];
	VideoModeTag (Tag, sizeof Tag);
	snprintf (Line, sizeof Line, "Screen: mode %s, %ux%u, scale %ux (HVS %ux, CPU %ux), filter %s",
		  Tag, m_Video.GetWidth (), m_Video.GetHeight (), m_Video.GetScale (),
		  m_Video.GetHVSFactor (), m_Video.GetScale () / m_Video.GetHVSFactor (),
		  FilterName (m_nFilter));
	REPORT (COLOR_TEXT);
	snprintf (Line, sizeof Line, "Frame times (ms)            avg       p99     max     min");
	REPORT (COLOR_TITLE);
	FormatMs (a, sizeof a, E.nAvg); FormatMs (b, sizeof b, E.nP99);
	FormatMs (c, sizeof c, E.nMax); FormatMs (d, sizeof d, E.nMin);
	snprintf (Line, sizeof Line, "  emulation + drawing     %7s %7s %7s %7s", a, b, c, d);
	REPORT (COLOR_TEXT);
	FormatMs (a, sizeof a, D.nAvg); FormatMs (b, sizeof b, D.nP99);
	FormatMs (c, sizeof c, D.nMax); FormatMs (d, sizeof d, D.nMin);
	snprintf (Line, sizeof Line, "  (drawing is per line)   %7s %7s %7s %7s", a, b, c, d);
	REPORT (COLOR_TEXT);
	FormatMs (a, sizeof a, T.nAvg); FormatMs (b, sizeof b, T.nP99);
	FormatMs (c, sizeof c, T.nMax); FormatMs (d, sizeof d, T.nMin);
	snprintf (Line, sizeof Line, "  total                   %7s %7s %7s %7s", a, b, c, d);
	REPORT (COLOR_TEXT);
	snprintf (Line, sizeof Line, "Top speed: %u%% of 60 fps", nSpeed);
	REPORT (nSpeed >= 150 ? COLOR_OK : COLOR_ERR);
	snprintf (Line, sizeof Line, "Frames over 16.7 ms (too slow for full speed): %u of %u (%u.%u%%)",
		  nOver, BENCH_FRAMES, nOver * 100 / BENCH_FRAMES, (nOver * 1000 / BENCH_FRAMES) % 10);
	REPORT (nOver ? COLOR_ERR : COLOR_OK);
	snprintf (Line, sizeof Line, "Slowest frames (second.frame: emulation + drawing ms):");
	REPORT (COLOR_TITLE);
	for (unsigned w = 0; w < nWorst; w += 2)
	{
		size_t n = 0;
		Line[0] = 0;
		for (unsigned j = w; j < w + 2 && j < nWorst; j++)
		{
			unsigned f = WorstIdx[j];
			FormatMs (a, sizeof a, Emu[f]); FormatMs (b, sizeof b, Draw[f]);
			n += snprintf (Line + n, sizeof Line - n, "  %3u.%02u: %s + %s",
				       f / 60, f % 60, a, b);
		}
		REPORT (COLOR_TEXT);
	}
	snprintf (Line, sizeof Line, "Audio: %u samples per frame at %u Hz",
		  (unsigned) (nSamples / BENCH_FRAMES), SAMPLE_RATE);
	REPORT (COLOR_DIM);
	// same game + same build options must give the same picture after the
	// same number of frames: used to check compiler optimisations
	u32 nHash = 2166136261U;
	for (unsigned i = 0; i < NES_W * NES_H; i++)
	{
		nHash = (nHash ^ s_VideoBuf[i]) * 16777619U;
	}
	snprintf (Line, sizeof Line, "Check: picture after %u frames = %08X",
		  BENCH_WARMUP + BENCH_FRAMES, nHash);
	REPORT (COLOR_DIM);
	snprintf (Line, sizeof Line, "Check: all %u measured frames = %08X", BENCH_FRAMES, nAllHash);
	REPORT (COLOR_DIM);
	if (bProfile)
	{
		snprintf (Line, sizeof Line, "Profile: %u samples (%u in frames over %u ms) -> PROFILE.TXT",
			  pProfiler->GetTotal (), pProfiler->GetSlow (), BENCH_SLOW_US / 1000);
	}
	else
	{
		snprintf (Line, sizeof Line, "Profile: not available");
	}
	REPORT (COLOR_DIM);

	WriteTextFile (BENCH_PATH, Report);
	if (bProfile)
	{
		const size_t nSize = 1024 * 1024;
		char *pText = new char[nSize];
		if (pText)
		{
			pProfiler->Format (pText, nSize);
			WriteTextFile (PROFILE_PATH, pText);
			delete [] pText;
		}
	}
	delete pProfiler;
	Log ("Results saved to nespi/BENCH.TXT and nespi/PROFILE.TXT", COLOR_OK);
	SaveLog ();

	nes_reset (1);		// play from a clean boot
}

// ---- statistics -------------------------------------------------------------

void CKernel::ReportWindow (boolean bFinal)
{
	if (s_nWindowFrames < 60)
	{
		return;
	}
	unsigned nElapsed = CTimer::GetClockTicks () - s_nWindowStart;
	unsigned nFPS100 = (unsigned) ((u64) s_nWindowFrames * 100 * 1000000 / nElapsed);
	char ea[16], ep[16], em[16], wa[16], wp[16], pm[16];
	FormatMs (ea, sizeof ea, s_HistEmu.Avg ());
	FormatMs (ep, sizeof ep, s_HistEmu.P99 ());
	FormatMs (em, sizeof em, s_HistEmu.nMax);
	FormatMs (wa, sizeof wa, s_HistWork.Avg ());
	FormatMs (wp, sizeof wp, s_HistWork.P99 ());
	FormatMs (pm, sizeof pm, s_HistPeriod.nMax);
	unsigned qmin, qmax, und;
	int adj;
	m_Audio.GetStats (&qmin, &qmax, &und, &adj);

	char Tag[8];
	VideoModeTag (Tag, sizeof Tag);
	char Line[MaxLineLen];
	snprintf (Line, sizeof Line,
		  "[%s] %u frames, FPS %u.%02u | emu %s/%s/%s ms | emu+draw %s/%s ms (>16ms: %u)",
		  bFinal ? "partial" : "1 min", s_nWindowFrames, nFPS100 / 100, nFPS100 % 100,
		  ea, ep, em, wa, wp, s_nOverBudget);
	Log (Line, COLOR_TEXT);
	snprintf (Line, sizeof Line,
		  "   video %s x%u %s | max period %s ms, missed vsyncs %u | audio %u-%u, underruns %u, adj %d | %u MHz %u C",
		  Tag, m_Video.GetScale (), FilterShort (m_Video.GetFilter ()),
		  pm, s_nMissedVSync, qmin, qmax, und, adj,
		  m_CPUThrottle.GetClockRate () / 1000000, m_CPUThrottle.GetTemperature ());
	Log (Line, COLOR_TEXT);

	// TV filter: per frame, all cores together (filtering vs drawing on the screen)
	unsigned nFilterUs, nDrawUs, nTvLines;
	TvTakeStats (&nFilterUs, &nDrawUs, &nTvLines);
	if (nTvLines >= 224)
	{
		unsigned nFrames = nTvLines / s_NtscSource.nH ? nTvLines / s_NtscSource.nH : 1;
		unsigned f = nFilterUs / nFrames, d = nDrawUs / nFrames;
		snprintf (Line, sizeof Line,
			  "   TV filter: per frame %u.%u ms filtering + %u.%u ms drawing, on %u core%s",
			  f / 1000, (f % 1000) / 100, d / 1000, (d % 1000) / 100,
			  TvGetWorkers () ? TvGetWorkers () : 1, TvGetWorkers () > 1 ? "s" : "");
		Log (Line, COLOR_TEXT);
	}

	if (s_HistLag.nCount > 0)
	{
		char la[16], lm[16], lx[16], sa[16], sm[16];
		FormatMs (la, sizeof la, s_HistLag.Avg ());
		FormatMs (lm, sizeof lm, s_HistLag.nMin);
		FormatMs (lx, sizeof lx, s_HistLag.nMax);
		FormatMs (sa, sizeof sa, s_HistSlack.Avg ());
		FormatMs (sm, sizeof sm, s_HistSlack.nMin);
		snprintf (Line, sizeof Line,
			  "   lag: %u inputs, report->scan-out %s/%s/%s ms avg/min/max | "
			  "ready before vsync %s/%s ms avg/min",
			  s_HistLag.nCount, la, lm, lx, sa, sm);
		Log (Line, COLOR_TEXT);
	}

	if (s_pRacer && s_pRacer->GetStats ().nFrames > 0)
	{
		const CBeamRacer::TStats &S = s_pRacer->GetStats ();
		const CRaster *R = s_pRaster;
		char la[16], lm[16], lx[16], va[16], pa[16], px[16];
		FormatMs (la, sizeof la, (unsigned) R->TicksToUs ((s64) (S.nLeadSum / S.nFrames)));
		FormatMs (lm, sizeof lm, (unsigned) R->TicksToUs ((s64) S.nLeadMin));
		FormatMs (lx, sizeof lx, (unsigned) R->TicksToUs ((s64) S.nLeadMax));
		FormatMs (va, sizeof va, (unsigned) R->TicksToUs ((s64) (S.nVBSum / S.nFrames)));
		FormatMs (pa, sizeof pa, (unsigned) R->TicksToUs ((s64) (S.nPadSum / S.nFrames)));
		FormatMs (px, sizeof px, (unsigned) R->TicksToUs ((s64) S.nPadMax));
		snprintf (Line, sizeof Line,
			  "   race: %u frames | pads->picture %s/%s ms avg/max | lead %s/%s/%s | vblank %s ms",
			  S.nFrames, pa, px, la, lm, lx, va);
		Log (Line, COLOR_TEXT);
		snprintf (Line, sizeof Line,
			  "   race: late rows %u in %u frames, skipped %u, min slack %d us | raster resyncs %u, last fix %d ns",
			  S.nLateRows, S.nLateFrames, S.nSkips, (int) R->TicksToUs (S.nSlackMin),
			  R->GetResyncs (), (int) R->TicksToNs (R->GetLastCorrection ()));
		Log (Line, S.nLateFrames || S.nSkips ? COLOR_ERR : COLOR_TEXT);
		s_pRacer->ResetStats ();
	}
	if (s_nDelayCount > 0)
	{
		char da[16], dm[16], dx[16];
		FormatMs (da, sizeof da, (unsigned) (s_nDelaySum / s_nDelayCount));
		FormatMs (dm, sizeof dm, s_nDelayMin);
		FormatMs (dx, sizeof dx, s_nDelayMax);
		char Mode[16];
		if (s_nDelayCfg < 0)		snprintf (Mode, sizeof Mode, "auto");
		else if (s_nDelayCfg == 0)	snprintf (Mode, sizeof Mode, "off");
		else				snprintf (Mode, sizeof Mode, "fixed %d ms", s_nDelayCfg);
		snprintf (Line, sizeof Line,
			  "   frame delay %s: %s/%s/%s ms avg/min/max, vsyncs missed with delay %u",
			  Mode, da, dm, dx, s_nDelayMisses);
		Log (Line, s_nDelayMisses ? COLOR_ERR : COLOR_TEXT);
	}

	s_HistEmu.Reset ();
	s_HistWork.Reset ();
	s_HistPeriod.Reset ();
	s_HistLag.Reset ();
	s_HistSlack.Reset ();
	s_nOverBudget = s_nMissedVSync = s_nWindowFrames = 0;
	s_nWindowStart = CTimer::GetClockTicks ();
	DelayStatsReset ();
}

// ---- configuration, system menu, power off ---------------------------------

const char *CKernel::FilterShort (unsigned nFilter)
{
	switch (nFilter)
	{
	case FILTER_SCAN_LIGHT:	 return "SCAN-L";
	case FILTER_SCAN_MEDIUM: return "SCAN-M";
	case FILTER_SCAN_STRONG: return "SCAN-S";
	default:		 return "NOFLT";
	}
}

// usbpoll= value for the log and nespi.cfg
static const char *UsbPollName (void)
{
	switch (g_nUSBInterruptPollMs)
	{
	case 0:		return "native";
	case 2:		return "500";
	case 4:		return "250";
	default:	return "1000";
	}
}

const char *CKernel::FilterName (unsigned nFilter)
{
	switch (nFilter)
	{
	case FILTER_SCAN_LIGHT:	 return "SCANLINES_LIGHT";
	case FILTER_SCAN_MEDIUM: return "SCANLINES";
	case FILTER_SCAN_STRONG: return "SCANLINES_STRONG";
	default:		 return "NONE";
	}
}

// "HVS4", "HVS2" or "CPU": how the picture is being enlarged right now
void CKernel::VideoModeTag (char *pOut, size_t nSize)
{
	if (m_Video.GetHVSFactor () > 1)
	{
		snprintf (pOut, nSize, "HVS%u", m_Video.GetHVSFactor ());
	}
	else
	{
		snprintf (pOut, nSize, "CPU");
	}
}

// TV FILTER: RGB = sharp pixels with light scanlines; RF / COMPOSITE /
// S-VIDEO = blargg's filter (no scanlines: the CPU draws at least 2x anyway)
void CKernel::SetTvMode (void)
{
	m_nFilter = s_nTvMode == NTSC_OFF && s_bTvScanlines ? FILTER_SCAN_LIGHT : FILTER_NONE;
	TvSetup (s_nTvMode);
	ApplyVideoMode ();
}

void CKernel::ApplyVideoMode (void)
{
	if (!m_Video.SetGameMode (m_nFilter, m_bHVS, s_nTvMode != NTSC_OFF))
	{
		Fatal (NEOPI_ERR_FB, "cannot set up the game framebuffer");
	}
	m_Video.SetSource (s_UISource);
	char Tag[8];
	VideoModeTag (Tag, sizeof Tag);
	char Line[MaxLineLen];
	snprintf (Line, sizeof Line, "Video: mode %s, HDMI %ux%u, framebuffer %ux%u, scale %ux (HVS %ux, CPU %ux), filter %s%s",
		  Tag, m_Video.GetWidth (), m_Video.GetHeight (),
		  m_Video.GetFBWidth (), m_Video.GetFBHeight (), m_Video.GetScale (),
		  m_Video.GetHVSFactor (), m_Video.GetScale () / m_Video.GetHVSFactor (),
		  FilterName (m_Video.GetFilter ()), m_bHVS ? "" : " (scaler=cpu)");
	Log (Line, COLOR_TEXT);
	snprintf (Line, sizeof Line, "Video: pixel order %s (%u), red in the %s byte%s",
		  m_Video.GetPixelOrder () ? "RGB" : "BGR", m_Video.GetPixelOrder (),
		  m_Video.IsRedLow () ? "low" : "high", "");
	Log (Line, COLOR_TEXT);
	snprintf (Line, sizeof Line, "Video: framebuffer write-combining %s (%u pages of 64 KB)",
		  m_Video.GetWCPages () ? "on" : (g_bFbWriteCombine ? "FAILED" : "off (writecombine=off)"),
		  m_Video.GetWCPages ());
	Log (Line, COLOR_TEXT);
}

void CKernel::StateExt (char *pExt, unsigned nSlot) const
{
	pExt[0] = 's';
	pExt[1] = 't';
	pExt[2] = (char) ('1' + nSlot);
	pExt[3] = '\0';
}

void CKernel::LoadConfig (void)
{
	FIL File;
	char Text[256];
	UINT nRead = 0;
	if (f_open (&File, CFG_PATH, FA_READ) != FR_OK)
	{
		return;
	}
	f_read (&File, Text, sizeof Text - 1, &nRead);
	f_close (&File);
	Text[nRead] = '\0';

	if (strstr (Text, "filter=SCANLINES_LIGHT"))		m_nFilter = FILTER_SCAN_LIGHT;
	else if (strstr (Text, "filter=SCANLINES_STRONG"))	m_nFilter = FILTER_SCAN_STRONG;
	else if (strstr (Text, "filter=SCANLINES"))		m_nFilter = FILTER_SCAN_MEDIUM;
	if (strstr (Text, "scaler=cpu"))	m_bHVS = FALSE;
	// USB pads are polled every 1 ms (like usbhid.jspoll=1 on Linux);
	// usbpoll=500 / 250 = every 2 / 4 ms, for pads that misbehave at 1000 Hz;
	// usbpoll=native keeps the interval requested by the device
	if (strstr (Text, "usbpoll=native"))	g_nUSBInterruptPollMs = 0;
	else if (strstr (Text, "usbpoll=500"))	g_nUSBInterruptPollMs = 2;
	else if (strstr (Text, "usbpoll=250"))	g_nUSBInterruptPollMs = 4;
	// input lag (see "input lag" at the top of this file)
	if (strstr (Text, "palette=consumer"))		s_nPalette = NES_PALETTE_CONSUMER;
	else if (strstr (Text, "palette=sony"))		s_nPalette = NES_PALETTE_SONY;
	else if (strstr (Text, "palette=custom"))	s_nPalette = NES_PALETTE_CUSTOM;
	if (strstr (Text, "sprites=unlimited"))		s_bUnlimSprites = TRUE;
	if (strstr (Text, "overscan=off"))
	{
		s_bOverscan = FALSE;
		s_NesSource.nH = s_NtscSource.nH = NES_H;
		s_NesSource.nY = s_NtscSource.nY = 0;
	}
	if (strstr (Text, "tvfilter=rf"))		s_nTvMode = NTSC_RF;
	else if (strstr (Text, "tvfilter=svideo"))	s_nTvMode = NTSC_SVIDEO;
	else if (strstr (Text, "tvfilter=composite"))	s_nTvMode = NTSC_COMPOSITE;
	else						s_nTvMode = NTSC_OFF;	// tvfilter=rgb / off
	s_bTvScanlines = strstr (Text, "tvfilter=off") == 0;
	if (strstr (Text, "writecombine=off"))		g_bFbWriteCombine = FALSE;
	if (strstr (Text, "beamracing=off"))		s_nRaceCfg = 0;
	if (strstr (Text, "beamracing=on"))		s_nRaceCfg = 1;
	const char *pDelay = strstr (Text, "framedelay=");
	if (pDelay)
	{
		pDelay += 11;
		if (strncmp (pDelay, "off", 3) == 0)
		{
			s_nDelayCfg = 0;
		}
		else if (*pDelay >= '0' && *pDelay <= '9')
		{
			int n = 0;
			while (*pDelay >= '0' && *pDelay <= '9' && n < 100)
			{
				n = n * 10 + (*pDelay++ - '0');
			}
			s_nDelayCfg = n > 12 ? 12 : n;
		}
	}

	char Line[140];
	char Delay[16];
	if (s_nDelayCfg < 0)		snprintf (Delay, sizeof Delay, "auto");
	else if (s_nDelayCfg == 0)	snprintf (Delay, sizeof Delay, "off");
	else				snprintf (Delay, sizeof Delay, "%d ms", s_nDelayCfg);
	snprintf (Line, sizeof Line, "Settings: filter %s, USB polling %s%s, overscan %s, frame delay %s, beam racing %s",
		  FilterName (m_nFilter), UsbPollName (), g_nUSBInterruptPollMs ? " Hz" : "",
		  s_bOverscan ? "hidden" : "shown", Delay,
		  s_nRaceCfg ? "on" : "off");
	Log (Line, COLOR_TEXT);
}

void CKernel::SaveConfig (void)
{
	char Text[200];
	snprintf (Text, sizeof Text, "filter=%s\r\n%s", FilterName (m_nFilter), m_bHVS ? "" : "scaler=cpu\r\n");
	if (g_nUSBInterruptPollMs != 1)
	{
		strcat (Text, "usbpoll=");
		strcat (Text, UsbPollName ());
		strcat (Text, "\r\n");
	}
	if (!s_bOverscan)
	{
		strcat (Text, "overscan=off\r\n");
	}
	if (s_nPalette != NES_PALETTE_NESTOPIA)
	{
		strcat (Text, "palette=");
		strcat (Text, PaletteName (s_nPalette));
		strcat (Text, "\r\n");
	}
	if (s_bUnlimSprites)
	{
		strcat (Text, "sprites=unlimited\r\n");
	}
	strcat (Text, s_nTvMode == NTSC_SVIDEO ? "tvfilter=svideo\r\n"
		      : s_nTvMode == NTSC_COMPOSITE ? "tvfilter=composite\r\n"
		      : s_nTvMode == NTSC_RF ? "tvfilter=rf\r\n"
		      : s_bTvScanlines ? "tvfilter=rgb\r\n" : "tvfilter=off\r\n");
	if (s_nRaceCfg == 0)
	{
		strcat (Text, "beamracing=off\r\n");
	}
	if (s_nDelayCfg >= 0)
	{
		char Line[24];
		if (s_nDelayCfg == 0)	snprintf (Line, sizeof Line, "framedelay=off\r\n");
		else			snprintf (Line, sizeof Line, "framedelay=%d\r\n", s_nDelayCfg);
		strcat (Text, Line);
	}
	WriteTextFile (CFG_PATH, Text);
}

void CKernel::RunSystemMenu (void)
{
	enum { SmTv, SmPalette, SmSprites, SmControls, SmPower, SmCount };

	unsigned nSel = 0;
	u32 nPrev = ~0u;
	unsigned nOldFilter = m_nFilter;
	int nOldPalette = s_nPalette;
	int nOldTv = s_nTvMode;
	boolean bOldScan = s_bTvScanlines;
	boolean bOldSprites = s_bUnlimSprites;
	const char *pMsg = "";
	unsigned nFrame = 0;

	CMachineInfo *pInfo = CMachineInfo::Get ();
	char Footer[48];
	snprintf (Footer, sizeof Footer, "NES-PI " NEOPI_VERSION "  PI %u  %u C", pInfo->GetModelMajor (),
		  m_CPUThrottle.GetTemperature ());

	while (1)
	{
		if (nFrame++ % 15 == 0)
		{
			m_pInput->UpdateDevices ();
		}
		if (nFrame % 120 == 0)
		{
			snprintf (Footer, sizeof Footer, "NES-PI " NEOPI_VERSION "  PI %u  %u C", pInfo->GetModelMajor (),
				  m_CPUThrottle.GetTemperature ());
		}

		u32 n = m_pInput->GetAny ();
		u32 nNew = nPrev == ~0u ? 0 : (n & ~nPrev);
		nPrev = n;

		// PALETTE only with RGB: the TV filter makes its own colours
		const boolean bPaletteOff = s_nTvMode != NTSC_OFF;
		if (nNew & BTN_UP)
		{
			nSel = (nSel + SmCount - 1) % SmCount;
			if (bPaletteOff && nSel == SmPalette) nSel = (nSel + SmCount - 1) % SmCount;
			pMsg = "";
		}
		if (nNew & BTN_DOWN)
		{
			nSel = (nSel + 1) % SmCount;
			if (bPaletteOff && nSel == SmPalette) nSel = (nSel + 1) % SmCount;
			pMsg = "";
		}
		int nDir = (nNew & (BTN_A | BTN_RIGHT)) ? 1 : ((nNew & BTN_LEFT) ? -1 : 0);

		if (nDir != 0)
		{
			switch (nSel)
			{
			case SmTv:
			{
				// OFF (sharp pixels) -> RGB (light scanlines) -> S-VIDEO ->
				// COMPOSITE -> RF; index 0 = OFF, then the NTSC modes
				static const int Order[5] = {NTSC_OFF, NTSC_OFF, NTSC_SVIDEO, NTSC_COMPOSITE, NTSC_RF};
				unsigned i = s_nTvMode == NTSC_OFF ? (s_bTvScanlines ? 1 : 0)
					   : s_nTvMode == NTSC_SVIDEO ? 2 : s_nTvMode == NTSC_COMPOSITE ? 3 : 4;
				i = (i + 5 + nDir) % 5;
				s_nTvMode = Order[i];
				s_bTvScanlines = i != 0;
				SetTvMode ();
				break;
			}

			case SmPalette:
				if (bPaletteOff)
				{
					break;
				}
			{
				int nCount = s_nCustomPal ? 4 : 3;	// CUSTOM only with nespi/palette.pal
				s_nPalette = (s_nPalette + nCount + nDir) % nCount;
				ApplyPalette ();
				if (m_nLoaded >= 0)
				{
					SnapshotForUI ();		// the paused game in the new colours
				}
				break;
			}

			case SmSprites:
				s_bUnlimSprites = !s_bUnlimSprites;
				nes_set_unlimited_sprites (s_bUnlimSprites);
				break;

			case SmControls:
				if (nNew & BTN_A)
				{
					int nPad = ChoosePad ();
					if (nPad >= 0)
					{
						RunMapping ((unsigned) nPad, m_nLoaded >= 0);
					}
					else if (m_pInput->GetPadCount () == 0)
					{
						pMsg = "CONNECT A USB GAMEPAD";
					}
					nPrev = ~0u;
				}
				break;

			case SmPower:
				if ((nNew & BTN_A) && PowerOffCountdown ())
				{
					PowerOff ();
				}
				nPrev = ~0u;
				break;
			}
		}

		if (nNew & BTN_B)
		{
			break;
		}

		static const char *Labels[SmCount] = {"TV FILTER", "PALETTE", "SPRITE FLICKER", "CONTROLS", "POWER OFF"};
		TMenuItem Items[SmCount];
		for (unsigned i = 0; i < SmCount; i++)
		{
			Items[i].pLabel = Labels[i];
			Items[i].pValue = 0;
			Items[i].bDisabled = FALSE;
		}
		static const char *FilterLabels[FILTER_COUNT] = {"OFF", "LIGHT", "MEDIUM", "STRONG"};
		(void) FilterLabels;
		static const char *TvLabels[4] = {"RGB", "S-VIDEO", "COMPOSITE", "RF"};
		Items[SmTv].pValue = s_nTvMode == NTSC_OFF && !s_bTvScanlines ? "OFF" : TvLabels[s_nTvMode & 3];
		static const char *PaletteLabels[4] = {"NESTOPIA", "CONSUMER", "SONY TV", "CUSTOM"};
		Items[SmPalette].pValue = PaletteLabels[s_nPalette & 3];
		Items[SmPalette].bDisabled = s_nTvMode != NTSC_OFF;
		Items[SmSprites].pValue = s_bUnlimSprites ? "REDUCED" : "ORIGINAL";

		static const TButtonHint Hints[2] = {{'A', "CHANGE"}, {'B', "BACK"}};
		m_pUI->DrawPanelMenu (m_nLoaded >= 0, "OPTIONS", Items, SmCount, nSel, pMsg, UI_VALUE,
				      Hints, 2, Footer);
		ShowUIFrame ();
	}

	if (m_nFilter != nOldFilter || s_nPalette != nOldPalette || s_bUnlimSprites != bOldSprites
	    || s_nTvMode != nOldTv || s_bTvScanlines != bOldScan)
	{
		SaveConfig ();
		char Line[140];
		snprintf (Line, sizeof Line, "New settings: TV filter %s, scanlines %s, palette %s, sprites %s",
			  TvName (s_nTvMode), FilterName (m_nFilter),
			  PaletteName (s_nPalette), s_bUnlimSprites ? "unlimited" : "8 per line");
		Log (Line, COLOR_TEXT);
	}
}

// ---- control mapping --------------------------------------------------------

void CKernel::LoadPads (void)
{
	static char Text[4096];
	FIL File;
	UINT nRead = 0;
	if (f_open (&File, PADS_PATH, FA_READ) == FR_OK)
	{
		f_read (&File, Text, sizeof Text - 1, &nRead);
		f_close (&File);
	}
	Text[nRead] = '\0';
	m_pInput->LoadMaps (Text);
}

void CKernel::SavePads (void)
{
	static char Text[4096];
	m_pInput->FormatMaps (Text, sizeof Text);
	WriteTextFile (PADS_PATH, Text);
}

// CONTROLS from the SYSTEM menu: which pad? (the only one, or the one that is pressed)
int CKernel::ChoosePad (void)
{
	unsigned nCount = m_pInput->GetPadCount ();
	if (nCount == 0)
	{
		return -1;
	}
	if (nCount == 1)
	{
		for (unsigned i = 0; i < MAX_PADS; i++)
		{
			if (m_pInput->IsPadConnected (i))
			{
				return (int) i;
			}
		}
	}

	boolean bReleased = FALSE;
	for (unsigned f = 0; f < 600; f++)		// 10 seconds
	{
		int nActive = m_pInput->GetActivePad ();
		if (!bReleased)
		{
			bReleased = nActive < 0;
		}
		else if (nActive >= 0)
		{
			return nActive;
		}
		if (m_pInput->GetKeyboard () & BTN_MENU)
		{
			break;
		}
		m_pUI->DrawMessage ("CONTROLS", "PRESS A BUTTON ON THE PAD", "YOU WANT TO SET UP");
		ShowUIFrame ();
	}
	return -1;
}

// Asks for A B START SELECT in turn on one pad (directions are automatic),
// then saves on its own. A button already used is ignored; Esc on the
// keyboard cancels. (pads.cfg keeps the C and D slots of the shared input
// code: they stay unset here, and are ignored if an older set-up has them.)
boolean CKernel::RunMapping (unsigned nSlot, boolean bOverGame)
{
	static const char *Names[4] = {"A", "B", "START", "SELECT"};
	static const unsigned Slot[4] = {MAP_FIRST_BUTTON + 0, MAP_FIRST_BUTTON + 1,
					 MAP_FIRST_BUTTON + 4, MAP_FIRST_BUTTON + 5};
	const unsigned nSteps = 4;

	TBind Binds[MAP_COUNT];
	memset (Binds, 0, sizeof Binds);
	static char Values[4][16];
	const char *pValues[4];
	for (unsigned i = 0; i < 4; i++)
	{
		Values[i][0] = '\0';
		pValues[i] = Values[i];
	}

	unsigned nStep = 0;
	boolean bWaitRelease = TRUE;		// e.g. A still held from the SYSTEM menu
	u32 nKeyPrev = ~0u;
	unsigned nFrame = 0;

	char Line[120];
	snprintf (Line, sizeof Line, "Controls: setting up pad %u (%04x:%04x)",
		  nSlot + 1, m_pInput->GetVendor (nSlot), m_pInput->GetProduct (nSlot));
	Log (Line, COLOR_TEXT);

	char Title[32], Footer[40];
	snprintf (Title, sizeof Title, "CONTROLS  PLAYER %u", m_pInput->GetPlayerOfPad (nSlot) + 1);
	snprintf (Footer, sizeof Footer, "PAD %04X:%04X", m_pInput->GetVendor (nSlot),
		  m_pInput->GetProduct (nSlot));

	while (nStep < nSteps)
	{
		if (nFrame++ % 15 == 0)
		{
			m_pInput->UpdateDevices ();
			if (!m_pInput->IsPadConnected (nSlot))
			{
				Log ("  pad unplugged: set-up cancelled", COLOR_TEXT);
				return FALSE;
			}
		}

		u32 nKey = m_pInput->GetKeyboard ();
		u32 nKeyNew = nKeyPrev == ~0u ? 0 : (nKey & ~nKeyPrev);
		nKeyPrev = nKey;
		if (nKeyNew & BTN_MENU)
		{
			Log ("  set-up cancelled", COLOR_TEXT);
			return FALSE;
		}

		if (bWaitRelease)
		{
			bWaitRelease = !m_pInput->IsReleased (nSlot);
		}
		else
		{
			int nButton = m_pInput->CaptureButton (nSlot);
			if (nButton >= 0)
			{
				boolean bUsed = FALSE;
				for (unsigned i = 0; i < nStep; i++)
				{
					if (Binds[Slot[i]].nIndex == (u8) nButton)
					{
						bUsed = TRUE;
					}
				}
				if (!bUsed)
				{
					TBind &B = Binds[Slot[nStep]];
					B.nType = BindButton;
					B.nIndex = (u8) nButton;
					B.nValue = 0;
					CInput::DescribeBind (B, Values[nStep], sizeof Values[nStep]);
					nStep++;
				}
				bWaitRelease = TRUE;
			}
		}

		m_pUI->DrawMapping (bOverGame, Title, (int) nStep, pValues, Footer);
		ShowUIFrame ();
	}

	m_pInput->SetMap (nSlot, Binds);
	SavePads ();
	for (unsigned i = 0; i < nSteps; i++)
	{
		snprintf (Line, sizeof Line, "  %s = %s", Names[i], Values[i]);
		Log (Line, COLOR_TEXT);
	}
	Log ("  controls saved to nespi/pads.cfg", COLOR_TEXT);
	SaveLog ();

	// "SAVED" for one second
	for (unsigned f = 0; f < 60; f++)
	{
		m_pUI->DrawMapping (bOverGame, Title, (int) nSteps, pValues, Footer);
		ShowUIFrame ();
	}
	return TRUE;
}

boolean CKernel::PowerOffCountdown (void)
{
	// 3 seconds, B cancels; drawn over whatever the UI buffer shows
	static u32 Saved[UI_SRC_PITCH * UI_SRC_LINES];
	memcpy (Saved, m_pUI->GetBuffer (), sizeof Saved);
	u32 nPrev = ~0u;
	for (unsigned f = 0; f < 180; f++)
	{
		u32 n = m_pInput->GetAny ();
		u32 nNew = nPrev == ~0u ? 0 : (n & ~nPrev);
		nPrev = n;
		if (nNew & BTN_B)
		{
			memcpy (m_pUI->GetBuffer (), Saved, sizeof Saved);
			Log ("  power off cancelled", COLOR_TEXT);
			return FALSE;
		}
		char Line[24];
		snprintf (Line, sizeof Line, "POWER OFF IN %u", 3 - f / 60);
		memcpy (m_pUI->GetBuffer (), Saved, sizeof Saved);
		static const TButtonHint Cancel = {'B', "CANCEL"};
		m_pUI->DrawBox (Line, "", &Cancel);
		ShowUIFrame ();
	}
	return TRUE;
}

void CKernel::PowerOff (void)
{
	Log ("Power off: saving data and halting", COLOR_TITLE);
	SaveGameData ();
	SaveLog ();
	f_mount (0, "SD:", 0);
	m_bSDOK = FALSE;

	// black picture, then halt (the firmware stops the HDMI output)
	m_Video.Clear (0, RGB (0, 0, 0));
	m_Video.Clear (1, RGB (0, 0, 0));
	m_Video.ShowBuffer (0);
	CTimer::SimpleMsDelay (300);

	CBcmWatchdog Watchdog;
	Watchdog.Restart (CBcmWatchdog::PartitionHalt);
	while (1)
	{
	}
}

// ---- game list ------------------------------------------------------------

unsigned CKernel::RunList (unsigned nSel)
{
	u32 nPrev = ~0u;		// require a release before acting
	unsigned nRepeat = 0;
	unsigned nFrame = 0;

	while (1)
	{
		if (nFrame++ % 15 == 0)
		{
			m_pInput->UpdateDevices ();
			m_pInput->FlushDiagnostics ();

			// a pad never seen before: set up its controls first
			int nPad = m_pInput->GetUnmappedPad ();
			if (nPad >= 0)
			{
				RunMapping ((unsigned) nPad, m_nLoaded >= 0);
				nPrev = ~0u;
				continue;
			}
		}

		u32 n = m_pInput->GetAny ();
		u32 nNew = n & ~nPrev;

		// up/down with auto-repeat, left/right = page
		u32 nMove = nNew;
		if (n & (BTN_UP | BTN_DOWN))
		{
			if (++nRepeat > 20 && nRepeat % 4 == 0)
			{
				nMove |= n & (BTN_UP | BTN_DOWN);
			}
		}
		else
		{
			nRepeat = 0;
		}
		if ((nMove & BTN_UP) && nSel > 0)			nSel--;
		if ((nMove & BTN_DOWN) && nSel + 1 < m_nGames)		nSel++;
		if (nNew & BTN_LEFT)	nSel = nSel >= 8 ? nSel - 8 : 0;
		if (nNew & BTN_RIGHT)	nSel = nSel + 8 < m_nGames ? nSel + 8 : m_nGames - 1;

		if (nNew & (BTN_A | BTN_START))
		{
			return nSel;
		}
		if (nNew & BTN_SELECT)
		{
			RunSystemMenu ();
			n = ~0u;		// wait for release
		}
		nPrev = n;

		const char *pMsg = m_pInput->GetPadCount () == 0 ? "CONNECT A USB GAMEPAD" : "";
		m_pUI->DrawList (m_pGames, m_nGames, nSel, "", m_nLoaded >= 0);
		ShowUIFrame (TRUE, pMsg);
	}
}

// ---- game loop ------------------------------------------------------------

// statistics windows restart after a pause (menu, credits)
void CKernel::ResetGameStats (void)
{
	s_nWindowStart = CTimer::GetClockTicks ();
	s_HistEmu.Reset (); s_HistWork.Reset (); s_HistPeriod.Reset ();
	s_HistLag.Reset (); s_HistSlack.Reset ();
	s_nOverBudget = s_nMissedVSync = s_nWindowFrames = 0;
	DelayStatsReset ();
	DelayRestart ();
	if (s_pRacer)
	{
		s_pRacer->Restart ();
		s_pRacer->ResetStats ();
	}
}

// Credits over the paused game; B (or Esc) goes back to the game
void CKernel::RunCredits (void)
{
	Log ("  credits", COLOR_TEXT);
	u32 nPrev = ~0u;
	for (unsigned nFrame = 0; ; nFrame++)
	{
		if (nFrame % 15 == 0)
		{
			m_pInput->UpdateDevices ();	// a new pad is set up after the credits
		}
		u32 n = m_pInput->GetAny ();
		u32 nNew = nPrev == ~0u ? 0 : (n & ~nPrev);
		if (nPrev == ~0u)
		{
			if (n == 0) nPrev = 0;	// wait for Select and D to be released
		}
		else
		{
			nPrev = n;
		}
		if (nNew & (BTN_B | BTN_MENU))
		{
			return;
		}
		m_pUI->DrawCredits (NEOPI_VERSION);
		ShowUIFrame ();
	}
}

// find the HVS channel and measure it (about 0.7 s)
void CKernel::SetupBeamRacing (void)
{
	s_bRaceReady = FALSE;
	if (s_pRaster == 0)
	{
		s_pRaster = new CRaster (HvsRead, ReadTicks, TicksFreq (), HVS_GEN);
		s_pRacer = new CBeamRacer (s_pRaster);
	}
	if (s_nRaceCfg == 0)
	{
		Log ("Beam racing: off (beamracing=off)", COLOR_TEXT);
		return;
	}
#if (RASPPI == 3 || RASPPI == 4)
	static char Report[600];
	boolean bOK = s_pRaster->Probe (m_Video.GetWidth (), m_Video.GetHeight (), Report, sizeof Report);
	// the report can be long: log it in pieces
	for (const char *p = Report; *p; )
	{
		char Line[MaxLineLen];
		size_t n = strlen (p);
		if (n > MaxLineLen - 3) n = MaxLineLen - 3;
		Line[0] = Line[1] = ' ';
		memcpy (Line + 2, p, n);
		Line[n + 2] = '\0';
		Log (Line, COLOR_DIM);
		p += n;
	}
	if (bOK)
	{
		bOK = s_pRaster->Calibrate (40, Report, sizeof Report);
		Log (Report, bOK ? COLOR_TEXT : COLOR_ERR);
	}
	s_bRaceReady = bOK;
	Log (bOK ? "Beam racing: ready" : "Beam racing: not available, double buffer + frame delay",
	     bOK ? COLOR_OK : COLOR_ERR);
#else
	Log ("Beam racing: not available in this build", COLOR_TEXT);
#endif
}

void CKernel::RunGame (void)
{
	const char *pName = m_pGames[m_nLoaded].Name;

	// status line (Select+C), refreshed once per second
	char StFps[16] = "", StLoad[8] = "", StTemp[8] = "", StRes[8] = "", StScale[16] = "";
	char StDelay[8] = "";
	u32 nFpsColor = UI_WHITE, nLoadColor = UI_OK, nTempColor = UI_WHITE;
	boolean bStatusReady = FALSE;

	// message in the top band (screenshot), in frames
	char Toast[64] = "";
	unsigned nToast = 0, nClearToast = 0;

	boolean bMenu = FALSE;
	unsigned nMenuSel = 0;
	boolean bSaved = FALSE;			// one save state per game
	char MenuMsg[40] = "";
	u32 nMenuMsgColor = UI_OK;

	u32 nPrev[2] = {~0u, ~0u};		// wait for release after the list
	boolean bSelectCombo[2] = {FALSE, FALSE};
	unsigned nSelectPulse[2] = {0, 0};	// a short Select press = Select for 8 frames
	unsigned nSelectHeld[2] = {0, 0};	// frames Select has been held alone
	boolean bLazy[2] = {FALSE, FALSE};	// lazy finger (turbo on A-D), Select+B
	unsigned nLazyCount[2][4] = {{0}};	// frames each of A-D has been held
	u32 nMenuPrev = ~0u;
	m_pInput->RestartDiagnostics ();		// log the pad reports of this game too

	unsigned nFrames = 0;
	unsigned nSeconds = 0;		// temperature: read every 4 s (firmware call)
	unsigned nTemp = 0;
	m_ActLED.On ();			// steady while playing (no firmware call every second)
	unsigned nWinStart = CTimer::GetClockTicks ();
	unsigned nWorkMax = 0;
	unsigned nLastVSync = CTimer::GetClockTicks ();
	unsigned nNextDeadline = nLastVSync + 16667;
	unsigned nTotalFrames = 0;

	s_HistEmu.Reset (); s_HistWork.Reset (); s_HistPeriod.Reset ();
	s_HistLag.Reset (); s_HistSlack.Reset ();
	s_nOverBudget = s_nMissedVSync = s_nWindowFrames = 0;
	s_nWindowStart = nLastVSync;
	DelayStatsReset ();
	DelayRestart ();
	s_bRaceGame = s_bRaceReady;
	m_Video.SetSource (GameSource ());
	if (s_bRaceGame)
	{
		s_pRacer->SetGeometry (m_Video.GetGameY (), m_Video.GetCPUScale (), m_Video.GetHVSFactor (), s_NesSource.nH);
		s_pRacer->Restart ();
		s_pRacer->ResetStats ();
	}
	{
		unsigned nOld;
		m_pInput->TakeChange (&nOld);	// a press from the list does not count
	}
	{
		unsigned a, b, c;
		int d;
		m_Audio.GetStats (&a, &b, &c, &d);	// reset audio statistics
	}

	while (1)
	{
		unsigned t0 = CTimer::GetClockTicks ();
		unsigned t1 = t0;

		if (nTotalFrames % 30 == 0)
		{
			m_pInput->UpdateDevices ();

			int nPad = bMenu ? -1 : m_pInput->GetUnmappedPad ();
			if (nPad >= 0)
			{
				// new pad during the game: pause and set it up
				SnapshotForUI ();
				m_Video.SetRacing (FALSE);
				RunMapping ((unsigned) nPad, TRUE);
				ResetGameStats ();
				nPrev[0] = nPrev[1] = ~0u;
				unsigned nOld;
				m_pInput->TakeChange (&nOld);	// presses during the set-up do not count
			}
		}

		// beam racing: start the frame (and read the pads) when the plan says,
		// against the HDMI scan-out; the time spent waiting does not count as work
		const boolean bRace = s_bRaceGame && !bMenu;
		if (!bMenu)
		{
			m_Video.SetSource (GameSource ());
		}
		m_Video.SetRacing (bRace);
		unsigned nGoUs = 0;
		if (bRace)
		{
			u64 nStart = s_pRacer->PlanFrame (ReadTicks ());
			unsigned tWait = CTimer::GetClockTicks ();
			while ((s64) (ReadTicks () - nStart) < 0)
			{
				s_pRaster->Sample ();
			}
			s_pRacer->FrameGo (ReadTicks ());
			nGoUs = CTimer::GetClockTicks ();
			t0 += nGoUs - tWait;
			t1 = t0;
		}
		// frame delay: start the frame (and read the pads) s_nDelayUs after the
		// vsync; the time spent waiting does not count as work
		else if (!bMenu && s_nDelayUs > 0)
		{
			unsigned tGo = nLastVSync + s_nDelayUs;
			unsigned tWait = CTimer::GetClockTicks ();
			while ((int) (CTimer::GetClockTicks () - tGo) < 0)
			{
			}
			t0 += CTimer::GetClockTicks () - tWait;
			t1 = t0;
		}
		boolean bOddFrame = FALSE;	// slow for its own reasons: not a measure of the game

		// read controls as late as possible, right before emulating the frame
		unsigned nChangeTicks;
		boolean bChange = m_pInput->TakeChange (&nChangeTicks);
		u32 Pad[2] = {m_pInput->Get (0), m_pInput->Get (1)};

		if (!bMenu)
		{
			boolean bOpenMenu = FALSE, bOpenCredits = FALSE, bScreenshot = FALSE;
			u32 Eff[2] = {0, 0};
			for (unsigned p = 0; p < 2; p++)
			{
				u32 n = Pad[p];
				if (nPrev[p] == ~0u)
				{
					// buttons still held from the list or the menu: ignore until released
					if (n == 0)
					{
						nPrev[p] = 0;
					}
					s_Pad[p] = 0;
					continue;
				}

				u32 nNew = n & ~nPrev[p];

				if (n & BTN_SELECT)
				{
					// Select + A: screenshot, + B: lazy finger, + Down: status line, + Up: credits
					if (nNew & (BTN_A | BTN_B | BTN_UP | BTN_DOWN))
					{
						bSelectCombo[p] = TRUE;
					}
					if (nNew & BTN_A)
					{
						bScreenshot = TRUE;
					}
					if (nNew & BTN_B)
					{
						bLazy[p] = !bLazy[p];
						snprintf (Toast, sizeof Toast, "%sLAZY FINGER %s",
							  p == 0 ? "" : "P2 ", bLazy[p] ? "ON" : "OFF");
						nToast = 60;
						Log (bLazy[p] ? "  lazy finger on" : "  lazy finger off", COLOR_TEXT);
					}
					if (nNew & BTN_DOWN)
					{
						m_bStatusOn = !m_bStatusOn;
						m_nClearStatus = 2;
					}
					if (nNew & BTN_UP)
					{
						bOpenCredits = TRUE;
					}
					// Select held alone for one second: menu (no coin)
					if (!bSelectCombo[p] && ++nSelectHeld[p] >= 60)
					{
						bOpenMenu = TRUE;
						bSelectCombo[p] = TRUE;
					}
				}
				else
				{
					if ((nPrev[p] & BTN_SELECT) && !bSelectCombo[p])
					{
						nSelectPulse[p] = 8;	// short press: the NES Select button
					}
					bSelectCombo[p] = FALSE;
					nSelectHeld[p] = 0;
				}
				if (nNew & BTN_MENU)
				{
					bOpenMenu = TRUE;
				}
				nPrev[p] = n;
				Eff[p] = n;

				// Lazy finger (Select+B): a held A or B repeats, 2 frames pressed and
				// 2 released (15 per second), starting pressed at once
				u32 nGame = n;
				static const u32 LazyBtn[2] = {BTN_A, BTN_B};
				for (unsigned b = 0; b < 2; b++)
				{
					if (!(n & LazyBtn[b]))
					{
						nLazyCount[p][b] = 0;
					}
					else if (bLazy[p] && (nLazyCount[p][b]++ & 3) >= 2)
					{
						nGame &= ~LazyBtn[b];
					}
				}

				// NES pad; while Select is held nothing goes through (Select + a
				// button or Up / Down are NES-Pi's own combinations)
				unsigned nPad = 0;
				if (!(n & BTN_SELECT))
				{
					if (n & BTN_UP)			nPad |= NES_UP;
					if (n & BTN_DOWN)		nPad |= NES_DOWN;
					if (n & BTN_LEFT)		nPad |= NES_LEFT;
					if (n & BTN_RIGHT)		nPad |= NES_RIGHT;
					if (nGame & BTN_A)		nPad |= NES_A;
					if (nGame & BTN_B)		nPad |= NES_B;
					if (nGame & BTN_START)		nPad |= NES_START;
				}
				if (nSelectPulse[p])
				{
					nPad |= NES_SELECT;
					nSelectPulse[p]--;
				}
				s_Pad[p] = nPad;
			}

			if (bOpenMenu || bOpenCredits)
			{
				Eff[0] = Eff[1] = 0;		// nothing reaches the game while they open
			}

			if (bOpenCredits)
			{
				// Select + D: credits over the paused game (B closes)
				s_Pad[0] = s_Pad[1] = 0;
				SnapshotForUI ();
				m_Video.SetRacing (FALSE);
				RunCredits ();
				nPrev[0] = nPrev[1] = ~0u;
				bSelectCombo[0] = bSelectCombo[1] = FALSE;
				nToast = 0;
				nClearToast = m_nClearStatus = 2;
				ResetGameStats ();
				continue;
			}
			if (bOpenMenu)
			{
				bMenu = TRUE;
				nMenuSel = 0;
				MenuMsg[0] = '\0';
				nMenuPrev = ~0u;
				nToast = 0;
				s_Pad[0] = s_Pad[1] = 0;
				{
					char Path[160], Ext[8];
					StateExt (Ext, 0);
					GamePath (Path, sizeof Path, Ext);
					FILINFO Info;
					bSaved = f_stat (Path, &Info) == FR_OK;
				}
				SnapshotForUI ();
				m_Video.SetRacing (FALSE);
				SaveGameData ();
				m_pInput->FlushDiagnostics ();
				ReportWindow (TRUE);
				SaveLog ();
			}
			else
			{
				if (bScreenshot)
				{
					bOddFrame = TRUE;
					// the frame on screen, before emulating the next one
					const char *pFile = SaveScreenshot ();
					snprintf (Toast, sizeof Toast, pFile ? "SCREENSHOT SAVED  %s" : "SCREENSHOT FAILED",
						  pFile);
					nToast = 60;
				}
				if (bRace)
				{
					s_bRaceFrame = TRUE;
					s_nRaceWaitTicks = 0;
					s_pRacer->RowArrived (0, ReadTicks ());	// (the first wait is in the hook)
				}
				NesRunFrame ();
				if (bRace)
				{
					s_bRaceFrame = FALSE;
					if (bOddFrame)
					{
						s_pRacer->Restart ();	// the screenshot took the time
					}
					else
					{
						s_pRacer->FrameEnd ();
					}
					s_pRaster->EndFrame ();
					// the waits for the beam are not work: as if the frame had started later
					t0 += (unsigned) s_pRaster->TicksToUs ((s64) s_nRaceWaitTicks);
					if (s_pRacer->Failed ())
					{
						s_bRaceGame = FALSE;
						Log ("Beam racing: rows late in too many frames, double buffer for this game",
						     COLOR_ERR);
					}
				}
				t1 = CTimer::GetClockTicks ();
				m_Audio.WriteFrame (s_AudioBuf, AUDIO_FRAME);
			}
		}

		if (bMenu)
		{
			unsigned Ids[GmCount], nIds = 0;
			Ids[nIds++] = GmSave;		// RESUME is the B button
			Ids[nIds++] = GmLoad;
			Ids[nIds++] = GmReset;
			Ids[nIds++] = GmList;
			Ids[nIds++] = GmPower;
			if (nMenuSel >= nIds) nMenuSel = 0;

			u32 n = Pad[0] | Pad[1];
			u32 nNew = nMenuPrev == ~0u ? 0 : (n & ~nMenuPrev);
			nMenuPrev = n;
			unsigned nId = Ids[nMenuSel];

			if (nNew & BTN_UP)	{ nMenuSel = (nMenuSel + nIds - 1) % nIds; MenuMsg[0] = '\0'; }
			if (nNew & BTN_DOWN)	{ nMenuSel = (nMenuSel + 1) % nIds; MenuMsg[0] = '\0'; }

			if (nNew & BTN_B)
			{
				bMenu = FALSE;		// resume
			}
			else if (nNew & BTN_A)
			{
				char Path[160], Ext[8];
				StateExt (Ext, 0);
				GamePath (Path, sizeof Path, Ext);

				switch (nId)
				{
				case GmSave:
					if (SaveState (Path))
					{
						bSaved = TRUE;
						snprintf (MenuMsg, sizeof MenuMsg, "SAVED");
						nMenuMsgColor = UI_OK;
					}
					else
					{
						snprintf (MenuMsg, sizeof MenuMsg, "SAVE FAILED");
						nMenuMsgColor = UI_ERR;
					}
					Log (bSaved ? "  save state: saved" : "  save state: FAILED", COLOR_TEXT);
					break;

				case GmLoad:
					if (!bSaved)
					{
						break;			// disabled until something is saved
					}
					if (LoadState (Path))
					{
						Log ("  save state: loaded", COLOR_TEXT);
						bMenu = FALSE;
					}
					else
					{
						snprintf (MenuMsg, sizeof MenuMsg, "LOAD FAILED");
						nMenuMsgColor = UI_ERR;
					}
					break;

				case GmReset:
					nes_reset (1);
					Log ("  reset", COLOR_TEXT);
					bMenu = FALSE;
					break;

				case GmList:
					SaveGameData ();
					SaveLog ();
					m_Video.SetRacing (FALSE);
					return;

				case GmPower:
					if (PowerOffCountdown ())
					{
						PowerOff ();
					}
					nMenuPrev = ~0u;
					break;
				}

			}
			if (!bMenu)
			{
				nPrev[0] = nPrev[1] = ~0u;	// ignore buttons until released
				bSelectCombo[0] = bSelectCombo[1] = FALSE;
				ResetGameStats ();
			}

			if (bMenu)
			{
				static const char *Labels[GmCount] =
				{
					"SAVE STATE", "LOAD STATE", "RESET",
					"GAME LIST", "POWER OFF"
				};
				TMenuItem Items[GmCount];
				for (unsigned i = 0; i < nIds; i++)
				{
					Items[i].pLabel = Labels[Ids[i]];
					Items[i].pValue = 0;
					Items[i].bDisabled = Ids[i] == GmLoad && !bSaved;
						}
				const char *pMsg = MenuMsg;
				u32 nMsgColor = nMenuMsgColor;
				static const TButtonHint Hints[2] = {{'A', "SELECT"}, {'B', "RESUME"}};
				m_pUI->DrawPanelMenu (TRUE, pName, Items, nIds, nMenuSel, pMsg, nMsgColor,
						      Hints, 2, 0);
				m_Video.SetSource (s_UISource);
				m_Video.DrawGame (m_pUI->GetBuffer ());
			}
			else
			{
				m_Video.SetSource (GameSource ());	// the menu closes: the paused game
				m_Video.DrawGame (GameBuffer ());
			}
		}

		// black bands: status line at the bottom, messages at the top (only while playing)
		if (m_bStatusOn && !bMenu && bStatusReady)
		{
			const CVideoOut::TTextSpan Spans[] =
			{
				{StFps, nFpsColor}, {"   LOAD ", UI_GREY}, {StLoad, nLoadColor},
				{"   ", UI_GREY}, {StTemp, nTempColor},
				{s_bRaceGame ? "   RACE " : "   DLY ", UI_GREY}, {StDelay, UI_WHITE},
				{"   ", UI_GREY}, {StRes, UI_WHITE}, {StScale, UI_GREY}
			};
			m_Video.DrawBandText (FALSE, Spans, sizeof Spans / sizeof Spans[0]);
			m_nClearStatus = 2;
		}
		else if (m_nClearStatus)
		{
			m_Video.DrawBandText (FALSE, 0, 0);
			m_nClearStatus--;
		}
		if (nToast && !bMenu)
		{
			const CVideoOut::TTextSpan Span = {Toast, UI_WHITE};
			m_Video.DrawBandText (TRUE, &Span, 1);
			nToast--;
			nClearToast = 2;
		}
		else if (nClearToast)
		{
			m_Video.DrawBandText (TRUE, 0, 0);
			nClearToast--;
		}
		unsigned t2 = CTimer::GetClockTicks ();
		const boolean bRaced = bRace && !bMenu;	// already on screen: no flip, no vsync
		if (!bRaced)
		{
			m_Video.Flip ();
		}
		unsigned t3 = CTimer::GetClockTicks ();

		if (bRaced)
		{
			nNextDeadline = t3 + 16667;	// the next frame start paces the loop
		}
		else if (m_bVSync)
		{
			m_Video.WaitForVSync ();
		}
		else
		{
			while ((int) (CTimer::GetClockTicks () - nNextDeadline) < 0)
			{
			}
			nNextDeadline += 16667;
		}
		unsigned t4 = CTimer::GetClockTicks ();
		unsigned nPeriod = t4 - nLastVSync;
		nLastVSync = t4;
		nTotalFrames++;

		if (!bMenu)
		{
			s_HistEmu.Add (t1 - t0);
			s_HistWork.Add (t2 - t0);
			s_HistPeriod.Add (nPeriod);
			if (bRaced)
			{
				// pad report -> the HVS starts composing the top of the picture
				if (bChange)
				{
					s_HistLag.Add (nGoUs - nChangeTicks
						       + (unsigned) s_pRaster->TicksToUs ((s64) s_pRacer->GetPadToPicture ()));
				}
			}
			else
			{
				s_HistSlack.Add (t4 - t3);
				if (!bOddFrame)
				{
					DelayUpdate (t3 - t0, nPeriod > 25000);
				}
				if (bChange)
				{
					s_HistLag.Add (t4 - nChangeTicks);
				}
			}
			if (t3 - t0 > 16000) s_nOverBudget++;
			if (nPeriod > 25000) s_nMissedVSync++;
			s_nWindowFrames++;
			if (s_nWindowFrames >= REPORT_FRAMES)
			{
				ReportWindow (FALSE);
				m_pInput->FlushDiagnostics ();
			}
		}

		// status line, once per second
		if (!bMenu && t2 - t0 > nWorkMax)
		{
			nWorkMax = t2 - t0;
		}
		if (++nFrames == 60)
		{
			unsigned nElapsed = t4 - nWinStart;
			unsigned nFPS100 = (unsigned) (6000ULL * 1000000 / nElapsed);

			if (m_bVSync && nFPS100 > 7500)
			{
				m_bVSync = FALSE;
				nNextDeadline = CTimer::GetClockTicks () + 16667;
				Log ("firmware vsync does not block: switching to timer pacing", COLOR_ERR);
			}

			// 60 FPS / 59.8 FPS, load = worst frame of the second vs 16.7 ms
			if (nFPS100 >= 5995)
				snprintf (StFps, sizeof StFps, "%u FPS", (nFPS100 + 50) / 100);
			else
				snprintf (StFps, sizeof StFps, "%u.%u FPS", nFPS100 / 100, (nFPS100 % 100) / 10);
			nFpsColor = nFPS100 < 5990 ? UI_WARN : UI_WHITE;
			unsigned nLoad = nWorkMax * 100 / 16667;
			snprintf (StLoad, sizeof StLoad, "%u%%", nLoad);
			nLoadColor = nLoad > 95 ? UI_ERR : (nLoad >= 75 ? UI_WARN : UI_OK);
			if (nSeconds++ % 4 == 0)
			{
				nTemp = m_CPUThrottle.GetTemperature ();
			}
			snprintf (StTemp, sizeof StTemp, "%u" FONT_DEGREE "C", nTemp);
			nTempColor = nTemp > 75 ? UI_ERR : (nTemp > 65 ? UI_WARN : UI_WHITE);
			snprintf (StRes, sizeof StRes, "%uP", m_Video.GetHeight ());
			if (s_bRaceGame)
			{
				unsigned us = (unsigned) s_pRaster->TicksToUs ((s64) s_pRacer->GetPadToPicture ());
				snprintf (StDelay, sizeof StDelay, "%u.%u", us / 1000, (us % 1000) / 100);
			}
			else
			{
				snprintf (StDelay, sizeof StDelay, "%u.%u", s_nDelayUs / 1000, (s_nDelayUs % 1000) / 100);
			}
			unsigned k = m_Video.GetHVSFactor (), S = m_Video.GetScale ();
			snprintf (StScale, sizeof StScale, " %uX %s", S,
				  k == S ? "HVS" : (k == 1 ? "CPU" : "CPU+HVS"));
			bStatusReady = TRUE;

			m_CPUThrottle.Update ();	// does its firmware calls every 4 s
			nFrames = 0;
			nWorkMax = 0;
			nWinStart = CTimer::GetClockTicks ();
		}
	}
}

// ---- main flow ------------------------------------------------------------

TShutdownMode CKernel::Run (void)
{
	if (!m_bVideoOK)
	{
		Fatal (NEOPI_ERR_FB, "cannot set up the HDMI output");
	}

	char Line[180];
	Log ("nes-pi - bare metal NES emulator", COLOR_TITLE);
	snprintf (Line, sizeof Line, "Version %s, built %s %s", NEOPI_VERSION, __DATE__, __TIME__);
	Log (Line, COLOR_DIM);

	CMachineInfo *pInfo = CMachineInfo::Get ();
	m_CPUThrottle.SetSpeed (CPUSpeedMaximum);
	snprintf (Line, sizeof Line, "%s, %u MB RAM, CPU %u MHz, temperature %u C",
		  pInfo->GetMachineName (), pInfo->GetRAMSize (),
		  m_CPUThrottle.GetClockRate () / 1000000, m_CPUThrottle.GetTemperature ());
	Log (Line, COLOR_TEXT);
	snprintf (Line, sizeof Line, "HDMI %ux%u, pixel order %s", m_Video.GetWidth (), m_Video.GetHeight (),
		  m_Video.GetPixelOrder () ? "RGB" : "BGR");
	Log (Line, COLOR_TEXT);

	// SD card
	if (!m_EMMC.Initialize ())
	{
		Fatal (NEOPI_ERR_SD, "SD card not initialised");
	}
	if (f_mount (&m_FileSystem, "SD:", 1) != FR_OK)
	{
		Fatal (NEOPI_ERR_SD, "cannot read the SD card file system");
	}
	m_bSDOK = TRUE;
	f_mkdir (NESPI_DIR);
	f_mkdir (SAVE_DIR);
	f_mkdir (SHOT_DIR);
	LoadConfig ();
	m_nFilter = s_nTvMode == NTSC_OFF && s_bTvScanlines ? FILTER_SCAN_LIGHT : FILTER_NONE;	// TV FILTER decides

	m_pUI = new CUI;		// no console font on the NES: the built-in one

	// games
	ScanGames ();
	snprintf (Line, sizeof Line, "Games found: %u", m_nGames);
	Log (Line, m_nGames ? COLOR_OK : COLOR_ERR);
	if (m_nGames == 0)
	{
		Fatal (NEOPI_ERR_GAME, "no .nes file in the nespi folder");
	}

	// USB and audio
	Log ("USB...", COLOR_TEXT);
	if (!m_USBHCI.Initialize ())
	{
		Log ("  USB controller not initialised", COLOR_ERR);
	}
	m_pInput = new CInput (&m_USBHCI, &m_DeviceNameService);
	LoadPads ();
	m_pInput->UpdateDevices ();

	if (m_Audio.Initialize (&m_Interrupt))
	{
		Log ("Audio HDMI: OK (48 kHz)", COLOR_OK);
	}
	else
	{
		Log ("HDMI audio: not available, playing without sound", COLOR_ERR);
	}

	SetupCoreOnce ();

	// last played game
	unsigned nSel = 0;
	{
		FIL File;
		char Last[64];
		UINT nRead = 0;
		if (f_open (&File, LAST_PATH, FA_READ) == FR_OK)
		{
			f_read (&File, Last, sizeof Last - 1, &nRead);
			f_close (&File);
			Last[nRead] = '\0';
			for (unsigned i = 0; i < m_nGames; i++)
			{
				if (strcmp (m_pGames[i].File, Last) == 0)
				{
					nSel = i;
				}
			}
		}
	}

	FILINFO Info;
	boolean bBench = f_stat (BENCH_FLAG, &Info) == FR_OK;
	if (bBench)
	{
		if (LoadGame (nSel))
		{
			Benchmark ();
		}
		CTimer::SimpleMsDelay (8000);		// time to read the results on screen
	}
	SaveLog ();

	// from here on the screen belongs to the UI
	ApplyVideoMode ();
	SetupBeamRacing ();
	SaveLog ();
	m_bQuiet = TRUE;

	boolean bDirect = m_nGames == 1;
	while (1)
	{
		if (!bDirect)
		{
			nSel = RunList (nSel);
		}
		bDirect = FALSE;

		if (m_nLoaded != (int) nSel && !LoadGame (nSel))
		{
			m_pUI->DrawMessage ("CANNOT LOAD GAME", m_pGames[nSel].Name, "SEE nespi/LOG.TXT");
			SaveLog ();
			for (unsigned i = 0; i < 180; i++)
			{
				ShowUIFrame ();
			}
			continue;
		}
		SaveLog ();
		RunGame ();
		m_bQuiet = TRUE;
	}

	return ShutdownHalt;
}
