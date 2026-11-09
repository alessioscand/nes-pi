/*
 * The TV filter: blargg's snes_ntsc 0.2.2 (LGPL 2.1+, third_party/snes_ntsc,
 * unmodified) with our own configuration: RGB565 in (converted from
 * 0x00RRGGBB), 0x00RRGGBB out, one line at a time.
 */
#define SNES_NTSC_CONFIG_H	/* the library's own configuration file is not used */
#define SNES_NTSC_IN_FORMAT	SNES_NTSC_RGB16
#define SNES_NTSC_OUT_DEPTH	32
#define SNES_NTSC_IN_T		unsigned short
#define SNES_NTSC_ADJ_IN(in)	in

/* The library keeps its tables as unsigned long: 64 bits on the Pi, twice the
   memory (and cache) its 32-bit arithmetic needs. Compiled with long = int
   it gives exactly the same pixels (checked line by line) with half the
   tables. The system headers it uses come first, untouched. */
#include <assert.h>
#include <math.h>
#include <limits.h>
#define long int
#include "snes_ntsc.h"
#include "snes_ntsc.c"
#undef long

#include "composite.h"

#define MAX_IN_W	512		/* longest line in */

size_t composite_size (void)
{
	return sizeof (snes_ntsc_t);
}

void composite_init (void *pMem, const TCompLook *pLook)
{
	snes_ntsc_setup_t Setup = snes_ntsc_composite;
	if (pLook)
	{
		Setup.artifacts  = pLook->fArtifacts;
		Setup.fringing   = pLook->fFringing;
		Setup.bleed      = pLook->fBleed;
		Setup.sharpness  = pLook->fSharpness;
		Setup.resolution = pLook->fResolution;
		Setup.saturation = pLook->fSaturation;
	}
	Setup.merge_fields = 0;		/* the real thing: artifacts crawl and shimmer */
	snes_ntsc_init ((snes_ntsc_t *) pMem, &Setup);
}

void composite_line (const void *pTables, const unsigned *pIn, unsigned nInW, unsigned nPhase, unsigned *pOut)
{
	unsigned short In[MAX_IN_W];
	if (nInW > MAX_IN_W)
	{
		nInW = MAX_IN_W;
	}
	for (unsigned x = 0; x < nInW; x++)
	{
		unsigned p = pIn[x];
		In[x] = (unsigned short) (((p >> 8) & 0xF800) | ((p >> 5) & 0x07E0) | ((p >> 3) & 0x001F));
	}
	snes_ntsc_blit ((const snes_ntsc_t *) pTables, In, nInW, (int) (nPhase % COMP_PHASES),
			nInW, 1, pOut, COMP_OUT_WIDTH (nInW) * 4);
}
