/*
 * Input lag without beam racing
 *
 * Frame phase: a frame starts at PHASE_LINE, after the picture, so the
 * vertical blank (pads read) comes first and the next vsync shows the answer.
 * Frame delay: the frame starts a few ms after the vsync, so the pads are
 * read later. framedelay=auto: grows slowly (~17 s) up to DELAY_MAX_US while
 * frames are light, keeping half the slowest recent frame plus
 * DELAY_GUARD_US free; drops at once on a heavier frame, to 0 on a missed
 * vsync. framedelay=off, or N (1-12) ms fixed, for tests.
 */
#ifndef PACING_H
#define PACING_H

#include <circle/types.h>
#include "project.h"

#define PHASE_LINE	PROJECT_PHASE_LINE
#define FRAME_US	16667

struct TFrameDelay
{
	int		nCfg;		// -1 auto, 0 off, N ms fixed
	unsigned	nUs;		// delay before the next frame
	unsigned	nPeak;		// slowest recent frame (decays)
	unsigned	nMisses;	// vsyncs missed with a delay active (auto)
	u64		nSum;		// statistics since DelayStatsReset
	unsigned	nMin, nMax, nCount;
};

extern TFrameDelay g_Delay;

void DelayRestart (void);
void DelayStatsReset (void);
void DelayUpdate (unsigned nWork, boolean bMissed);	// nWork: pads to flip done

#endif
