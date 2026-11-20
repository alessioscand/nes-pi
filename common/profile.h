/*
 * Sampling profiler for the benchmark: a timer IRQ (ARM_IRQ_TIMER1) records
 * the interrupted PC in 16-byte buckets, for all frames and for slow ones;
 * PROFILE.TXT lists "address all slow" (resolve with the kernel's .elf).
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
