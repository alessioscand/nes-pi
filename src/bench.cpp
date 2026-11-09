/*
 * nes-pi - BENCH: 30 s of play, then 60 s measured; the results on screen
 * and in BENCH.TXT (and PROFILE.TXT)
 */
#include "kernel.h"
#include "frame.h"
#include "stats.h"
#include "smp.h"
#include "profile.h"

#define BENCH_PROFILE_US 200		// profiler: one sample every 200 us
#define BENCH_SLOW_US	16000		// frames from 16 ms on count as slow

void CKernel::Benchmark (void)
{
	static unsigned Emu[BENCH_FRAMES], Draw[BENCH_FRAMES], Total[BENCH_FRAMES];
	u64 nSamples = 0;

	boolean bQuiet = m_bQuiet;
	ApplyVideoMode ();		// measure the real drawing path (filter, HVS)
	m_Video.SetSource (FrameSource ());
	m_bQuiet = TRUE;

	// on screen once per second (outside the timings): the game and, above it,
	// what the benchmark is doing, so it is clear that it is not stuck
	auto Progress = [this] (const char *pPhase, unsigned nFrame, unsigned nFrames)
	{
		char Text[64];
		snprintf (Text, sizeof Text, "BENCHMARK  %s %u/%u S", pPhase,
			  nFrame / 60, nFrames / 60);
		const CScreen::TTextSpan Span = {Text, UI_WHITE};
		m_Video.DrawBandText (TRUE, &Span, 1);
		m_Video.Flip ();
	};

	Log ("Benchmark: warm-up", COLOR_TEXT);
	SaveLog ();

	// 1. warm-up: let the BIOS logo and the game's start-up pass
	for (unsigned i = 0; i < BENCH_WARMUP; i++)
	{
		FrameRun ();
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
		FrameRun ();			// emulation and drawing go together,
		unsigned t1 = CTimer::GetClockTicks ();	// line by line
		unsigned t2 = t1;
		boolean bSlow = t2 - t0 >= BENCH_SLOW_US;
		if (bProfile) pProfiler->EndFrame (bSlow);

		// hash of every measured frame (outside the timings): the same game and
		// build options must give the same value
		for (unsigned p = 0; p < NES_W * NES_H; p++)
		{
			nAllHash = (nAllHash ^ g_VideoBuf[p]) * 16777619U;
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
		  PROJECT_VERSION, pInfo->GetMachineName (), pInfo->GetRAMSize (),
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
		nHash = (nHash ^ g_VideoBuf[i]) * 16777619U;
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
