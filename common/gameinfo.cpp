/*
 * What the list and the border show of each game (see gameinfo.h)
 */
#include "gameinfo.h"
#include <circle/util.h>

const TGameInfo *GameInfoFind (const char *pSet)
{
	for (unsigned i = 0; i < g_nGameInfo; i++)
	{
		if (strcmp (g_GameInfo[i].pSet, pSet) == 0)
		{
			return &g_GameInfo[i];
		}
	}
	return 0;
}
