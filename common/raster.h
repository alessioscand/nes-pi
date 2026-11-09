/*
 * where the HDMI picture is being read, for beam racing (Pi 3 and 4)
 *
 * The Pi's hardware scaler (HVS) builds the HDMI picture one line at a time,
 * reading our framebuffer as it goes; SCALER_DISPSTATX(channel) bits 11:0 say
 * which output line it is composing (Linux: vc4_crtc_get_scanout_position).
 * This is exactly what beam racing needs: a framebuffer line must be written
 * before the HVS composes the first output line that uses it.
 *
 * CRaster finds the HVS channel that drives the HDMI mode, then measures for
 * a few dozen frames, polling the register as fast as it can:
 *   - the frame period and the time of one output line;
 *   - for every output line L, how long after the frame origin the HVS
 *     starts composing it (the first lines are composed early, during the
 *     vertical blank, to fill the HVS FIFO: measured, not guessed).
 * The frame origin is where the line counter would be 0 if it ran at the
 * same pace as during the active picture. From then on everything is time
 * (ARM generic timer ticks), and the origin is corrected while playing from
 * the register readings (Sample()).
 *
 * Plain C++, no Circle classes: the same file is built on Linux with a
 * simulated HVS for testing (test/race_sim.cpp).
 */
#ifndef RASTER_H
#define RASTER_H

#ifdef PROJECT_HOST
#include <stdint.h>
#include <stddef.h>
typedef uint32_t u32;
typedef uint64_t u64;
typedef int64_t s64;
typedef int32_t s32;
typedef int boolean;
#define TRUE 1
#define FALSE 0
#else
#include <circle/types.h>
#endif

#define RASTER_MAX_LINES	4096

class CRaster
{
public:
	// register access and time source (on the Pi: HVS registers, CNTPCT_EL0)
	typedef u32 TReadReg (unsigned nOffset);
	typedef u64 TReadTicks (void);

	// nGen: HVS generation, 4 = Pi 3 (BCM2837), 5 = Pi 4 (BCM2711)
	CRaster (TReadReg *pReadReg, TReadTicks *pReadTicks, u64 nTicksPerSec, unsigned nGen = 5);

	// find the HVS channel that outputs nDispW x nDispH; report: what was seen
	boolean Probe (unsigned nDispW, unsigned nDispH, char *pReport, size_t nSize);
	// measure for nFrames (about 16.7 ms each, busy); FALSE = do not race
	boolean Calibrate (unsigned nFrames, char *pReport, size_t nSize);
	boolean IsReady (void) const		{ return m_bReady; }

	u64 Ticks (void) const			{ return (*m_pReadTicks) (); }
	u64 TicksPerSec (void) const		{ return m_nTicksPerSec; }
	u64 UsToTicks (u64 nUs) const		{ return nUs * m_nTicksPerSec / 1000000; }
	s64 TicksToUs (s64 nTicks) const	{ return nTicks * 1000000 / (s64) m_nTicksPerSec; }
	s64 TicksToNs (s64 nTicks) const	{ return nTicks * 1000000000 / (s64) m_nTicksPerSec; }
	unsigned ReadLine (void) const;		// raw line counter of the channel
	unsigned GetChannel (void) const	{ return m_nChannel; }

	// time model (valid after Calibrate)
	u64 Period (void) const			{ return m_nPeriod; }
	u64 LineTicks (void) const		{ return (m_nLine16 + 0x8000) >> 16; }
	u64 Origin (s64 nFrame) const		{ return m_nT0 + (u64) (nFrame * (s64) m_nPeriod); }
	s64 FrameAt (u64 nTicks) const;		// frame whose origin is the last one <= nTicks
	// ticks after the frame origin at which output line nLine starts being composed
	s64 ComposeOffset (unsigned nLine) const;

	// while playing: read the register and correct the origin (cheap)
	void Sample (void);
	void EndFrame (void);			// apply the corrections of the last frame
	s64 GetLastCorrection (void) const	{ return m_nLastCorr; }
	unsigned GetResyncs (void) const	{ return m_nResyncs; }

private:
	void Track (u64 nTicks, unsigned nLine);

private:
	TReadReg	*m_pReadReg;
	TReadTicks	*m_pReadTicks;
	u64		 m_nTicksPerSec;
	unsigned	 m_nGen;

	boolean		 m_bReady;
	unsigned	 m_nChannel;
	unsigned	 m_nDispH;
	unsigned	 m_nLo, m_nHi;		// line window where the counter runs linearly

	u64		 m_nPeriod;		// ticks per frame
	u64		 m_nLine16;		// ticks per output line, 16.16
	u64		 m_nT0;			// origin of frame 0
	s32		 m_Offset[RASTER_MAX_LINES];

	// tracking
	boolean		 m_bHaveErr;
	s64		 m_nMinErr;
	s64		 m_nLastCorr;
	unsigned	 m_nResyncs;
	u64		 m_nLastSampleLine;
};

#endif
