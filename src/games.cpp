/*
 * nes-pi - the games: the list from the SD card, loading, battery RAM, save states
 */
#include "kernel.h"
#include "frame.h"
#include "racing.h"
#include <circle/alloc.h>
#include <circle/new.h>

// battery-backed RAM: read when a game loads, written when it is unloaded
static char s_BatteryPath[160];

static boolean HasNesExtension (const char *pName)
{
	size_t n = strlen (pName);
	if (n < 4)
	{
		return FALSE;
	}
	const char *e = pName + n - 4;
	return    e[0] == '.'
	       && (e[1] == 'n' || e[1] == 'N')
	       && (e[2] == 'e' || e[2] == 'E')
	       && (e[3] == 's' || e[3] == 'S');
}

size_t CKernel::BatteryLoad (void *pBuffer, size_t nMax)
{
	FIL File;
	UINT nRead = 0;
	if (s_BatteryPath[0] && f_open (&File, s_BatteryPath, FA_READ) == FR_OK)
	{
		f_read (&File, pBuffer, nMax, &nRead);
		f_close (&File);
	}
	return nRead;
}

void CKernel::BatterySave (const void *pData, size_t nSize)
{
	FIL File;
	if (s_BatteryPath[0] && f_open (&File, s_BatteryPath, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK)
	{
		UINT nWritten;
		f_write (&File, pData, nSize, &nWritten);
		f_close (&File);
	}
}

u8 *CKernel::LoadFile (const char *pPath, size_t *pSize, int nHeap, const char *pShowName)
{
	FIL File;
	if (f_open (&File, pPath, FA_READ) != FR_OK)
	{
		return 0;
	}

	size_t nSize = f_size (&File);
	u8 *pBuffer = new (nHeap) u8[nSize + 16];
	if (pBuffer == 0)
	{
		f_close (&File);
		char Msg[160];
		snprintf (Msg, sizeof Msg, "not enough memory for %s (%u MB)",
			  pPath, (unsigned) (nSize >> 20));
		Log (Msg, COLOR_ERR);
		return 0;
	}

	unsigned nStart = CTimer::GetClockTicks ();
	size_t nDone = 0;
	const size_t nChunk = 1 << 20;
	while (nDone < nSize)
	{
		size_t nWant = nSize - nDone < nChunk ? nSize - nDone : nChunk;
		UINT nRead = 0;
		if (   f_read (&File, pBuffer + nDone, nWant, &nRead) != FR_OK
		    || nRead == 0)
		{
			break;
		}
		nDone += nRead;
		if (pShowName != 0 && m_pUI != 0)
		{
			m_pUI->DrawLoading (pShowName, (unsigned) ((u64) nDone * 100 / nSize));
			ShowUIFrame (FALSE);
		}
	}
	f_close (&File);

	if (nDone != nSize)
	{
		delete [] pBuffer;
		return 0;
	}

	unsigned nMs = (CTimer::GetClockTicks () - nStart) / 1000;
	char Msg[180];
	unsigned nSpeed10 = nMs ? (unsigned) ((u64) nSize * 10 * 1000 / nMs / (1024 * 1024)) : 0;
	snprintf (Msg, sizeof Msg, "  read %s: %u KB in %u ms (%u.%u MB/s)", pPath + 4,
		  (unsigned) (nSize >> 10), nMs, nSpeed10 / 10, nSpeed10 % 10);
	Log (Msg, COLOR_OK);

	*pSize = nSize;
	return pBuffer;
}

// the TV system a file name's region tags say, in the No-Intro naming:
// (USA) (Europe) (Japan, USA) (World) ..., or GoodNES: (U) (E) (J) (JUE) ...
// "NTSC", "PAL", "NTSC/PAL", "DENDY", or "" without a known tag
static const char *TagRegion (const char *pFile)
{
	enum { NTSC = 1, PAL = 2, DENDY = 4 };
	static const struct { const char *pTag; unsigned nBits; } Tags[] =
	{
		{"USA", NTSC}, {"Japan", NTSC}, {"Korea", NTSC}, {"Canada", NTSC},
		{"Brazil", NTSC}, {"Taiwan", NTSC},
		{"Europe", PAL}, {"Australia", PAL}, {"Germany", PAL}, {"France", PAL},
		{"Spain", PAL}, {"Italy", PAL}, {"UK", PAL}, {"Netherlands", PAL},
		{"Sweden", PAL}, {"Denmark", PAL}, {"Finland", PAL}, {"Norway", PAL},
		{"Scandinavia", PAL}, {"Greece", PAL}, {"Poland", PAL}, {"Portugal", PAL},
		{"Russia", DENDY},
		{"World", NTSC | PAL},
		{"PAL", PAL}, {"NTSC", NTSC},
		{"G", PAL}, {"F", PAL}, {"S", PAL}, {"I", PAL}, {"Sw", PAL}, {"B", NTSC}
	};
	unsigned nBits = 0;
	for (const char *p = strchr (pFile, '('); p; p = strchr (p, '('))
	{
		p++;
		while (*p && *p != ')')			// "(Japan, USA)": one tag after the other
		{
			while (*p == ' ')
			{
				p++;
			}
			size_t n = 0;
			while (p[n] && p[n] != ',' && p[n] != ')')
			{
				n++;
			}
			boolean bFound = FALSE;
			for (unsigned i = 0; i < sizeof Tags / sizeof Tags[0]; i++)
			{
				if (strlen (Tags[i].pTag) == n && strncmp (Tags[i].pTag, p, n) == 0)
				{
					nBits |= Tags[i].nBits;
					bFound = TRUE;
				}
			}
			// GoodNES letters, also together: (U) (J) (K) (E) (A) (UE) (JUE) ...
			unsigned nLetters = 0;
			for (size_t i = 0; i < n; i++)
			{
				nLetters |= strchr ("UJK", p[i]) ? NTSC : strchr ("EA", p[i]) ? PAL : 8;
			}
			if (!bFound && n > 0 && n <= 3 && !(nLetters & 8))
			{
				nBits |= nLetters;
			}
			p += n;
			if (*p == ',')
			{
				p++;
			}
		}
	}
	if ((nBits & (NTSC | PAL)) == (NTSC | PAL)) return "NTSC/PAL";
	if (nBits & NTSC) return "NTSC";
	if (nBits & PAL) return "PAL";
	return nBits & DENDY ? "DENDY" : "";
}

void CKernel::ScanGames (void)
{
	m_pGames = new TGameEntry[MaxGames];
	m_nGames = 0;

	DIR Dir;
	FILINFO Info;
	if (f_opendir (&Dir, NESPI_DIR) != FR_OK)
	{
		return;
	}
	while (   m_nGames < MaxGames
	       && f_readdir (&Dir, &Info) == FR_OK
	       && Info.fname[0] != '\0')
	{
		if ((Info.fattrib & AM_DIR) || !HasNesExtension (Info.fname)
		    || strlen (Info.fname) >= sizeof m_pGames[0].File)
		{
			continue;
		}

		// the list shows the file name as written: '_' as spaces, no extension
		// and nothing after " (" (region / revision tags)
		TGameEntry &G = m_pGames[m_nGames];
		strcpy (G.File, Info.fname);
		strcpy (G.Info, TagRegion (Info.fname));
		unsigned n = 0;
		size_t nLen = strlen (Info.fname) - 4;
		for (size_t i = 0; i < nLen && n < sizeof G.Name - 1; i++)
		{
			char c = Info.fname[i];
			if (c == '(' || c == '[')
			{
				break;
			}
			if (c == '_') c = ' ';
			if (c < 32 || c > 126) c = ' ';
			G.Name[n++] = c;
		}
		while (n > 0 && G.Name[n-1] == ' ') n--;
		G.Name[n] = '\0';
		if (n == 0)
		{
			strncpy (G.Name, Info.fname, sizeof G.Name - 1);
			G.Name[sizeof G.Name - 1] = '\0';
		}
		m_nGames++;
	}
	f_closedir (&Dir);

	// sort by name, ignoring upper / lower case
	auto Compare = [] (const char *a, const char *b)
	{
		for (;; a++, b++)
		{
			int x = (unsigned char) *a, y = (unsigned char) *b;
			if (x >= 'a' && x <= 'z') x -= 32;
			if (y >= 'a' && y <= 'z') y -= 32;
			if (x != y || x == 0) return x - y;
		}
	};
	for (unsigned i = 1; i < m_nGames; i++)
	{
		TGameEntry t = m_pGames[i];
		int j = (int) i - 1;
		while (j >= 0 && Compare (m_pGames[j].Name, t.Name) > 0)
		{
			m_pGames[j+1] = m_pGames[j];
			j--;
		}
		m_pGames[j+1] = t;
	}
}

void CKernel::GamePath (char *pOut, size_t nSize, const char *pExt) const
{
	char Base[64];
	strcpy (Base, m_pGames[m_nLoaded].File);
	size_t n = strlen (Base);
	if (n > 4) Base[n - 4] = '\0';
	snprintf (pOut, nSize, "%s/%s.%s", SAVE_DIR, Base, pExt);
}

void CKernel::StateExt (char *pExt, unsigned nSlot) const
{
	pExt[0] = 's';
	pExt[1] = 't';
	pExt[2] = (char) ('1' + nSlot);
	pExt[3] = '\0';
}

void CKernel::UnloadGame (void)
{
	if (m_nLoaded < 0)
	{
		return;
	}
	nes_unload ();
	s_BatteryPath[0] = '\0';
	delete [] m_pGame;
	m_pGame = 0;
	m_nLoaded = -1;
}

boolean CKernel::LoadGame (unsigned nIndex)
{
	UnloadGame ();

	const TGameEntry &G = m_pGames[nIndex];
	char Path[128], Line[180];
	snprintf (Path, sizeof Path, "%s/%s", NESPI_DIR, G.File);
	snprintf (Line, sizeof Line, "Game: %s (%s)", G.Name, G.File);
	Log (Line, COLOR_TITLE);

	m_pGame = LoadFile (Path, &m_nGameSize, HEAP_ANY, m_bQuiet ? G.Name : 0);
	if (m_pGame == 0)
	{
		Log ("  read failed", COLOR_ERR);
		return FALSE;
	}

	m_nLoaded = (int) nIndex;			// GamePath needs it
	GamePath (s_BatteryPath, sizeof s_BatteryPath, "sav");
	// beam racing needs the lines as the frame runs: PPU caught up after every
	// CPU instruction (patches/nestopia-linehook.py); without racing, the lazy PPU
	nes_set_ppu_sync (g_Race.bReady);
	if (!nes_load (m_pGame, m_nGameSize))
	{
		snprintf (Line, sizeof Line, "  %s", nes_last_error ());
		Log (Line, COLOR_ERR);
		s_BatteryPath[0] = '\0';
		delete [] m_pGame;
		m_pGame = 0;
		m_nLoaded = -1;
		return FALSE;
	}
	snprintf (Line, sizeof Line, "  board %s", nes_board_name ());
	Log (Line, COLOR_DIM);
	m_nShots = 0;					// screenshot numbers: next free one
	memset (g_VideoBuf, 0, sizeof g_VideoBuf);

	WriteTextFile (LAST_PATH, G.File);
	return TRUE;
}

boolean CKernel::SaveState (const char *pPath)
{
	size_t nSize = 0;
	void *pState = nes_state_save (&nSize);
	if (pState == 0)
	{
		return FALSE;
	}
	FIL File;
	boolean bOK = FALSE;
	if (f_open (&File, pPath, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK)
	{
		UINT nWritten = 0;
		bOK = f_write (&File, pState, nSize, &nWritten) == FR_OK && nWritten == nSize;
		f_close (&File);
	}
	nes_free (pState);
	return bOK;
}

boolean CKernel::LoadState (const char *pPath)
{
	size_t nSize = 0;
	u8 *pState = LoadFile (pPath, &nSize, HEAP_ANY, 0);
	if (pState == 0)
	{
		return FALSE;
	}
	boolean bOK = nes_state_load (pState, nSize) != 0;
	delete [] pState;
	return bOK;
}
