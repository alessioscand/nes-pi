/*
 * nes-pi - kernel
 */
#ifndef KERNEL_H
#define KERNEL_H

#include <circle/actled.h>
#include <circle/koptions.h>
#include <circle/devicenameservice.h>
#include <circle/serial.h>
#include <circle/exceptionhandler.h>
#include <circle/interrupt.h>
#include <circle/timer.h>
#include <circle/logger.h>
#include <circle/cputhrottle.h>
#include <circle/device.h>
#include <circle/usb/usbhcidevice.h>
#include <SDCard/emmc.h>
#include <fatfs/ff.h>
#include <circle/types.h>
#include "video.h"
#include "input.h"
#include "audio.h"
#include "ui.h"

enum TShutdownMode
{
	ShutdownNone,
	ShutdownHalt,
	ShutdownReboot
};

// Receives Circle's own log messages and forwards them to the console
class CLogSink : public CDevice
{
public:
	int Write (const void *pBuffer, size_t nCount);
};

class CKernel
{
public:
	CKernel (void);
	~CKernel (void);

	boolean Initialize (void);
	TShutdownMode Run (void);

	// Console (screen during boot + serial + LOG.TXT)
	void Log (const char *pLine, u32 nColor);
	void Fatal (int nCode, const char *pMessage);
	void SaveLog (void);

	static CKernel *Get (void) { return s_pThis; }

private:
	void RedrawConsole (void);
	u8 *LoadFile (const char *pPath, size_t *pSize, int nHeap, const char *pShowName);
	void ScanGames (void);
	void SetupCoreOnce (void);
	void ApplyPalette (void);
	void SetTvMode (void);
	boolean LoadGame (unsigned nIndex);
	void UnloadGame (void);
	boolean SaveState (const char *pPath);
	boolean LoadState (const char *pPath);
	void SnapshotForUI (void);
	void Benchmark (void);
	unsigned RunList (unsigned nSel);
	void RunGame (void);
	void SetupBeamRacing (void);
	void ShowUIFrame (boolean bWait = TRUE, const char *pTopMessage = 0);
	void WriteTextFile (const char *pPath, const char *pText);
	const char *SaveScreenshot (void);
	void RunCredits (void);
	void ResetGameStats (void);
	void GamePath (char *pOut, size_t nSize, const char *pExt) const;
	void ReportWindow (boolean bFinal);
	static const char *FilterName (unsigned nFilter);
	void ApplyVideoMode (void);
	void VideoModeTag (char *pOut, size_t nSize);
	static const char *FilterShort (unsigned nFilter);
	void StateExt (char *pExt, unsigned nSlot) const;
	void LoadConfig (void);
	void SaveConfig (void);
	void RunSystemMenu (void);
	boolean PowerOffCountdown (void);
	void LoadPads (void);
	void SavePads (void);
	int ChoosePad (void);
	boolean RunMapping (unsigned nSlot, boolean bOverGame);
	void PowerOff (void);

private:
	// do not change this order
	CActLED			m_ActLED;
	CKernelOptions		m_Options;
	CDeviceNameService	m_DeviceNameService;
	CSerialDevice		m_Serial;
	CExceptionHandler	m_ExceptionHandler;
	CInterruptSystem	m_Interrupt;
	CTimer			m_Timer;
	CLogSink		m_LogSink;
	CLogger			m_Logger;
	CCPUThrottle		m_CPUThrottle;
	CUSBHCIDevice		m_USBHCI;
	CEMMCDevice		m_EMMC;
	FATFS			m_FileSystem;
	CVideoOut		m_Video;
	CAudioOut		m_Audio;
	CInput			*m_pInput;
	CUI			*m_pUI;

	enum { MaxLines = 400, MaxLineLen = 140, MaxGames = 256 };

	boolean			m_bVideoOK;
	boolean			m_bSDOK;
	boolean			m_bQuiet;	// UI/emulation running: console not drawn
	char			m_LastRaw[MaxLineLen];
	unsigned		m_nRepeat;

	// console lines
	char			m_Lines[MaxLines][MaxLineLen];
	u32			m_LineColor[MaxLines];
	unsigned		m_nLines;

	// emulator data
	u8			*m_pGame;
	size_t			 m_nGameSize;
	TGameEntry		*m_pGames;
	unsigned		 m_nGames;
	int			 m_nLoaded;	// index in m_pGames, -1 = none

	// frame pacing / statistics
	boolean			 m_bVSync;
	boolean			 m_bStatusOn;
	unsigned		 m_nClearStatus;
	unsigned		 m_nShots;

	// configuration (nespi.cfg)
	unsigned		 m_nFilter;	// FILTER_*
	boolean			 m_bHVS;		// FALSE: scaler=cpu in nespi.cfg
	boolean			 m_bFilterAllowed;	// Pi 4 only
	boolean			 m_bForceFilters;	// filters=on in nespi.cfg

	static CKernel		*s_pThis;
};

#endif
