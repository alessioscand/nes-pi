/*
 * nes-pi - beam racing (Pi 4)
 *
 * One emulated frame per HDMI frame, raced against the HVS (see raster.h):
 *
 *   - frame start (core 0, before the pads are read): wait until
 *       deadline(row 0) - lead - vblank budget
 *     then read the pads and run geo_exec(): the frame starts with the
 *     vertical blank (patches/geolith-phase.py), where games read the pads;
 *   - each visible row (core 0, line hook in the video chip): wait until
 *       deadline(row) - lead
 *     so emulation never runs more than `lead` ahead of the HVS, and a row is
 *     never written while the HVS may still need the previous frame's one;
 *   - core 1 draws each row into the framebuffer being shown and checks it
 *     was there before its deadline (if not: a "late row", the old row is
 *     shown for one frame, a small tear).
 * deadline(row) = when the HVS starts composing the first output line that
 * may read that framebuffer line (one framebuffer line earlier, for the
 * scaler's filter taps, plus 2 lines).
 *
 * lead and the vertical blank budget adapt like the automatic frame delay:
 * they follow the slowest recent frame with a margin, start large and shrink
 * slowly, grow at once. If rows are late in too many frames, Failed() says so
 * and the game goes back to the double-buffered path.
 *
 * Plain C++, built on Linux too (test/race_sim.cpp).
 */
#ifndef RACE_H
#define RACE_H

#include "raster.h"

#define RACE_MAX_ROWS	256

class CBeamRacer
{
public:
	CBeamRacer (CRaster *pRaster);

	// framebuffer line of the first game row, CPU scale m, HVS factor k, rows
	void SetGeometry (unsigned nGameY, unsigned nM, unsigned nK, unsigned nRows);
	void Restart (void);			// new game, after a pause: large margins again
	void Rearm (void);			// a game started from the list: try again after Failed()

	// core 0: frame start; returns the time to wait for (ticks)
	u64 PlanFrame (u64 nNow);
	void FrameGo (u64 nNow);		// the wait is over: pads read now
	u64 RowRelease (unsigned nRow) const	{ return m_Deadline[nRow] - m_nLead; }
	void RowArrived (unsigned nRow, u64 nNow)
	{
		if (nRow == 0) m_nVBTime = nNow - m_nGoTime;
	}
	// row nRow is in the framebuffer (from the TV filter's cores 1-3, possibly
	// several at once and out of order: atomic updates)
	void RowDone (unsigned nRow, u64 nNow)
	{
		s64 nSlack = (s64) (m_Deadline[nRow] - nNow);
		s64 nMin = __atomic_load_n (&m_nMinSlack, __ATOMIC_RELAXED);
		while (   nSlack < nMin
		       && !__atomic_compare_exchange_n (&m_nMinSlack, &nMin, nSlack, TRUE,
							__ATOMIC_RELAXED, __ATOMIC_RELAXED))
		{
		}
		if (nSlack < 0) __atomic_fetch_add (&m_nLateRowsFrame, 1, __ATOMIC_RELAXED);
		__atomic_fetch_add (&m_nRowsDone, 1, __ATOMIC_RELAXED);
	}
	void FrameEnd (void);			// core 0, after core 1 has finished

	boolean Failed (void) const		{ return m_bFailed; }

	// statistics since the last ResetStats (times in ticks)
	struct TStats
	{
		unsigned nFrames, nLateFrames, nLateRows, nSkips;
		u64 nLeadSum, nLeadMin, nLeadMax;
		u64 nVBSum;
		u64 nPadSum, nPadMax;		// pads read -> HVS starts the top row
		s64 nSlackMin;
	};
	const TStats &GetStats (void) const	{ return m_Stats; }
	void ResetStats (void);
	u64 GetLead (void) const		{ return m_nLead; }
	u64 GetPadToPicture (void) const	{ return m_nPadToTop; }
	s64 GetFrame (void) const		{ return m_nFrame; }

private:
	CRaster *m_pRaster;
	unsigned m_nRows;
	unsigned m_Need[RACE_MAX_ROWS];		// output line that sets each row's deadline

	volatile u64 m_Deadline[RACE_MAX_ROWS];
	volatile s64 m_nMinSlack;
	volatile unsigned m_nLateRowsFrame, m_nRowsDone;

	boolean	m_bStarted;
	s64	m_nFrame;			// HDMI frame of the current emulated frame
	u64	m_nLead, m_nVBBudget;
	u64	m_nNeedPeak, m_nVBPeak;
	u64	m_nGoTime, m_nVBTime, m_nPadToTop;
	unsigned m_nWindowFrames, m_nWindowLate;
	boolean	m_bFailed;
	TStats	m_Stats;
};

#endif
