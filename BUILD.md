# nes-pi - how to build (version 0.1.1)

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
   (see `third_party/nestopia/COMMIT`), with `nes_ntsc`. The xBR, hqx, 2xSaI and
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
6. **Raspberry Pi firmware**: raspberrypi/firmware commit 0641c5bf, copied into `dist/sd/`.
7. `./make_release.sh` checks and packages `dist/nes-pi-<version>-sd.zip`.

## Source files
- `src/kernel.cpp` - start-up, settings, game list, game loop, menus, benchmark, log,
  beam racing set-up, frame delay
- `src/nes_core.cpp/.h` - Nestopia wrapper (load, frame, pads, audio, battery saves, states)
- `src/raster.cpp` - HVS raster position and calibration; `src/race.cpp` - per-row pacing
- `src/video.cpp` - framebuffer, HVS/CPU integer scaling, scanlines, text in the black bands
- `src/ui.cpp` - menus and screens; `src/input.cpp` - pads and keyboard; `src/audio.cpp` - HDMI audio
- `src/nes_sys.c` - newlib stubs; `src/ziphelp.c` - PNG screenshots (miniz)
- `test/race_sim.cpp` - host simulation of the beam racer

## Testing without hardware
QEMU `-M raspi3b` with a Circle build using `-DNO_SDHOST -DNO_SCREEN_DMA_BURST_LENGTH`;
`-DNESPI_FAKEPAD` turns the USB keyboard into a pad. QEMU has no HVS, so beam racing
is reported "not available" and the normal double buffer is used.
