/*
 * where the HDMI picture is being read (see raster.h)
 */
#include "raster.h"

#ifdef PROJECT_HOST
#include <stdio.h>
#include <string.h>
#else
#include <circle/util.h>
extern "C" int snprintf (char *pBuffer, size_t nSize, const char *pFormat, ...);
#endif

// HVS registers (Linux vc4_regs.h): the same on the Pi 3 (HVS 4) and Pi 4
// (HVS 5) except the size fields of DISPCTRLX
#define SCALER_DISPCTRLX(x)	(0x40 + (x) * 0x10)
#define SCALER_DISPSTATX(x)	(0x48 + (x) * 0x10)
#define DISPCTRL_ENABLE		(1u << 31)
#define DISPCTRL5_WIDTH(v)	(((v) >> 16) & 0x1FFF)
#define DISPCTRL5_HEIGHT(v)	((v) & 0x1FFF)
#define DISPCTRL4_WIDTH(v)	(((v) >> 12) & 0xFFF)
#define DISPCTRL4_HEIGHT(v)	((v) & 0xFFF)
#define DISPSTAT_LINE(v)	((v) & 0xFFF)
#define DISPSTAT_MODE(v)	((v) >> 30)

#define MAX_CAL_FRAMES		64

static inline s64 Abs64 (s64 n) { return n < 0 ? -n : n; }

CRaster::CRaster (TReadReg *pReadReg, TReadTicks *pReadTicks, u64 nTicksPerSec, unsigned nGen)
:	m_pReadReg (pReadReg),
	m_pReadTicks (pReadTicks),
	m_nTicksPerSec (nTicksPerSec),
	m_nGen (nGen),
	m_bReady (FALSE),
	m_nChannel (0),
	m_nDispH (0),
	m_nLo (0),
	m_nHi (0),
	m_nPeriod (0),
	m_nLine16 (0),
	m_nT0 (0),
	m_bHaveErr (FALSE),
	m_nMinErr (0),
	m_nLastCorr (0),
	m_nResyncs (0),
	m_nLastSampleLine (0)
{
}

unsigned CRaster::ReadLine (void) const
{
	return DISPSTAT_LINE ((*m_pReadReg) (SCALER_DISPSTATX (m_nChannel)));
}

boolean CRaster::Probe (unsigned nDispW, unsigned nDispH, char *pReport, size_t nSize)
{
	size_t nPos = 0;
	int nFound = -1, nMoving = -1;
	unsigned nMovingCount = 0;
	m_nDispH = nDispH;
	if (nDispH < 200 || nDispH >= RASTER_MAX_LINES - 64)
	{
		snprintf (pReport, nSize, "raster: display height %u not supported", nDispH);
		return FALSE;
	}

	for (unsigned ch = 0; ch < 3; ch++)
	{
		u32 nCtrl = (*m_pReadReg) (SCALER_DISPCTRLX (ch));

		// is its line counter moving? (20 ms)
		unsigned nMin = 0xFFFF, nMax = 0, nChanges = 0;
		u32 nStat = (*m_pReadReg) (SCALER_DISPSTATX (ch));
		unsigned nPrev = DISPSTAT_LINE (nStat);
		u64 nEnd = Ticks () + m_nTicksPerSec / 50;
		while (Ticks () < nEnd)
		{
			nStat = (*m_pReadReg) (SCALER_DISPSTATX (ch));
			unsigned L = DISPSTAT_LINE (nStat);
			if (L < nMin) nMin = L;
			if (L > nMax) nMax = L;
			if (L != nPrev) nChanges++;
			nPrev = L;
		}

		unsigned nW = m_nGen >= 5 ? DISPCTRL5_WIDTH (nCtrl) : DISPCTRL4_WIDTH (nCtrl);
		unsigned nH = m_nGen >= 5 ? DISPCTRL5_HEIGHT (nCtrl) : DISPCTRL4_HEIGHT (nCtrl);
		boolean bMatch =    (nCtrl & DISPCTRL_ENABLE)
				 && nW == nDispW && nH == nDispH
				 && nChanges > 100 && nMax + 8 >= nDispH && nMin < 64;
		if (bMatch && nFound < 0)
		{
			nFound = (int) ch;
		}
		if ((nCtrl & DISPCTRL_ENABLE) && nChanges > 100 && nMax + 8 >= nDispH && nMin < 64)
		{
			nMoving = (int) ch;
			nMovingCount++;
		}
		nPos += snprintf (pReport + nPos, nSize - nPos,
				  "%sch%u ctrl %08X (%s %ux%u) stat %08X lines %u-%u, %u changes%s",
				  ch ? " | " : "raster: ", ch, (unsigned) nCtrl,
				  (nCtrl & DISPCTRL_ENABLE) ? "on" : "off", nW, nH, (unsigned) nStat,
				  nMin, nMax, nChanges, bMatch ? " <-" : "");
		if (nPos >= nSize) nPos = nSize - 1;
	}
	if (nFound < 0 && nMovingCount == 1)
	{
		// size fields not as expected, but only one channel is scanning a
		// picture of this height: use it (the calibration checks it anyway)
		nFound = nMoving;
		if (nPos < nSize)
		{
			snprintf (pReport + nPos, nSize - nPos, " | using ch%d (by its line counter)", nMoving);
		}
	}
	if (nFound < 0)
	{
		return FALSE;
	}
	m_nChannel = (unsigned) nFound;
	return TRUE;
}

boolean CRaster::Calibrate (unsigned nFrames, char *pReport, size_t nSize)
{
	m_bReady = FALSE;
	const unsigned H = m_nDispH;
	const unsigned A = H / 4, B = (3 * H) / 4;
	m_nLo = H / 8;
	m_nHi = H - H / 8;
	if (nFrames > MAX_CAL_FRAMES) nFrames = MAX_CAL_FRAMES;
	if (nFrames < 8) nFrames = 8;

	// ---- pass 1: line time and period from the times the counter crosses A and B
	u64 CrossA[MAX_CAL_FRAMES];
	unsigned nCross = 0;
	u64 nLineSum16 = 0;
	unsigned nLineCount = 0;
	{
		unsigned nPrev = ReadLine ();
		boolean bSeenA = FALSE, bSeenB = FALSE, bStarted = FALSE;
		u64 tA = 0;
		u64 nTimeout = Ticks () + m_nTicksPerSec / 2 + (m_nTicksPerSec / 30) * nFrames;
		while (nCross < 12 && Ticks () < nTimeout)
		{
			u64 t = Ticks ();
			unsigned L = ReadLine ();
			if (L + 4 < nPrev)		// new frame
			{
				bStarted = TRUE;
				bSeenA = bSeenB = FALSE;
			}
			nPrev = L;
			if (!bStarted) continue;
			if (!bSeenA && L >= A)
			{
				bSeenA = TRUE;
				tA = t;
				CrossA[nCross++] = t;
			}
			if (bSeenA && !bSeenB && L >= B)
			{
				bSeenB = TRUE;
				nLineSum16 += ((t - tA) << 16) / (B - A);
				nLineCount++;
			}
		}
		if (nCross < 12 || nLineCount < 8)
		{
			snprintf (pReport, nSize, "raster: calibration failed, %u frames seen in time", nCross);
			return FALSE;
		}
	}
	m_nLine16 = nLineSum16 / nLineCount;
	// period: median of the frame-to-frame differences
	{
		u64 Diff[MAX_CAL_FRAMES];
		unsigned n = nCross - 1;
		for (unsigned i = 0; i < n; i++) Diff[i] = CrossA[i + 1] - CrossA[i];
		for (unsigned i = 1; i < n; i++)
		{
			u64 v = Diff[i]; int j = (int) i - 1;
			while (j >= 0 && Diff[j] > v) { Diff[j + 1] = Diff[j]; j--; }
			Diff[j + 1] = v;
		}
		m_nPeriod = Diff[n / 2];
	}

	// ---- pass 2: origin of each frame and when each line starts being composed
	static u64 First[RASTER_MAX_LINES];
	for (unsigned l = 0; l < RASTER_MAX_LINES; l++)
	{
		m_Offset[l] = 0x7FFFFFFF;
	}
	u64 Origins[MAX_CAL_FRAMES];
	unsigned nOrigins = 0;
	unsigned nMaxSeen = 0;
	{
		unsigned nPrev = ReadLine ();
		boolean bStarted = FALSE;
		unsigned nTop = 0;			// highest line seen in this frame
		u64 nOrigin = ~0ull;
		u64 nTimeout = Ticks () + m_nPeriod * (nFrames + 4);
		while (nOrigins < nFrames && Ticks () < nTimeout)
		{
			unsigned L = ReadLine ();
			u64 t = Ticks ();
			if (L + 4 < nPrev)		// new frame: close the previous one
			{
				if (bStarted && nOrigin != ~0ull && nTop >= m_nHi)
				{
					for (unsigned l = 0; l <= nTop; l++)
					{
						s64 off = (s64) (First[l] - nOrigin);
						if (off < m_Offset[l]) m_Offset[l] = (s32) off;
					}
					if (nTop > nMaxSeen) nMaxSeen = nTop;
					Origins[nOrigins++] = nOrigin;
				}
				bStarted = TRUE;
				nOrigin = ~0ull;
				for (unsigned l = 0; l <= L; l++) First[l] = t;
				nTop = L;
			}
			else if (bStarted && L > nTop && L < RASTER_MAX_LINES)
			{
				for (unsigned l = nTop + 1; l <= L; l++) First[l] = t;
				nTop = L;
			}
			if (bStarted && L >= m_nLo && L <= m_nHi)
			{
				u64 o = t - ((L * m_nLine16) >> 16);
				if (o < nOrigin) nOrigin = o;
			}
			nPrev = L;
		}
	}
	if (nOrigins < nFrames / 2)
	{
		snprintf (pReport, nSize, "raster: calibration failed, %u complete frames", nOrigins);
		return FALSE;
	}
	// period again, from the origins (all consecutive frames)
	{
		u64 nSpan = Origins[nOrigins - 1] - Origins[0];
		u64 nFramesIn = (nSpan + m_nPeriod / 2) / m_nPeriod;
		if (nFramesIn > 0) m_nPeriod = nSpan / nFramesIn;
	}
	m_nT0 = Origins[nOrigins - 1];

	// lines never seen (bottom of the frame): continue the straight line
	for (unsigned l = 0; l < RASTER_MAX_LINES; l++)
	{
		if (l > nMaxSeen || m_Offset[l] == 0x7FFFFFFF)
		{
			m_Offset[l] = (s32) ((l * m_nLine16) >> 16);
		}
	}

	// ---- checks
	u64 nActive = (H * m_nLine16) >> 16;
	unsigned nEarlyLines = 0;		// lines composed ahead of the straight line
	for (unsigned l = 0; l < H; l++)
	{
		s64 nLinear = (s64) ((l * m_nLine16) >> 16);
		if (m_Offset[l] + (s64) LineTicks () < nLinear) nEarlyLines = l + 1;
	}
	s64 nMid = m_Offset[H / 2] - (s64) (((H / 2) * m_nLine16) >> 16);
	boolean bOK =    m_nPeriod > m_nTicksPerSec / 80 && m_nPeriod < m_nTicksPerSec / 45
		      && nActive * 100 > m_nPeriod * 75 && nActive <= m_nPeriod
		      && nMaxSeen + 8 >= H
		      && Abs64 (nMid) < (s64) (4 * LineTicks ());

	unsigned nHz100 = (unsigned) (m_nTicksPerSec * 100 / m_nPeriod);
	snprintf (pReport, nSize,
		  "raster: ch%u, %u frames, period %u us (%u.%02u Hz), line %u ns, active %u%%, "
		  "lines composed early %u (line 0 at %d us), max line %u, middle %d ns: %s",
		  m_nChannel, nOrigins, (unsigned) TicksToUs ((s64) m_nPeriod), nHz100 / 100, nHz100 % 100,
		  (unsigned) TicksToNs ((s64) ((m_nLine16 + 0x8000) >> 16)),
		  (unsigned) (nActive * 100 / m_nPeriod),
		  nEarlyLines, (int) TicksToUs (m_Offset[0]), nMaxSeen,
		  (int) TicksToNs (nMid), bOK ? "OK" : "NOT USABLE");
	m_bReady = bOK;
	m_bHaveErr = FALSE;
	return bOK;
}

s64 CRaster::FrameAt (u64 nTicks) const
{
	s64 d = (s64) (nTicks - m_nT0);
	s64 k = d / (s64) m_nPeriod;
	if (d < 0 && k * (s64) m_nPeriod != d) k--;
	return k;
}

s64 CRaster::ComposeOffset (unsigned nLine) const
{
	if (nLine >= RASTER_MAX_LINES) nLine = RASTER_MAX_LINES - 1;
	return m_Offset[nLine];
}

void CRaster::Sample (void)
{
	unsigned L = ReadLine ();
	u64 t = Ticks ();
	if (L >= m_nLo && L <= m_nHi)
	{
		Track (t, L);
	}
}

void CRaster::Track (u64 nTicks, unsigned nLine)
{
	u64 o = nTicks - ((nLine * m_nLine16) >> 16);
	s64 k = FrameAt (o + m_nPeriod / 2);
	s64 err = (s64) (o - Origin (k));
	if (!m_bHaveErr || err < m_nMinErr)
	{
		m_nMinErr = err;
		m_nLastSampleLine = (u64) k;
	}
	m_bHaveErr = TRUE;
}

void CRaster::EndFrame (void)
{
	if (!m_bHaveErr)
	{
		return;
	}
	m_bHaveErr = FALSE;
	s64 k = (s64) m_nLastSampleLine;
	s64 err = m_nMinErr;
	u64 nOrigin = Origin (k);
	if (Abs64 (err) > (s64) (8 * LineTicks ()))
	{
		// far off (a hiccup in the model): jump to the measured origin
		m_nResyncs++;
		m_nT0 = nOrigin + (u64) err - (u64) (k * (s64) m_nPeriod);
		m_nLastCorr = err;
		return;
	}
	// phase: a quarter of the error; frequency: very slowly
	s64 nPhase = err / 4;
	s64 nFreq = err / 128;
	s64 nMaxFreq = (s64) (m_nPeriod / 2000);
	if (nFreq > nMaxFreq) nFreq = nMaxFreq;
	if (nFreq < -nMaxFreq) nFreq = -nMaxFreq;
	u64 nNewOrigin = nOrigin + (u64) nPhase;
	m_nPeriod = (u64) ((s64) m_nPeriod + nFreq);
	m_nT0 = nNewOrigin - (u64) (k * (s64) m_nPeriod);
	m_nLastCorr = nPhase;
}
