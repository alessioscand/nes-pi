/*
 * nes-pi - one statistics line per minute of play in LOG.TXT
 */
#include "kernel.h"
#include "frame.h"
#include "pacing.h"
#include "racing.h"
#include "stats.h"
#include "tvcore.h"

// statistics windows restart after a pause (menu, credits)
void CKernel::ResetGameStats (void)
{
	g_Stats.nStart = CTimer::GetClockTicks ();
	g_Stats.Emu.Reset (); g_Stats.Work.Reset (); g_Stats.Period.Reset ();
	g_Stats.Lag.Reset (); g_Stats.Slack.Reset ();
	g_Stats.nOverBudget = g_Stats.nMissedVSync = g_Stats.nFrames = 0;
	DelayStatsReset ();
	DelayRestart ();
	if (g_Race.pRacer)
	{
		g_Race.pRacer->Restart ();
		g_Race.pRacer->ResetStats ();
	}
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

	unsigned nErrors = nes_take_frame_errors ();
	if (nErrors)
	{
		snprintf (Line, sizeof Line, "   Nestopia: %u frames with an error (%s)", nErrors, nes_last_error ());
		Log (Line, COLOR_ERR);
	}

	// TV filter: per frame (filtering vs drawing on the screen)
	unsigned nFilterUs, nDrawUs, nTvLines;
	TvTakeStats (&nFilterUs, &nDrawUs, &nTvLines);
	if (nTvLines >= 224)
	{
		unsigned nFrames = nTvLines / g_NtscSource.nH ? nTvLines / g_NtscSource.nH : 1;
		unsigned f = nFilterUs / nFrames, d = nDrawUs / nFrames;
		snprintf (Line, sizeof Line,
			  "   TV filter: per frame %u.%u ms filtering + %u.%u ms drawing, on 1 core",
			  f / 1000, (f % 1000) / 100, d / 1000, (d % 1000) / 100);
		Log (Line, COLOR_TEXT);
	}

	if (g_Stats.Lag.nCount > 0)
	{
		char la[16], lm[16], lx[16], sa[16], sm[16];
		FormatMs (la, sizeof la, g_Stats.Lag.Avg ());
		FormatMs (lm, sizeof lm, g_Stats.Lag.nMin);
		FormatMs (lx, sizeof lx, g_Stats.Lag.nMax);
		FormatMs (sa, sizeof sa, g_Stats.Slack.Avg ());
		FormatMs (sm, sizeof sm, g_Stats.Slack.nMin);
		snprintf (Line, sizeof Line,
			  "   lag: %u inputs, report->scan-out %s/%s/%s ms avg/min/max | "
			  "ready before vsync %s/%s ms avg/min",
			  g_Stats.Lag.nCount, la, lm, lx, sa, sm);
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
			  "   frame delay %s: %s/%s/%s ms avg/min/max, vsyncs missed with delay %u",
			  Mode, da, dm, dx, g_Delay.nMisses);
		Log (Line, g_Delay.nMisses ? COLOR_ERR : COLOR_TEXT);
	}

	g_Stats.Emu.Reset ();
	g_Stats.Work.Reset ();
	g_Stats.Period.Reset ();
	g_Stats.Lag.Reset ();
	g_Stats.Slack.Reset ();
	g_Stats.nOverBudget = g_Stats.nMissedVSync = g_Stats.nFrames = 0;
	g_Stats.nStart = CTimer::GetClockTicks ();
	DelayStatsReset ();
}
