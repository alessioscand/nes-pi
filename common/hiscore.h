/*
 * Hiscores and battery RAM on the card
 *
 * CHiscore: MAME's hiscore plugin (hiscore.dat, MAME's .hi files): the table
 * is loaded once the game has set it up, saved when it changes.
 * CBatteryRam: the board's battery RAM (as MAME's NVRAM), loaded at start,
 * saved when it changes.
 * The core through THiCore / TNvCore; paths and log are the caller's.
 */
#ifndef HISCORE_H
#define HISCORE_H

#include <circle/types.h>
#include <stddef.h>

enum TCardResult
{
	CardNothing,		// nothing to do (no table, not ready, not changed)
	CardLoaded,		// the card's copy is in the game
	CardBlank,		// none on the card yet
	CardSaved,
	CardFailed		// could not be written
};

struct THiCore
{
	size_t	(*pSize) (void);		// bytes of the table (0 = no hiscores)
	int	(*pReady) (void);		// has the game set its table up?
	void	(*pGet) (u8 *pDst);
	void	(*pPut) (const u8 *pSrc);
};

struct TNvCore
{
	size_t	(*pSize) (void);		// 0 = nothing kept
	u8 *	(*pData) (void);
};

class CHiscore
{
public:
	CHiscore (void);
	~CHiscore (void);

	void Start (const THiCore *pCore, const char *pPath);	// a game is loaded or reset
	void Stop (void);
	TCardResult Tick (void);	// after each frame: CardLoaded / CardBlank once
	TCardResult Save (void);	// when it differs from the card's and the game's own

private:
	enum { Off, Waiting, Active };
	enum { GuardFrames = 600 };	// about 10 seconds, too short for a game to be played
	const THiCore *m_pCore;
	unsigned m_nState;
	unsigned m_nGuard;		// frames left watching for the game's own set-up
	size_t m_nSize;
	u8 *m_pDefault;			// the game's own table (power-on)
	u8 *m_pCard;			// what the .hi file holds
	u8 *m_pNow;
	boolean m_bCard;		// ... if there is one
	char m_Path[160];
};

class CBatteryRam
{
public:
	CBatteryRam (void);
	~CBatteryRam (void);

	TCardResult Start (const TNvCore *pCore, const char *pPath);	// CardLoaded / CardBlank
	TCardResult Save (void);	// CardSaved / CardFailed when it changed
	void Clear (void);		// back to the board's own contents, file deleted

private:
	const TNvCore *m_pCore;
	size_t m_nSize;
	u8 *m_pCard;			// what the file holds (or will)
	u8 *m_pBlank;			// the core's contents before the file (zeros, a factory EEPROM)
	char m_Path[160];
};

#endif
