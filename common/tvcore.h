/*
 * The TV filter's core: a whole core that filters the picture one line at a
 * time.
 *
 * The core that finishes a line queues it here and goes on; the TV filter's
 * core runs the project's filter on it (PROJECT_TV_IN_BYTES in,
 * PROJECT_TV_OUT_W pixels out) and hands it to the output callback, which
 * puts it on screen and tells the beam racer. Nothing waits for a whole
 * frame: no added lag, beam racing still works. If that core does not run,
 * or stops answering, the lines are filtered by the caller instead.
 *
 * The project starts the core (CMultiCoreSupport) and calls TvWorkerRun ()
 * from it; the filter itself is the project's own.
 */
#ifndef TVCORE_H
#define TVCORE_H

#include <circle/types.h>
#include "project.h"

// one line: the source in, the filtered pixels (0x00RRGGBB) out
typedef void TTvFilter (const void *pSource, unsigned nPhase, u32 *pOut);

// called for each filtered line; nSide = 0 on the TV filter's core, 1 on the
// caller (each side needs its own scratch)
typedef void TTvLineOut (unsigned nRow, const u32 *pFiltered, boolean bRace, unsigned nSide);

void TvSetFilter (TTvFilter *pFilter);	// once the filter is ready to use
boolean TvReady (void);
void TvSetOutput (TTvLineOut *pOut);

// the TV filter's core loop (from CMultiCoreSupport::Run); never returns
void TvWorkerRun (void);
boolean TvWorkerAlive (void);

// queues one line: PROJECT_TV_IN_BYTES of source (copied), its colour burst
// phase, its row in the picture, whether the frame is raced, and where the
// filtered pixels go (0: a scratch line)
void TvPost (const void *pSource, unsigned nPhase, unsigned nRow, boolean bRace, u32 *pOut = 0);

// waits until every queued line has been filtered and drawn
void TvSync (void);

// time spent since the last call: filtering, drawing (microseconds), lines
void TvTakeStats (unsigned *pFilterUs, unsigned *pDrawUs, unsigned *pLines);

#endif
