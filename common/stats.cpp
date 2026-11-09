/*
 * Timing statistics (see stats.h)
 */
#include "stats.h"

extern "C" int snprintf (char *pBuffer, size_t nSize, const char *pFormat, ...);

TPlayStats g_Stats;

void FormatMs (char *pOut, size_t nSize, unsigned nMicros)
{
	snprintf (pOut, nSize, "%u.%02u", nMicros / 1000, (nMicros % 1000) / 10);
}

void ComputeStats (const unsigned *pValues, unsigned nCount, TStats *pStats)
{
	static unsigned Sorted[STATS_MAX_VALUES];
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

unsigned THisto::P99 (void) const
{
	unsigned nTarget = nCount - nCount / 100, n = 0;
	for (unsigned b = 0; b < Bins; b++)
	{
		n += Bin[b];
		if (n >= nTarget) return b * 100 + 100;
	}
	return nMax;
}

void TPlayStats::Reset (unsigned nNow)
{
	Emu.Reset (); Work.Reset (); Period.Reset ();
	Lag.Reset (); Slack.Reset ();
	nOverBudget = nMissedVSync = nFrames = 0;
	nStart = nNow;
}
