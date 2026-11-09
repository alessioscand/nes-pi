/*
 * The TV filter's core (see tvcore.h)
 *
 * One ring of lines, written by the producer only at the head and by the TV
 * filter's core only at the tail: release and acquire on the two counters,
 * dsb + sev to wake the other side from wfe.
 */
#include "tvcore.h"
#include "smp.h"
#include <circle/util.h>

#define RING	256				// lines: more than a frame

struct TTvLine
{
	u8	 Src[PROJECT_TV_IN_BYTES];
	unsigned nPhase;
	unsigned nRow;
	boolean	 bRace;
	u32	 *pOut;
};

static TTvLine s_Ring[RING];
static unsigned s_nHead;			// written by the producer only
static unsigned s_nTail;			// written by the TV filter's core only
static boolean s_bAlive = FALSE;		// the core is running the loop
static boolean s_bDead = FALSE;			// the core stopped answering
static TTvFilter *s_pFilter = 0;
static TTvLineOut *s_pOut = 0;

// time spent (for LOG.TXT); only one side processes at a time
static u64 s_nFilterTicks, s_nDrawTicks;
static unsigned s_nLines;

static void Process (const TTvLine *e)
{
	static u32 Out[2][PROJECT_TV_OUT_W];	// [0] the TV filter's core, [1] the caller
	const unsigned nSide = TvWorkerAlive () ? 0 : 1;
	u32 *pOut = e->pOut ? e->pOut : Out[nSide];
	u64 t0 = Ticks ();
	s_pFilter (e->Src, e->nPhase, pOut);
	u64 t1 = Ticks ();
	if (s_pOut)
	{
		s_pOut (e->nRow, pOut, e->bRace, nSide);
	}
	u64 t2 = Ticks ();
	SharedStore (s_nFilterTicks, s_nFilterTicks + (t1 - t0));
	SharedStore (s_nDrawTicks, s_nDrawTicks + (t2 - t1));
	SharedStore (s_nLines, s_nLines + 1);
}

void TvSetFilter (TTvFilter *pFilter)
{
	TvSync ();
	SharedRelease (s_pFilter, pFilter);
}

boolean TvReady (void)
{
	return s_pFilter != 0;
}

void TvSetOutput (TTvLineOut *pOut)
{
	TvSync ();
	s_pOut = pOut;
}

void TvWorkerRun (void)
{
	SharedRelease (s_bAlive, (boolean) TRUE);
	SignalEvent ();
	for (;;)
	{
		unsigned t = s_nTail;
		while (SharedAcquire (s_nHead) == t)
		{
			WaitEvent ();
		}
		Process (&s_Ring[t & (RING - 1)]);
		SharedRelease (s_nTail, t + 1);
		SignalEvent ();
	}
}

boolean TvWorkerAlive (void)
{
	return SharedAcquire (s_bAlive) && !SharedLoad (s_bDead);
}

void TvSync (void)
{
	if (!TvWorkerAlive ())
	{
		return;
	}
	if (SharedAcquire (s_nTail) == s_nHead)
	{
		return;
	}
	u64 nLimit = Ticks () + TicksFreq () / 2;
	while (SharedAcquire (s_nTail) != s_nHead)
	{
		if (Ticks () > nLimit)
		{
			// the core stopped answering: finish here, without it from now on
			SharedStore (s_bDead, (boolean) TRUE);
			for (unsigned t = SharedLoad (s_nTail); t != s_nHead; t++)
			{
				Process (&s_Ring[t & (RING - 1)]);
			}
			return;
		}
		WaitEvent ();
	}
}

void TvPost (const void *pSource, unsigned nPhase, unsigned nRow, boolean bRace, u32 *pOut)
{
	if (s_pFilter == 0)
	{
		return;
	}
	if (!TvWorkerAlive ())
	{
		static TTvLine Line;
		memcpy (Line.Src, pSource, sizeof Line.Src);
		Line.nPhase = nPhase;
		Line.nRow = nRow;
		Line.bRace = bRace;
		Line.pOut = pOut;
		Process (&Line);
		return;
	}
	unsigned h = s_nHead;
	if (h - SharedAcquire (s_nTail) >= RING)
	{
		TvSync ();			// full (never in practice)
		TvPost (pSource, nPhase, nRow, bRace, pOut);
		return;
	}
	TTvLine *e = &s_Ring[h & (RING - 1)];
	memcpy (e->Src, pSource, sizeof e->Src);
	e->nPhase = nPhase;
	e->nRow = nRow;
	e->bRace = bRace;
	e->pOut = pOut;
	SharedRelease (s_nHead, h + 1);
	SignalEvent ();
}

void TvTakeStats (unsigned *pFilterUs, unsigned *pDrawUs, unsigned *pLines)
{
	u64 f = SharedLoad (s_nFilterTicks), d = SharedLoad (s_nDrawTicks);
	unsigned n = SharedLoad (s_nLines);
	SharedStore (s_nFilterTicks, (u64) 0);
	SharedStore (s_nDrawTicks, (u64) 0);
	SharedStore (s_nLines, 0u);
	*pFilterUs = (unsigned) (f * 1000000 / TicksFreq ());
	*pDrawUs = (unsigned) (d * 1000000 / TicksFreq ());
	*pLines = n;
}
