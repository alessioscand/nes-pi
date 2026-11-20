/*
 * Each game's title, maker and buttons (src/gametable.cpp)
 */
#ifndef COMMON_GAMEINFO_H
#define COMMON_GAMEINFO_H

#define GAMEINFO_BUTTONS	6

struct TGameInfo
{
	const char *pSet;			// the game's name on the card (MAME's set)
	const char *pTitle;			// short title: "Undercover Cops"
	const char *pRegion;			// under the list
	const char *pYear;
	const char *pMaker;
	const char *pButtons[GAMEINFO_BUTTONS];	// buttons A..F, 0 after the last,
						// "" = not used;
						// all 0 = not known, nothing is shown
};

extern const TGameInfo g_GameInfo[];		// src/gametable.cpp
extern const unsigned g_nGameInfo;

// 0 if the game is not in the table
const TGameInfo *GameInfoFind (const char *pSet);

#endif
