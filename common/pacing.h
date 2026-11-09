/*
 * Input lag without beam racing: frame phase and frame delay
 *
 * Frame phase: each emulated frame starts at PHASE_LINE, the first line after
 * the picture, so it runs the vertical blank first (the game reads the pads
 * there) and then every visible line; the frame shown at the next vsync
 * already answers the pads read just before it.
 *
 * Frame delay: the frame starts a few ms after the vsync instead of right
 * after it, so the pads are read later and the finished frame waits less
 * before the next vsync. framedelay=auto (default) measures how long each
 * frame takes and only waits what this game has proved it can afford:
 * - the delay starts at 0 and grows slowly (about 17 s time constant) while
 *   the frames stay light, never above DELAY_MAX_US;
 * - it keeps at least half of the slowest recent frame, and DELAY_GUARD_US,
 *   free before the vsync;
 * - one heavier frame lowers it at once; a missed vsync with a delay active
 *   sets it back to 0 and it starts growing again.
 * framedelay=off = no delay; framedelay=N (1-12) = fixed N ms, for testing.
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
