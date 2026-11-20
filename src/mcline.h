/*
 * nes-pi - no line drawing core: Nestopia draws each line on core 0 (the
 * TV filter has its own core, tvcore.h); for common/report.cpp
 */
#ifndef MCLINE_H
#define MCLINE_H

#include <circle/types.h>
#include <circle/util.h>

static inline boolean McFailed (void)	{ return FALSE; }
static inline void McFailText (char *pOut, size_t nSize, unsigned nFrame)
{
	(void) nFrame;
	if (nSize) pOut[0] = '\0';
}

#endif
