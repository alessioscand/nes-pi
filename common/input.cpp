/*
 * USB gamepads and keyboard
 */
#include "input.h"
#include "project.h"
#include <circle/usb/usbhid.h>
#include <circle/usb/usbdevice.h>
#include <circle/util.h>
#include <circle/timer.h>
#include <stdio.h>

CInput *CInput::s_pThis = 0;
volatile u32 CInput::s_PadState[MAX_PADS];
volatile u32 CInput::s_KeyState;
unsigned CInput::s_PadProps[MAX_PADS];
CInput::TRaw CInput::s_Raw[CInput::RawSize];
volatile unsigned CInput::s_nRawIn = 0;
unsigned CInput::s_nRawPerPad[MAX_PADS];
TPadMap CInput::s_Maps[MAX_MAPS];
unsigned CInput::s_nMaps = 0;
volatile int CInput::s_SlotMap[MAX_PADS];
TGamePadState CInput::s_Last[MAX_PADS];
volatile boolean CInput::s_bGotReport[MAX_PADS];
volatile boolean CInput::s_bChange = FALSE;
volatile unsigned CInput::s_nChangeTicks = 0;

static const char *MapKeys[MAP_COUNT] =
	{"UP", "DOWN", "LEFT", "RIGHT", "A", "B", "C", "D", "START", "SELECT",
#if PROJECT_BUTTONS == 6
	 "E", "F"
#endif
	};

#define RAW_LOG_PER_PAD	40	// first raw changes logged for each pad

CInput::CInput (CUSBHCIDevice *pUSB, CDeviceNameService *pNames)
:	m_pUSB (pUSB),
	m_pNames (pNames),
	m_pKeyboard (0),
	m_nRawOut (0)
{
	s_pThis = this;
	for (unsigned i = 0; i < MAX_PADS; i++)
	{
		m_pPad[i] = 0;
		s_PadState[i] = 0;
		s_PadProps[i] = 0;
		s_nRawPerPad[i] = 0;
		s_SlotMap[i] = -1;
		m_bAsked[i] = FALSE;
		memset (&m_Rest[i], 0, sizeof m_Rest[i]);
		memset (&s_Last[i], 0, sizeof s_Last[i]);
	}
	s_KeyState = 0;
}

#ifdef PROJECT_FAKEPAD
// QEMU has no USB gamepad: a fake one in slot 0, driven by the keyboard
// (arrows = hat, Q W E R T Y U I O P = buttons 1-10), id 1234:5678
#define FAKE_PAD	((CUSBGamePadDevice *) 1)
static TGamePadState s_FakeState;
#endif

void CInput::UpdateDevices (void)
{
#ifdef PROJECT_FAKEPAD
	if (m_pPad[0] == 0)
	{
		memset (&s_FakeState, 0, sizeof s_FakeState);
		s_FakeState.nbuttons = 12;
		s_FakeState.nhats = 1;
		s_FakeState.hats[0] = 8;
		s_FakeState.naxes = 2;
		for (int a = 0; a < 2; a++)
		{
			s_FakeState.axes[a].minimum = 0;
			s_FakeState.axes[a].maximum = 255;
			s_FakeState.axes[a].value = 128;
		}
		m_pPad[0] = FAKE_PAD;
		m_Rest[0] = s_FakeState;
		m_Rest[0].hats[0] = -1;
		s_Last[0] = s_FakeState;
		UpdateSlotMap (0);
		project_log ("Fake pad 1 (QEMU): 1234:5678, %s",
			   s_SlotMap[0] >= 0 ? "controls set up" : "controls to set up");
	}
#endif
	if (!m_pUSB->UpdatePlugAndPlay ())
	{
		return;
	}

	for (unsigned i = 0; i < MAX_PADS; i++)
	{
		if (m_pPad[i] != 0)
		{
			continue;
		}
		m_pPad[i] = (CUSBGamePadDevice *) m_pNames->GetDevice ("upad", i + 1, FALSE);
		if (m_pPad[i] == 0)
		{
			continue;
		}

		s_PadProps[i] = m_pPad[i]->GetProperties ();
		const TGamePadState *pState = m_pPad[i]->GetInitialState ();
		memset (&m_Rest[i], 0, sizeof m_Rest[i]);
		if (pState)
		{
			m_Rest[i] = *pState;		// state at rest: reference for the capture
		}
		for (int h = 0; h < MAX_HATS; h++)
		{
			if (m_Rest[i].hats[h] >= 0 && m_Rest[i].hats[h] <= 7)
			{
				m_Rest[i].hats[h] = -1;	// a hat is never pressed at rest
			}
		}
		s_Last[i] = m_Rest[i];
		s_bGotReport[i] = FALSE;
		UpdateSlotMap (i);
		project_log ("Pad %u connected: %04x:%04x, %s, %d buttons, %d hats, %d axes, %s",
			   i + 1, GetVendor (i), GetProduct (i),
			   (s_PadProps[i] & GamePadPropertyIsKnown) ? "known layout" : "generic HID",
			   pState ? pState->nbuttons : -1, pState ? pState->nhats : -1,
			   pState ? pState->naxes : -1,
			   s_SlotMap[i] >= 0 ? "controls set up" : "controls to set up");
		for (int a = 0; pState && a < pState->naxes && a < 8; a++)
		{
			project_log ("  axis %d: %d..%d, at rest %d", a, pState->axes[a].minimum,
				   pState->axes[a].maximum, pState->axes[a].value);
		}

		m_pPad[i]->RegisterRemovedHandler (GamePadRemovedHandler, this);
		m_pPad[i]->RegisterStatusHandler (GamePadStatusHandler);
	}

	if (m_pKeyboard == 0)
	{
		m_pKeyboard = (CUSBKeyboardDevice *) m_pNames->GetDevice ("ukbd1", FALSE);
		if (m_pKeyboard != 0)
		{
			project_log ("USB keyboard connected");
			m_pKeyboard->RegisterRemovedHandler (KeyboardRemovedHandler, this);
			m_pKeyboard->RegisterKeyStatusHandlerRaw (KeyboardRawHandler);
		}
	}
}

boolean CInput::TakeChange (unsigned *pTicks)
{
	if (!s_bChange)
	{
		return FALSE;
	}
	*pTicks = s_nChangeTicks;
	s_bChange = FALSE;
	return TRUE;
}

unsigned CInput::GetPadCount (void) const
{
	unsigned n = 0;
	for (unsigned i = 0; i < MAX_PADS; i++)
	{
		if (m_pPad[i] != 0)
		{
			n++;
		}
	}
	return n;
}

u32 CInput::Get (unsigned nPlayer) const
{
	// players are assigned in pad connection order, skipping empty slots
	u32 nState = 0;
	unsigned nFound = 0;
	for (unsigned i = 0; i < MAX_PADS; i++)
	{
		if (m_pPad[i] == 0)
		{
			continue;
		}
		if (nFound++ == nPlayer)
		{
			nState = s_PadState[i];
			break;
		}
	}
	if (nPlayer == 0)
	{
		nState |= s_KeyState;
	}
	return nState;
}

static boolean AxisDir (const TGamePadState *pState, int nAxis, int *pDir)
{
	if (nAxis >= pState->naxes)
	{
		return FALSE;
	}
	int nMin = pState->axes[nAxis].minimum;
	int nMax = pState->axes[nAxis].maximum;
	if (nMax <= nMin)
	{
		return FALSE;
	}
	int nCenter = (nMin + nMax) / 2;
	int nDead = (nMax - nMin) * 3 / 10;		// 30% of the range from the centre
	int v = pState->axes[nAxis].value;
	*pDir = v < nCenter - nDead ? -1 : (v > nCenter + nDead ? 1 : 0);
	return TRUE;
}

u32 CInput::MapPad (unsigned nIndex, const TGamePadState *pState)
{
	u32 b = pState->buttons;
	u32 n = 0;

	if (s_PadProps[nIndex] & GamePadPropertyIsKnown)
	{
		// Known layout (Xbox naming): A bottom, B right, X left, Y top
		if (b & GamePadButtonUp)	n |= BTN_UP;
		if (b & GamePadButtonDown)	n |= BTN_DOWN;
		if (b & GamePadButtonLeft)	n |= BTN_LEFT;
		if (b & GamePadButtonRight)	n |= BTN_RIGHT;
		if (b & GamePadButtonA)		n |= BTN_A;
		if (b & GamePadButtonB)		n |= BTN_B;
		if (b & GamePadButtonX)		n |= BTN_C;
		if (b & GamePadButtonY)		n |= BTN_D;
#if PROJECT_BUTTONS == 6
		if (b & GamePadButtonLB)	n |= BTN_E;
		if (b & GamePadButtonRB)	n |= BTN_F;
#endif
		if (b & (GamePadButtonStart | GamePadButtonPlus))	n |= BTN_START;
		if (b & (GamePadButtonSelect | GamePadButtonMinus))	n |= BTN_SELECT;
	}
	else
	{
		// Generic HID pad / arcade encoder: buttons 1-4 (1-6) = A-D (A-F), 9 = Select, 10 = Start
		if (b & (1u << 0))	n |= BTN_A;
		if (b & (1u << 1))	n |= BTN_B;
		if (b & (1u << 2))	n |= BTN_C;
		if (b & (1u << 3))	n |= BTN_D;
#if PROJECT_BUTTONS == 6
		if (b & (1u << 4))	n |= BTN_E;
		if (b & (1u << 5))	n |= BTN_F;
#endif
		if (b & (1u << 8))	n |= BTN_SELECT;
		if (b & (1u << 9))	n |= BTN_START;

		if (pState->nhats > 0)
		{
			int h = pState->hats[0];		// 0 = up, clockwise, else centred
			if (h == 7 || h == 0 || h == 1)	n |= BTN_UP;
			if (h >= 1 && h <= 3)		n |= BTN_RIGHT;
			if (h >= 3 && h <= 5)		n |= BTN_DOWN;
			if (h >= 5 && h <= 7)		n |= BTN_LEFT;
		}
	}

	// Left stick works as the joystick on every pad
	int nDir;
	if (AxisDir (pState, 0, &nDir))
	{
		if (nDir < 0) n |= BTN_LEFT;
		if (nDir > 0) n |= BTN_RIGHT;
	}
	if (AxisDir (pState, 1, &nDir))
	{
		if (nDir < 0) n |= BTN_UP;
		if (nDir > 0) n |= BTN_DOWN;
	}

	// buttons chosen by the user replace the automatic ones (directions stay)
	int nMap = s_SlotMap[nIndex];
	if (nMap >= 0)
	{
		const TPadMap &rMap = s_Maps[nMap];
		n &= BTN_UP | BTN_DOWN | BTN_LEFT | BTN_RIGHT;
		for (unsigned i = MAP_FIRST_BUTTON; i < MAP_COUNT; i++)
		{
			if (BindActive (rMap.Bind[i], pState))
			{
				n |= 1u << i;
			}
		}
	}

	return n;
}

void CInput::GamePadStatusHandler (unsigned nIndex, const TGamePadState *pState)
{
	if (nIndex >= MAX_PADS || pState == 0)
	{
		return;
	}
	u32 nMapped = MapPad (nIndex, pState);
	if (nMapped != s_PadState[nIndex] && !s_bChange)
	{
		s_nChangeTicks = CTimer::GetClockTicks ();	// lag measurement
		s_bChange = TRUE;
	}
	s_PadState[nIndex] = nMapped;
	s_bGotReport[nIndex] = TRUE;

	// diagnostics: only reports that differ from the previous one
	const TGamePadState &rPrev = s_Last[nIndex];
	boolean bChanged =    pState->buttons != rPrev.buttons
			   || (pState->nhats > 0 && pState->hats[0] != rPrev.hats[0]);
	for (int a = 0; a < pState->naxes && a < 4; a++)
	{
		int d = pState->axes[a].value - rPrev.axes[a].value;
		int r = (pState->axes[a].maximum - pState->axes[a].minimum) / 8;
		if (d > r || d < -r)
		{
			bChanged = TRUE;
		}
	}
	s_Last[nIndex] = *pState;

	if (bChanged && s_nRawPerPad[nIndex] < RAW_LOG_PER_PAD)
	{
		s_nRawPerPad[nIndex]++;
		TRaw &r = s_Raw[s_nRawIn % RawSize];
		r.nPad = (u8) nIndex;
		r.nButtons = pState->buttons;
		r.nHat = pState->nhats > 0 ? pState->hats[0] : -1;
		r.nAx0 = pState->naxes > 0 ? pState->axes[0].value : 0;
		r.nAx1 = pState->naxes > 1 ? pState->axes[1].value : 0;
		r.nAx2 = pState->naxes > 2 ? pState->axes[2].value : 0;
		r.nAx3 = pState->naxes > 3 ? pState->axes[3].value : 0;
		r.nMapped = nMapped;
		s_nRawIn++;
	}
}

void CInput::FlushDiagnostics (void)
{
	while (m_nRawOut != s_nRawIn)
	{
		const TRaw &r = s_Raw[m_nRawOut % RawSize];
		project_log ("  pad %u: buttons 0x%x hat %d axes %d %d %d %d -> " PROJECT_NAME " 0x%x",
			   r.nPad + 1, r.nButtons, r.nHat, r.nAx0, r.nAx1, r.nAx2, r.nAx3, r.nMapped);
		m_nRawOut++;
	}
}

void CInput::GamePadRemovedHandler (CDevice *pDevice, void *pContext)
{
	CInput *pThis = (CInput *) pContext;
	for (unsigned i = 0; i < MAX_PADS; i++)
	{
		if (pThis->m_pPad[i] == (CUSBGamePadDevice *) pDevice)
		{
			pThis->m_pPad[i] = 0;
			s_PadState[i] = 0;
			s_SlotMap[i] = -1;
			pThis->m_bAsked[i] = FALSE;
		}
	}
}

void CInput::KeyboardRemovedHandler (CDevice *, void *pContext)
{
	CInput *pThis = (CInput *) pContext;
	pThis->m_pKeyboard = 0;
	s_KeyState = 0;
}

void CInput::KeyboardRawHandler (unsigned char, const unsigned char RawKeys[6])
{
	// Hidden fallback for testing: arrows, Z X C V (B N) = A B C D (E F),
	// Enter = Start, 5 / Space = Select (coin), Esc = menu
	u32 n = 0;
	for (unsigned i = 0; i < 6; i++)
	{
		switch (RawKeys[i])
		{
		case 0x52: n |= BTN_UP;		break;
		case 0x51: n |= BTN_DOWN;	break;
		case 0x50: n |= BTN_LEFT;	break;
		case 0x4F: n |= BTN_RIGHT;	break;
		case 0x1D: n |= BTN_A;		break;	// Z
		case 0x1B: n |= BTN_B;		break;	// X
		case 0x06: n |= BTN_C;		break;	// C
		case 0x19: n |= BTN_D;		break;	// V
#if PROJECT_BUTTONS == 6
		case 0x05: n |= BTN_E;		break;	// B
		case 0x11: n |= BTN_F;		break;	// N
#endif
		case 0x28: n |= BTN_START;	break;	// Enter
		case 0x22: case 0x2C: n |= BTN_SELECT; break;	// 5, Space
		case 0x29: n |= BTN_MENU;	break;	// Esc
		default: break;
		}
	}
	s_KeyState = n;

#ifdef PROJECT_FAKEPAD
	static const unsigned char Btn[10] = {0x14, 0x1A, 0x08, 0x15, 0x17, 0x1C, 0x18, 0x0C, 0x12, 0x13};	// Q..P
	TGamePadState S = s_FakeState;
	S.buttons = 0;
	int nU = 0, nD = 0, nL = 0, nR = 0;
	for (unsigned i = 0; i < 6; i++)
	{
		for (unsigned b = 0; b < 10; b++)
		{
			if (RawKeys[i] == Btn[b]) S.buttons |= 1u << b;
		}
		if (RawKeys[i] == 0x52) nU = 1;
		if (RawKeys[i] == 0x51) nD = 1;
		if (RawKeys[i] == 0x50) nL = 1;
		if (RawKeys[i] == 0x4F) nR = 1;
	}
	S.hats[0] = nU ? (nR ? 1 : (nL ? 7 : 0)) : nD ? (nR ? 3 : (nL ? 5 : 4)) : nR ? 2 : nL ? 6 : 8;
	GamePadStatusHandler (0, &S);
#endif
}

// ---- control mapping -------------------------------------------------------

u16 CInput::GetVendor (unsigned nSlot) const
{
	if (nSlot >= MAX_PADS || m_pPad[nSlot] == 0)
	{
		return 0;
	}
#ifdef PROJECT_FAKEPAD
	if (m_pPad[nSlot] == FAKE_PAD)
	{
		return 0x1234;
	}
#endif
	const TUSBDeviceDescriptor *pDesc = m_pPad[nSlot]->GetDevice ()->GetDeviceDescriptor ();
	return pDesc ? pDesc->idVendor : 0;
}

u16 CInput::GetProduct (unsigned nSlot) const
{
	if (nSlot >= MAX_PADS || m_pPad[nSlot] == 0)
	{
		return 0;
	}
#ifdef PROJECT_FAKEPAD
	if (m_pPad[nSlot] == FAKE_PAD)
	{
		return 0x5678;
	}
#endif
	const TUSBDeviceDescriptor *pDesc = m_pPad[nSlot]->GetDevice ()->GetDeviceDescriptor ();
	return pDesc ? pDesc->idProduct : 0;
}

unsigned CInput::GetPlayerOfPad (unsigned nSlot) const
{
	unsigned n = 0;
	for (unsigned i = 0; i < nSlot && i < MAX_PADS; i++)
	{
		if (m_pPad[i] != 0)
		{
			n++;
		}
	}
	return n;
}

int CInput::FindMap (u16 usVendor, u16 usProduct)
{
	for (unsigned i = 0; i < s_nMaps; i++)
	{
		if (s_Maps[i].usVendor == usVendor && s_Maps[i].usProduct == usProduct)
		{
			return (int) i;
		}
	}
	return -1;
}

void CInput::UpdateSlotMap (unsigned nSlot)
{
	s_SlotMap[nSlot] = m_pPad[nSlot] ? FindMap (GetVendor (nSlot), GetProduct (nSlot)) : -1;
}

boolean CInput::BindActive (const TBind &rBind, const TGamePadState *pState)
{
	switch (rBind.nType)
	{
	case BindButton:
		return (pState->buttons >> rBind.nIndex) & 1;

	case BindHat: {
		if (rBind.nIndex >= pState->nhats)
		{
			return FALSE;
		}
		int h = pState->hats[rBind.nIndex];
		if (h < 0 || h > 7)
		{
			return FALSE;
		}
		int d = (h - rBind.nValue + 8) % 8;	// diagonals count for both directions
		return d == 0 || d == 1 || d == 7; }

	case BindAxis: {
		if (rBind.nIndex >= pState->naxes)
		{
			return FALSE;
		}
		int nMin = pState->axes[rBind.nIndex].minimum;
		int nMax = pState->axes[rBind.nIndex].maximum;
		if (nMax <= nMin)
		{
			return FALSE;
		}
		int nCenter = (nMin + nMax) / 2;
		int nDead = (nMax - nMin) * 3 / 10;
		int v = pState->axes[rBind.nIndex].value;
		return rBind.nValue < 0 ? v < nCenter - nDead : v > nCenter + nDead; }

	default:
		return FALSE;
	}
}




boolean CInput::IsButtonPressed (unsigned nSlot) const
{
	if (nSlot >= MAX_PADS || m_pPad[nSlot] == 0 || !s_bGotReport[nSlot])
	{
		return FALSE;
	}
	const TGamePadState &s = s_Last[nSlot];
	if (s.buttons != 0)
	{
		return TRUE;
	}
	for (int h = 0; h < s.nhats && h < MAX_HATS; h++)
	{
		if (s.hats[h] >= 0 && s.hats[h] <= 7)
		{
			return TRUE;
		}
	}
	return FALSE;
}

int CInput::GetActivePad (void) const
{
	for (unsigned i = 0; i < MAX_PADS; i++)
	{
		if (m_pPad[i] != 0 && IsButtonPressed (i))
		{
			return (int) i;
		}
	}
	return -1;
}

int CInput::GetUnmappedPad (void)
{
	for (unsigned i = 0; i < MAX_PADS; i++)
	{
		if (m_pPad[i] != 0 && s_SlotMap[i] < 0 && !m_bAsked[i])
		{
			m_bAsked[i] = TRUE;
			return (int) i;
		}
	}
	return -1;
}

void CInput::SetMap (unsigned nSlot, const TBind *pBinds)
{
	u16 usVendor = GetVendor (nSlot), usProduct = GetProduct (nSlot);
	int n = FindMap (usVendor, usProduct);
	if (n < 0)
	{
		if (s_nMaps >= MAX_MAPS)
		{
			// table full: forget the oldest model
			memmove (&s_Maps[0], &s_Maps[1], sizeof s_Maps[0] * (MAX_MAPS - 1));
			s_nMaps--;
		}
		n = (int) s_nMaps++;
	}
	TPadMap &rMap = s_Maps[n];
	rMap.usVendor = usVendor;
	rMap.usProduct = usProduct;
	memcpy (rMap.Bind, pBinds, sizeof rMap.Bind);

	// every connected pad of the same model uses it
	for (unsigned i = 0; i < MAX_PADS; i++)
	{
		UpdateSlotMap (i);
		s_PadState[i] = 0;
	}
}

static unsigned ParseNum (const char **pp)
{
	unsigned n = 0;
	while (**pp >= '0' && **pp <= '9')
	{
		n = n * 10 + (unsigned) (**pp - '0');
		(*pp)++;
	}
	return n;
}

static int Hex (char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

void CInput::LoadMaps (const char *pText)
{
	s_nMaps = 0;
	const char *p = pText;
	while (*p && s_nMaps < MAX_MAPS)
	{
		// line: vvvv:pppp UP=h0:0 DOWN=h0:4 ... SELECT=b8
		const char *pLineEnd = p;
		while (*pLineEnd && *pLineEnd != '\n')
		{
			pLineEnd++;
		}

		unsigned v = 0, d = 0;
		int i = 0, x;
		for (; i < 4 && (x = Hex (p[i])) >= 0; i++) v = v * 16 + (unsigned) x;
		if (i == 4 && p[4] == ':')
		{
			unsigned pr = 0;
			for (i = 5; i < 9 && (x = Hex (p[i])) >= 0; i++) pr = pr * 16 + (unsigned) x;
			if (i == 9)
			{
				TPadMap &rMap = s_Maps[s_nMaps];
				memset (&rMap, 0, sizeof rMap);
				rMap.usVendor = (u16) v;
				rMap.usProduct = (u16) pr;

				const char *q = p + 9;
				while (q < pLineEnd)
				{
					while (q < pLineEnd && (*q == ' ' || *q == '\t' || *q == '\r')) q++;
					const char *pKey = q;
					while (q < pLineEnd && *q != '=' && *q != ' ') q++;
					if (q >= pLineEnd || *q != '=')
					{
						break;
					}
					size_t nKeyLen = (size_t) (q - pKey);
					q++;
					for (d = 0; d < MAP_COUNT; d++)
					{
						if (strlen (MapKeys[d]) == nKeyLen && strncmp (MapKeys[d], pKey, nKeyLen) == 0)
						{
							break;
						}
					}
					TBind B = {BindNone, 0, 0};
					if (*q == 'b')
					{
						q++; B.nType = BindButton; B.nIndex = (u8) ParseNum (&q);
					}
					else if (*q == 'h')
					{
						q++; B.nType = BindHat; B.nIndex = (u8) ParseNum (&q);
						if (*q == ':') { q++; B.nValue = (s8) (ParseNum (&q) & 7); }
					}
					else if (*q == 'a')
					{
						q++; B.nType = BindAxis; B.nIndex = (u8) ParseNum (&q);
						B.nValue = *q == '-' ? -1 : 1;
					}
					if (d < MAP_COUNT)
					{
						rMap.Bind[d] = B;
					}
					while (q < pLineEnd && *q != ' ' && *q != '\r') q++;
				}
				s_nMaps++;
			}
		}
		p = *pLineEnd ? pLineEnd + 1 : pLineEnd;
	}
	for (unsigned i = 0; i < MAX_PADS; i++)
	{
		UpdateSlotMap (i);
	}
	project_log ("Controls: %u pad models set up", s_nMaps);
}

void CInput::FormatMaps (char *pOut, size_t nSize) const
{
	size_t n = (size_t) snprintf (pOut, nSize,
		"# " PROJECT_NAME ": controls per pad model (vendor:product)\r\n"
		"# b = button number (directions are automatic)\r\n");
	for (unsigned m = 0; m < s_nMaps && n < nSize; m++)
	{
		n += (size_t) snprintf (pOut + n, nSize - n, "%04x:%04x", s_Maps[m].usVendor, s_Maps[m].usProduct);
		for (unsigned i = 0; i < MAP_COUNT && n < nSize; i++)
		{
			const TBind &B = s_Maps[m].Bind[i];
			switch (B.nType)
			{
			case BindButton: n += (size_t) snprintf (pOut + n, nSize - n, " %s=b%u", MapKeys[i], B.nIndex); break;
			case BindHat:	 n += (size_t) snprintf (pOut + n, nSize - n, " %s=h%u:%d", MapKeys[i], B.nIndex, B.nValue); break;
			case BindAxis:	 n += (size_t) snprintf (pOut + n, nSize - n, " %s=a%u%c", MapKeys[i], B.nIndex, B.nValue < 0 ? '-' : '+'); break;
			default:	 n += (size_t) snprintf (pOut + n, nSize - n, " %s=-", MapKeys[i]); break;
			}
		}
		if (n < nSize)
		{
			n += (size_t) snprintf (pOut + n, nSize - n, "\r\n");
		}
	}
}

void CInput::DescribeBind (const TBind &rBind, char *pOut, size_t nSize)
{
	static const char *HatDir[8] = {"UP", "UP-R", "RIGHT", "DN-R", "DOWN", "DN-L", "LEFT", "UP-L"};
	switch (rBind.nType)
	{
	case BindButton: snprintf (pOut, nSize, "BUTTON %u", rBind.nIndex + 1); break;
	case BindHat:	 snprintf (pOut, nSize, "DPAD %s", HatDir[rBind.nValue & 7]); break;
	case BindAxis:	 snprintf (pOut, nSize, "STICK %u%c", rBind.nIndex + 1, rBind.nValue < 0 ? '-' : '+'); break;
	default:	 snprintf (pOut, nSize, "-"); break;
	}
}


boolean CInput::IsReleased (unsigned nSlot) const
{
	if (nSlot >= MAX_PADS || m_pPad[nSlot] == 0 || !s_bGotReport[nSlot])
	{
		return TRUE;
	}
	return s_Last[nSlot].buttons == 0;
}

// lowest pressed button of this pad (-1 = none)
int CInput::CaptureButton (unsigned nSlot) const
{
	if (nSlot >= MAX_PADS || m_pPad[nSlot] == 0 || !s_bGotReport[nSlot])
	{
		return -1;
	}
	u32 b = s_Last[nSlot].buttons;
	for (int i = 0; i < 32; i++)
	{
		if (b & (1u << i))
		{
			return i;
		}
	}
	return -1;
}

boolean CInput::IsButtonBound (const TBind *pBinds, unsigned nCount, int nButton)
{
	for (unsigned i = 0; i < nCount; i++)
	{
		if (pBinds[i].nType == BindButton && pBinds[i].nIndex == (u8) nButton)
		{
			return TRUE;
		}
	}
	return FALSE;
}

void CInput::RestartDiagnostics (void)
{
	for (unsigned i = 0; i < MAX_PADS; i++)
	{
		s_nRawPerPad[i] = 0;
	}
}
