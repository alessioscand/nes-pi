/*
 * nes-pi - version and error codes
 */
#ifndef NESPI_H
#define NESPI_H

#define NESPI_VERSION	"0.1.1-rc1"

/* Error codes = number of green LED blinks when the screen is unusable */
#define NESPI_ERR_FB    2   /* framebuffer / HDMI */
#define NESPI_ERR_SD    3   /* SD card or FAT filesystem */
#define NESPI_ERR_GAME  5   /* no .nes game in the nespi folder */
#define NESPI_ERR_CORE  6   /* emulator core error */
#define NESPI_ERR_MEM   7   /* out of memory */

#ifdef __cplusplus
extern "C" {
#endif

void nespi_log(const char *fmt, ...);
void nespi_fatal(int code, const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif
