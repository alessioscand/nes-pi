/*
 * nes-pi - the menus: game list, options, controls set-up, power off, credits
 */
#include "kernel.h"
#include "frame.h"

// the NES picture in the menus' layout (304x224): 8 lines off the top and
// bottom (as most TVs did), centred, for the darkened copy behind the menus
void CKernel::SnapshotForUI (void)
{
	static u32 Buf[UI_SRC_PITCH * UI_SRC_LINES];
	memset (Buf, 0, sizeof Buf);
	// the game behind the menus is sharp too, like the menus (whatever the TV filter)
	for (unsigned y = 0; y < UI_H; y++)
	{
		const u32 *s = &g_VideoBuf[(y + 8) * NES_W];
		u32 *d = &Buf[(y + UI_SRC_Y) * UI_SRC_PITCH + UI_SRC_X + (UI_W - NES_W) / 2];
		memcpy (d, s, NES_W * 4);
	}
	m_pUI->SnapshotGame (Buf);
}

void CKernel::ShowUIFrame (boolean bWait, const char *pTopMessage)
{
	m_Video.SetRacing (FALSE);
	m_Video.SetSource (FrameUISource ());
	m_Video.DrawGame (FrameUIBuffer (m_pUI->GetBuffer ()), TRUE);	// with the TV filter, like the game
	if (pTopMessage && pTopMessage[0])
	{
		// in the black band above the picture, like the in-game messages
		const CScreen::TTextSpan Span = {pTopMessage, UI_ERR};
		m_Video.DrawBandText (TRUE, &Span, 1);
	}
	else
	{
		m_Video.DrawBandText (TRUE, 0, 0);	// no status line or message over menus
	}
	m_Video.DrawBandText (FALSE, 0, 0);
	m_Video.Flip ();
	if (!bWait)
	{
		return;
	}
	if (m_bVSync)
	{
		m_Video.WaitForVSync ();
	}
	else
	{
		CTimer::SimpleusDelay (16667);
	}
}

unsigned CKernel::RunList (unsigned nSel)
{
	u32 nPrev = ~0u;		// require a release before acting
	unsigned nRepeat = 0;
	unsigned nFrame = 0;
	unsigned nTicks = 0;		// frames since the selection changed

	while (1)
	{
		if (nFrame++ % 15 == 0)
		{
			m_pInput->UpdateDevices ();
			m_pInput->FlushDiagnostics ();

			// a pad never seen before: set up its controls first
			int nPad = m_pInput->GetUnmappedPad ();
			if (nPad >= 0)
			{
				RunMapping ((unsigned) nPad, m_nLoaded >= 0);
				nPrev = ~0u;
				continue;
			}
		}

		u32 n = m_pInput->GetAny ();
		u32 nNew = n & ~nPrev;

		// up/down with auto-repeat, left/right = page
		u32 nMove = nNew;
		if (n & (BTN_UP | BTN_DOWN))
		{
			if (++nRepeat > 20 && nRepeat % 4 == 0)
			{
				nMove |= n & (BTN_UP | BTN_DOWN);
			}
		}
		else
		{
			nRepeat = 0;
		}
		unsigned nOld = nSel;
		if ((nMove & BTN_UP) && nSel > 0)			nSel--;
		if ((nMove & BTN_DOWN) && nSel + 1 < m_nGames)		nSel++;
		if (nNew & BTN_LEFT)	nSel = nSel >= 8 ? nSel - 8 : 0;
		if (nNew & BTN_RIGHT)	nSel = nSel + 8 < m_nGames ? nSel + 8 : m_nGames - 1;
		nTicks = nSel == nOld ? nTicks + 1 : 0;

		if (nNew & (BTN_A | BTN_START))
		{
			return nSel;
		}
		if (nNew & BTN_SELECT)
		{
			RunSystemMenu ();
			n = ~0u;		// wait for release
		}
		nPrev = n;

		const char *pMsg = m_pInput->GetPadCount () == 0 ? "CONNECT A USB GAMEPAD" : "";
		m_pUI->DrawList (m_pGames, m_nGames, nSel, "", m_nLoaded >= 0, nTicks);
		ShowUIFrame (TRUE, pMsg);
	}
}

void CKernel::RunSystemMenu (void)
{
	enum { SmTv, SmPalette, SmSprites, SmControls, SmPower, SmCount };

	unsigned nSel = 0;
	u32 nPrev = ~0u;
	int nOldPalette = g_Pic.nPalette;
	int nOldTv = g_Pic.nTvMode;
	boolean bOldScan = g_Pic.bTvScanlines;
	boolean bOldSprites = g_Pic.bUnlimSprites;
	const char *pMsg = "";
	unsigned nFrame = 0;

	// "Raspberry Pi 3 Model B+" -> "PI 3 B+"
	char Model[32];
	const char *pName = CMachineInfo::Get ()->GetMachineName ();
	if (strncmp (pName, "Raspberry ", 10) == 0) pName += 10;
	unsigned nLen = 0;
	for (; *pName && nLen < sizeof Model - 1; pName++)
	{
		if (strncmp (pName, "Model ", 6) == 0) pName += 6;
		char c = *pName;
		Model[nLen++] = c >= 'a' && c <= 'z' ? (char) (c - 32) : c;
	}
	Model[nLen] = '\0';
	char Footer[48];
	snprintf (Footer, sizeof Footer, "NES-PI " PROJECT_VERSION "  %s", Model);

	while (1)
	{
		if (nFrame++ % 15 == 0)
		{
			m_pInput->UpdateDevices ();
		}

		u32 n = m_pInput->GetAny ();
		u32 nNew = nPrev == ~0u ? 0 : (n & ~nPrev);
		nPrev = n;

		if (nNew & BTN_UP)
		{
			nSel = (nSel + SmCount - 1) % SmCount;
			pMsg = "";
		}
		if (nNew & BTN_DOWN)
		{
			nSel = (nSel + 1) % SmCount;
			pMsg = "";
		}
		int nDir = (nNew & (BTN_A | BTN_RIGHT)) ? 1 : ((nNew & BTN_LEFT) ? -1 : 0);

		if (nDir != 0)
		{
			switch (nSel)
			{
			case SmTv:
			{
				// OFF (sharp pixels) -> RGB (light scanlines) -> S-VIDEO ->
				// COMPOSITE -> RF; index 0 = OFF, then the NTSC modes
				static const int Order[5] = {NTSC_OFF, NTSC_OFF, NTSC_SVIDEO, NTSC_COMPOSITE, NTSC_RF};
				unsigned i = g_Pic.nTvMode == NTSC_OFF ? (g_Pic.bTvScanlines ? 1 : 0)
					   : g_Pic.nTvMode == NTSC_SVIDEO ? 2 : g_Pic.nTvMode == NTSC_COMPOSITE ? 3 : 4;
				i = (i + 5 + nDir) % 5;
				g_Pic.nTvMode = Order[i];
				g_Pic.bTvScanlines = i != 0;
				SetTvMode ();
				break;
			}

			case SmPalette:
			{
				int nCount = g_Pic.nCustomPal ? 4 : 3;	// CUSTOM only with nespi/palette.pal
				g_Pic.nPalette = (g_Pic.nPalette + nCount + nDir) % nCount;
				ApplyPalette ();
				if (m_nLoaded >= 0)
				{
					SnapshotForUI ();		// the paused game in the new colours
				}
				break;
			}

			case SmSprites:
				g_Pic.bUnlimSprites = !g_Pic.bUnlimSprites;
				nes_set_unlimited_sprites (g_Pic.bUnlimSprites);
				break;

			case SmControls:
				if (nNew & BTN_A)
				{
					int nPad = ChoosePad ();
					if (nPad >= 0)
					{
						RunMapping ((unsigned) nPad, m_nLoaded >= 0);
					}
					else if (m_pInput->GetPadCount () == 0)
					{
						pMsg = "CONNECT A USB GAMEPAD";
					}
					nPrev = ~0u;
				}
				break;

			case SmPower:
				if ((nNew & BTN_A) && PowerOffCountdown ())
				{
					PowerOff ();
				}
				nPrev = ~0u;
				break;
			}
		}

		if (nNew & BTN_B)
		{
			break;
		}

		static const char *Labels[SmCount] = {"TV FILTER", "PALETTE", "SPRITE FLICKER", "CONTROLS", "POWER OFF"};
		TMenuItem Items[SmCount];
		for (unsigned i = 0; i < SmCount; i++)
		{
			Items[i].pLabel = Labels[i];
			Items[i].pValue = 0;
			Items[i].bDisabled = FALSE;
		}
		static const char *TvLabels[4] = {"RGB", "S-VIDEO", "COMPOSITE", "RF"};
		Items[SmTv].pValue = g_Pic.nTvMode == NTSC_OFF && !g_Pic.bTvScanlines ? "OFF" : TvLabels[g_Pic.nTvMode & 3];
		static const char *PaletteLabels[4] = {"NESTOPIA", "CONSUMER", "SONY TV", "CUSTOM"};
		Items[SmPalette].pValue = PaletteLabels[g_Pic.nPalette & 3];
		Items[SmSprites].pValue = g_Pic.bUnlimSprites ? "REDUCED" : "ORIGINAL";

		static const TButtonHint Hints[2] = {{'A', "CHANGE"}, {'B', "BACK"}};
		m_pUI->DrawPanelMenu (m_nLoaded >= 0, "OPTIONS", Items, SmCount, nSel, pMsg, UI_VALUE,
				      Hints, 2, Footer);
		ShowUIFrame ();
	}

	if (g_Pic.nPalette != nOldPalette || g_Pic.bUnlimSprites != bOldSprites
	    || g_Pic.nTvMode != nOldTv || g_Pic.bTvScanlines != bOldScan)
	{
		SaveConfig ();
		char Line[140];
		snprintf (Line, sizeof Line, "New settings: TV filter %s, palette %s, sprites %s",
			  TvName (g_Pic.nTvMode), PaletteName (g_Pic.nPalette), g_Pic.bUnlimSprites ? "unlimited" : "8 per line");
		Log (Line, COLOR_TEXT);
	}
}

// CONTROLS from the SYSTEM menu: which pad? (the only one, or the one that is pressed)
int CKernel::ChoosePad (void)
{
	unsigned nCount = m_pInput->GetPadCount ();
	if (nCount == 0)
	{
		return -1;
	}
	if (nCount == 1)
	{
		for (unsigned i = 0; i < MAX_PADS; i++)
		{
			if (m_pInput->IsPadConnected (i))
			{
				return (int) i;
			}
		}
	}

	boolean bReleased = FALSE;
	for (unsigned f = 0; f < 600; f++)		// 10 seconds
	{
		int nActive = m_pInput->GetActivePad ();
		if (!bReleased)
		{
			bReleased = nActive < 0;
		}
		else if (nActive >= 0)
		{
			return nActive;
		}
		if (m_pInput->GetKeyboard () & BTN_MENU)
		{
			break;
		}
		m_pUI->DrawMessage ("CONTROLS", "PRESS A BUTTON ON THE PAD", "YOU WANT TO SET UP");
		ShowUIFrame ();
	}
	return -1;
}

// Asks for A B START SELECT in turn on one pad (directions are automatic),
// then saves on its own. A button already used is ignored; Esc on the
// keyboard cancels. (pads.cfg keeps the C and D slots of the shared input
// code: they stay unset here, and are ignored if an older set-up has them.)
boolean CKernel::RunMapping (unsigned nSlot, boolean bOverGame)
{
	static const char *Names[4] = {"A", "B", "START", "SELECT"};
	static const unsigned Slot[4] = {MAP_FIRST_BUTTON + 0, MAP_FIRST_BUTTON + 1,
					 MAP_FIRST_BUTTON + 4, MAP_FIRST_BUTTON + 5};
	const unsigned nSteps = 4;

	TBind Binds[MAP_COUNT];
	memset (Binds, 0, sizeof Binds);
	static char Values[4][16];
	const char *pValues[4];
	for (unsigned i = 0; i < 4; i++)
	{
		Values[i][0] = '\0';
		pValues[i] = Values[i];
	}

	unsigned nStep = 0;
	boolean bWaitRelease = TRUE;		// e.g. A still held from the SYSTEM menu
	u32 nKeyPrev = ~0u;
	unsigned nFrame = 0;

	char Line[120];
	snprintf (Line, sizeof Line, "Controls: setting up pad %u (%04x:%04x)",
		  nSlot + 1, m_pInput->GetVendor (nSlot), m_pInput->GetProduct (nSlot));
	Log (Line, COLOR_TEXT);

	char Title[32], Footer[40];
	snprintf (Title, sizeof Title, "CONTROLS  PLAYER %u", m_pInput->GetPlayerOfPad (nSlot) + 1);
	snprintf (Footer, sizeof Footer, "PAD %04X:%04X", m_pInput->GetVendor (nSlot),
		  m_pInput->GetProduct (nSlot));

	while (nStep < nSteps)
	{
		if (nFrame++ % 15 == 0)
		{
			m_pInput->UpdateDevices ();
			if (!m_pInput->IsPadConnected (nSlot))
			{
				Log ("  pad unplugged: set-up cancelled", COLOR_TEXT);
				return FALSE;
			}
		}

		u32 nKey = m_pInput->GetKeyboard ();
		u32 nKeyNew = nKeyPrev == ~0u ? 0 : (nKey & ~nKeyPrev);
		nKeyPrev = nKey;
		if (nKeyNew & BTN_MENU)
		{
			Log ("  set-up cancelled", COLOR_TEXT);
			return FALSE;
		}

		if (bWaitRelease)
		{
			bWaitRelease = !m_pInput->IsReleased (nSlot);
		}
		else
		{
			int nButton = m_pInput->CaptureButton (nSlot);
			if (nButton >= 0)
			{
				boolean bUsed = FALSE;
				for (unsigned i = 0; i < nStep; i++)
				{
					if (Binds[Slot[i]].nIndex == (u8) nButton)
					{
						bUsed = TRUE;
					}
				}
				if (!bUsed)
				{
					TBind &B = Binds[Slot[nStep]];
					B.nType = BindButton;
					B.nIndex = (u8) nButton;
					B.nValue = 0;
					CInput::DescribeBind (B, Values[nStep], sizeof Values[nStep]);
					nStep++;
				}
				bWaitRelease = TRUE;
			}
		}

		m_pUI->DrawMapping (bOverGame, Title, (int) nStep, pValues, Footer);
		ShowUIFrame ();
	}

	m_pInput->SetMap (nSlot, Binds);
	SavePads ();
	for (unsigned i = 0; i < nSteps; i++)
	{
		snprintf (Line, sizeof Line, "  %s = %s", Names[i], Values[i]);
		Log (Line, COLOR_TEXT);
	}
	Log ("  controls saved to nespi/pads.cfg", COLOR_TEXT);
	SaveLog ();

	// "SAVED" for one second
	for (unsigned f = 0; f < 60; f++)
	{
		m_pUI->DrawMapping (bOverGame, Title, (int) nSteps, pValues, Footer);
		ShowUIFrame ();
	}
	return TRUE;
}

boolean CKernel::PowerOffCountdown (void)
{
	// 3 seconds, B cancels; drawn over whatever the UI buffer shows
	static u32 Saved[UI_SRC_PITCH * UI_SRC_LINES];
	memcpy (Saved, m_pUI->GetBuffer (), sizeof Saved);
	u32 nPrev = ~0u;
	for (unsigned f = 0; f < 180; f++)
	{
		u32 n = m_pInput->GetAny ();
		u32 nNew = nPrev == ~0u ? 0 : (n & ~nPrev);
		nPrev = n;
		if (nNew & BTN_B)
		{
			memcpy (m_pUI->GetBuffer (), Saved, sizeof Saved);
			Log ("  power off cancelled", COLOR_TEXT);
			return FALSE;
		}
		char Line[24];
		snprintf (Line, sizeof Line, "POWER OFF IN %u", 3 - f / 60);
		memcpy (m_pUI->GetBuffer (), Saved, sizeof Saved);
		static const TButtonHint Cancel = {'B', "CANCEL"};
		m_pUI->DrawBox (Line, "", &Cancel);
		ShowUIFrame ();
	}
	return TRUE;
}

// Credits over the paused game; B (or Esc) goes back to the game
void CKernel::RunCredits (void)
{
	Log ("  credits", COLOR_TEXT);
	u32 nPrev = ~0u;
	for (unsigned nFrame = 0; ; nFrame++)
	{
		if (nFrame % 15 == 0)
		{
			m_pInput->UpdateDevices ();	// a new pad is set up after the credits
		}
		u32 n = m_pInput->GetAny ();
		u32 nNew = nPrev == ~0u ? 0 : (n & ~nPrev);
		if (nPrev == ~0u)
		{
			if (n == 0) nPrev = 0;	// wait for Select and D to be released
		}
		else
		{
			nPrev = n;
		}
		if (nNew & (BTN_B | BTN_MENU))
		{
			return;
		}
		m_pUI->DrawCredits (PROJECT_VERSION);
		ShowUIFrame ();
	}
}
