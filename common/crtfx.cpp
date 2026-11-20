//
// crtfx.cpp - TV FILTER RGB and CRT (see crtfx.h)
//
// Sources: libretro/slang-shaders crt/shaders/hyllian/crt-hyllian-fast.slang
// and crt/shaders/crt-pi.slang. At an integer scale M the columns x M + p of
// a game pixel x share the weights of phase p, and the rows of a game line
// are symmetric: weights are worked out once per scale, the loops run per
// phase (vectorized) and each pair of rows s, M - 1 - s is written by one loop.
//
#include "crtfx.h"
#include "screen.h"
#include "smp.h"
#include <math.h>

#define MAXM	8
#define MAXOW	(GAME_W * MAXM)
#define PAD	3			// game pixels repeated past each edge

static unsigned s_nFx, s_M, s_OW;
static unsigned s_rs, s_bs;

// per phase
static int s_Off[MAXM];			// first game pixel, from x
static float s_W4[MAXM][4];		// RGB: weights of pixels -1..2
static u16 s_W8[MAXM];			// CRT: weight of the first pixel, Q8
// per column
static u16 s_MQ[3][MAXOW];		// RGB: mask ^ (1 / 2.2), Q8
// per row (and column)
static u16 s_RowK[MAXM];		// RGB: scanline ^ (1 / 2.2), Q7
static u16 s_K[(MAXM + 1) / 2][3][MAXOW];	// CRT: scanline x mask, Q7
// RGB: g ^ (2.4 / 2.2), Q15, for g = i / 3276 (up to 1.25: overshoot kept)
static u16 s_Gamma[4096];

// one game line
static float s_F[3][GAME_W + 2 * PAD];	// RGB: 0..1, planar
static u16 s_S[3][GAME_W + 2 * PAD];	// CRT: 0..255, planar
static u16 s_P[MAXM][GAME_W];		// per phase
static u16 s_T[MAXOW];			// RGB: s_Gamma per column
static u16 s_Q[3][MAXOW];		// per column: RGB q, CRT h

// statistics: totals and the maximum written by the core that draws, read by
// core 0 (ReportWindow); a new epoch from core 0 restarts the maximum
static u64 s_nSum, s_nMax, s_nCalls;
static unsigned s_nEpoch, s_nSeen;

static inline float Clamp (float v, float a, float b)	{ return v < a ? a : (v > b ? b : v); }
static inline u16 Sat (u16 v)				{ return v > 255 ? 255 : v; }

void CrtFxSetup (unsigned nFx, unsigned M, boolean bRLow)
{
	if (M > MAXM) M = MAXM;
	if (M < 2) nFx = CRTFX_NONE;
	s_rs = bRLow ? 0 : 16;
	s_bs = bRLow ? 16 : 0;
	if (nFx == s_nFx && M == s_M)
	{
		return;
	}
	SharedStore (s_nFx, (unsigned) CRTFX_NONE);
	s_M = M;
	s_OW = GAME_W * M;
	const unsigned OW = s_OW;

	switch (nFx)
	{
	case CRTFX_RGB:
		// Catmull-Rom across in gamma space, then GAMMA_INPUT 2.4, SCANLINES
		// 0.72, BRIGHTBOOST 1.5, mask 0.5 (green / magenta), OUTPUT 2.2: all
		// but the first separable, out = g ^ (2.4 / 2.2) x row x mask
		for (unsigned p = 0; p < M; p++)
		{
			float sx = (p + 0.5f) / M - 0.49999f, b = floorf (sx), f = sx - b;
			float f2 = f * f, f3 = f2 * f;
			s_Off[p] = (int) b;
			s_W4[p][0] = -0.5f * f3 + f2 - 0.5f * f;
			s_W4[p][1] = 1.5f * f3 - 2.5f * f2 + 1.0f;
			s_W4[p][2] = -1.5f * f3 + 2.0f * f2 + 0.5f * f;
			s_W4[p][3] = 0.5f * f3 - 0.5f * f2;
		}
		for (unsigned j = 0; j < OW; j++)
		{
			const boolean bOdd = ((int) floorf (j + 0.5f - 0.49999f * M) & 1) != 0;
			const u16 lo = (u16) (powf (0.5f, 1.0f / 2.2f) * 256.0f + 0.5f), hi = 256;
			s_MQ[0][j] = s_MQ[2][j] = bOdd ? lo : hi;
			s_MQ[1][j] = bOdd ? hi : lo;
		}
		for (unsigned s = 0; s < M; s++)
		{
			float fy = (s + 0.5f) / M;
			float d1 = Clamp (1.5f - 0.72f - fabsf (fy - 0.5f), 0.0f, 1.0f);
			float k = powf (d1 * d1 * (3.0f + 1.5f - 2.0f * d1), 1.0f / 2.2f);
			s_RowK[s] = (u16) (k * 128.0f + 0.5f);
		}
		for (unsigned i = 0; i < 4096; i++)
		{
			s_Gamma[i] = (u16) (powf (i / 3276.0f, 2.4f / 2.2f) * 32767.0f + 0.5f);
		}
		break;

	case CRTFX_CRT:
		// linear across, SCANLINE_WEIGHT 6, SCANLINE_GAP_BRIGHTNESS 0.12,
		// MULTISAMPLE, BLOOM_FACTOR 1.5, trinitron mask 0.7, no gamma
		for (unsigned p = 0; p < M; p++)
		{
			float x = (p + 0.5f) / M - 0.5f, b = floorf (x);
			s_Off[p] = (int) b;
			s_W8[p] = (u16) ((1.0f - (x - b)) * 256.0f + 0.5f);
		}
		for (unsigned s = 0; s < (M + 1) / 2; s++)
		{
			float dy = (s + 0.5f) / M - 0.5f, fw = 1.0f / (3.0f * M), w = 0;
			for (int k = -1; k <= 1; k++)
			{
				float d = dy + k * fw, v = 1.0f - d * d * 6.0f;
				w += v > 0.12f ? v : 0.12f;
			}
			w = w / 3.0f * 1.5f;
			for (unsigned j = 0; j < OW; j++)
			{
				float wm = fmodf ((j + 0.5f) / 3.0f, 1.0f);
				unsigned lit = wm < 1.0f / 3 ? 0 : (wm < 2.0f / 3 ? 1 : 2);
				for (unsigned c = 0; c < 3; c++)
					s_K[s][c][j] = (u16) (w * (c == lit ? 1.0f : 0.7f) * 128.0f + 0.5f);
			}
		}
		break;

	default:
		nFx = CRTFX_NONE;
		break;
	}
	SharedStore (s_nFx, nFx);
}

unsigned CrtFxActive (void)
{
	return SharedLoad (s_nFx);
}

// phases to columns
template <unsigned MM, typename T>
static void Interleave (T *__restrict h, const T (*P)[GAME_W])
{
	for (unsigned x = 0; x < GAME_W; x++)
		for (unsigned p = 0; p < MM; p++)
			h[x * MM + p] = P[p][x];
}

template <typename T>
static void Columns (T *__restrict h, const T (*P)[GAME_W], unsigned M)
{
	switch (M)
	{
	case 2:	Interleave<2> (h, P);	break;
	case 3:	Interleave<3> (h, P);	break;
	case 4:	Interleave<4> (h, P);	break;
	default:
		for (unsigned x = 0; x < GAME_W; x++)
			for (unsigned p = 0; p < M; p++)
				h[x * M + p] = P[p][x];
		break;
	}
}

// RGB: q = g ^ (2.4 / 2.2) x mask ^ (1 / 2.2) per column, Q8 (0..326)
static void LineRgb (const u32 *pSrc)
{
	const unsigned M = s_M, OW = s_OW;
	for (unsigned x = 0; x < GAME_W; x++)
	{
		const u32 c = pSrc[x];
		s_F[0][x + PAD] = ((c >> 16) & 0xFF) / 255.0f;
		s_F[1][x + PAD] = ((c >> 8) & 0xFF) / 255.0f;
		s_F[2][x + PAD] = (c & 0xFF) / 255.0f;
	}
	for (unsigned ch = 0; ch < 3; ch++)
	{
		float *f = s_F[ch];
		for (unsigned i = 0; i < PAD; i++)
		{
			f[i] = f[PAD];
			f[GAME_W + PAD + i] = f[GAME_W + PAD - 1];
		}
		for (unsigned p = 0; p < M; p++)
		{
			const float *__restrict a = f + PAD + s_Off[p] - 1;
			const float w0 = s_W4[p][0], w1 = s_W4[p][1], w2 = s_W4[p][2], w3 = s_W4[p][3];
			u16 *__restrict ix = s_P[p];
			for (unsigned x = 0; x < GAME_W; x++)
			{
				float v = w0 * a[x] + w1 * a[x + 1] + w2 * a[x + 2] + w3 * a[x + 3];
				v = v < 0.0f ? 0.0f : (v > 1.25f ? 1.25f : v);
				ix[x] = (u16) (v * 3276.0f + 0.5f);
			}
		}
		u16 *__restrict t = s_T;		// gather (not vectorized)
		for (unsigned x = 0; x < GAME_W; x++)
			for (unsigned p = 0; p < M; p++)
				*t++ = s_Gamma[s_P[p][x]];
		const u16 *__restrict tt = s_T, *__restrict mq = s_MQ[ch];
		u16 *__restrict q = s_Q[ch];
		for (unsigned j = 0; j < OW; j++)
			q[j] = (u16) (((u32) tt[j] * mq[j]) >> 15);
	}
}

// CRT: h, linear across, per column (0..255)
static void LineCrt (const u32 *pSrc)
{
	const unsigned M = s_M;
	for (unsigned x = 0; x < GAME_W; x++)
	{
		const u32 c = pSrc[x];
		s_S[0][x + PAD] = (u16) ((c >> 16) & 0xFF);
		s_S[1][x + PAD] = (u16) ((c >> 8) & 0xFF);
		s_S[2][x + PAD] = (u16) (c & 0xFF);
	}
	for (unsigned ch = 0; ch < 3; ch++)
	{
		u16 *s = s_S[ch];
		for (unsigned i = 0; i < PAD; i++)
		{
			s[i] = s[PAD];
			s[GAME_W + PAD + i] = s[GAME_W + PAD - 1];
		}
		for (unsigned p = 0; p < M; p++)
		{
			const u16 *__restrict a = s + PAD + s_Off[p];
			const u16 wa = s_W8[p], wb = (u16) (256 - wa);
			u16 *__restrict o = s_P[p];
			for (unsigned x = 0; x < GAME_W; x++)
				o[x] = (u16) ((u16) (a[x] * wa + a[x + 1] * wb + 128) >> 8);
		}
		Columns (s_Q[ch], (const u16 (*)[GAME_W]) s_P, M);
	}
}

// rows s and M - 1 - s of game line y (16-bit lanes)
static void Rows (unsigned y, TCrtRowFn pRow, void *pCtx)
{
	const unsigned M = s_M, OW = s_OW, rs = s_rs, bs = s_bs;
	const u16 *__restrict q0 = s_Q[0], *__restrict q1 = s_Q[1], *__restrict q2 = s_Q[2];
	for (unsigned s = 0; s < (M + 1) / 2; s++)
	{
		u32 *__restrict d0 = pRow (pCtx, y * M + s);
		u32 *__restrict d1 = pRow (pCtx, y * M + M - 1 - s);
		const boolean bTwo = M - 1 - s != s;
		if (s_nFx == CRTFX_RGB)
		{
			const u16 k = s_RowK[s];
			#define RGB_CH(q)	((u32) Sat ((u16) ((u16) (q * k) >> 7)))
			#define RGB_PX(j)	(0xFF000000u | RGB_CH (q0[j]) << rs | RGB_CH (q1[j]) << 8 | RGB_CH (q2[j]) << bs)
			if (bTwo)
				for (unsigned j = 0; j < OW; j++) { const u32 v = RGB_PX (j); d0[j] = v; d1[j] = v; }
			else
				for (unsigned j = 0; j < OW; j++) d0[j] = RGB_PX (j);
			#undef RGB_PX
			#undef RGB_CH
		}
		else
		{
			const u16 *__restrict k0 = s_K[s][0], *__restrict k1 = s_K[s][1], *__restrict k2 = s_K[s][2];
			#define CRT_CH(h, k)	((u32) Sat ((u16) ((u16) (h * k) >> 7)))
			#define CRT_PX(j)	(0xFF000000u | CRT_CH (q0[j], k0[j]) << rs | CRT_CH (q1[j], k1[j]) << 8 \
						 | CRT_CH (q2[j], k2[j]) << bs)
			if (bTwo)
				for (unsigned j = 0; j < OW; j++) { const u32 v = CRT_PX (j); d0[j] = v; d1[j] = v; }
			else
				for (unsigned j = 0; j < OW; j++) d0[j] = CRT_PX (j);
			#undef CRT_PX
			#undef CRT_CH
		}
	}
}

void CrtFxLine (const u32 *pFrame, unsigned y, TCrtRowFn pRow, void *pCtx)
{
	const unsigned nFx = SharedLoad (s_nFx);
	if (nFx == CRTFX_NONE || y >= GAME_H)
	{
		return;
	}
	const u64 t0 = Ticks ();
	const u32 *pSrc = pFrame + (GAME_SRC_Y + y) * GAME_SRC_PITCH + GAME_SRC_X;
	if (nFx == CRTFX_RGB)
		LineRgb (pSrc);
	else
		LineCrt (pSrc);
	Rows (y, pRow, pCtx);
	const u64 d = Ticks () - t0;
	SharedStore (s_nSum, s_nSum + d);
	SharedStore (s_nCalls, s_nCalls + 1);
	const unsigned nEpoch = SharedLoad (s_nEpoch);
	if (nEpoch != s_nSeen)
	{
		s_nSeen = nEpoch;
		SharedStore (s_nMax, d);
	}
	else if (d > s_nMax)
	{
		SharedStore (s_nMax, d);
	}
}

void CrtFxTakeStats (unsigned *pSumUs, unsigned *pMaxUs, unsigned *pCalls)
{
	static u64 s_nLastSum, s_nLastCalls;	// core 0's: the totals at the last call
	const u64 f = TicksFreq ();
	const u64 nSum = SharedLoad (s_nSum), nCalls = SharedLoad (s_nCalls);
	*pSumUs = (unsigned) ((nSum - s_nLastSum) * 1000000 / f);
	*pMaxUs = (unsigned) (SharedLoad (s_nMax) * 1000000 / f);
	*pCalls = (unsigned) (nCalls - s_nLastCalls);
	s_nLastSum = nSum;
	s_nLastCalls = nCalls;
	SharedStore (s_nEpoch, s_nEpoch + 1);
}
