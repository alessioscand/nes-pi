/*
 * Beam racing: one emulated frame per HDMI frame, raced against the HVS
 * (raster.h)
 *
 * - frame start (core 0): wait until deadline(row 0) - lead - vblank budget,
 *   then read the pads and run the frame (vertical blank first);
 * - each visible row (core 0, line hook): wait until deadline(row) - lead;
 * - core 1 draws the row into the shown framebuffer; a row past its deadline
 *   is "late" (a one-frame tear).
 * deadline(row): when the HVS starts the first output line that may read it.
 * lead and the vblank budget follow the slowest recent frame (grow at once,
 * shrink slowly); too many late rows: Failed(), back to double buffering.
 * Plain C++, also built on Linux (test/race_sim.cpp).
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
	void Restart (void);			// new game or new drawing: large margins again
	void Resume (void);			// after a pause: the margins it had
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
