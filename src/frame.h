/*
 * nes-pi - the emulated frame: its picture and sound, and where its lines go
 *
 * Each line, as soon as the PPU finishes it, gets its colours from the palette
 * and goes to the screen (the buffer being shown, while racing); with the TV
 * filter it goes to core 2, which filters it and draws it (tvcore.h). The
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

// TV FILTER: OFF / RGB (FAST scanlines: see TPicture), S-VIDEO, COMPOSITE, RF
#define NTSC_OFF	0
#define NTSC_SVIDEO	1
#define NTSC_COMPOSITE	2
#define NTSC_RF		3

// Overscan: a TV of the time hid about 8 lines at the top and at the bottom,
// and a little of each side, and games leave rubbish there (tiles being
// updated while scrolling, wrong colours at the edges). By default those
// lines and 8 pixels on each side are drawn black; overscan=off in nespi.cfg
// shows all 256 x 240.
#define OVERSCAN_LINES	8
#define OVERSCAN_SIDE	8

// the picture settings (OPTIONS, nespi.cfg)
struct TPicture
{
	boolean	bOverscan;
	int	nTvMode;		// NTSC_* (TV FILTER)
	boolean	bTvScanlines;		// with no TV filter: RGB (scanlines) or OFF
	int	nPalette;		// NES_PALETTE_*
	boolean	bUnlimSprites;		// SPRITE FLICKER REDUCED
	u8	CustomPal[512 * 3];	// nespi/palette.pal
	size_t	nCustomPal;		// 0 = none
};
extern TPicture g_Pic;

// the NES picture as 0x00RRGGBB at GAME_SRC_X, with room for the menus'
// canvas around it (filled line by line by the line hook), and as palette
// indices (to recolour it)
extern u32 g_VideoBuf[GAME_SRC_PITCH * GAME_SRC_LINES + 64];
extern u16 g_IdxBuf[NES_W * NES_H];

extern int16_t g_AudioBuf[2 * (AUDIO_FRAME + 64)];	// the frame's sound, stereo

extern volatile unsigned g_Pad[2];		// pads for the next frame (NES_* bits)
extern volatile boolean g_bRaceFrame;		// this frame is raced

void FrameSetup (CScreen *pVideo);		// once: the line hook, the TV filter's output
void FrameSetTvMode (int nMode);		// NTSC_*: the TV filter's tables for it (takes a while)
unsigned FrameTvTimeUs (unsigned nLines);	// how long the TV filter takes for nLines
void FrameRun (void);				// one frame: emulation, picture and sound
void FrameRecolour (void);			// g_VideoBuf again from g_IdxBuf (a new palette)
void FrameDraw (void);				// the whole frame to the screen again
void FrameDrawPicture (u32 *pBuffer, boolean bFilter = TRUE);	// a menus' canvas, through the same filter (or none)
unsigned FrameMcFailFrame (void);		// no line drawing core: always 0

#endif
