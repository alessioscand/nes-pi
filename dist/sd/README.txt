nes-pi 1.0.0 - Bare-metal NES emulator for Raspberry Pi 3 and 4
============================================================================

WHAT'S NEW IN 1.0.0

  - Beam racing: the picture reaches the TV line by line, 2-4 ms after
    the pads are read.
  - Pixel perfect: integer scaling, and five TV filters from sharp
    square pixels to grandma's TV, with no added lag.
  - Game list with each game's region (NTSC, PAL, DENDY); long titles
    scroll.
  - Save states, cartridge saves, screenshots, lazy finger, two players.

SET-UP

  1. Use a microSD card formatted as FAT32, with a standard partition
     table.
  2. Copy EVERYTHING in this folder to the card's root.
  3. Put your games in .nes format (iNES / NES 2.0) in the nespi folder.
     No BIOS is needed. NTSC games only (no PAL or Famicom Disk System
     yet).
  4. Connect a USB gamepad and the display (on a Pi 4, HDMI 0, next to the
     power socket). Insert the card and switch on.

START-UP

  - The screen stays black briefly, then the game list appears.
  - Toaster mode: with only one game in nespi, the list is skipped and the
    game starts at once. GAME LIST in the in-game menu still opens the
    list.
  - Game list: Up/Down to choose, Left/Right to jump by 8, A or Start to
    play, Select for OPTIONS. Names come from the file names (anything
    from the first "(" on is left out). Bottom left: the region, from the
    file name's tags, such as (USA) or (Europe).
  - A game whose board (mapper) is not supported is not started: the list
    stays on screen and LOG.TXT says why.
  - Benchmark: an empty file named nespi/BENCH (no extension) runs one on
    the selected game before normal play: 30 seconds of the game, then 60
    seconds measured. Do not touch anything. Results in nespi/BENCH.TXT
    and nespi/PROFILE.TXT. Delete BENCH to start normally.

SETTING UP THE CONTROLS

  The first time a pad model is connected, the CONTROLS screen opens by
  itself. Press the button you want for each function, in this order: A,
  B, START, SELECT. After the last one, the set-up is saved (SAVED).

  Directions need no set-up: the d-pad, hat and left stick always work. A
  button already assigned is ignored. On a keyboard, Esc cancels.

  The set-up applies to every pad of the same model and is stored in
  nespi/pads.cfg. To redo it: OPTIONS (Select in the game list) >
  CONTROLS.

CONTROLS IN GAME

  D-pad or left stick       NES d-pad
  A, B                      NES A and B
  Start                     Start
  Select (short press)      NES Select
  Select held 1 second      In-game menu (the game pauses)
  Select + A                Screenshot: nespi/shots/<game>_NNN.png
  Select + B                Lazy finger on/off: held, A and B fire
                            repeatedly (per player, off at every game
                            start)
  Select + Down             Status line on/off
  Select + Up               Credits (B closes them)

  The second pad connected is player 2.

IN-GAME MENU (hold Select for 1 second; B resumes the game)

  SAVE STATE    Save the game (one save per game).
  LOAD STATE    Load it (available once a save has been made).
  OPTIONS       The OPTIONS menu, over the paused game; B comes back here.
  RESET         Press the console's RESET button.
  GAME LIST     Back to the game list.
  POWER OFF     3-second countdown (B cancels). Everything is saved, then
                picture and sound are switched off: you can unplug the
                power.

OPTIONS MENU (Select in the game list, or from the in-game menu)

  A or Left/Right changes a setting, B goes back. Settings are stored in
  nespi/nespi.cfg. The menus go through the TV filter too, like the game.

  TV FILTER     OFF / RGB / S-VIDEO / COMPOSITE / RF: Video filters.
                OFF: sharp square pixels. RGB: light scanlines.
                S-VIDEO, COMPOSITE and RF use blargg's NTSC filter:
                a 4:3 picture as a TV of the time showed it.
                S-VIDEO: soft colours.
                COMPOSITE: also the colour fringes, the dots crawling
                along the edges and the shimmer, and the dithering blended
                into shades, as many games expected.
                RF: the aerial cable on grandma's TV, every defect at its
                worst, for true nostalgia.
                Each line is filtered as soon as it is drawn, on its own
                CPU core: no added lag, and beam racing still works.
  PALETTE       The NES colours: NESTOPIA (Nestopia's own), CONSUMER (a TV
                of the time), SONY TV (the Sony CXA2025AS decoder of many
                US TVs), CUSTOM (your own: rename any .pal file of 64 or
                512 colours to palette.pal and put it in nespi; CUSTOM
                appears only if the file is there).
  SPRITE FLICKER
                ORIGINAL: like the console, at most 8 sprites on a line,
                so when more share a line games make them flicker.
                REDUCED: no limit, much less flicker. A few games use the
                limit on purpose, to hide something behind the edge of a
                door or a pipe.
  CONTROLS      Set up a pad (with several pads, press a button on the one
                to set up).
  POWER OFF     As in the in-game menu.

STATUS LINE (Select + Down)

  Below the picture:

    60 FPS   LOAD 20%   39 C   RACE 2.8   1080P 4X CPU+HVS

  - LOAD: how much of each frame is used (green; yellow from 75%; red
    above 95%). With beam racing, the time spent waiting for the HDMI
    output between lines is left out.
  - RACE: the ms from reading the pads to sending the top of the picture
    to the TV. Without beam racing, DLY instead: the frame delay in ms.
  - 4X HVS: scaled by the Pi's hardware scaler. CPU+HVS: the CPU draws the
    scanlines at 2x and the scaler doubles it. CPU: all by the CPU.

LOW INPUT LAG

  Each frame starts with the vertical blank, when the games read the pads,
  then draws the picture: the next frame shown already answers the pads.
  No run-ahead.

  Beam racing (default): the picture is drawn line by line just ahead of
  the HDMI output, as a real NES draws it on a CRT: the pads are read
  about 2-4 ms before the top of the picture is sent to the TV. At
  start-up nes-pi measures the Pi's video scaler for about a second. If
  lines are late too often, that game goes back to double buffering +
  frame delay (LOG.TXT says so); the next game started from the list
  tries again.

PIXEL PERFECT

  Integer scaling only (square pixels, never stretched), with black
  borders. 1080p: 1024x896 picture. 720p: 768x672 picture.

  Overscan: like a TV of the time, the 8 lines at the top and at the
  bottom are hidden and the 8 pixels on each side are black: many games
  leave rubbish there.

TESTING OPTIONS (in nespi/nespi.cfg, one per line)

    overscan=off     Show all 256 x 240 pixels
    beamracing=off   Double buffering + frame delay (more input lag)
    framedelay=off   No frame delay
    framedelay=5     Fixed delay in ms (1 to 12; too much causes stutter)
    scaler=cpu       No hardware scaler (slower)
    usbpoll=500      Poll pads every 2 ms instead of every 1 ms (1000 Hz)
    usbpoll=250      Every 4 ms
    usbpoll=native   At the rate the pad asks for (usually every 8-10 ms)

  If a pad misbehaves (double or missed presses, disconnects), try
  usbpoll=500, then usbpoll=250.

FILES ON THE CARD (nespi folder)

  LOG.TXT        Diagnostics: model, HDMI, pads, audio, per-minute
                 timings. Updated when the menu opens, when the game
                 changes and at power-off
  BENCH.TXT      Benchmark results
  PROFILE.TXT    Benchmark: where the time goes (for the developer)
  shots/         Screenshots (Select + A)
  saves/         Save states (.st1) and cartridge saves (.sav, games with
                 a battery)
  LAST.TXT       Last game played (the list starts there)
  nespi.cfg      OPTIONS settings
  pads.cfg       Controls for each pad model
  palette.pal    Your custom palette (you put it there: see PALETTE)

  Always switch off with POWER OFF: it saves settings, cartridge saves and
  LOG.TXT. For help, LOG.TXT and BENCH.TXT are all that is needed.

IF SOMETHING GOES WRONG

  The green LED blinks a number of times, repeatedly:
  2 = HDMI   3 = SD card   5 = no .nes game   6 = emulator   7 = memory

  No picture at all: put a # in front of hdmi_group and hdmi_mode in
  config.txt.

----------------------------------------------------------------------------

nes-pi is a human-led, vibe-coded experiment by Alessio Scanderebech,
written with Claude. Free software, licensed under GPLv3 (see LICENSES.txt).
Site: https://alessioscand.github.io/nes-pi/

Emulation by Nestopia. NES is a trademark of Nintendo. Games are not
included.
