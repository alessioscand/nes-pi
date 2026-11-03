/*
 * nes-pi - TV filter (blargg's nes_ntsc), C interface
 */
#ifndef NTSC_BLARGG_H
#define NTSC_BLARGG_H

#ifdef __cplusplus
extern "C" {
#endif

#define NTSC_IN_W	256
#define NTSC_OUT_W	602		/* NES_NTSC_OUT_WIDTH (256) */

#define NTSC_OFF	0
#define NTSC_SVIDEO	1
#define NTSC_COMPOSITE	2
#define NTSC_RF		3	/* composite with every defect at its worst */

/* tables for a mode; the colours come from the filter's own model of the
   NES video signal (the TV is the palette); takes a few ms */
void ntsc_blargg_setup (int nMode);

/* one line: 256 palette indices in, 602 pixels 0x00RRGGBB out; nPhase = the
   colour burst phase of this line (0-2) */
void ntsc_blargg_line (const unsigned short *pIn, unsigned nPhase, unsigned *pOut);

#ifdef __cplusplus
}
#endif

#endif
