/*
 * The fonts, all from the ZX Origins collection by Damien Guard: each
 * project links the ones it uses (its theme says which)
 */
#ifndef COMMON_FONTS_H
#define COMMON_FONTS_H

#ifdef __cplusplus
extern "C" {
#endif

#define FONT_DEGREE	"\x7F"		// the degree sign, in every font

extern const unsigned char g_FontMatinee[96][8];	// font_matinee.c
extern const unsigned char g_FontYouSquared[96][8];	// font_yousquared.c
extern const unsigned char g_FontCyberwire[96][8];	// font_cyberwire.c
extern const unsigned char g_FontLander[96][8];		// font_lander.c

#ifdef __cplusplus
}
#endif

#endif
