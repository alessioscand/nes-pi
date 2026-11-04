nes-pi 0.1.1 - bare metal NES emulator for Raspberry Pi 3 and 4
==================================================================
NTSC games only (still no PAL or Famicom Disk System).

SET-UP
  1. A micro SD card formatted FAT32 (with a normal partition table).
  2. Copy EVERYTHING in this folder to the root of the card.
  3. Put your games in .nes format (iNES / NES 2.0) in the nespi folder.
     No BIOS is needed.
  4. Connect a pad and the TV (on a Pi 4: HDMI 0, next to the power
     socket), insert the card and switch on.

START-UP
  - The screen stays black for a moment, then the game list appears.
  - TOASTER MODE: with only one game in the nespi folder, the list is
    skipped and the game starts straight away. GAME LIST in the in-game
    menu still shows the list.
  - Game list: up/down to choose, left/right to jump by 8, A or Start to
    play, Select for the OPTIONS menu. Names come from the file names
    (anything from the first "(" on is left out).
  - A game whose board (mapper) is not supported is not started: the list
    stays on screen and LOG.TXT says why.
  - An empty file called nespi/BENCH (no extension) runs a benchmark on the
    selected game first: 30 seconds of the game, then 60 seconds measured
    (do not touch anything). Results in nespi/BENCH.TXT and
    nespi/PROFILE.TXT. Delete BENCH to start normally.

SETTING UP THE CONTROLS
  The first time a pad model is connected, the CONTROLS screen opens by
  itself: press the button you want as A, then B, START and SELECT.
  After the last one the set-up is saved (SAVED). Directions are not set
  up: the d-pad, hat and left stick always work. A button already used is
  ignored. On a keyboard, Esc cancels. The set-up applies to every pad of
  the same model and is stored in nespi/pads.cfg.
  To redo it: OPTIONS (Select in the list) > CONTROLS.

CONTROLS IN GAME
  d-pad or left stick     NES d-pad
  A B                     NES A and B
  Start                   Start
  Select (short press)    NES Select
  Select held 1 second    in-game menu (the game pauses)
  Select + A              screenshot (nespi/shots/<game>_NNN.png)
  Select + B              lazy finger on/off (holding A or B fires
                          repeatedly; for each player, off at every game
                          start)
  Select + Down           status line on/off
  Select + Up             credits (B closes them)
  The second pad connected is Player 2.

IN-GAME MENU (hold Select for 1 second; B resumes the game)
  SAVE STATE     saves the game (one save per game)
  LOAD STATE     loads it (available once something has been saved)
  OPTIONS        the OPTIONS menu (below), over the paused game; B comes
                 back here
  RESET          presses the console's RESET button
  GAME LIST      back to the game list
  POWER OFF      3 second countdown (B cancels), saves everything and
                 switches picture and sound off: then the power can be
                 unplugged

OPTIONS MENU (Select in the list or OPTIONS in the in-game menu; A or
              left/right changes, B goes back)
  TV FILTER      OFF / RGB / S-VIDEO / COMPOSITE / RF: how the console
                 reaches the TV. OFF: sharp square pixels. RGB: the same
                 with light scanlines. S-VIDEO, COMPOSITE and RF use
                 blargg's NTSC filter, a 4:3 picture as a TV of the time
                 showed it, colours included (made by the filter from the
                 NES video signal). S-VIDEO: soft colours. COMPOSITE: also
                 the colour fringes, the dots crawling along the edges and
                 the shimmer, even on a still picture, and the dithering
                 blended into shades, as many games expected. RF: the aerial
                 cable on grandma's TV, every defect at its worst for true
                 nostalgia. Each line is filtered as soon as it is drawn, on
                 a CPU core of its own: no added lag, beam racing still
                 works.
  PALETTE        (OFF and RGB only) the NES colours: NESTOPIA (Nestopia's
                 own), CONSUMER (a TV of the time), SONY TV (the Sony
                 CXA2025AS decoder of many US TVs), CUSTOM (your own: rename
                 any .pal file of 64 or 512 colours to palette.pal and put
                 it in the nespi folder; CUSTOM appears only if the file is
                 there)
  SPRITE FLICKER ORIGINAL (like the console: it shows at most 8 sprites on
                 a line, so when more share a line games make them flicker)
                 or REDUCED (no limit: much less flicker; a few games use
                 the limit on purpose, to hide something behind the edge of
                 a door or a pipe)
  CONTROLS       sets up a pad (with several pads: press a button on the
                 one to set up)
  POWER OFF      as above
  Settings are stored in nespi/nespi.cfg.

STATUS LINE (Select + Down), below the picture

  60 FPS   LOAD 20%   39 C   RACE 2.8   1080P 4X CPU+HVS

  LOAD is how much of each 1/60 s frame is used (green, yellow from 75%,
  red above 95%). 4X HVS = scaled by the Pi's hardware scaler,
  CPU+HVS = the CPU draws the scanlines at 2x and the scaler doubles it,
  CPU = all by the CPU. With beam racing, LOAD leaves out the time spent
  waiting for the HDMI output between lines. RACE (beam racing) is the
  time in ms from reading the pads to the top of the picture being sent
  to the TV; DLY (without beam racing) is the frame delay in ms.

INPUT LAG
  Each frame starts with the vertical blank, when games read the pads, and
  then draws the picture.
  Beam racing (default): the picture is not prepared in advance and shown
  at the next vsync, it is drawn line by line just ahead of the HDMI
  output, as a real NES draws it on a CRT: the pads are read about 2-4 ms
  before the top of the picture is sent to the TV. At start-up nes-pi
  measures the Pi's video scaler for about one second (LOG.TXT: "raster"
  and "Beam racing" lines). The margins adapt to each game; if lines
  arrive late too often, that game goes back to the normal way (double
  buffer + frame delay) and LOG.TXT says so.
  Testing options in nespi/nespi.cfg (one per line), for tests and
  comparisons only: they give more lag or stutter than the defaults.
    beamracing=off      normal way (double buffer + frame delay)
    framedelay=off      no frame delay
    framedelay=5        fixed delay in ms (1-12; too much = stutter)

PICTURE
  Always integer scaling (square pixels, never stretched) with black
  borders. 1080p: 1024x896 picture. 720p: 768x672 picture.
  Overscan: like a TV of the time, the 8 lines at the top and at the bottom
  are hidden and the 8 pixels on each side are black: many games leave
  rubbish there.
  Testing options in nespi/nespi.cfg (one per line), for tests and
  comparisons only: apart from overscan=off, they make nes-pi slower or
  the controls less responsive.
    overscan=off     show all 256 x 240 pixels
    scaler=cpu       do not use the hardware scaler (slower)
    usbpoll=500      poll pads every 2 ms instead of every 1 ms (1000 Hz),
                     only if a pad misbehaves (double or missed presses,
                     disconnects): try 500, then 250
    usbpoll=250      every 4 ms
    usbpoll=native   at the rate the pad asks for (usually every 8-10 ms)

FILES ON THE CARD (nespi folder)
  LOG.TXT      diagnostics: model, HDMI, pads, audio, per-minute timings.
               Updated when the menu opens, when the game changes and at
               power off.
  BENCH.TXT    benchmark results
  PROFILE.TXT  benchmark: where the time goes (for the developer)
  shots/       screenshots (Select + A)
  saves/       save states (.st1) and cartridge saves (.sav, games with a
               battery)
  LAST.TXT     last game played (the list starts there)
  nespi.cfg    OPTIONS settings
  pads.cfg     controls for each pad model
  palette.pal  your custom palette (you put it there: see PALETTE)
  Always switch off with POWER OFF: that is when cartridge saves and
  LOG.TXT are written.

IF SOMETHING GOES WRONG
  The green LED blinks a number of times, repeatedly:
  2 = HDMI   3 = SD card   5 = no .nes game   6 = emulator   7 = memory
  No picture at all: put a # in front of hdmi_group and hdmi_mode in
  config.txt.

nes-pi by Alessio Scanderebech. Licence: GPLv3 (see LICENSES.txt).
Emulation by Nestopia. NES is a trademark of Nintendo. Games not included.
