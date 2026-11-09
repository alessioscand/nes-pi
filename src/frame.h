/*
 * nes-pi - the emulated frame: its picture and sound, and where its lines go
 *
 * Each line, as soon as the PPU finishes it, gets its colours from the palette
 * and goes to the screen (the buffer being shown, while racing); with the TV
 * filter it goes to core 1, which filters it and draws it (tvcore.h). The
 * menus go through the same filter.
 */
#ifndef FRAME_H
#define FRAME_H

#include "screen.h"
#include "nes_core.h"
#include "composite.h"
#include <stdint.h>

#define SAMPLE_RATE	48000
#define AUDIO_FRAME	(SAMPLE_RATE / 60)	// samples per frame (Nestopia paced at 60 Hz)

// TV FILTER: OFF / RGB (no filter: see TPicture), S-VIDEO, COMPOSITE, RF
#define NTSC_OFF	0
#define NTSC_SVIDEO	1
#define NTSC_COMPOSITE	2
#define NTSC_RF		3
#define NTSC_OUT_W	COMP_OUT_W		// 602 pixels from 256

// Overscan: a TV of the time hid about 8 lines at the top and at the bottom,
// and a little of each side, and games leave rubbish there (tiles being
// updated while scrolling, wrong colours at the edges). By default the picture
// is the 224 lines from 8 to 231, with 8 black pixels on each side;
// overscan=off in nespi.cfg shows all 256 x 240.
#define OVERSCAN_LINES	8
#define OVERSCAN_SIDE	8

// the picture settings (OPTIONS, nespi.cfg)
struct TPicture
{
	boolean	bOverscan;
	int	nTvMode;		// NTSC_* (TV FILTER)
	boolean	bTvScanlines;		// with no TV filter: RGB (light scanlines) or OFF
	int	nPalette;		// NES_PALETTE_*
	boolean	bUnlimSprites;		// SPRITE FLICKER REDUCED
	u8	CustomPal[512 * 3];	// nespi/palette.pal
	size_t	nCustomPal;		// 0 = none
};
extern TPicture g_Pic;

// the NES picture as 0x00RRGGBB (filled line by line by the line hook), as
// palette indices (to recolour it), and filtered (TV filter)
extern u32 g_VideoBuf[NES_W * NES_H];
extern u16 g_IdxBuf[NES_W * NES_H];
extern u32 g_NtscBuf[NTSC_OUT_W * NES_H];

// where the pictures sit for the video output; the menus have their own layout
extern TVideoSource g_NesSource;
extern TVideoSource g_NtscSource;
extern const TVideoSource g_UISource;
const TVideoSource &FrameSource (void);	// the game's, with or without the TV filter
const u32 *FrameBuffer (void);

// the menus' picture (UI buffer) as the video output gets it: through the
// TV filter when it is on
const TVideoSource &FrameUISource (void);
const u32 *FrameUIBuffer (const u32 *pUI);

extern int16_t g_AudioBuf[2 * (AUDIO_FRAME + 64)];	// the frame's sound, stereo

extern volatile unsigned g_Pad[2];		// pads for the next frame (NES_* bits)
extern volatile boolean g_bRaceFrame;		// this frame is raced

void FrameSetup (CScreen *pVideo);		// once: the line hook, the TV filter's output
void FrameSetTvMode (int nMode);		// NTSC_*: the TV filter's tables for it (takes a while)
unsigned FrameTvTimeUs (unsigned nLines);	// how long the TV filter takes for nLines
void FrameRun (void);				// one frame: emulation, picture and sound

#endif
