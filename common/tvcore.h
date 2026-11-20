/*
 * The TV filter on its own core, one line at a time: queued lines are
 * filtered (PROJECT_TV_IN_BYTES in, PROJECT_TV_OUT_W out) and handed to the
 * output callback; no added lag. Without that core the caller filters.
 * The project starts the core and calls TvWorkerRun () from it.
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

// queues one line: PROJECT_TV_IN_BYTES of source (copied; 0 when the filter
// reads the line itself, as RGB and CRT do), its colour burst
// phase, its row in the picture, whether the frame is raced, and where the
// filtered pixels go (0: a scratch line)
void TvPost (const void *pSource, unsigned nPhase, unsigned nRow, boolean bRace, u32 *pOut = 0);

// waits until every queued line has been filtered and drawn
void TvSync (void);

// time spent since the last call: filtering, drawing (microseconds), lines
void TvTakeStats (unsigned *pFilterUs, unsigned *pDrawUs, unsigned *pLines);

#endif
