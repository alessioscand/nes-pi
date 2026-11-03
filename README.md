# nes-pi

A streamlined, bare-metal NES emulator for Raspberry Pi 3 & 4, with no
unnecessary layers and highly responsive input.

Website:
[alessioscand.github.io/nes-pi](https://alessioscand.github.io/nes-pi/)

If you enjoy nes-pi, please consider supporting me:
[paypal.me/alescandy](https://paypal.me/alescandy)

## Why nes-pi

The Nintendo Entertainment System (NES for short) was my first console and
my introduction to gaming at home: before that, my gaming world was all
arcades. I still collect NES cartridges to this day, but rarely get to play
them: original hardware takes love, dedication and time that I don't always
have. How many times have we been greeted by that blinking screen, even
after the ritual blow into the cartridge? And then there's the input lag of
modern displays and emulators, which makes games that need split-second
timing even harder to play.

So, after [neo-pi](https://alessioscand.github.io/neo-pi/), and making the
most of vibe coding (with all due respect to its detractors), I decided to
take on its sibling project: **nes-pi**. The idea was the same: a focused
experience, as close to the original hardware as possible, with the
conveniences of modern emulation (save states, turbo buttons, the look of a
TV of the time) and the responsiveness these games deserve. So let's look at
how it's made.

## The choices behind it

**Bare metal.** Nothing sits between the game and the screen: no operating
system on the Pi's CPU and no graphics stack adding latency. nes-pi IS the
kernel itself: it runs directly on the Pi's hardware, boots in seconds and
talks to the hardware directly, down to the video scaler's registers.
**Circle** provides the essential drivers (USB, SD, HDMI, audio), so bare
metal doesn't mean starting from scratch. nes-pi shares this foundation with
its older brother, [neo-pi](https://github.com/alessioscand/neo-pi).

**Nestopia.** One of the most accurate NES emulators around, with support
for hundreds of cartridge boards (mappers). Its core is written in portable
C++ that doesn't assume an OS: it renders into a buffer it is given and
produces sound at the rate it is asked for (48 kHz, exactly what HDMI wants,
so no resampler is needed). That made it a natural fit for Circle.

**Line-by-line rendering.** A real NES draws the picture one line at a time,
as the beam of the TV goes down the screen. Nestopia, to save time, may wait
and draw several lines at once. nes-pi asks it to keep pace with the CPU, so
that each line is ready as soon as the emulated PPU reaches it, and never
touched again. And since each line is ready while the frame is still being
emulated, it can be sent to the screen just before the HDMI output reaches
it: *beam racing!* So the pads are read only a couple of milliseconds before
the picture appears, as they would be on the original hardware with a CRT.
With the TV filter, a second core filters each line as it arrives.

**User experience.** The rest was a matter of choices. Display options are
cut to the bone: integer scaling is always on, and the TV filter offers just
the ways a NES could reach a TV of the time (RGB, S-Video, composite, RF),
so you just play, with nothing to tweak and nothing to distract you. Save
states are limited to one slot per game: enough to soften the gameplay
(think of the last levels of some notoriously unforgiving platformers) while
keeping that edge-of-your-seat tension that the games of the time could give
you.

## Features

**Gameplay**

- Accurate emulation, built on the
  [Nestopia](https://github.com/0ldsk00l/nestopia) core: hundreds of
  cartridge boards, NTSC games, battery saves kept on the SD card
- Responsive controls: pads are read every millisecond, and what you press
  shows up on screen almost as quickly as on the original hardware with a
  CRT
- Beam racing: each line is sent to the screen just after it is drawn, so
  the pads are read a couple of milliseconds before the picture appears
- *Lazy finger* (turbo buttons): Select + B toggles it; while on, holding A
  or B fires repeatedly
- Save states: one slot per game

**Picture**

- Integer scaling with square pixels at 1080p or 720p, done by the Pi's
  hardware scaler
- TV filter: OFF (sharp pixels), RGB (light scanlines), or Blargg's NTSC
  filter for the picture of a TV of the time through S-Video, composite or
  RF, filtered line by line on a core of its own: no added lag *(to be
  tested on Pi 3)*
- Overscan like a TV of the time: the borders where many games leave rubbish
  are hidden
- Palettes: Nestopia's own, a consumer TV, the Sony CXA2025AS decoder, or
  any `.pal` file of your choice (e.g. the FirebrandX palettes)
- Sprite flicker: original (at most 8 sprites per line, like the console) or
  reduced (no limit)

**Convenience**

- Toaster mode: with only one game on the SD card, nes-pi skips the list and
  boots straight into it, like a cartridge in the slot
- Screenshots (PNG) and an optional status line (FPS, load, temperature,
  video mode)
- Safe power off that saves the cartridge saves and the log

**Planned:**

<!-- TODO (Alessio): planned features -->


## Requirements

- Raspberry Pi 4 (the Pi 400 should work too) or Raspberry Pi 3 B+
- A micro SD card formatted FAT32
- A USB gamepad and an HDMI TV or monitor
- Your own games in `.nes` format (iNES / NES 2.0), NTSC only (no PAL, no
  Famicom Disk System). No BIOS is needed. **Games are not included and are
  never distributed with nes-pi.**


## Getting started

1. Download the SD card package from the [Releases](../../releases) page.
2. Copy everything in it to the root of the card.
3. Put your games in `.nes` format in the `nespi` folder.
4. Connect a pad and the TV (HDMI 0 on RPi4, next to the power socket) and
   switch on.


## Controls

Although wireless gamepads are supported, wired ones are preferable for
maximum responsiveness. The first time a pad model is connected, nes-pi asks
you to press the buttons you want as A, B, Start and Select. The d-pad, hat
and left stick always work. Second pad connected is Player 2.

| In game | |
|---|---|
| Select (short press) | NES Select |
| Select, held 1 s | In-game menu: save / load state, reset, game list, power off |
| Select + A | Screenshot (PNG) |
| Select + B | *Lazy finger* (turbo A and B) on/off |
| Select + Down | Status line on/off |
| Select + Up | Credits |

In the game list, Select opens the options (TV filter, palette, sprite
flicker, controls, power off). In the menus the buttons are shown on screen.
Always switch off with POWER OFF (in-game menu or options) before
unplugging: it saves the cartridge saves and settings.


## Tinkerer's corner

Everything nes-pi writes stays in the `nespi` folder: `saves/` (save states
and cartridge saves), `shots/` (screenshots), `nespi.cfg` (options),
`pads.cfg` (controls for each pad model), `LAST.TXT` (last game played),
`LOG.TXT` (diagnostics).

**Testing options.** The defaults are what nes-pi is designed and tuned for:
the lines below are only meant for testing and comparisons, not for everyday
play. Add them to `nespi/nespi.cfg`, one per line:

| Option | |
|---|---|
| `beamracing=off` | double buffer instead of beam racing (more lag) |
| `framedelay=off` | no automatic frame delay |
| `framedelay=5` | fixed frame delay in ms (1-12; too much = stutter) |
| `overscan=off` | show all 256 x 240 pixels |
| `usbpoll=500` | poll pads every 2 ms instead of every 1 ms (if a pad misbehaves at 1000 Hz) |
| `usbpoll=250` | poll pads every 4 ms |
| `usbpoll=native` | poll pads at the rate they ask for instead of every 1 ms |
| `scaler=cpu` | scale with the CPU instead of the hardware scaler (slower) |
| `writecombine=off` | write the framebuffer the old way (slower) |

An empty `nespi/BENCH` file runs a built-in benchmark on the selected game
(results in `BENCH.TXT` and `PROFILE.TXT`).

**If something goes wrong.** The green LED blinks a number of times,
repeatedly: 2 = HDMI, 3 = SD card, 5 = no `.nes` game, 6 = emulator, 7 =
memory. A game whose cartridge board is not supported is not started: the
list stays on screen and `LOG.TXT` says why. With no picture at all, put a
`#` in front of `hdmi_group` and `hdmi_mode` in `config.txt`. To report a
problem, `nespi/LOG.TXT` is all that is needed; for performance, add an
empty `nespi/BENCH` file and send `BENCH.TXT` and `PROFILE.TXT` too.

**Heat.** If games start to slow down after a while, it is usually heat: a
heatsink is recommended. The status line (Select + Down) shows the
temperature; the Pi 3 B+ lowers its CPU clock above 60 °C, the Pi 4 from
about 80 °C.

## Building from source

See [BUILD.md](BUILD.md). nes-pi is built with the xPack AArch64 GCC
toolchain on top of the Circle bare-metal library and the Nestopia core.


## Credits

nes-pi is written by Alessio Scanderebech and his much-maligned programming
partner Claude, standing on the shoulders of giants:

| Component | Thanks to |
|---|---|
| Bare-metal system | [Circle](https://github.com/rsta2/circle) by Rene Stange |
| NES core | [Nestopia](https://github.com/0ldsk00l/nestopia) by Martin Freij, maintained as Nestopia UE by Phil Smith, Rupert Carmichael and contributors |
| NTSC filter | nes_ntsc by Shay Green (blargg) |
| Zip and PNG | miniz by Rich Geldreich |
| Menu font | "Matinee" from [ZX Origins](https://damieng.com/typography/zx-origins/) by Damien Guard |


## License

nes-pi is free software, released under the [GNU General Public License
v3](LICENSE) or any later version, as required by Circle (GPLv3). The
third-party components keep their own licences (Nestopia: GPL v2 or later;
nes_ntsc: LGPL; MIT and others); the full texts are in
[`dist/sd/LICENSES.txt`](dist/sd/LICENSES.txt).

NES is a trademark of Nintendo. nes-pi is an independent project, not
affiliated with or endorsed by Nintendo, and contains no games or other
Nintendo material.