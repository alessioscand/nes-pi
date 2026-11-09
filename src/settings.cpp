/*
 * nes-pi - settings: nespi.cfg, pads.cfg, the video mode, the palette
 */
#include "kernel.h"
#include "frame.h"
#include "pacing.h"
#include "racing.h"

const char *CKernel::TvName (int n)
{
	return n == NTSC_SVIDEO ? "S-Video" : n == NTSC_COMPOSITE ? "composite" : n == NTSC_RF ? "RF"
	       : g_Pic.bTvScanlines ? "RGB" : "off";
}

const char *CKernel::PaletteName (int n)
{
	static const char *Names[4] = {"nestopia", "consumer", "sony", "custom"};
	return Names[n & 3];
}

// the palette to the core, and the last picture recoloured (menus over it)
void CKernel::ApplyPalette (void)
{
	if (!nes_set_palette (g_Pic.nPalette, g_Pic.CustomPal, g_Pic.nCustomPal))
	{
		g_Pic.nPalette = NES_PALETTE_NESTOPIA;
		nes_set_palette (g_Pic.nPalette, 0, 0);
	}
	const u32 *pPal = nes_palette ();
	for (unsigned y = 0; y < NES_H; y++)
	{
		for (unsigned x = 0; x < NES_W; x++)
		{
			boolean bSide = g_Pic.bOverscan && (x < OVERSCAN_SIDE || x >= NES_W - OVERSCAN_SIDE);
			g_VideoBuf[y * NES_W + x] = bSide ? 0 : pPal[g_IdxBuf[y * NES_W + x] & 511];
		}
	}
}

const char *CKernel::FilterShort (unsigned nFilter)
{
	switch (nFilter)
	{
	case FILTER_SCAN_LIGHT:	 return "SCAN-L";
	case FILTER_SCAN_MEDIUM: return "SCAN-M";
	case FILTER_SCAN_STRONG: return "SCAN-S";
	default:		 return "NOFLT";
	}
}

// usbpoll= value for the log and nespi.cfg
static const char *UsbPollName (void)
{
	switch (g_nUSBInterruptPollMs)
	{
	case 0:		return "native";
	case 2:		return "500";
	case 4:		return "250";
	default:	return "1000";
	}
}

const char *CKernel::FilterName (unsigned nFilter)
{
	switch (nFilter)
	{
	case FILTER_SCAN_LIGHT:	 return "SCANLINES_LIGHT";
	case FILTER_SCAN_MEDIUM: return "SCANLINES";
	case FILTER_SCAN_STRONG: return "SCANLINES_STRONG";
	default:		 return "NONE";
	}
}

// "HVS4", "HVS2" or "CPU": how the picture is being enlarged right now
void CKernel::VideoModeTag (char *pOut, size_t nSize)
{
	if (m_Video.GetHVSFactor () > 1)
	{
		snprintf (pOut, nSize, "HVS%u", m_Video.GetHVSFactor ());
	}
	else
	{
		snprintf (pOut, nSize, "CPU");
	}
}

// TV FILTER: RGB = sharp pixels with light scanlines; RF / COMPOSITE /
// S-VIDEO = blargg's filter (no scanlines: the CPU draws at least 2x anyway)
void CKernel::SetTvMode (void)
{
	m_nFilter = g_Pic.nTvMode == NTSC_OFF && g_Pic.bTvScanlines ? FILTER_SCAN_LIGHT : FILTER_NONE;
	FrameSetTvMode (g_Pic.nTvMode);
	ApplyVideoMode ();
}

void CKernel::ApplyVideoMode (void)
{
	if (!m_Video.SetGameMode (m_nFilter, m_bHVS, g_Pic.nTvMode != NTSC_OFF))
	{
		Fatal (PROJECT_ERR_FB, "cannot set up the game framebuffer");
	}
	m_Video.SetSource (g_UISource);
	char Tag[8];
	VideoModeTag (Tag, sizeof Tag);
	char Line[MaxLineLen];
	snprintf (Line, sizeof Line, "Video: mode %s, HDMI %ux%u, framebuffer %ux%u, scale %ux (HVS %ux, CPU %ux), filter %s%s",
		  Tag, m_Video.GetWidth (), m_Video.GetHeight (),
		  m_Video.GetFBWidth (), m_Video.GetFBHeight (), m_Video.GetScale (),
		  m_Video.GetHVSFactor (), m_Video.GetScale () / m_Video.GetHVSFactor (),
		  FilterName (m_Video.GetFilter ()), m_bHVS ? "" : " (scaler=cpu)");
	Log (Line, COLOR_TEXT);
	snprintf (Line, sizeof Line, "Video: pixel order %s (%u), red in the %s byte%s",
		  m_Video.GetPixelOrder () ? "RGB" : "BGR", m_Video.GetPixelOrder (),
		  m_Video.IsRedLow () ? "low" : "high", "");
	Log (Line, COLOR_TEXT);
	snprintf (Line, sizeof Line, "Video: framebuffer write-combining %s (%u pages of 64 KB)",
		  m_Video.GetWCPages () ? "on" : "FAILED",
		  m_Video.GetWCPages ());
	Log (Line, COLOR_TEXT);
}

void CKernel::LoadConfig (void)
{
	FIL File;
	char Text[256];
	UINT nRead = 0;
	if (f_open (&File, CFG_PATH, FA_READ) != FR_OK)
	{
		return;
	}
	f_read (&File, Text, sizeof Text - 1, &nRead);
	f_close (&File);
	Text[nRead] = '\0';

	if (strstr (Text, "scaler=cpu"))	m_bHVS = FALSE;
	// USB pads are polled every 1 ms (like usbhid.jspoll=1 on Linux);
	// usbpoll=500 / 250 = every 2 / 4 ms, for pads that misbehave at 1000 Hz;
	// usbpoll=native keeps the interval requested by the device
	if (strstr (Text, "usbpoll=native"))	g_nUSBInterruptPollMs = 0;
	else if (strstr (Text, "usbpoll=500"))	g_nUSBInterruptPollMs = 2;
	else if (strstr (Text, "usbpoll=250"))	g_nUSBInterruptPollMs = 4;
	// input lag (see "input lag" at the top of this file)
	if (strstr (Text, "palette=consumer"))		g_Pic.nPalette = NES_PALETTE_CONSUMER;
	else if (strstr (Text, "palette=sony"))		g_Pic.nPalette = NES_PALETTE_SONY;
	else if (strstr (Text, "palette=custom"))	g_Pic.nPalette = NES_PALETTE_CUSTOM;
	if (strstr (Text, "sprites=unlimited"))		g_Pic.bUnlimSprites = TRUE;
	if (strstr (Text, "overscan=off"))
	{
		g_Pic.bOverscan = FALSE;
		g_NesSource.nH = g_NtscSource.nH = NES_H;
		g_NesSource.nY = g_NtscSource.nY = 0;
	}
	if (strstr (Text, "tvfilter=rf"))		g_Pic.nTvMode = NTSC_RF;
	else if (strstr (Text, "tvfilter=svideo"))	g_Pic.nTvMode = NTSC_SVIDEO;
	else if (strstr (Text, "tvfilter=composite"))	g_Pic.nTvMode = NTSC_COMPOSITE;
	else						g_Pic.nTvMode = NTSC_OFF;	// tvfilter=rgb / off
	g_Pic.bTvScanlines = strstr (Text, "tvfilter=off") == 0;
	if (strstr (Text, "beamracing=off"))		g_Race.nCfg = 0;
	if (strstr (Text, "beamracing=on"))		g_Race.nCfg = 1;
	const char *pDelay = strstr (Text, "framedelay=");
	if (pDelay)
	{
		pDelay += 11;
		if (strncmp (pDelay, "off", 3) == 0)
		{
			g_Delay.nCfg = 0;
		}
		else if (*pDelay >= '0' && *pDelay <= '9')
		{
			int n = 0;
			while (*pDelay >= '0' && *pDelay <= '9' && n < 100)
			{
				n = n * 10 + (*pDelay++ - '0');
			}
			g_Delay.nCfg = n > 12 ? 12 : n;
		}
	}

	char Line[140];
	char Delay[16];
	if (g_Delay.nCfg < 0)		snprintf (Delay, sizeof Delay, "auto");
	else if (g_Delay.nCfg == 0)	snprintf (Delay, sizeof Delay, "off");
	else				snprintf (Delay, sizeof Delay, "%d ms", g_Delay.nCfg);
	snprintf (Line, sizeof Line, "Settings: USB polling %s%s, overscan %s, frame delay %s, beam racing %s",
		  UsbPollName (), g_nUSBInterruptPollMs ? " Hz" : "",
		  g_Pic.bOverscan ? "hidden" : "shown", Delay,
		  g_Race.nCfg ? "on" : "off");
	Log (Line, COLOR_TEXT);
}

void CKernel::SaveConfig (void)
{
	char Text[200];
	snprintf (Text, sizeof Text, "%s", m_bHVS ? "" : "scaler=cpu\r\n");
	if (g_nUSBInterruptPollMs != 1)
	{
		strcat (Text, "usbpoll=");
		strcat (Text, UsbPollName ());
		strcat (Text, "\r\n");
	}
	if (!g_Pic.bOverscan)
	{
		strcat (Text, "overscan=off\r\n");
	}
	if (g_Pic.nPalette != NES_PALETTE_NESTOPIA)
	{
		strcat (Text, "palette=");
		strcat (Text, PaletteName (g_Pic.nPalette));
		strcat (Text, "\r\n");
	}
	if (g_Pic.bUnlimSprites)
	{
		strcat (Text, "sprites=unlimited\r\n");
	}
	strcat (Text, g_Pic.nTvMode == NTSC_SVIDEO ? "tvfilter=svideo\r\n"
		      : g_Pic.nTvMode == NTSC_COMPOSITE ? "tvfilter=composite\r\n"
		      : g_Pic.nTvMode == NTSC_RF ? "tvfilter=rf\r\n"
		      : g_Pic.bTvScanlines ? "tvfilter=rgb\r\n" : "tvfilter=off\r\n");
	if (g_Race.nCfg == 0)
	{
		strcat (Text, "beamracing=off\r\n");
	}
	if (g_Delay.nCfg >= 0)
	{
		char Line[24];
		if (g_Delay.nCfg == 0)	snprintf (Line, sizeof Line, "framedelay=off\r\n");
		else			snprintf (Line, sizeof Line, "framedelay=%d\r\n", g_Delay.nCfg);
		strcat (Text, Line);
	}
	WriteTextFile (CFG_PATH, Text);
}

void CKernel::LoadPads (void)
{
	static char Text[4096];
	FIL File;
	UINT nRead = 0;
	if (f_open (&File, PADS_PATH, FA_READ) == FR_OK)
	{
		f_read (&File, Text, sizeof Text - 1, &nRead);
		f_close (&File);
	}
	Text[nRead] = '\0';
	m_pInput->LoadMaps (Text);
}

void CKernel::SavePads (void)
{
	static char Text[4096];
	m_pInput->FormatMaps (Text, sizeof Text);
	WriteTextFile (PADS_PATH, Text);
}
