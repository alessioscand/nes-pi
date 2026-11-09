/*
 * USB gamepads and keyboard -> up to four M92 control panels
 */
#ifndef INPUT_H
#define INPUT_H

#include <circle/usb/usbhcidevice.h>
#include <circle/usb/usbgamepad.h>
#include <circle/usb/usbkeyboard.h>
#include <circle/devicenameservice.h>
#include <circle/types.h>
#include "project.h"

// Logical buttons
#define BTN_UP		(1u << 0)
#define BTN_DOWN	(1u << 1)
#define BTN_LEFT	(1u << 2)
#define BTN_RIGHT	(1u << 3)
#define BTN_A		(1u << 4)
#define BTN_B		(1u << 5)
#define BTN_C		(1u << 6)
#define BTN_D		(1u << 7)
#define BTN_START	(1u << 8)
#define BTN_SELECT	(1u << 9)
#define BTN_E		(1u << 10)	// buttons 5 and 6 (PROJECT_BUTTONS 6)
#define BTN_F		(1u << 11)
#define BTN_MENU	(1u << 12)	// keyboard Esc (= Select+Start)

#define MAX_PADS	4

// Controls that can be assigned (bit n of a mapping = logical bit n above)
#if PROJECT_BUTTONS == 6
#define MAP_COUNT	12	// UP DOWN LEFT RIGHT A B C D START SELECT E F
#define MAP_E		10	// E and F can be skipped (pads with 4 buttons)
#define MAP_F		11
#else
#define MAP_COUNT	10	// UP DOWN LEFT RIGHT A B C D START SELECT
#endif
#define MAP_FIRST_BUTTON 4	// only the buttons are set up (directions are automatic)
#define MAX_MAPS	32	// pad models remembered in pads.cfg

enum TBindType { BindNone, BindButton, BindHat, BindAxis };

struct TBind
{
	u8	nType;		// TBindType
	u8	nIndex;		// button, hat or axis number
	s8	nValue;		// hat: direction 0-7 (0 = up, clockwise); axis: -1 / +1
};

struct TPadMap
{
	u16	usVendor;
	u16	usProduct;
	TBind	Bind[MAP_COUNT];
};

class CInput
{
public:
	CInput (CUSBHCIDevice *pUSB, CDeviceNameService *pNames);

	// Scan for newly attached devices (task level, not every frame)
	void UpdateDevices (void);

	// Latest state of player 0 or 1 (pads in connection order; keyboard = player 1)
	u32 Get (unsigned nPlayer) const;

	// lag measurement: time (CTimer ticks, us) of the oldest pad report that
	// changed the controls and has not been taken yet; FALSE if none
	boolean TakeChange (unsigned *pTicks);
	// Any player: used by the menus
	u32 GetAny (void) const	{ return Get (0) | Get (1); }

	unsigned GetPadCount (void) const;

	// Raw-report diagnostics: logs the first changes of each pad (call at task level)
	void FlushDiagnostics (void);
	void RestartDiagnostics (void);		// log the first changes again (new game)

	// ---- control mapping ----
	// pads.cfg: one line per pad model (vendor:product)
	void LoadMaps (const char *pText);
	void FormatMaps (char *pOut, size_t nSize) const;

	// connected pad without a saved mapping, not asked yet in this session (-1 = none)
	int GetUnmappedPad (void);
	// pad slot that is sending any input right now (-1 = none)
	int GetActivePad (void) const;
	boolean IsPadConnected (unsigned nSlot) const	{ return nSlot < MAX_PADS && m_pPad[nSlot] != 0; }
	unsigned GetPlayerOfPad (unsigned nSlot) const;	// 0-based
	u16 GetVendor (unsigned nSlot) const;
	u16 GetProduct (unsigned nSlot) const;

	// a button or the d-pad hat is pressed
	boolean IsButtonPressed (unsigned nSlot) const;
	// set-up: no button pressed on this pad
	boolean IsReleased (unsigned nSlot) const;
	// set-up: number of the lowest pressed button (-1 = none)
	int CaptureButton (unsigned nSlot) const;
	// save a mapping for this pad model and use it at once
	void SetMap (unsigned nSlot, const TBind *pBinds);
	static void DescribeBind (const TBind &rBind, char *pOut, size_t nSize);

	u32 GetKeyboard (void) const	{ return s_KeyState; }


private:
	static void GamePadStatusHandler (unsigned nIndex, const TGamePadState *pState);
	static void GamePadRemovedHandler (CDevice *pDevice, void *pContext);
	static void KeyboardRawHandler (unsigned char ucModifiers, const unsigned char RawKeys[6]);
	static void KeyboardRemovedHandler (CDevice *pDevice, void *pContext);
	static u32 MapPad (unsigned nIndex, const TGamePadState *pState);
	static boolean BindActive (const TBind &rBind, const TGamePadState *pState);
	static int FindMap (u16 usVendor, u16 usProduct);
	void UpdateSlotMap (unsigned nSlot);

private:
	CUSBHCIDevice		*m_pUSB;
	CDeviceNameService	*m_pNames;
	CUSBGamePadDevice	*m_pPad[MAX_PADS];
	CUSBKeyboardDevice	*m_pKeyboard;

	static CInput		*s_pThis;
	static volatile u32	 s_PadState[MAX_PADS];
	static volatile u32	 s_KeyState;
	static unsigned		 s_PadProps[MAX_PADS];

	// mapping
	static TPadMap		 s_Maps[MAX_MAPS];
	static unsigned		 s_nMaps;
	static volatile int	 s_SlotMap[MAX_PADS];	// index in s_Maps, -1 = default layout
	static TGamePadState	 s_Last[MAX_PADS];	// latest raw report
	TGamePadState		 m_Rest[MAX_PADS];	// state at rest (reference for the capture)
	static volatile boolean	 s_bGotReport[MAX_PADS];
	static volatile boolean	 s_bChange;		// a report changed the controls
	static volatile unsigned s_nChangeTicks;	// when (the oldest not taken)
	boolean			 m_bAsked[MAX_PADS];

	// diagnostics ring (filled in interrupt context, logged at task level)
	struct TRaw { u8 nPad; u32 nButtons; int nHat; int nAx0, nAx1, nAx2, nAx3; u32 nMapped; };
	enum { RawSize = 64 };
	static TRaw		 s_Raw[RawSize];
	static volatile unsigned s_nRawIn;
	unsigned		 m_nRawOut;
	static unsigned		 s_nRawPerPad[MAX_PADS];
};

#endif
