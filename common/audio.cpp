/*
 * HDMI audio output: ~800 frames at 48 kHz per emulated frame. Video and
 * audio clocks drift, so each block is stretched or shrunk by a few samples
 * (linear interpolation) to keep the queue near TARGET.
 */
#include "audio.h"
#include "project.h"

#define SAMPLE_RATE	48000
#define CHUNK_SIZE	(384 * 2)		// words per DMA chunk (multiple of 384): 384 stereo frames = 8 ms
#define QUEUE_FRAMES	4800			// 100 ms queue capacity
// the queue level aimed at, and how far it may drift before each block is
// stretched or shrunk: by default ~2 video frames (~33 ms); a project whose
// frames are never late can ask for less in its project.h (less audio lag)
#ifdef PROJECT_AUDIO_TARGET
#define TARGET_FRAMES	PROJECT_AUDIO_TARGET
#define BAND_FRAMES	PROJECT_AUDIO_BAND
#else
#define TARGET_FRAMES	1600
#define BAND_FRAMES	400
#endif

CAudioOut::CAudioOut (void)
:	m_pSound (0),
	m_bOK (FALSE),
	m_nMinQueue (~0u),
	m_nMaxQueue (0),
	m_nUnderruns (0),
	m_nAdjust (0)
{
}

CAudioOut::~CAudioOut (void)
{
	delete m_pSound;
}

boolean CAudioOut::Initialize (CInterruptSystem *pInterrupt)
{
	m_pSound = new CHDMISoundBaseDevice (pInterrupt, SAMPLE_RATE, CHUNK_SIZE);
	if (m_pSound == 0)
	{
		return FALSE;
	}
	if (!m_pSound->AllocateQueueFrames (QUEUE_FRAMES))
	{
		return FALSE;
	}
	m_pSound->SetWriteFormat (SoundFormatSigned16, 2);
	if (!m_pSound->Start ())
	{
		return FALSE;
	}
	m_bOK = TRUE;
	return TRUE;
}

// after a pause (no sound was written): the queue back to TARGET with
// silence, so the first frames find it full
void CAudioOut::Refill (void)
{
	if (!m_bOK)
	{
		return;
	}
	static const s16 Zero[2 * 256] = {0};
	unsigned nQueued = m_pSound->GetQueueFramesAvail ();
	while (nQueued < TARGET_FRAMES)
	{
		const unsigned n = TARGET_FRAMES - nQueued < 256 ? TARGET_FRAMES - nQueued : 256;
		m_pSound->Write (Zero, n * 4);
		nQueued += n;
	}
}

void CAudioOut::WriteFrame (const s16 *pSamples, unsigned nFrames)
{
	if (!m_bOK || nFrames == 0 || nFrames > 1800)
	{
		return;
	}

	unsigned nQueued = m_pSound->GetQueueFramesAvail ();
	if (nQueued < m_nMinQueue) m_nMinQueue = nQueued;
	if (nQueued > m_nMaxQueue) m_nMaxQueue = nQueued;
	if (nQueued == 0)
	{
		m_nUnderruns++;
	}

	// proportional correction: at most 1% of the block
	int nDelta = 0;
	if (nQueued + BAND_FRAMES < TARGET_FRAMES)
	{
		nDelta = nQueued < TARGET_FRAMES / 2 ? 8 : 2;		// stretch
	}
	else if (nQueued > TARGET_FRAMES + BAND_FRAMES)
	{
		nDelta = nQueued > 2 * TARGET_FRAMES ? -8 : -2;		// shrink
	}
	m_nAdjust += nDelta;

	unsigned nOut = nFrames + nDelta;
	if (nDelta == 0 || nFrames < 16)	// a very short block goes out as it is
	{
		m_pSound->Write (pSamples, nFrames * 4);
		return;
	}

	// resample nFrames -> nOut (16.16 fixed point, linear)
	u32 nStep = (u32) (((u64) (nFrames - 1) << 16) / (nOut - 1));
	u32 nPos = 0;
	for (unsigned i = 0; i < nOut; i++)
	{
		unsigned k = nPos >> 16;
		unsigned f = nPos & 0xFFFF;
		unsigned k1 = k + 1 < nFrames ? k + 1 : k;
		for (unsigned c = 0; c < 2; c++)
		{
			int a = pSamples[k * 2 + c];
			int b = pSamples[k1 * 2 + c];
			m_Buffer[i * 2 + c] = (s16) (a + (((b - a) * (int) f) >> 16));
		}
		nPos += nStep;
	}
	m_pSound->Write (m_Buffer, nOut * 4);
}

void CAudioOut::GetStats (unsigned *pMinQueue, unsigned *pMaxQueue, unsigned *pUnderruns,
			  int *pAdjust)
{
	*pMinQueue = m_nMinQueue == ~0u ? 0 : m_nMinQueue;
	*pMaxQueue = m_nMaxQueue;
	*pUnderruns = m_nUnderruns;
	*pAdjust = m_nAdjust;
	m_nMinQueue = ~0u;
	m_nMaxQueue = 0;
	m_nUnderruns = 0;
	m_nAdjust = 0;
}
