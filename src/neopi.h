/*
 * nes-pi - version and error codes
 */
#ifndef NEOPI_H
#define NEOPI_H

#define NEOPI_VERSION	"0.1.0-test8"

/* Error codes = number of green LED blinks when the screen is unusable */
#define NEOPI_ERR_FB    2   /* framebuffer / HDMI */
#define NEOPI_ERR_SD    3   /* SD card or FAT filesystem */
#define NEOPI_ERR_GAME  5   /* no .nes game in the nespi folder */
#define NEOPI_ERR_CORE  6   /* emulator core error */
#define NEOPI_ERR_MEM   7   /* out of memory */

#ifdef __cplusplus
extern "C" {
#endif

void neopi_log(const char *fmt, ...);
void neopi_fatal(int code, const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif
