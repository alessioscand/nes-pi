/*
 * nes-pi - C interface to the Nestopia core (src/nes_core.cpp)
 *
 * Nestopia is C++ with exceptions and the standard library; the rest of
 * nes-pi is built the Circle way (no exceptions, no standard C++ library).
 * This interface keeps them apart: plain C types only.
 */
#ifndef NES_CORE_H
#define NES_CORE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NES_W		256
#define NES_H		240

// pad bits (Nestopia's own order)
#define NES_A		0x01
#define NES_B		0x02
#define NES_SELECT	0x04
#define NES_START	0x08
#define NES_UP		0x10
#define NES_DOWN	0x20
#define NES_LEFT	0x40
#define NES_RIGHT	0x80

int nes_init (unsigned nSampleRate);		// once; 0 = failed
// battery RAM: the core asks for it when a game loads and gives it back on unload
typedef size_t TNesBatteryLoad (void *pBuffer, size_t nMax);	// returns bytes read
typedef void TNesBatterySave (const void *pData, size_t nSize);
void nes_set_battery_io (TNesBatteryLoad *pLoad, TNesBatterySave *pSave);

int nes_load (const void *pRom, size_t nSize);	// iNES / NES 2.0 / UNIF image; 0 = failed
void nes_unload (void);				// saves battery RAM
void nes_reset (int bHard);
const char *nes_last_error (void);
const char *nes_board_name (void);		// mapper / board of the loaded game

// one frame: starts with the vertical blank (the game reads the pads there),
// then every line; nSamples mono samples are written to pAudio
void nes_frame (unsigned nPad0, unsigned nPad1, int16_t *pAudio, unsigned nSamples);

// frames the core stopped with an error since the last call (nes_last_error
// says what); the game goes on with the next frame
unsigned nes_take_frame_errors (void);

// called at the end of every line of the frame: nLine -1 is the pre-render
// line (pPixels = NULL), 0..239 the picture (256 palette indices each)
typedef void TNesLineHook (int nLine, const uint16_t *pPixels);
void nes_set_line_hook (TNesLineHook *pHook);

// 1 = catch the PPU up after every CPU instruction, so the line hook is called
// as the frame runs (beam racing); 0 = Nestopia's lazy PPU (lines in a burst at
// the end of the frame, a little less CPU). Takes effect at the next load/reset.
void nes_set_ppu_sync (int bOn);

// 512 colours (64 x 8 emphasis combinations) as 0x00RRGGBB
const uint32_t *nes_palette (void);

// Palette: 0 Nestopia's own (YUV, canonical), 1 "consumer" TV, 2 Sony
// CXA2025AS (US TVs), 3 custom: pCustom = a .pal file, 192 bytes (64
// colours) or 1536 bytes (512, emphasis included). Returns 0 if refused
// (the palette is left as it was). nes_palette () gives the new colours.
#define NES_PALETTE_NESTOPIA	0
#define NES_PALETTE_CONSUMER	1
#define NES_PALETTE_SONY	2
#define NES_PALETTE_CUSTOM	3
int nes_set_palette (int nPalette, const void *pCustom, size_t nSize);

// 1 = no limit of 8 sprites per line (less flicker; a few games use the limit
// on purpose to hide sprites), 0 = like the real console
void nes_set_unlimited_sprites (int bOn);

// save states: nes_state_save returns a buffer to give back to nes_free
void *nes_state_save (size_t *pSize);
int nes_state_load (const void *pData, size_t nSize);
void nes_free (void *p);

#ifdef __cplusplus
}
#endif

#endif
