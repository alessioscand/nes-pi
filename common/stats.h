/*
 * Timing statistics: the benchmark's and the per-minute LOG.TXT lines
 */
#ifndef STATS_H
#define STATS_H

#include <circle/types.h>
#include <circle/util.h>
#include <stddef.h>

void FormatMs (char *pOut, size_t nSize, unsigned nMicros);	// "12.34"

struct TStats
{
	unsigned nAvg, nP99, nMax, nMin;
};

#define STATS_MAX_VALUES 3600
void ComputeStats (const unsigned *pValues, unsigned nCount, TStats *pStats);	// nCount <= STATS_MAX_VALUES

// Histogram in 0.1 ms bins (0..50 ms)
struct THisto
{
	enum { Bins = 500 };
	unsigned Bin[Bins];
	unsigned nCount, nMax, nMin;
	u64 nSum;

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
	unsigned P99 (void) const;
};

// the current window of play (one LOG.TXT report per minute)
struct TPlayStats
{
	THisto	 Emu;		// emulation + audio
	THisto	 Work;		// emulation + drawing
	THisto	 Period;	// vsync to vsync
	THisto	 Lag;		// pad report -> start of scan-out of the frame that uses it
	THisto	 Slack;		// frame ready (flip) -> vsync
	unsigned nOverBudget, nMissedVSync, nFrames;
	unsigned nStart;	// CTimer ticks
	boolean	 bSkipFrame;	// first frame after a pause: its times span the pause

	void Reset (unsigned nNow);
};

extern TPlayStats g_Stats;

#endif
