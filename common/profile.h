/*
 * sampling profiler for the benchmark
 *
 * A hardware timer interrupt (ARM_IRQ_TIMER1) fires every few hundred
 * microseconds and records the interrupted program counter. Samples are
 * collected per frame and added to two tables of 16-byte code buckets: one
 * for all frames, one for slow frames only. The tables are written to
 * PROFILE.TXT as "address all slow" lines; addresses are resolved to
 * function names offline with the matching kernel .elf file.
 */
#ifndef PROFILE_H
#define PROFILE_H

#include <circle/interrupt.h>
#include <circle/usertimer.h>
#include <circle/types.h>

class CProfiler
{
public:
	CProfiler (CInterruptSystem *pInterrupt);
	~CProfiler (void);

	boolean Start (unsigned nIntervalMicros);
	void Stop (void);

	void BeginFrame (void);
	void EndFrame (boolean bSlow);	// adds this frame's samples to the tables

	unsigned GetTotal (void) const	{ return m_nTotal; }
	unsigned GetSlow (void) const	{ return m_nSlowTotal; }
	unsigned GetLost (void) const	{ return m_nLost; }
	unsigned GetOutside (void) const { return m_nOutside; }

	// "address all slow" text, nonzero buckets only; returns length
	size_t Format (char *pOut, size_t nSize) const;

private:
	static void TimerHandler (CUserTimer *pTimer, void *pParam);

private:
	enum { FrameMax = 1024, Shift = 4 };

	CUserTimer	 m_Timer;
	unsigned	 m_nInterval;
	boolean		 m_bRunning;

	volatile unsigned m_nFrameCount;
	uintptr		 m_FramePC[FrameMax];

	uintptr		 m_nBase, m_nEnd;
	unsigned	 m_nBuckets;
	unsigned	*m_pAll;
	unsigned	*m_pSlow;

	unsigned	 m_nTotal, m_nSlowTotal, m_nLost, m_nOutside;
};

#endif
