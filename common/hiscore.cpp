/*
 * Hiscores and battery RAM, kept on the card (see hiscore.h)
 */
#include "hiscore.h"
#include <circle/util.h>
#include <fatfs/ff.h>

static UINT ReadFile (const char *pPath, u8 *pDst, size_t nSize)
{
	FIL File;
	UINT nRead = 0;
	if (f_open (&File, pPath, FA_READ) == FR_OK)
	{
		f_read (&File, pDst, nSize, &nRead);
		f_close (&File);
	}
	return nRead;
}

static boolean WriteFile (const char *pPath, const u8 *pSrc, size_t nSize)
{
	FIL File;
	UINT nWritten = 0;
	if (f_open (&File, pPath, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK)
	{
		f_write (&File, pSrc, nSize, &nWritten);
		f_close (&File);
	}
	return nWritten == nSize;
}

CHiscore::CHiscore (void)
:	m_pCore (0),
	m_nState (Off),
	m_nGuard (0),
	m_nSize (0),
	m_pDefault (0),
	m_pCard (0),
	m_pNow (0),
	m_bCard (FALSE)
{
	m_Path[0] = '\0';
}

CHiscore::~CHiscore (void)
{
	Stop ();
}

void CHiscore::Start (const THiCore *pCore, const char *pPath)
{
	Stop ();
	m_pCore = pCore;
	m_nSize = pCore->pSize ();
	if (m_nSize == 0)
	{
		return;
	}
	m_pDefault = new u8[m_nSize];
	m_pCard = new u8[m_nSize];
	m_pNow = new u8[m_nSize];
	if (!m_pDefault || !m_pCard || !m_pNow)
	{
		Stop ();
		return;
	}
	strncpy (m_Path, pPath, sizeof m_Path - 1);
	m_Path[sizeof m_Path - 1] = '\0';
	m_bCard = ReadFile (m_Path, m_pCard, m_nSize) == m_nSize;
	m_nState = Waiting;
}

void CHiscore::Stop (void)
{
	delete [] m_pDefault;
	delete [] m_pCard;
	delete [] m_pNow;
	m_pDefault = m_pCard = m_pNow = 0;
	m_nSize = 0;
	m_bCard = FALSE;
	m_nState = Off;
}

// puts the saved table in as soon as the game has set its own up. The RAM may
// hold a table already (battery RAM, a reset) that the game then sets up again
// as it starts: for a while after, a new table of the game's gets the same
TCardResult CHiscore::Tick (void)
{
	if (m_nState == Active && m_nGuard > 0)
	{
		m_nGuard--;
		if (m_pCore->pReady ())
		{
			m_pCore->pGet (m_pNow);
			if (memcmp (m_pNow, m_pCard, m_nSize) != 0)
			{
				memcpy (m_pDefault, m_pNow, m_nSize);
				if (m_bCard)
				{
					m_pCore->pPut (m_pCard);
				}
				else
				{
					memcpy (m_pCard, m_pNow, m_nSize);
				}
			}
		}
		return CardNothing;
	}
	if (m_nState != Waiting || !m_pCore->pReady ())
	{
		return CardNothing;
	}
	m_nGuard = GuardFrames;
	m_pCore->pGet (m_pDefault);
	if (m_bCard)
	{
		m_pCore->pPut (m_pCard);
	}
	else
	{
		memcpy (m_pCard, m_pDefault, m_nSize);
	}
	m_nState = Active;
	return m_bCard ? CardLoaded : CardBlank;
}

TCardResult CHiscore::Save (void)
{
	if (m_nState != Active)
	{
		return CardNothing;
	}
	m_pCore->pGet (m_pNow);
	if (   memcmp (m_pNow, m_pCard, m_nSize) == 0
	    || memcmp (m_pNow, m_pDefault, m_nSize) == 0)
	{
		return CardNothing;
	}
	if (!WriteFile (m_Path, m_pNow, m_nSize))
	{
		return CardFailed;
	}
	memcpy (m_pCard, m_pNow, m_nSize);
	m_bCard = TRUE;
	return CardSaved;
}

CBatteryRam::CBatteryRam (void)
:	m_pCore (0),
	m_nSize (0),
	m_pCard (0),
	m_pBlank (0)
{
	m_Path[0] = '\0';
}

CBatteryRam::~CBatteryRam (void)
{
	delete [] m_pCard;
	delete [] m_pBlank;
}

TCardResult CBatteryRam::Start (const TNvCore *pCore, const char *pPath)
{
	delete [] m_pCard;
	delete [] m_pBlank;
	m_pCard = m_pBlank = 0;
	m_pCore = pCore;
	m_nSize = pCore->pSize ();
	if (   m_nSize == 0
	    || (m_pCard = new u8[m_nSize]) == 0
	    || (m_pBlank = new u8[m_nSize]) == 0)
	{
		m_nSize = 0;
		return CardNothing;
	}
	memcpy (m_pBlank, pCore->pData (), m_nSize);
	strncpy (m_Path, pPath, sizeof m_Path - 1);
	m_Path[sizeof m_Path - 1] = '\0';
	if (ReadFile (m_Path, m_pCard, m_nSize) == m_nSize)
	{
		memcpy (pCore->pData (), m_pCard, m_nSize);
		return CardLoaded;
	}
	memcpy (m_pCard, pCore->pData (), m_nSize);	// blank: written once it changes
	return CardBlank;
}

TCardResult CBatteryRam::Save (void)
{
	if (m_nSize == 0 || memcmp (m_pCore->pData (), m_pCard, m_nSize) == 0)
	{
		return CardNothing;
	}
	if (!WriteFile (m_Path, m_pCore->pData (), m_nSize))
	{
		return CardFailed;
	}
	memcpy (m_pCard, m_pCore->pData (), m_nSize);
	return CardSaved;
}

void CBatteryRam::Clear (void)
{
	if (m_nSize == 0)
	{
		return;
	}
	memcpy (m_pCore->pData (), m_pBlank, m_nSize);
	memcpy (m_pCard, m_pBlank, m_nSize);
	f_unlink (m_Path);
}
