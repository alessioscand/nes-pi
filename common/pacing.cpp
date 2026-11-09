/*
 * Frame delay (see pacing.h)
 */
#include "pacing.h"

#define DELAY_GUARD_US	3000
#define DELAY_MAX_US	6000

TFrameDelay g_Delay = {-1, 0, FRAME_US, 0, 0, 0, 0, 0};

void DelayRestart (void)
{
	g_Delay.nPeak = FRAME_US;
	g_Delay.nUs = g_Delay.nCfg > 0 ? (unsigned) g_Delay.nCfg * 1000 : 0;
}

void DelayStatsReset (void)
{
	g_Delay.nSum = 0;
	g_Delay.nMin = ~0u;
	g_Delay.nMax = g_Delay.nCount = g_Delay.nMisses = 0;
}

void DelayUpdate (unsigned nWork, boolean bMissed)
{
	TFrameDelay &D = g_Delay;
	D.nSum += D.nUs;
	D.nCount++;
	if (D.nUs < D.nMin) D.nMin = D.nUs;
	if (D.nUs > D.nMax) D.nMax = D.nUs;

	if (D.nCfg >= 0)
	{
		return;				// off or fixed
	}
	if (bMissed && D.nUs > 0)
	{
		D.nMisses++;
		DelayRestart ();
		return;
	}
	D.nPeak -= D.nPeak >> 10;
	if (nWork > D.nPeak)
	{
		D.nPeak = nWork;
	}
	unsigned nFree = D.nPeak / 2 > DELAY_GUARD_US ? D.nPeak / 2 : DELAY_GUARD_US;
	unsigned nNeed = D.nPeak + nFree;
	unsigned nDelay = nNeed < FRAME_US ? FRAME_US - nNeed : 0;
	D.nUs = nDelay < DELAY_MAX_US ? nDelay : DELAY_MAX_US;
}
