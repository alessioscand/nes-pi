//
// crtfx.h - TV FILTER RGB and CRT: CPU ports of two libretro slang shaders
//
// Default parameters, no curvature; each screen row comes from its own game
// line only, so a line is drawn as soon as it is ready (no lag):
//   RGB  crt-hyllian-fast (Hyllian, MIT)
//   CRT  crt-pi (davej, GPL v2 or later)
// One game line in, M screen rows out (M >= 2, an integer scale).
//
#ifndef CRTFX_H
#define CRTFX_H

#include <circle/types.h>

#define CRTFX_NONE	0
#define CRTFX_RGB	1
#define CRTFX_CRT	2

void CrtFxSetup (unsigned nFx, unsigned M, boolean bRLow);
unsigned CrtFxActive (void);

// the screen row nRow of the picture (0 .. GAME_H * M - 1)
typedef u32 *(*TCrtRowFn) (void *pCtx, unsigned nRow);

// game line y of pFrame (the emulator's frame) to its M rows
void CrtFxLine (const u32 *pFrame, unsigned y, TCrtRowFn pRow, void *pCtx);

// time spent in CrtFxLine since the last call (core 0)
void CrtFxTakeStats (unsigned *pSumUs, unsigned *pMaxUs, unsigned *pCalls);

#endif
