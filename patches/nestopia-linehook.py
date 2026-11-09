import sys
# nes-pi: a hook called when the PPU finishes each line (beam racing).
#
# Nestopia's PPU writes every pixel as it emulates it, into its own screen
# buffer (palette indices, 256 x 240); the conversion to RGB normally happens
# once per frame, after the frame. At dot 338 of each line the PPU moves on to
# the next one (`if (scanline++ != 239)`, once in the rendering-on path and
# once in the rendering-off path): there the line just finished is complete.
# The hook gets that line (-1 = the pre-render line, no pixels) and a pointer
# to its 256 pixels, so the frontend can show it at once and pace the next.
#
# Second part: Nestopia emulates the PPU lazily. It catches up only when the
# CPU touches a PPU register (or a mapper needs it) and at the end of the
# frame, so in most games the 240 lines come out in a burst after the whole
# frame has run: too late to race the beam. nes_ppu_sync = 1 installs
# Nestopia's own Ppu::Hook_Sync (EnableCpuSynchronization, otherwise unused)
# at every reset: the PPU is caught up after each CPU instruction and every
# line comes out within a few microseconds of emulated time of when the real
# PPU finishes it. The emulation is identical (same pictures, test ROMs
# checked frame by frame); it costs some CPU, so it is only on for beam racing.
#
# Applied to the copy of NstPpu.cpp in the build directory, never to
# third_party/nestopia. Usage: python3 patches/nestopia-linehook.py build/rpi4/nst/NstPpu.cpp
p = sys.argv[1]
t = open(p).read()
if 'nes_line_hook' in t:
    sys.exit(0)

def one(a, b, n=1):
    global t
    assert t.count(a) == n, (a[:60], t.count(a))
    t = t.replace(a, b)

one('#include "NstState.hpp"\n', '''#include "NstState.hpp"

// nes-pi: called at the end of each line, with the line just finished
extern "C" void (*nes_line_hook)(int line, const unsigned short *pixels);
void (*nes_line_hook)(int line, const unsigned short *pixels) = 0;
// nes-pi: catch the PPU up after every CPU instruction (set before a reset)
extern "C" int nes_ppu_sync;
int nes_ppu_sync = 0;
''')

one('''				cpu.Map( 0x4014U ).Set( this, &Ppu::Peek_4014, &Ppu::Poke_4014 );
			}
''', '''				cpu.Map( 0x4014U ).Set( this, &Ppu::Peek_4014, &Ppu::Poke_4014 );

				// nes-pi: lines out as the frame runs (cpu.Reset cleared the hooks)
				if (nes_ppu_sync)
				{
					cpu.RemoveHook( Hook(this,&Ppu::Hook_Sync) );
					cpu.AddHook( Hook(this,&Ppu::Hook_Sync) );
				}
			}
''')

one('''						if (scanline++ != 239)''', '''						if (nes_line_hook)
							nes_line_hook(scanline, scanline >= 0 ? output.pixels + scanline * 256 : 0);

						if (scanline++ != 239)''', 2)

open(p, 'w').write(t)
print(f'{p}: line hook and PPU sync ok')
