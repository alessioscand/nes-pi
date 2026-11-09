/*
 * beam racing (Pi 3 and 4)
 *
 * One emulated frame per HDMI frame, raced against the HVS (see raster.h):
 *
 *   - frame start (core 0, before the pads are read): wait until
 *       deadline(row 0) - lead - vblank budget
 *     then read the pads and run the frame: it starts with the vertical
 *     blank, where games read the pads;
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
#include "project.h"

#define RACE_ROWS	PROJECT_HEIGHT		// the most rows a picture can have

class CBeamRacer
{
public:
	CBeamRacer (CRaster *pRaster);

	// framebuffer line of the first game row, CPU scale m, HVS factor k, rows
	void SetGeometry (unsigned nGameY, unsigned nM, unsigned nK, unsigned nRows = RACE_ROWS);
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
	// core 1: row nRow is in the framebuffer
	void RowDone (unsigned nRow, u64 nNow)
	{
		s64 nSlack = (s64) (m_Deadline[nRow] - nNow);
		if (nSlack < m_nMinSlack) m_nMinSlack = nSlack;
		if (nSlack < 0) m_nLateRowsFrame++;
		m_nRowsDone++;
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
	unsigned m_Need[RACE_ROWS];		// output line that sets each row's deadline

	volatile u64 m_Deadline[RACE_ROWS];
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
