/*
 * nes-pi - built-in menu font
 */
#ifndef NESPI_FONTDATA_H
#define NESPI_FONTDATA_H

#ifdef __cplusplus
extern "C" {
#endif

#define FONT_DEGREE	"\x7F"		// degree sign in g_MenuFont

extern const unsigned char g_MenuFont[96][8];	// characters 0x20-0x7F

#ifdef __cplusplus
}
#endif

#endif
