/*
 * nes-pi - TV filter (OPTIONS > TV FILTER): blargg's NTSC filter, one line at
 * a time, on core 1.
 *
 * Core 0 (the emulation) queues each finished line (its 256 palette indices)
 * and goes on; core 1 filters it into a 602-pixel line and hands it to the
 * output callback (which draws it on the screen and tells the beam racer).
 * Nothing waits for a whole frame: no added lag, beam racing still works.
 * If core 1 cannot start, or stops answering, the lines are filtered on
 * core 0 instead.
 */
#ifndef TVFILTER_H
#define TVFILTER_H

#include <circle/types.h>
#include "ntsc_blargg.h"

// called for each filtered line, on worker nWorker (0: its own scratch)
typedef void TTvLineOut (unsigned nRow, unsigned nWorker);

// starts core 1 (once, at start-up); returns 1, or 0 if it did not start
unsigned TvStartCores (void);
unsigned TvGetWorkers (void);

// time spent since the last call (all workers together): filtering, drawing
void TvTakeStats (unsigned *pFilterUs, unsigned *pDrawUs, unsigned *pLines);

// time in microseconds the workers take for nLines lines (start-up check)
unsigned TvBenchmarkUs (unsigned nLines);

// mode (NTSC_OFF / NTSC_SVIDEO / NTSC_COMPOSITE / NTSC_RF); waits for the
// queued lines first
void TvSetup (int nMode);
int TvGetMode (void);

void TvSetOutput (TTvLineOut *pOut);

// queues one line: 256 palette indices (copied), the burst phase, where the
// 602 filtered pixels go, and the row number passed to the output callback
void TvPost (const u16 *pIndices, unsigned nPhase, u32 *pOut, unsigned nRow);

// waits until every queued line has been filtered and drawn
void TvSync (void);

#endif
