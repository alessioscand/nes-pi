/*
 * HDMI audio output with a small queue and drift correction
 */
#ifndef AUDIO_H
#define AUDIO_H

#include <circle/sound/hdmisoundbasedevice.h>
#include <circle/interrupt.h>
#include <circle/types.h>

class CAudioOut
{
public:
	CAudioOut (void);
	~CAudioOut (void);

	boolean Initialize (CInterruptSystem *pInterrupt);
	boolean IsOK (void) const	{ return m_bOK; }

	// Queue one emulated frame of interleaved stereo samples
	void WriteFrame (const s16 *pSamples, unsigned nFrames);

	// Statistics since the last call
	void GetStats (unsigned *pMinQueue, unsigned *pMaxQueue, unsigned *pUnderruns,
		       int *pAdjust);

private:
	CHDMISoundBaseDevice *m_pSound;
	boolean	 m_bOK;
	s16	 m_Buffer[4096];

	unsigned m_nMinQueue, m_nMaxQueue, m_nUnderruns;
	int	 m_nAdjust;
};

#endif
