/*
 * The per-minute report in LOG.TXT (CKernel: the project's kernel.h)
 */
#include "crtfx.h"
#include "kernel.h"
#include "frame.h"
#include "mcline.h"
#include "pacing.h"
#include "racing.h"
#include "stats.h"
#include "tvcore.h"

// statistics windows restart after a pause (menu, credits)
void CKernel::ResetGameStats (void)
{
	g_Stats.Reset (CTimer::GetClockTicks ());
	g_Stats.bSkipFrame = TRUE;
	DelayStatsReset ();
	DelayRestart ();
	if (g_Race.pRacer)
	{
		if (m_bVideoChanged)		// OPTIONS changed the drawing
		{
			g_Race.pRacer->SetGeometry (m_Video.GetGameY (), m_Video.GetCPUScale (), m_Video.GetHVSFactor ());
			g_Race.pRacer->Restart ();
		}
		else
		{
			g_Race.pRacer->Resume ();
		}
		g_Race.pRacer->ResetStats ();
	}
	m_bVideoChanged = FALSE;
	m_Audio.Refill ();
}

void CKernel::ReportWindow (boolean bFinal)
{
	if (g_Stats.nFrames < 60)
	{
		return;
	}
	unsigned nElapsed = CTimer::GetClockTicks () - g_Stats.nStart;
	unsigned nFPS100 = (unsigned) ((u64) g_Stats.nFrames * 100 * 1000000 / nElapsed);
	char ea[16], ep[16], em[16], wa[16], wp[16], pm[16];
	FormatMs (ea, sizeof ea, g_Stats.Emu.Avg ());
	FormatMs (ep, sizeof ep, g_Stats.Emu.P99 ());
	FormatMs (em, sizeof em, g_Stats.Emu.nMax);
	FormatMs (wa, sizeof wa, g_Stats.Work.Avg ());
	FormatMs (wp, sizeof wp, g_Stats.Work.P99 ());
	FormatMs (pm, sizeof pm, g_Stats.Period.nMax);
	unsigned qmin, qmax, und;
	int adj;
	m_Audio.GetStats (&qmin, &qmax, &und, &adj);

	char Tag[8];
	VideoModeTag (Tag, sizeof Tag);
	char Line[MaxLineLen];
	snprintf (Line, sizeof Line,
		  "[%s] %u frames, FPS %u.%02u | emu %s/%s/%s ms | emu+draw %s/%s ms (>16ms: %u)",
		  bFinal ? "partial" : "1 min", g_Stats.nFrames, nFPS100 / 100, nFPS100 % 100,
		  ea, ep, em, wa, wp, g_Stats.nOverBudget);
	Log (Line, COLOR_TEXT);
	snprintf (Line, sizeof Line,
		  "   video %s x%u %s | max period %s ms, missed vsyncs %u | audio %u-%u, underruns %u, adj %d | %u MHz %u C",
		  Tag, m_Video.GetScale (), FilterShort (m_Video.GetFilter ()),
		  pm, g_Stats.nMissedVSync, qmin, qmax, und, adj,
		  m_CPUThrottle.GetClockRate () / 1000000, m_CPUThrottle.GetTemperature ());
	Log (Line, COLOR_TEXT);
	if (CrtFxActive ())
	{
		unsigned nSum, nMax, nCalls;
		CrtFxTakeStats (&nSum, &nMax, &nCalls);
		const unsigned nAvg = nCalls ? nSum * 10 / nCalls : 0;
		const unsigned nFrames = nCalls / GAME_H ? nCalls / GAME_H : 1;
		const unsigned f = nSum / nFrames;
		snprintf (Line, sizeof Line, "   %s: per line %u.%u us avg, %u us max | per frame %u.%u ms, %s",
			  FilterShort (m_Video.GetFilter ()), nAvg / 10, nAvg % 10, nMax,
			  f / 1000, (f % 1000) / 100, TvWorkerAlive () ? "on core 2" : "without core 2");
		Log (Line, COLOR_TEXT);
	}
	if (m_Video.GetFilter () == FILTER_COMPOSITE)
	{
		unsigned nFilterUs, nDrawUs, nLines;
		TvTakeStats (&nFilterUs, &nDrawUs, &nLines);
		unsigned nFrames = nLines / GAME_H ? nLines / GAME_H : 1;
		unsigned f = nFilterUs / nFrames, d = nDrawUs / nFrames;
		snprintf (Line, sizeof Line, "   COMPOSITE: per frame %u.%u ms filtering + %u.%u ms drawing, %s",
			  f / 1000, (f % 1000) / 100, d / 1000, (d % 1000) / 100,
			  TvWorkerAlive () ? "on core 2" : "without core 2");
		Log (Line, COLOR_TEXT);
	}

	static boolean s_bFailLogged = FALSE;
	if (McFailed () && !s_bFailLogged)
	{
		Log ("Multicore: core 1 stopped answering, single core from now on", COLOR_ERR);
		char Fail[160];
		McFailText (Fail, sizeof Fail, FrameMcFailFrame ());
		Log (Fail, COLOR_ERR);
		s_bFailLogged = TRUE;
	}
	if (g_Stats.Lag.nCount > 0)
	{
		char la[16], lm[16], lx[16], Ready[64] = "";
		FormatMs (la, sizeof la, g_Stats.Lag.Avg ());
		FormatMs (lm, sizeof lm, g_Stats.Lag.nMin);
		FormatMs (lx, sizeof lx, g_Stats.Lag.nMax);
		if (g_Stats.Slack.nCount > 0)	// only measured on frames that were not beam raced
		{
			char sa[16], sm[16];
			FormatMs (sa, sizeof sa, g_Stats.Slack.Avg ());
			FormatMs (sm, sizeof sm, g_Stats.Slack.nMin);
			snprintf (Ready, sizeof Ready, " | ready before vsync %s/%s ms avg/min", sa, sm);
		}
		snprintf (Line, sizeof Line,
			  "   lag: %u inputs, report->scan-out %s/%s/%s ms avg/min/max%s",
			  g_Stats.Lag.nCount, la, lm, lx, Ready);
		Log (Line, COLOR_TEXT);
	}

	if (g_Race.pRacer && g_Race.pRacer->GetStats ().nFrames > 0)
	{
		const CBeamRacer::TStats &S = g_Race.pRacer->GetStats ();
		const CRaster *R = g_Race.pRaster;
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
		g_Race.pRacer->ResetStats ();
	}
	if (g_Delay.nCount > 0)
	{
		char da[16], dm[16], dx[16];
		FormatMs (da, sizeof da, (unsigned) (g_Delay.nSum / g_Delay.nCount));
		FormatMs (dm, sizeof dm, g_Delay.nMin);
		FormatMs (dx, sizeof dx, g_Delay.nMax);
		char Mode[16];
		if (g_Delay.nCfg < 0)		snprintf (Mode, sizeof Mode, "auto");
		else if (g_Delay.nCfg == 0)	snprintf (Mode, sizeof Mode, "off");
		else				snprintf (Mode, sizeof Mode, "fixed %d ms", g_Delay.nCfg);
		snprintf (Line, sizeof Line,
			  "   frame delay %s: %s/%s/%s ms avg/min/max, vsyncs missed with delay %u | frame starts at line %u",
			  Mode, da, dm, dx, g_Delay.nMisses, PHASE_LINE);
		Log (Line, g_Delay.nMisses ? COLOR_ERR : COLOR_TEXT);
	}

	g_Stats.Reset (CTimer::GetClockTicks ());
	DelayStatsReset ();
}
