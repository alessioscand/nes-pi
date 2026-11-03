/*
 * neo-pi - host test of the beam racing logic (src/raster.cpp, src/race.cpp)
 * against a simulated HVS and a simulated emulator.
 *
 *   g++ -O2 -DNEOPI_HOST -I src -o race_sim test/race_sim.cpp src/raster.cpp src/race.cpp
 *   ./race_sim [seconds] [emu_us_per_line] [spike_us] [fifo_lines]
 *
 * HVS model (1080p60, CEA timings: 1125 lines, 2200 x 1125 at 148.5 MHz):
 *   - the pixel valve (PV) scans active lines 0..1079, then 45 lines of blank;
 *   - at VSTART (start of the blank, after the front porch) the HVS restarts
 *     its line counter and fills its FIFO (FIFO lines, 1 us each), then stalls;
 *   - during the active picture it composes line PV + FIFO + 1;
 *   - after the last line it shows the last line until the next VSTART.
 * The clock ratio is off by +50 ppm, to exercise the tracking.
 * Ground truth: compose start time of every output line of every frame.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <initializer_list>
#include "raster.h"
#include "race.h"

static double TPS = 54e6;			// ARM generic timer: Pi 4 54 MHz, Pi 3 19.2 MHz
static int g_Pi3 = 0;				// PI3=1: HVS 4 registers, 19.2 MHz timer
static double g_Now = 0;			// simulated time, ticks
static double g_Frame;				// PV frame period, ticks
static double g_Line;				// PV line, ticks
static int g_Fifo = 12;
static const int H = 1080, VT = 1125, FP = 4;
static unsigned g_Reads;
static double g_SpikeP = 0.002;

static double FrameStart (long f) { return f * g_Frame; }
// time the HVS starts composing output line L of PV frame f (f's active area)
static double Compose (long f, int L)
{
	double vstart = FrameStart (f - 1) + (H + FP) * g_Line;	// blank before frame f
	if (L <= g_Fifo) return vstart + L * TPS * 1e-6;
	return FrameStart (f) + (L - g_Fifo - 1) * g_Line;
}
static unsigned LineAt (double t)
{
	long f = (long) floor (t / g_Frame);
	// which frame's composition are we in? after VSTART of f+1 -> frame f+1
	if (t >= FrameStart (f) + (H + FP) * g_Line) f++;
	double vstart = FrameStart (f - 1) + (H + FP) * g_Line;
	double us = TPS * 1e-6;
	if (t < vstart + g_Fifo * us) return (unsigned) ((t - vstart) / us);
	if (t < FrameStart (f)) return g_Fifo;
	int L = (int) ((t - FrameStart (f)) / g_Line) + g_Fifo + 1;
	return L > H ? H : L;
}

static u32 ReadReg (unsigned off)
{
	g_Reads++;
	g_Now += TPS * (g_Pi3 ? 300e-9 : 185e-9);	// one register read
	if (off == 0x40 + 0x10 * 1)		// ch1: 1080p, on
		return g_Pi3 ? 0x80000000u | (1920u << 12) | 1080u : 0x80000000u | (1920u << 16) | 1080u;
	if (off == 0x48 + 0x10 * 1) return (2u << 30) | LineAt (g_Now);
	return 0;
}
static u64 ReadTicks (void) { g_Now += TPS * 37e-9; return (u64) g_Now; }

static double Rand (void) { return rand () / (double) RAND_MAX; }

int main (int argc, char **argv)
{
	int nSecs = argc > 1 ? atoi (argv[1]) : 60;
	double nEmuLine = argc > 2 ? atof (argv[2]) : 30;	// us per emulated line
	double nSpike = argc > 3 ? atof (argv[3]) : 0;	// occasional extra us on one line
	if (argc > 4) g_Fifo = atoi (argv[4]);
	if (argc > 5) g_SpikeP = atof (argv[5]);
	if (getenv ("PI3")) { g_Pi3 = 1; TPS = 19.2e6; }
	g_Frame = TPS / 60.0 * (1 + 50e-6);
	g_Line = g_Frame / VT;
	g_Now = 12345;
	srand (1);

	CRaster R (ReadReg, ReadTicks, (u64) TPS, g_Pi3 ? 4 : 5);
	char Rep[600];
	boolean bProbe = R.Probe (1920, 1080, Rep, sizeof Rep);
	printf ("%s\n", Rep);
	if (!bProbe) return 1;
	boolean bCal = R.Calibrate (30, Rep, sizeof Rep);
	printf ("%s\n", Rep);
	if (!bCal) return 1;

	// compare the measured compose offsets with the truth for a few lines
	{
		long f = (long) floor (R.Origin (0) / g_Frame + 0.5);
		for (int L : {0, 5, 12, 13, 20, 92, 540, 1000, 1079})
		{
			double truth = Compose (f, L) - (double) R.Origin (0);
			printf ("  line %4d: measured %+8.2f us, truth %+8.2f us\n", L,
				R.ComposeOffset (L)  / (TPS / 1e6), truth  / (TPS / 1e6));
		}
	}

	CBeamRacer Race (&R);
	const unsigned GameY = 23, M = 1, K = 4;	// 1080p: 480x270 framebuffer, 4x HVS
	Race.SetGeometry (GameY, M, K, 224);
	Race.Restart ();

	unsigned nFrames = (unsigned) (nSecs * 60), nLateTruth = 0, nEarlyTruth = 0;
	double nPadSum = 0, nPadMax = 0;
	for (unsigned fr = 0; fr < nFrames; fr++)
	{
		g_Now += 300 * (TPS / 1e6);			// audio, status line, loop (300 us)
		u64 tStart = Race.PlanFrame (R.Ticks ());
		while ((s64) (R.Ticks () - tStart) < 0) R.Sample ();
		u64 tGo = R.Ticks ();
		Race.FrameGo (tGo);
		long hdmi = (long) floor ((double) R.Origin (Race.GetFrame ()) / g_Frame + 0.5);

		// vertical blank lines 248..263, 0..23 (40 lines)
		for (int i = 0; i < 40; i++) g_Now += nEmuLine * (TPS / 1e6) * (0.8 + 0.4 * Rand ());
		for (unsigned r = 0; r < 224; r++)
		{
			u64 tRel = Race.RowRelease (r);
			Race.RowArrived (r, R.Ticks ());
			while ((s64) (R.Ticks () - tRel) < 0) R.Sample ();
			double emu = nEmuLine * (0.8 + 0.4 * Rand ());
			if (nSpike > 0 && Rand () < g_SpikeP) emu += nSpike;
			g_Now += emu * (TPS / 1e6);
			double tDone = g_Now + 15 * (TPS / 1e6);		// core 1 draws and copies (15 us)
			Race.RowDone (r, (u64) tDone);
			// truth: before the HVS needs it this frame, after it was used last frame
			int first = (GameY + r * M) * K - 1;		// one line earlier (filter)
			int last = (GameY + r * M + M) * K;		// one line later
			if (tDone > Compose (hdmi, first)) nLateTruth++;
			if (tDone < Compose (hdmi - 1, last > H - 1 ? H - 1 : last) + g_Line) nEarlyTruth++;
			if (r == 0)
			{
				double pad = (Compose (hdmi, GameY * K) - (double) tGo)  / (TPS / 1e6);
				nPadSum += pad;
				if (pad > nPadMax) nPadMax = pad;
			}
		}
		Race.FrameEnd ();
		R.EndFrame ();
		if (fr % 600 == 599 || fr == 59)
		{
			const CBeamRacer::TStats &S = Race.GetStats ();
			printf ("t=%3us lead %.0f us (min %.0f max %.0f), vblank %.0f us, pads->top %.0f us (max %.0f), "
				"late rows %u, skips %u, slack min %.0f us | truth: late %u, early %u\n",
				(fr + 1) / 60, S.nLeadSum / (double) S.nFrames / (TPS / 1e6), S.nLeadMin  / (TPS / 1e6),
				S.nLeadMax  / (TPS / 1e6), S.nVBSum / (double) S.nFrames / (TPS / 1e6),
				S.nPadSum / (double) S.nFrames / (TPS / 1e6), S.nPadMax  / (TPS / 1e6), S.nLateRows, S.nSkips,
				S.nSlackMin  / (TPS / 1e6), nLateTruth, nEarlyTruth);
			Race.ResetStats ();
		}
		if (Race.Failed ())
		{
			printf ("FAILED at frame %u\n", fr);
			return 2;
		}
	}
	printf ("pads -> top of picture (truth): avg %.0f us, max %.0f us; model resyncs %u\n",
		nPadSum / nFrames, nPadMax, R.GetResyncs ());
	printf ("late rows (truth) %u, rows overwritten too early (truth) %u\n", nLateTruth, nEarlyTruth);
	return nLateTruth || nEarlyTruth ? 3 : 0;
}
