/*
 * nes-pi - what the shared files in common/ need to know about the project
 */
#ifndef PROJECT_H
#define PROJECT_H

#define PROJECT_NAME	"nes-pi"
#define PROJECT_VERSION	"1.0.1"

/* The emulated picture (with overscan=off: all 240 lines) */
#define PROJECT_WIDTH	256
#define PROJECT_HEIGHT	240

/* TV filter (common/tvcore, common/composite): one line in (256 pixels
   0x00RRGGBB), the filtered line out (COMP_OUT_W) */
#define PROJECT_TV_IN_BYTES	(256 * 4)
#define PROJECT_TV_OUT_W	602

/* Players: one pad each (common/input) */
#define PROJECT_PLAYERS	2

/* Buttons besides the directions, START and SELECT: A B C D (C D = turbo) */
#define PROJECT_BUTTONS	4

/* Error codes = number of green LED blinks when the screen is unusable */
#define PROJECT_ERR_FB    2   /* framebuffer / HDMI */
#define PROJECT_ERR_SD    3   /* SD card or FAT filesystem */
#define PROJECT_ERR_GAME  5   /* no .nes game in the nespi folder */
#define PROJECT_ERR_CORE  6   /* emulator core error */
#define PROJECT_ERR_MEM   7   /* out of memory */

#ifdef __cplusplus
extern "C" {
#endif

void project_log (const char *pFormat, ...);
void project_fatal (int nCode, const char *pFormat, ...);

#ifdef __cplusplus
}
#endif

#endif
