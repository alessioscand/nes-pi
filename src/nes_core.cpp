/*
 * nes-pi - C interface to the Nestopia core (see nes_core.h)
 *
 * Built with Nestopia's flags (C++ exceptions and the standard library on),
 * not with Circle's: see build.sh.
 */
#include "nes_core.h"
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include "NstApiEmulator.hpp"
#include "NstApiMachine.hpp"
#include "NstApiCartridge.hpp"
#include "NstApiVideo.hpp"
#include "NstApiSound.hpp"
#include "NstApiInput.hpp"
#include "NstApiUser.hpp"

using namespace Nes::Api;

extern "C" void (*nes_line_hook)(int line, const unsigned short *pixels);	// NstPpu.cpp (patched)
extern "C" int nes_ppu_sync;					// NstPpu.cpp (patched)

static Emulator *s_pEmulator = 0;
static uint32_t s_Palette[512];
static char s_Error[96] = "";
static char s_Board[48] = "";
static TNesBatteryLoad *s_pBatteryLoad = 0;
static TNesBatterySave *s_pBatterySave = 0;
static bool s_bLoaded = false;

static void SetError (const char *pWhat, Nes::Result nResult)
{
	std::snprintf (s_Error, sizeof s_Error, "%s (Nestopia error %d)", pWhat, (int) nResult);
}

// battery RAM in and out of the core
static void NST_CALLBACK FileIo (User::UserData, User::File &File)
{
	try
	{
		switch (File.GetAction ())
		{
		case User::File::LOAD_BATTERY:
			if (s_pBatteryLoad)
			{
				static unsigned char Buffer[0x20000];
				size_t n = s_pBatteryLoad (Buffer, sizeof Buffer);
				if (n > 0)
				{
					File.SetContent (Buffer, (Nes::ulong) n);
				}
			}
			break;

		case User::File::SAVE_BATTERY:
			if (s_pBatterySave)
			{
				const void *pData = 0;
				Nes::ulong nSize = 0;
				if (NES_SUCCEEDED (File.GetContent (pData, nSize)) && pData && nSize)
				{
					s_pBatterySave (pData, (size_t) nSize);
				}
			}
			break;

		default:
			break;
		}
	}
	catch (...)
	{
	}
}

extern "C" int nes_init (unsigned nSampleRate)
{
	try
	{
		if (s_pEmulator == 0)
		{
			s_pEmulator = new Emulator;
		}
		Emulator &e = *s_pEmulator;

		Sound Snd (e);
		if (NES_FAILED (Snd.SetSampleRate (nSampleRate)))
		{
			SetError ("sample rate", Nes::RESULT_ERR_UNSUPPORTED);
			return 0;
		}
		Snd.SetSpeed (60);			// one frame per 60 Hz HDMI frame
		Snd.SetVolume (Sound::ALL_CHANNELS, 85);

		Video Vid (e);
		Video::RenderState State;
		State.filter = Video::RenderState::FILTER_NONE;
		State.width = NES_W;
		State.height = NES_H;
		State.bits.count = 32;
		State.bits.mask.r = 0x00FF0000;
		State.bits.mask.g = 0x0000FF00;
		State.bits.mask.b = 0x000000FF;
		Vid.SetRenderState (State);
		Vid.GetPalette ().SetMode (Video::Palette::MODE_YUV);
		Vid.EnableUnlimSprites (false);

		User::fileIoCallback.Set (FileIo, 0);
		return 1;
	}
	catch (...)
	{
		SetError ("out of memory", Nes::RESULT_ERR_OUT_OF_MEMORY);
		return 0;
	}
}

extern "C" void nes_set_battery_io (TNesBatteryLoad *pLoad, TNesBatterySave *pSave)
{
	s_pBatteryLoad = pLoad;
	s_pBatterySave = pSave;
}

static void UpdatePalette (void)
{
	Video Vid (*s_pEmulator);
	Video::Palette::Colors Colors = Vid.GetPalette ().GetColors ();
	for (unsigned i = 0; i < 512; i++)
	{
		s_Palette[i] = Colors ? ((uint32_t) Colors[i][0] << 16) | ((uint32_t) Colors[i][1] << 8) | Colors[i][2]
				      : 0;
	}
}

extern "C" int nes_set_palette (int nPalette, const void *pCustom, size_t nSize)
{
	if (s_pEmulator == 0)
	{
		return 0;
	}
	try
	{
		Video Vid (*s_pEmulator);
		Video::Palette Pal = Vid.GetPalette ();
		if (nPalette == NES_PALETTE_CUSTOM)
		{
			if (pCustom == 0 || (nSize != 64 * 3 && nSize != 512 * 3))
			{
				return 0;
			}
			const Video::Palette::CustomType Type = nSize == 64 * 3 ? Video::Palette::STD_PALETTE
										: Video::Palette::EXT_PALETTE;
			if (NES_FAILED (Pal.SetCustom ((Video::Palette::Colors) pCustom, Type)))
			{
				return 0;
			}
			Pal.SetMode (Video::Palette::MODE_CUSTOM);
		}
		else
		{
			Video::DecoderPreset Preset = nPalette == NES_PALETTE_CONSUMER ? Video::DECODER_CONSUMER
						    : nPalette == NES_PALETTE_SONY ? Video::DECODER_CXA2025AS_US
						    : Video::DECODER_CANONICAL;
			Vid.SetDecoder (Video::Decoder (Preset));
			Pal.SetMode (Video::Palette::MODE_YUV);
		}
		UpdatePalette ();
		return 1;
	}
	catch (...)
	{
		return 0;
	}
}

extern "C" void nes_set_unlimited_sprites (int bOn)
{
	if (s_pEmulator != 0)
	{
		Video (*s_pEmulator).EnableUnlimSprites (bOn != 0);
	}
}

extern "C" int nes_load (const void *pRom, size_t nSize)
{
	nes_unload ();
	try
	{
		Emulator &e = *s_pEmulator;
		Machine M (e);
		std::istringstream Stream (std::string ((const char *) pRom, nSize));
		Nes::Result r = M.Load (Stream, Machine::FAVORED_NES_NTSC, Machine::DONT_ASK_PROFILE);
		if (NES_FAILED (r))
		{
			SetError (r == Nes::RESULT_ERR_UNSUPPORTED_MAPPER ? "unsupported mapper" : "cannot load the game", r);
			return 0;
		}
		M.SetMode (Machine::NTSC);

		Input In (e);
		In.ConnectController (0, Input::PAD1);
		In.ConnectController (1, Input::PAD2);

		const Cartridge::Profile *pProfile = Cartridge (e).GetProfile ();
		s_Board[0] = '\0';
		if (pProfile)
		{
			// board type is a wide string: keep the ASCII part
			size_t i = 0;
			for (; i < pProfile->board.type.size () && i < sizeof s_Board - 1; i++)
			{
				wchar_t c = pProfile->board.type[i];
				s_Board[i] = c >= 32 && c < 127 ? (char) c : '?';
			}
			s_Board[i] = '\0';
		}

		r = M.Power (true);
		if (NES_FAILED (r))
		{
			SetError ("cannot start the game", r);
			M.Unload ();
			return 0;
		}
		UpdatePalette ();
		s_bLoaded = true;
		s_Error[0] = '\0';
		return 1;
	}
	catch (...)
	{
		SetError ("out of memory", Nes::RESULT_ERR_OUT_OF_MEMORY);
		return 0;
	}
}

extern "C" void nes_unload (void)
{
	if (!s_bLoaded)
	{
		return;
	}
	try
	{
		Machine M (*s_pEmulator);
		M.Power (false);		// battery RAM goes out here
		M.Unload ();
	}
	catch (...)
	{
	}
	s_bLoaded = false;
}

extern "C" void nes_reset (int bHard)
{
	if (s_bLoaded)
	{
		Machine (*s_pEmulator).Reset (bHard != 0);
	}
}

extern "C" const char *nes_last_error (void)
{
	return s_Error;
}

extern "C" const char *nes_board_name (void)
{
	return s_Board;
}

extern "C" void nes_frame (unsigned nPad0, unsigned nPad1, int16_t *pAudio, unsigned nSamples)
{
	if (!s_bLoaded)
	{
		return;
	}
	static Input::Controllers Ctrl;
	Ctrl.pad[0].buttons = nPad0 & 0xFF;
	Ctrl.pad[1].buttons = nPad1 & 0xFF;

	Sound::Output Out;
	Out.samples[0] = pAudio;
	Out.length[0] = nSamples;
	Out.samples[1] = 0;
	Out.length[1] = 0;

	try
	{
		s_pEmulator->Execute (0, pAudio ? &Out : 0, &Ctrl);	// no video output: the
	}								// lines go out one by one
	catch (...)
	{
	}
}

extern "C" void nes_set_ppu_sync (int bOn)
{
	nes_ppu_sync = bOn ? 1 : 0;
}

extern "C" void nes_set_line_hook (TNesLineHook *pHook)
{
	nes_line_hook = (void (*)(int, const unsigned short *)) pHook;
}

extern "C" const uint32_t *nes_palette (void)
{
	return s_Palette;
}

extern "C" void *nes_state_save (size_t *pSize)
{
	*pSize = 0;
	if (!s_bLoaded)
	{
		return 0;
	}
	try
	{
		std::ostringstream Stream (std::ios::out | std::ios::binary);
		if (NES_FAILED (Machine (*s_pEmulator).SaveState (Stream, Machine::NO_COMPRESSION)))
		{
			return 0;
		}
		const std::string Data = Stream.str ();
		void *p = std::malloc (Data.size ());
		if (p)
		{
			std::memcpy (p, Data.data (), Data.size ());
			*pSize = Data.size ();
		}
		return p;
	}
	catch (...)
	{
		return 0;
	}
}

extern "C" int nes_state_load (const void *pData, size_t nSize)
{
	if (!s_bLoaded)
	{
		return 0;
	}
	try
	{
		std::istringstream Stream (std::string ((const char *) pData, nSize),
					   std::ios::in | std::ios::binary);
		return NES_SUCCEEDED (Machine (*s_pEmulator).LoadState (Stream));
	}
	catch (...)
	{
		return 0;
	}
}

extern "C" void nes_free (void *p)
{
	std::free (p);
}
