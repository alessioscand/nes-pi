/*
 * sampling profiler for the benchmark (see profile.h)
 */
#include "profile.h"
#include <circle/memorymap.h>
#include <circle/util.h>
#include "project.h"

extern "C" {
extern char _etext[];
int snprintf (char *pBuffer, size_t nSize, const char *pFormat, ...);
}

CProfiler::CProfiler (CInterruptSystem *pInterrupt)
:	m_Timer (pInterrupt, TimerHandler, this),
	m_nInterval (200),
	m_bRunning (FALSE),
	m_nFrameCount (0),
	m_nBase (MEM_KERNEL_START),
	m_nEnd ((uintptr) _etext),
	m_pAll (0),
	m_pSlow (0),
	m_nTotal (0), m_nSlowTotal (0), m_nLost (0), m_nOutside (0)
{
	m_nBuckets = (unsigned) ((m_nEnd - m_nBase) >> Shift) + 1;
}

CProfiler::~CProfiler (void)
{
	Stop ();
	delete [] m_pAll;
	delete [] m_pSlow;
}

boolean CProfiler::Start (unsigned nIntervalMicros)
{
	if (!m_pAll)
	{
		m_pAll = new unsigned[m_nBuckets];
		m_pSlow = new unsigned[m_nBuckets];
		if (!m_pAll || !m_pSlow)
		{
			return FALSE;
		}
	}
	memset (m_pAll, 0, m_nBuckets * sizeof (unsigned));
	memset (m_pSlow, 0, m_nBuckets * sizeof (unsigned));
	m_nTotal = m_nSlowTotal = m_nLost = m_nOutside = 0;
	m_nFrameCount = 0;

	m_nInterval = nIntervalMicros;
	if (!m_Timer.Initialize ())
	{
		return FALSE;
	}
	m_bRunning = TRUE;
	m_Timer.Start (m_nInterval);
	return TRUE;
}

void CProfiler::Stop (void)
{
	if (m_bRunning)
	{
		m_bRunning = FALSE;
		m_Timer.Stop ();
	}
}

void CProfiler::BeginFrame (void)
{
	m_nFrameCount = 0;
}

void CProfiler::EndFrame (boolean bSlow)
{
	unsigned nCount = m_nFrameCount;	// samples arriving now land in the next frame
	if (nCount > FrameMax)
	{
		m_nLost += nCount - FrameMax;
		nCount = FrameMax;
	}
	for (unsigned i = 0; i < nCount; i++)
	{
		uintptr pc = m_FramePC[i];
		if (pc < m_nBase || pc >= m_nEnd)
		{
			m_nOutside++;
			continue;
		}
		unsigned b = (unsigned) ((pc - m_nBase) >> Shift);
		m_pAll[b]++;
		m_nTotal++;
		if (bSlow)
		{
			m_pSlow[b]++;
			m_nSlowTotal++;
		}
	}
	m_nFrameCount = 0;
}

void CProfiler::TimerHandler (CUserTimer *pTimer, void *pParam)
{
	CProfiler *pThis = (CProfiler *) pParam;

	// no FIQ is in use, so ELR_EL1 still holds the interrupted address
	uintptr pc;
	asm volatile ("mrs %0, elr_el1" : "=r" (pc));

	unsigned n = pThis->m_nFrameCount;
	if (n < FrameMax)
	{
		pThis->m_FramePC[n] = pc;
	}
	pThis->m_nFrameCount = n + 1;

	if (pThis->m_bRunning)
	{
		pTimer->Start (pThis->m_nInterval);
	}
}

size_t CProfiler::Format (char *pOut, size_t nSize) const
{
	size_t nPos = 0;
	nPos += snprintf (pOut + nPos, nSize - nPos,
			  "# " PROJECT_NAME " profile: bucket = 16 bytes, interval %u us\r\n"
			  "# samples %u (slow frames %u), outside kernel text %u, lost %u\r\n"
			  "# address all slow\r\n",
			  m_nInterval, m_nTotal, m_nSlowTotal, m_nOutside, m_nLost);
	for (unsigned b = 0; b < m_nBuckets && nPos + 40 < nSize; b++)
	{
		if (m_pAll[b] == 0)
		{
			continue;
		}
		nPos += snprintf (pOut + nPos, nSize - nPos, "%lx %u %u\r\n",
				  (unsigned long) (m_nBase + ((uintptr) b << Shift)),
				  m_pAll[b], m_pSlow[b]);
	}
	return nPos;
}
