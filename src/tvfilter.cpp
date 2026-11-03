/*
 * nes-pi - TV filter on core 1 (see tvfilter.h)
 *
 * Core 1 has a ring of lines, written by core 0 only at the head and by
 * core 1 only at the tail (the same scheme as neo-pi's video chip on core 1):
 * release and acquire on the two counters, dsb + sev to wake the other side
 * from wfe. On a Pi 4 a frame costs core 1 about 3 ms (filter + drawing).
 */
#include "tvfilter.h"
#include "fbmap.h"
#include <circle/multicore.h>
#include <circle/memory.h>
#include <circle/util.h>

#define RING		256			// lines (a frame is at most 240)
#define MAX_WORKERS	1			// core 1

struct TTvLine
{
	u16	Idx[NTSC_IN_W];
	unsigned nPhase;
	u32	*pOut;
	unsigned nRow;
};

struct TTvWorker
{
	TTvLine Ring[RING];
	volatile unsigned nHead;		// written by core 0 only
	volatile unsigned nTail;		// written by the worker only
	volatile boolean bAlive;
} __attribute__ ((aligned (64)));

static TTvWorker s_Worker[MAX_WORKERS];
static unsigned s_nWorkers = 0;		// running (0 = everything on core 0)
static unsigned s_nNext = 0;		// next worker to get a line
static int s_nMode = NTSC_OFF;
static TTvLineOut *s_pOut = 0;

static inline u64 Ticks (void)
{
	u64 v;
	__asm__ volatile ("isb; mrs %0, cntpct_el0" : "=r" (v));
	return v;
}

static inline u64 Freq (void)
{
	u64 v;
	__asm__ volatile ("mrs %0, cntfrq_el0" : "=r" (v));
	return v;
}

// time spent per worker: filtering, drawing (for LOG.TXT)
static volatile u64 s_nFilterTicks[MAX_WORKERS], s_nDrawTicks[MAX_WORKERS];
static volatile unsigned s_nLines[MAX_WORKERS];

static void Process (TTvLine *e, unsigned nWorker)
{
	u64 t0 = Ticks ();
	ntsc_blargg_line (e->Idx, e->nPhase, e->pOut);
	u64 t1 = Ticks ();
	if (s_pOut)
	{
		s_pOut (e->nRow, nWorker);
	}
	u64 t2 = Ticks ();
	s_nFilterTicks[nWorker] += t1 - t0;
	s_nDrawTicks[nWorker] += t2 - t1;
	s_nLines[nWorker]++;
}

void TvTakeStats (unsigned *pFilterUs, unsigned *pDrawUs, unsigned *pLines)
{
	u64 f = 0, d = 0;
	unsigned n = 0;
	for (unsigned i = 0; i < MAX_WORKERS; i++)
	{
		f += s_nFilterTicks[i]; d += s_nDrawTicks[i]; n += s_nLines[i];
		s_nFilterTicks[i] = s_nDrawTicks[i] = 0; s_nLines[i] = 0;
	}
	*pFilterUs = (unsigned) (f * 1000000 / Freq ());
	*pDrawUs = (unsigned) (d * 1000000 / Freq ());
	*pLines = n;
}

static void Worker (unsigned nWorker)
{
	TTvWorker *w = &s_Worker[nWorker];
	w->bAlive = TRUE;
	__asm__ volatile ("dsb ish; sev" ::: "memory");
	for (;;)
	{
		unsigned t = w->nTail;
		while (__atomic_load_n (&w->nHead, __ATOMIC_ACQUIRE) == t)
		{
			__asm__ volatile ("wfe");
		}
		Process (&w->Ring[t & (RING - 1)], nWorker);
		__atomic_store_n (&w->nTail, t + 1, __ATOMIC_RELEASE);
		__asm__ volatile ("dsb ish; sev" ::: "memory");
	}
}

class CTvCores : public CMultiCoreSupport
{
public:
	CTvCores (void) : CMultiCoreSupport (CMemorySystem::Get ()) {}
	void Run (unsigned nCore)
	{
		if (nCore >= 1 && nCore <= MAX_WORKERS)
		{
			FbSetMair ();		// the framebuffer's write-combining type
			Worker (nCore - 1);
		}
	}
};

unsigned TvStartCores (void)
{
#ifdef ARM_ALLOW_MULTI_CORE
	static CTvCores *pCores = 0;
	if (pCores == 0)
	{
		pCores = new CTvCores;
		if (pCores == 0 || !pCores->Initialize ())
		{
			return 0;
		}
		u64 nLimit = Ticks () + Freq () / 5;
		while (Ticks () < nLimit)
		{
			unsigned n = 0;
			while (n < MAX_WORKERS && s_Worker[n].bAlive)
			{
				n++;
			}
			if (n == MAX_WORKERS)
			{
				break;
			}
		}
		// only a run of workers from core 1 on (lines go to them in turn)
		s_nWorkers = 0;
		while (s_nWorkers < MAX_WORKERS && s_Worker[s_nWorkers].bAlive)
		{
			s_nWorkers++;
		}
	}
	return s_nWorkers;
#else
	return 0;
#endif
}

unsigned TvGetWorkers (void)
{
	return s_nWorkers;
}

void TvSync (void)
{
	for (unsigned i = 0; i < s_nWorkers; i++)
	{
		TTvWorker *w = &s_Worker[i];
		if (__atomic_load_n (&w->nTail, __ATOMIC_ACQUIRE) == w->nHead)
		{
			continue;
		}
		u64 nLimit = Ticks () + Freq () / 2;
		while (__atomic_load_n (&w->nTail, __ATOMIC_ACQUIRE) != w->nHead)
		{
			if (Ticks () > nLimit)
			{
				// a worker stopped answering: finish everything here and
				// stay on core 0 from now on
				unsigned n = s_nWorkers;
				s_nWorkers = 0;
				for (unsigned j = 0; j < n; j++)
				{
					TTvWorker *v = &s_Worker[j];
					for (unsigned t = v->nTail; t != v->nHead; t++)
					{
						Process (&v->Ring[t & (RING - 1)], 0);
					}
				}
				return;
			}
			__asm__ volatile ("wfe");
		}
	}
}

void TvSetup (int nMode)
{
	TvSync ();
	s_nMode = nMode;
	if (nMode == NTSC_OFF)
	{
		return;
	}
	ntsc_blargg_setup (nMode);
}

int TvGetMode (void)
{
	return s_nMode;
}

void TvSetOutput (TTvLineOut *pOut)
{
	TvSync ();
	s_pOut = pOut;
}

void TvPost (const u16 *pIndices, unsigned nPhase, u32 *pOut, unsigned nRow)
{
	if (s_nWorkers == 0)
	{
		static TTvLine Line;
		memcpy (Line.Idx, pIndices, sizeof Line.Idx);
		Line.nPhase = nPhase;
		Line.pOut = pOut;
		Line.nRow = nRow;
		Process (&Line, 0);
		return;
	}
	TTvWorker *w = &s_Worker[s_nNext];
	if (++s_nNext >= s_nWorkers)
	{
		s_nNext = 0;
	}
	unsigned h = w->nHead;
	if (h - __atomic_load_n (&w->nTail, __ATOMIC_ACQUIRE) >= RING)
	{
		TvSync ();			// full (never in practice)
		TvPost (pIndices, nPhase, pOut, nRow);
		return;
	}
	TTvLine *e = &w->Ring[h & (RING - 1)];
	memcpy (e->Idx, pIndices, sizeof e->Idx);
	e->nPhase = nPhase;
	e->pOut = pOut;
	e->nRow = nRow;
	__atomic_store_n (&w->nHead, h + 1, __ATOMIC_RELEASE);
	__asm__ volatile ("dsb ish; sev" ::: "memory");
}

// how long the workers take for a frame of lines (startup check, in LOG.TXT)
unsigned TvBenchmarkUs (unsigned nLines)
{
	static u32 Out[NTSC_OUT_W];
	static u16 Idx[NTSC_IN_W];
	for (unsigned x = 0; x < NTSC_IN_W; x++)
	{
		Idx[x] = (u16) ((x * 7) & 0x3F);	// many colours, like a busy scene
	}
	TTvLineOut *pOut = s_pOut;
	TvSetOutput (0);
	u64 t0 = Ticks ();
	for (unsigned i = 0; i < nLines; i++)
	{
		TvPost (Idx, i % 3, Out, i);
	}
	TvSync ();
	u64 t1 = Ticks ();
	TvSetOutput (pOut);
	return (unsigned) ((t1 - t0) * 1000000 / Freq ());
}
