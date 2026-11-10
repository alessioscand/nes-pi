# nes-pi - how to build

Build host: Linux x86_64.

1. **Toolchain**: xPack aarch64-none-elf-gcc 14.2.1-1.1 (newlib, libstdc++) in `toolchain/`
   (https://github.com/xpack-dev-tools/aarch64-none-elf-gcc-xpack/releases).
2. **Circle** (rsta2/circle) commit 7a81e1b, copied into `circle-rpi4mc/` and `circle-rpi3mc/`
   with this `Config.mk` (RASPPI = 4 or 3):

   ```
   AARCH = 64
   RASPPI = 4
   PREFIX64 = <path>/toolchain/xpack-aarch64-none-elf-gcc-14.2.1-1.1/bin/aarch64-none-elf-
   STDLIB_SUPPORT = 1
   DEFINE += -DKERNEL_MAX_SIZE=0x1000000
   DEFINE += -DARM_ALLOW_MULTI_CORE
   ```
3. **Circle patches** (pads polled every 1 ms; wired XInput pads):

       python3 patches/circle-usb-poll.py circle-rpi4mc
       python3 patches/circle-usb-poll.py circle-rpi3mc
       python3 patches/circle-xinput.py circle-rpi4mc
       python3 patches/circle-xinput.py circle-rpi3mc

   then `./makeall` in each copy, and `make` in `addon/SDCard` and `addon/fatfs`.
4. **Nestopia** in `third_party/nestopia`: `source/core` of Nestopia UE
   (see `third_party/nestopia/COMMIT`), plus six core fixes from
   libretro/nestopia listed there. The xBR, hqx, 2xSaI and
   ScaleX video filters are left out (`NST_NO_*`). `build.sh` applies
   `patches/nestopia-linehook.py` to the copy of `NstPpu.cpp` in the build
   directory: a hook called at the end of each scanline (dot 338), used for
   beam racing; the same patch installs Nestopia's own per-instruction PPU sync hook
   (`nes_set_ppu_sync`, on when beam racing is available), otherwise the lazy PPU
   would hand over all 240 lines at the end of the frame. Nestopia is C++ with exceptions: it is compiled on its own into
   `libnst.a` (`-O3 -fexceptions`) and linked with the toolchain's libstdc++,
   libsupc++, libm and libc; `src/nes_sys.c` has the newlib system stubs and sends
   malloc to Circle's allocator. `src/nes_core.cpp` is the only file that sees
   the Nestopia API; the rest of nes-pi uses the C interface in `src/nes_core.h`.
5. `./build.sh all` builds `build/rpi4/kernel8-rpi4.img` and `build/rpi3/kernel8.img`.
6. **Raspberry Pi firmware** in `third_party/rpi-firmware`, unmodified (raspberrypi/firmware
   commit 0641c5bf, the one Circle uses; see `COMMIT`), with Circle's armstub8-rpi4.bin.
   `make_release.sh` puts them on the card.
7. `./make_release.sh` checks and packages `dist/nes-pi-<version>-sd.zip`.

## Source files
`src/` is nes-pi's own; `common/` has the files that are the same in neo-pi and
m92-pi (nothing NES in them: what they need comes from `src/project.h`).

- `src/project.h` - name, version, picture size, buttons, LED error codes
- `src/kernel.h`, `src/kernel.cpp` - the kernel class; start-up, the main flow, power off
- `src/console.cpp` - the start-up log on screen, serial and `LOG.TXT`; fatal errors
- `src/settings.cpp` - `nespi.cfg`, the video mode it asks for, the palette, `pads.cfg`
- `src/games.cpp` - the list of .nes files, loading a game, battery RAM, save states
- `src/menus.cpp` - game list, OPTIONS, CONTROLS, power off countdown, credits
- `src/gameloop.cpp` - pads to the NES pads, one emulated frame per screen frame, the in-game menu, the status line
- `src/frame.cpp` - the emulated frame: picture and sound buffers, the line hook, where the lines go
- `src/racing.cpp` - beam racing state for the game loop
- `src/report.cpp`, `src/bench.cpp` - the per-minute report in the log; BENCH
- `src/nes_core.cpp/.h` - Nestopia wrapper (load, frame, pads, audio, battery saves, states)
- `common/composite.c` - the TV filter (blargg's snes_ntsc, in `third_party/snes_ntsc`); its looks
  (S-Video, composite, RF) are in `src/frame.cpp`; `common/tvcore.cpp` runs it on core 1, one line
  at a time
- `src/screen.h` - `CScreen`, the only interface the kernel draws through; `src/video.cpp` is
  the HDMI one (double-buffered framebuffer, HVS/CPU integer scaling, scanlines, text in the black bands).
  A CRT output on the composite jack would be another `CScreen`, chosen in `kernel.h`
- `src/ui.cpp` - menus and screens; `src/fontdata.c` - menu font ("Matinee", ZX Origins by Damien Guard)
- `src/nes_sys.c` - newlib stubs; `src/ziphelp.c` - zip and PNG screenshots (miniz)
- `common/smp.h` - work shared between cores: `SharedLoad` / `SharedStore`, `SharedAcquire` / `SharedRelease`, events, `Ticks`
- `common/raster.cpp`, `common/race.cpp` - beam racing: where the HDMI output is (HVS), and when each line can be sent
- `common/input.cpp` - USB pads and keyboard, control set-up (`nespi/pads.cfg`)
- `common/audio.cpp` - HDMI audio with drift correction
- `common/profile.cpp` - sampling profiler used by the benchmark (`nespi/PROFILE.TXT`)
- `common/fbmap.cpp` - maps the framebuffer as write-combining memory
- `common/pacing.cpp` - frame delay (without beam racing)
- `common/stats.cpp` - timing statistics: histograms, averages, percentiles
- `common/tvcore.cpp` - the TV filter's core: the line queue, the filter (the project's) on its own core
- `src/Makefile.in`, `build.sh`, `make_release.sh`
- `tools/resolve_profile.py` - turns `PROFILE.TXT` into time per function, using the kernel's .elf
- `test/race_sim.cpp` - host simulation of the beam racing plan (late or overwritten lines)

nes-pi's own files are built with `-Wall -Wextra -Wshadow` and must stay free
of warnings.

## Testing without hardware
`./build.sh qemu` builds `build/rpiqemu/kernel8.img` for QEMU `-M raspi3b`, with a
Circle copy in `circle-qemumc/` built with `-DNO_SDHOST -DNO_SCREEN_DMA_BURST_LENGTH`.
`PROJECT_FAKEPAD` turns the USB keyboard into a fake pad (arrows = hat,
Q W E R T Y U I O P = buttons 1-10), since QEMU has no USB gamepad. Not in the real kernels.
`QEMU_DEFINES` adds definitions, e.g. a short benchmark:
`QEMU_DEFINES="-DBENCH_WARMUP=120 -DBENCH_FRAMES=300" ./build.sh qemu`.
QEMU has no HVS, so beam racing is reported "not available" and the normal
double buffer is used.

## Benchmark and profile
An empty file `nespi/BENCH` (no extension) on the card runs a benchmark on the
selected game before the list: 30 s of play, then 60 s measured at full speed.
`nespi/BENCH.TXT` has the frame times, the slowest frames and check values of
the pictures (the same build options must give the same values);
`nespi/PROFILE.TXT` has where the time went, sampled every 200 us. To read it,
keep the `.elf` of the kernel that produced it:

    python3 tools/resolve_profile.py build/rpi3/kernel8.elf PROFILE.TXT

## Release
`./make_release.sh` (after `./build.sh all`) checks that both kernels are built
and carry the version in `src/project.h`, that the firmware (`third_party/rpi-firmware/`) and the
card files (`dist/sd/`) are there and that no games, BENCH file or other user data are in the package, then
writes `dist/nes-pi-<version>-sd.zip`.
