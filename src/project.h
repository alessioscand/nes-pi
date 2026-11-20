/*
 * nes-pi - what the shared files in common/ need to know about the project
 */
#ifndef PROJECT_H
#define PROJECT_H

#define PROJECT_NAME	"nes-pi"
#define PROJECT_VERSION	"1.2.0"

/* The emulated picture: all 256 x 240 (the overscan is drawn black), in a
   buffer as wide as the menus' canvas (304, common/screen.h) */
#define PROJECT_WIDTH	256
#define PROJECT_HEIGHT	240
#define PROJECT_SRC_PITCH	304
#define PROJECT_SRC_LINES	240
#define PROJECT_SRC_X	24
#define PROJECT_SRC_Y	0

/* TV filter (common/tvcore, common/composite): one line in (0x00RRGGBB), the
   filtered line out; the game's 256 pixels give 602, the menus' 304 give 714 */
#define PROJECT_TV_IN_BYTES	(304 * 4)
#define PROJECT_TV_OUT_W	714

/* Players: one pad each (common/input) */
#define PROJECT_PLAYERS	2

/* Buttons besides the directions, START and SELECT: A B */
#define PROJECT_BUTTONS	2

/* Each emulated frame starts with the vertical blank, after line 239 */
#define PROJECT_PHASE_LINE	240

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
