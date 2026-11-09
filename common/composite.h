/*
 * The TV filter: blargg's snes_ntsc on an RGB picture (the library works on
 * any 15/16-bit RGB source), one line at a time. Each project chooses the
 * look (TCompLook); 0 = blargg's own composite.
 */
#ifndef COMPOSITE_H
#define COMPOSITE_H

#include <stddef.h>
#include "project.h"

#ifdef __cplusplus
extern "C" {
#endif

#define COMP_IN_W	PROJECT_WIDTH
#define COMP_OUT_WIDTH(w) ((((w) - 1) / 3 + 1) * 7)	/* SNES_NTSC_OUT_WIDTH */
#define COMP_OUT_W	COMP_OUT_WIDTH (COMP_IN_W)
#define COMP_PHASES	3		/* colour burst phases */

/* blargg's controls, -1 to +1 (all 0 = his composite; his S-Video:
   artifacts and fringing -1, sharpness and resolution 0.2) */
typedef struct
{
	float	fArtifacts;		/* colour artifacts from colour changes */
	float	fFringing;		/* colour fringes from brightness changes */
	float	fBleed;			/* colour bleeding */
	float	fSharpness;		/* edges */
	float	fResolution;		/* detail */
	float	fSaturation;
} TCompLook;

/* memory the tables need (about 4 MB); the caller allocates it */
size_t composite_size (void);

/* builds the tables for a look (0: blargg's composite) in pMem, every field
   shown (no merging); takes a while, call it when the look changes */
void composite_init (void *pMem, const TCompLook *pLook);

/* one line: nInW pixels 0x00RRGGBB in, COMP_OUT_WIDTH (nInW) pixels
   0x00RRGGBB out; nPhase = the colour burst phase of this line (0-2) */
void composite_line (const void *pTables, const unsigned *pIn, unsigned nInW, unsigned nPhase, unsigned *pOut);

#ifdef __cplusplus
}
#endif

#endif
