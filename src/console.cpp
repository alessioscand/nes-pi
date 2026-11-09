/*
 * nes-pi - the console: boot messages on screen, serial, LOG.TXT
 */
#include "kernel.h"
#include <stdarg.h>

extern "C" int vsnprintf (char *pBuffer, size_t nSize, const char *pFormat, va_list Args);

extern "C" void project_log (const char *pFormat, ...)
{
	char Buffer[160];
	va_list Args;
	va_start (Args, pFormat);
	vsnprintf (Buffer, sizeof Buffer, pFormat, Args);
	va_end (Args);
	CKernel::Get ()->Log (Buffer, COLOR_TEXT);
}

extern "C" void project_fatal (int nCode, const char *pFormat, ...)
{
	char Buffer[160];
	va_list Args;
	va_start (Args, pFormat);
	vsnprintf (Buffer, sizeof Buffer, pFormat, Args);
	va_end (Args);
	CKernel::Get ()->Fatal (nCode, Buffer);
}

int CLogSink::Write (const void *pBuffer, size_t nCount)
{
	static char Line[160];
	static unsigned nPos = 0;
	const char *p = (const char *) pBuffer;
	for (size_t i = 0; i < nCount; i++)
	{
		char c = p[i];
		if (c == '\x1b')
		{
			while (i + 1 < nCount && !((p[i+1] >= 'a' && p[i+1] <= 'z') || (p[i+1] >= 'A' && p[i+1] <= 'Z')))
			{
				i++;
			}
			i++;
			continue;
		}
		if (c == '\n' || nPos >= sizeof Line - 1)
		{
			Line[nPos] = '\0';
			if (nPos > 0 && CKernel::Get ())
			{
				CKernel::Get ()->Log (Line, COLOR_DIM);
			}
			nPos = 0;
			continue;
		}
		if (c != '\r')
		{
			Line[nPos++] = c;
		}
	}
	return (int) nCount;
}

void CKernel::Log (const char *pLine, u32 nColor)
{
	if (m_nLines > 0 && strcmp (pLine, m_LastRaw) == 0)
	{
		m_nRepeat++;
		unsigned n = (m_nLines - 1) % MaxLines;
		snprintf (m_Lines[n], MaxLineLen, "%s (x%u)", m_LastRaw, m_nRepeat);
		if (!m_bQuiet && m_bVideoOK && !m_Video.IsGameMode () && m_nLines <= m_Video.GetRows ())
		{
			m_Video.DrawText (0, 0, m_nLines - 1, m_Lines[n], nColor, RGB (0, 0, 0));
		}
		return;
	}
	strncpy (m_LastRaw, pLine, MaxLineLen - 1);
	m_LastRaw[MaxLineLen - 1] = '\0';
	m_nRepeat = 1;

	unsigned n = m_nLines % MaxLines;
	strncpy (m_Lines[n], pLine, MaxLineLen - 1);
	m_Lines[n][MaxLineLen - 1] = '\0';
	m_LineColor[n] = nColor;
	m_nLines++;

	if (m_bQuiet)
	{
		return;
	}

	m_Serial.Write (pLine, strlen (pLine));
	m_Serial.Write ("\r\n", 2);

	if (!m_bVideoOK || m_Video.IsGameMode ())	// never write text over the game screen
	{
		return;
	}

	unsigned nRows = m_Video.GetRows ();
	if (m_nLines <= nRows)
	{
		m_Video.DrawText (0, 0, m_nLines - 1, m_Lines[n], nColor, RGB (0, 0, 0));
	}
	else
	{
		RedrawConsole ();
	}
}

void CKernel::RedrawConsole (void)
{
	unsigned nRows = m_Video.GetRows ();
	unsigned nFirst = m_nLines > nRows ? m_nLines - nRows : 0;
	m_Video.ClearRows (0, 0, nRows, RGB (0, 0, 0));
	for (unsigned i = nFirst; i < m_nLines; i++)
	{
		unsigned n = i % MaxLines;
		m_Video.DrawText (0, 0, i - nFirst, m_Lines[n], m_LineColor[n], RGB (0, 0, 0));
	}
}

void CKernel::WriteTextFile (const char *pPath, const char *pText)
{
	if (!m_bSDOK)
	{
		return;
	}
	FIL File;
	if (f_open (&File, pPath, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK)
	{
		return;
	}
	UINT nWritten;
	f_write (&File, pText, strlen (pText), &nWritten);
	f_close (&File);
}

void CKernel::SaveLog (void)
{
	static char Text[MaxLines * (MaxLineLen + 2)];
	unsigned nFirst = m_nLines > MaxLines ? m_nLines - MaxLines : 0;
	size_t nPos = 0;
	for (unsigned i = nFirst; i < m_nLines; i++)
	{
		const char *pLine = m_Lines[i % MaxLines];
		size_t nLen = strlen (pLine);
		memcpy (Text + nPos, pLine, nLen);
		nPos += nLen;
		Text[nPos++] = '\r';
		Text[nPos++] = '\n';
	}
	Text[nPos] = '\0';
	WriteTextFile (LOG_PATH, Text);
}

void CKernel::Fatal (int nCode, const char *pMessage)
{
	if (m_bQuiet || m_Video.IsGameMode ())
	{
		m_bQuiet = FALSE;
		if (m_bVideoOK && m_Video.SetConsoleMode ())
		{
			RedrawConsole ();
		}
	}

	char Line[160];
	snprintf (Line, sizeof Line, "ERROR: %s", pMessage);
	Log (Line, COLOR_ERR);

	switch (nCode)
	{
	case PROJECT_ERR_SD:
		Log ("  Check that the card is FAT32 and that the files are in its root.", COLOR_TEXT);
		break;
	case PROJECT_ERR_GAME:
		Log ("  Copy at least one .nes game into the nespi folder.", COLOR_TEXT);
		break;
	case PROJECT_ERR_MEM:
		Log ("  Not enough memory.", COLOR_TEXT);
		break;
	default:
		break;
	}
	snprintf (Line, sizeof Line, "The green LED blinks %d times: error code %d.", nCode, nCode);
	Log (Line, COLOR_TEXT);

	SaveLog ();

	while (1)
	{
		m_ActLED.Blink (nCode, 250, 350);
		CTimer::SimpleMsDelay (1500);
	}
}
