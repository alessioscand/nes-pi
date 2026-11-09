/*
 * beam racing (see race.h)
 */
#include "race.h"

// margins, in microseconds
#define LEAD_START_US		3000	// first estimate of what a row needs
#define LEAD_MARGIN_US		300
#define LEAD_MIN_US		500
#define LEAD_MAX_US		12000
#define VB_START_US		1500	// first estimate of the vertical blank
#define VB_MARGIN_US		200
#define FAIL_WINDOW		600	// frames (10 s)
#define FAIL_LATE_FRAMES	10	// frames with late rows (or skipped) in a window: give up

CBeamRacer::CBeamRacer (CRaster *pRaster)
:	m_pRaster (pRaster),
	m_nRows (RACE_ROWS),
	m_nMinSlack (0),
	m_nLateRowsFrame (0),
	m_nRowsDone (0),
	m_bStarted (FALSE),
	m_nFrame (0),
	m_nLead (0),
	m_nVBBudget (0),
	m_nNeedPeak (0),
	m_nVBPeak (0),
	m_nGoTime (0),
	m_nVBTime (0),
	m_nPadToTop (0),
	m_nWindowFrames (0),
	m_nWindowLate (0),
	m_bFailed (FALSE)
{
	for (unsigned r = 0; r < RACE_ROWS; r++)
	{
		m_Need[r] = 0;
		m_Deadline[r] = 0;
	}
	ResetStats ();
}

void CBeamRacer::SetGeometry (unsigned nGameY, unsigned nM, unsigned nK, unsigned nRows)
{
	m_nRows = nRows < RACE_ROWS ? nRows : RACE_ROWS;
	for (unsigned r = 0; r < m_nRows; r++)
	{
		int n = (int) ((nGameY + r * nM) * nK) - (int) nK - 2;
		m_Need[r] = n > 0 ? (unsigned) n : 0;
	}
}

void CBeamRacer::Rearm (void)
{
	m_bFailed = FALSE;
	m_nWindowFrames = m_nWindowLate = 0;
}

void CBeamRacer::Restart (void)
{
	const CRaster *R = m_pRaster;
	m_bStarted = FALSE;
	m_nNeedPeak = R->UsToTicks (LEAD_START_US);
	m_nVBPeak = R->UsToTicks (VB_START_US);
	m_nLead = m_nNeedPeak + m_nNeedPeak / 4 + R->UsToTicks (LEAD_MARGIN_US);
	m_nVBBudget = m_nVBPeak + m_nVBPeak / 4 + R->UsToTicks (VB_MARGIN_US);
	m_nWindowFrames = m_nWindowLate = 0;
}

void CBeamRacer::ResetStats (void)
{
	m_Stats.nFrames = m_Stats.nLateFrames = m_Stats.nLateRows = m_Stats.nSkips = 0;
	m_Stats.nLeadSum = m_Stats.nVBSum = m_Stats.nPadSum = m_Stats.nPadMax = 0;
	m_Stats.nLeadMin = ~0ull;
	m_Stats.nLeadMax = 0;
	m_Stats.nSlackMin = 0x7FFFFFFFFFFFFFFFll;
}

u64 CBeamRacer::PlanFrame (u64 nNow)
{
	const CRaster *R = m_pRaster;
	const s64 P = (s64) R->Period ();
	// start of frame k = Origin (k) + c
	s64 c = R->ComposeOffset (m_Need[0]) - (s64) m_nLead - (s64) m_nVBBudget;

	// earliest HDMI frame we can still start on time (a quarter frame late at most)
	s64 kMin = R->FrameAt (nNow - (u64) (P / 4) - (u64) c - 1) + 1;
	s64 k = m_bStarted ? m_nFrame + 1 : kMin;
	if (k < kMin)
	{
		m_Stats.nSkips++;		// behind (a pause, a slow frame): skip ahead
		m_nWindowLate++;
		k = kMin;
	}
	else if (k > kMin + 1)
	{
		k = kMin + 1;			// never more than one frame ahead
	}
	m_nFrame = k;
	m_bStarted = TRUE;

	u64 nOrigin = R->Origin (k);
	for (unsigned r = 0; r < m_nRows; r++)
	{
		m_Deadline[r] = nOrigin + (u64) R->ComposeOffset (m_Need[r]);
	}
	m_nMinSlack = 0x7FFFFFFFFFFFFFFFll;
	m_nLateRowsFrame = 0;
	m_nRowsDone = 0;
	m_nVBTime = 0;
	return nOrigin + (u64) c;
}

void CBeamRacer::FrameGo (u64 nNow)
{
	m_nGoTime = nNow;
	m_nPadToTop = m_Deadline[0] - nNow;
}

void CBeamRacer::FrameEnd (void)
{
	if (m_nRowsDone == 0)
	{
		return;
	}
	s64 nSlack = m_nMinSlack;
	unsigned nLate = m_nLateRowsFrame;

	// statistics
	m_Stats.nFrames++;
	m_Stats.nLeadSum += m_nLead;
	if (m_nLead < m_Stats.nLeadMin) m_Stats.nLeadMin = m_nLead;
	if (m_nLead > m_Stats.nLeadMax) m_Stats.nLeadMax = m_nLead;
	m_Stats.nVBSum += m_nVBTime;
	m_Stats.nPadSum += m_nPadToTop;
	if (m_nPadToTop > m_Stats.nPadMax) m_Stats.nPadMax = m_nPadToTop;
	if (nSlack < m_Stats.nSlackMin) m_Stats.nSlackMin = nSlack;
	if (nLate)
	{
		m_Stats.nLateFrames++;
		m_Stats.nLateRows += nLate;
	}

	// give up if rows are late in too many frames
	m_nWindowFrames++;
	if (nLate) m_nWindowLate++;
	if (m_nWindowLate > FAIL_LATE_FRAMES)
	{
		m_bFailed = TRUE;
	}
	if (m_nWindowFrames >= FAIL_WINDOW)
	{
		m_nWindowFrames = m_nWindowLate = 0;
	}

	// lead: what the worst row needed from its release to the framebuffer
	const CRaster *R = m_pRaster;
	s64 nUsed = (s64) m_nLead - nSlack;
	if (nUsed < 0) nUsed = 0;
	m_nNeedPeak -= m_nNeedPeak >> 10;
	if ((u64) nUsed > m_nNeedPeak) m_nNeedPeak = (u64) nUsed;
	if (nLate && m_nNeedPeak < m_nLead + R->UsToTicks (1000))
	{
		m_nNeedPeak = m_nLead + R->UsToTicks (1000);	// late: a clear step up
	}
	u64 nLead = m_nNeedPeak + m_nNeedPeak / 4 + R->UsToTicks (LEAD_MARGIN_US);
	if (nLead < R->UsToTicks (LEAD_MIN_US)) nLead = R->UsToTicks (LEAD_MIN_US);
	if (nLead > R->UsToTicks (LEAD_MAX_US)) nLead = R->UsToTicks (LEAD_MAX_US);
	m_nLead = nLead;

	// vertical blank budget: from the frame start to the first row
	m_nVBPeak -= m_nVBPeak >> 10;
	if (m_nVBTime > m_nVBPeak) m_nVBPeak = m_nVBTime;
	m_nVBBudget = m_nVBPeak + m_nVBPeak / 4 + R->UsToTicks (VB_MARGIN_US);
}
