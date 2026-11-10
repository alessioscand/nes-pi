/*
 * nes-pi - kernel: start-up, the main loop, power off
 */
#include "kernel.h"
#include "frame.h"
#include <circle/machineinfo.h>
#include <circle/bcmwatchdog.h>
#include <circle/multicore.h>
#include <circle/memory.h>
#include "tvcore.h"
#include "fbmap.h"

CKernel *CKernel::s_pThis = 0;

// core 1: the TV filter (tvcore.h); cores 2 and 3 stay idle
class CTvFilterCore : public CMultiCoreSupport
{
public:
	CTvFilterCore (void) : CMultiCoreSupport (CMemorySystem::Get ()) {}
	void Run (unsigned nCore)
	{
		if (nCore == 1)
		{
			FbSetMair ();			// the framebuffer's write-combining type
			TvWorkerRun ();
		}
	}
};

CKernel::CKernel (void)
:	m_Timer (&m_Interrupt),
	m_Logger (m_Options.GetLogLevel (), &m_Timer),
	m_CPUThrottle (CPUSpeedMaximum),
	m_USBHCI (&m_Interrupt, &m_Timer, TRUE),
	m_EMMC (&m_Interrupt, &m_Timer, &m_ActLED),
	m_pInput (0),
	m_pUI (0),
	m_bVideoOK (FALSE),
	m_bSDOK (FALSE),
	m_bQuiet (TRUE),		// black screen while booting; text only on errors
	m_nLines (0),
	m_nRepeat (0),
	m_pGame (0),
	m_nGameSize (0),
	m_pGames (0),
	m_nGames (0),
	m_nLoaded (-1),
	m_nShots (0),
	m_nFilter (0),
	m_bHVS (TRUE),
	m_bVSync (TRUE),
	m_bStatusOn (FALSE),
	m_nClearStatus (0)
{
	s_pThis = this;
	m_LastRaw[0] = '\0';
}

CKernel::~CKernel (void)
{
	s_pThis = 0;
}

boolean CKernel::Initialize (void)
{
	m_bVideoOK = m_Video.Initialize ();

	m_Serial.Initialize (115200);

	if (!m_Logger.Initialize (&m_LogSink))
	{
		return FALSE;
	}
	if (!m_Interrupt.Initialize ())
	{
		return FALSE;
	}
	if (!m_Timer.Initialize ())
	{
		return FALSE;
	}

	return TRUE;
}

TShutdownMode CKernel::Run (void)
{
	if (!m_bVideoOK)
	{
		Fatal (PROJECT_ERR_FB, "cannot set up the HDMI output");
	}

	char Line[180];
	Log ("nes-pi - bare metal NES emulator", COLOR_TITLE);
	snprintf (Line, sizeof Line, "Version %s, built %s %s", PROJECT_VERSION, __DATE__, __TIME__);
	Log (Line, COLOR_DIM);

	CMachineInfo *pInfo = CMachineInfo::Get ();
	m_CPUThrottle.SetSpeed (CPUSpeedMaximum);
	snprintf (Line, sizeof Line, "%s, %u MB RAM, CPU %u MHz, temperature %u C",
		  pInfo->GetMachineName (), pInfo->GetRAMSize (),
		  m_CPUThrottle.GetClockRate () / 1000000, m_CPUThrottle.GetTemperature ());
	Log (Line, COLOR_TEXT);
	snprintf (Line, sizeof Line, "HDMI %ux%u, pixel order %s", m_Video.GetWidth (), m_Video.GetHeight (),
		  m_Video.GetPixelOrder () ? "RGB" : "BGR");
	Log (Line, COLOR_TEXT);

	// SD card
	if (!m_EMMC.Initialize ())
	{
		Fatal (PROJECT_ERR_SD, "SD card not initialised");
	}
	if (f_mount (&m_FileSystem, "SD:", 1) != FR_OK)
	{
		Fatal (PROJECT_ERR_SD, "cannot read the SD card file system");
	}
	m_bSDOK = TRUE;
	f_mkdir (NESPI_DIR);
	f_mkdir (SAVE_DIR);
	f_mkdir (SHOT_DIR);
	LoadConfig ();
	m_nFilter = g_Pic.nTvMode == NTSC_OFF && g_Pic.bTvScanlines ? FILTER_SCAN_LIGHT : FILTER_NONE;	// TV FILTER decides

	m_pUI = new CUI;		// no console font on the NES: the built-in one

	// games
	ScanGames ();
	snprintf (Line, sizeof Line, "Games found: %u", m_nGames);
	Log (Line, m_nGames ? COLOR_OK : COLOR_ERR);
	if (m_nGames == 0)
	{
		Fatal (PROJECT_ERR_GAME, "no .nes file in the nespi folder");
	}

	// USB and audio
	Log ("USB...", COLOR_TEXT);
	if (!m_USBHCI.Initialize ())
	{
		Log ("  USB controller not initialised", COLOR_ERR);
	}
	m_pInput = new CInput (&m_USBHCI, &m_DeviceNameService);
	LoadPads ();
	m_pInput->UpdateDevices ();

	if (m_Audio.Initialize (&m_Interrupt))
	{
		Log ("HDMI audio: OK (48 kHz)", COLOR_OK);
	}
	else
	{
		Log ("HDMI audio: not available, playing without sound", COLOR_ERR);
	}

	SetupCoreOnce ();

	unsigned nSel = LastPlayed ();

	FILINFO Info;
	boolean bBench = f_stat (BENCH_FLAG, &Info) == FR_OK;
	if (bBench)
	{
		if (LoadGame (nSel))
		{
			Benchmark ();
		}
		CTimer::SimpleMsDelay (8000);		// time to read the results on screen
	}
	SaveLog ();

	// from here on the screen belongs to the UI
	ApplyVideoMode ();
	SetupBeamRacing ();
	SaveLog ();
	m_bQuiet = TRUE;

	boolean bDirect = m_nGames == 1;
	while (1)
	{
		if (!bDirect)
		{
			nSel = RunList (nSel);
		}
		bDirect = FALSE;

		if (m_nLoaded != (int) nSel && !LoadGame (nSel))
		{
			m_pUI->DrawMessage ("CANNOT LOAD GAME", 0, "SEE nespi/LOG.TXT");
			SaveLog ();
			for (unsigned i = 0; i < 180; i++)
			{
				ShowUIFrame ();
			}
			continue;
		}
		SaveLog ();
		RunGame ();
		m_bQuiet = TRUE;
	}

	return ShutdownHalt;
}

// starts core 1 for the TV filter (once); FALSE: it is not running
boolean CKernel::StartCores (void)
{
	CTvFilterCore *pCores = new CTvFilterCore;
	if (pCores == 0 || !pCores->Initialize ())
	{
		return FALSE;
	}
	unsigned nWait = CTimer::GetClockTicks ();
	while (!TvWorkerAlive () && CTimer::GetClockTicks () - nWait < 200000)
	{
	}
	return TvWorkerAlive ();
}

// the game played last (LAST.TXT), or the first one
unsigned CKernel::LastPlayed (void)
{
	unsigned nSel = 0;
	FIL File;
	char Last[64];
	UINT nRead = 0;
	if (f_open (&File, LAST_PATH, FA_READ) == FR_OK)
	{
		f_read (&File, Last, sizeof Last - 1, &nRead);
		f_close (&File);
		Last[nRead] = '\0';
		for (unsigned i = 0; i < m_nGames; i++)
		{
			if (strcmp (m_pGames[i].File, Last) == 0)
			{
				nSel = i;
			}
		}
	}
	return nSel;
}

void CKernel::PowerOff (void)
{
	Log ("Power off: saving data and halting", COLOR_TITLE);
	UnloadGame ();			// the cartridge's battery RAM goes to the card here
	SaveLog ();
	f_mount (0, "SD:", 0);
	m_bSDOK = FALSE;

	// black picture, then halt (the firmware stops the HDMI output)
	m_Video.Clear (0, RGB (0, 0, 0));
	m_Video.Clear (1, RGB (0, 0, 0));
	m_Video.ShowBuffer (0);
	CTimer::SimpleMsDelay (300);

	CBcmWatchdog Watchdog;
	Watchdog.Restart (CBcmWatchdog::PartitionHalt);
	while (1)
	{
	}
}
