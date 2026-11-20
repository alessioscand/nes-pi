/*
 * nes-pi - menus: game list, OPTIONS, CONTROLS, power off countdown, credits
 */
#include "kernel.h"
#include "frame.h"

void CKernel::ShowUIFrame (boolean bWait)
{
	UpdateBezel ();
	// a menu over the grid (no game behind it): no filter, so that its grid
	// and the border's are one
	FrameDrawPicture (m_pUI->GetBuffer (), m_nLoaded >= 0 && !m_pUI->ShowsGrid ());
	m_Video.DrawSideText (FALSE, 0, 0);	// no status line or message over menus
	m_Video.DrawSideText (TRUE, 0, 0);
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
	unsigned nShown = nSel;

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
		if ((nMove & BTN_UP) && nSel > 0)			nSel--;
		if ((nMove & BTN_DOWN) && nSel + 1 < m_nGames)		nSel++;
		if (nNew & BTN_LEFT)	nSel = nSel >= 8 ? nSel - 8 : 0;
		if (nNew & BTN_RIGHT)	nSel = nSel + 8 < m_nGames ? nSel + 8 : m_nGames - 1;

		if (nNew & (BTN_A | BTN_START))
		{
			return nSel;
		}
		if (nNew & (BTN_SELECT | BTN_D))
		{
			RunSystemMenu ();
			n = ~0u;		// wait for release
		}
		nPrev = n;

		nTicks = nSel == nShown ? nTicks + 1 : 0;
		nShown = nSel;
		m_pUI->DrawList (m_pGames, m_nGames, nSel, m_nLoaded >= 0, nTicks);
		ShowUIFrame ();
	}
}

void CKernel::RunSystemMenu (void)
{
	enum { SmTv, SmPalette, SmSprites, SmBezel, SmControls, SmPower, SmCount };

	unsigned nSel = 0;
	u32 nPrev = ~0u;
	int nOldPalette = g_Pic.nPalette;
	int nOldTv = g_Pic.nTvMode;
	boolean bOldScan = g_Pic.bTvScanlines;
	boolean bOldSprites = g_Pic.bUnlimSprites;
	unsigned nOldBezel = m_nBezel;
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
					m_pUI->SnapshotGame (g_VideoBuf);	// the paused game in the new colours
				}
				break;
			}

			case SmSprites:
				g_Pic.bUnlimSprites = !g_Pic.bUnlimSprites;
				nes_set_unlimited_sprites (g_Pic.bUnlimSprites);
				break;

			case SmBezel:
				m_nBezel = m_nBezel ? 0 : BEZEL_GRID;	// GRID or OFF; drawn by the next frame
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

		static const char *Labels[SmCount] = {"TV FILTER", "PALETTE", "SPRITE FLICKER", "BEZEL", "CONTROLS", "POWER OFF"};
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
		Items[SmBezel].pValue = BezelName (m_nBezel, TRUE);

		static const TButtonHint Hints[2] = {{'A', "CHANGE"}, {'B', "BACK"}};
		m_pUI->DrawPanelMenu (m_nLoaded >= 0, "OPTIONS", Items, SmCount, nSel, pMsg, UI_VALUE,
				      Hints, 2, Footer);
		ShowUIFrame ();
	}

	if (g_Pic.nPalette != nOldPalette || g_Pic.bUnlimSprites != bOldSprites
	    || g_Pic.nTvMode != nOldTv || g_Pic.bTvScanlines != bOldScan || m_nBezel != nOldBezel)
	{
		SaveConfig ();
		char Line[140];
		snprintf (Line, sizeof Line, "New settings: TV filter %s, palette %s, sprites %s, bezel %s",
			  TvName (g_Pic.nTvMode), PaletteName (g_Pic.nPalette), g_Pic.bUnlimSprites ? "unlimited" : "8 per line",
			  BezelName (m_nBezel, FALSE));
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

// Asks for A B SELECT START in turn on one pad (directions are automatic),
// then saves on its own; a button already assigned skips the function. Esc
// on the keyboard cancels. (pads.cfg keeps the C and D slots of the shared
// input code: they stay unset here.)
boolean CKernel::RunMapping (unsigned nSlot, boolean bOverGame)
{
	// slots in the order of the logical bits: A B C D START SELECT;
	// asked in the order A B SELECT START
	enum { Slots = 6 };
	static const char *Names[Slots] = {"A", "B", "C", "D", "START", "SELECT"};
	static const unsigned SlotOfStep[4] = {0, 1, 5, 4};
	const unsigned nSteps = 4;

	TBind Binds[MAP_COUNT];
	memset (Binds, 0, sizeof Binds);
	static char Values[Slots][16];
	const char *pValues[Slots];
	for (unsigned i = 0; i < Slots; i++)
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
				const unsigned nCur = SlotOfStep[nStep];
				TBind &B = Binds[MAP_FIRST_BUTTON + nCur];
				if (!CInput::IsButtonBound (Binds, MAP_COUNT, nButton))
				{
					B.nType = BindButton;
					B.nIndex = (u8) nButton;
					B.nValue = 0;
				}
				// a button already assigned skips this function: it stays unassigned
				CInput::DescribeBind (B, Values[nCur], sizeof Values[nCur]);
				nStep++;
				bWaitRelease = TRUE;
			}
		}

		m_pUI->DrawMapping (bOverGame, Title, nStep < nSteps ? (int) SlotOfStep[nStep] : Slots,
				    pValues, Footer);
		ShowUIFrame ();
	}

	m_pInput->SetMap (nSlot, Binds);
	SavePads ();
	for (unsigned i = 0; i < nSteps; i++)
	{
		unsigned k = SlotOfStep[i];
		snprintf (Line, sizeof Line, "  %s = %s", Names[k], Values[k]);
		Log (Line, COLOR_TEXT);
	}
	Log ("  controls saved to nespi/pads.cfg", COLOR_TEXT);
	SaveLog ();

	// "SAVED" for one second
	for (unsigned f = 0; f < 60; f++)
	{
		m_pUI->DrawMapping (bOverGame, Title, Slots, pValues, Footer);
		ShowUIFrame ();
	}
	return TRUE;
}

boolean CKernel::PowerOffCountdown (void)
{
	// 3 seconds, B cancels: a box alone, over the paused game or the grid
	static u32 Saved[GAME_SRC_PITCH * GAME_SRC_LINES];	// the menu, for B
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
		static const TButtonHint Cancel = {'B', "CANCEL"};
		m_pUI->DrawBox (m_nLoaded >= 0, Line, "", &Cancel);
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
