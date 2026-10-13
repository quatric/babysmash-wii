# BabySmash for Wii

A Wii homebrew port of Scott Hanselman's [BabySmash](https://github.com/shanselman/babysmash):
let a baby mash every button and watch colourful shapes, letters and numbers pop up with giggles.
Written in C with devkitPPC, libogc and GRRLIB; all shapes and letters are drawn as vector
triangles, so there are no image assets. Sounds and the word list are the originals.

## What it does (ported from the original)

* Every press spawns a figure at a random spot with a bouncy random spin/scale-in (the original's
  Tweener easings), a blinking face and a sound. Figures fade over 20 s and only the newest 35
  are kept (both configurable).
* **USB keyboard**: letters and digits become big outlined letters/numbers; every other key makes
  a random shape (circle, oval, rectangle, hexagon, trapezoid, star, square, triangle, heart).
* **Controller buttons** have no letters, so mashing them types random characters (mostly letters,
  some digits, now and then a plain shape).
* Type a real word (2-15 letters from the original `Words.txt`) and its letters slide together
  into a row, and the word is spoken.
* Click a figure (A / left mouse) and it jiggles, snaps, throbs or spins and laughs.
* Click empty space and drag to leave a trail of discs (30 kept) with the bumblebee sound.
* Mouse wheel plays the rising / falling sounds and zooms the figure under the cursor.
* "BabySmash!" startup jingle, the info label, `Hand` / `Arrow` fun cursors, `--fps` overlay.
* Options screen (below) with every setting of the original that makes sense on a Wii.

## Controllers

Same controller set as the GlowWii homebrew app, plus USB mouse and keyboard:

| Device | Pointer | Click | Mash (new figure) |
| --- | --- | --- | --- |
| Wiimote | IR pointer | A | B, 1, 2, +, -, D-pad (random chars) |
| Nunchuk | stick | (A) | Z, C |
| Classic Controller | sticks | A | B, X, Y, L/R/ZL/ZR, +, -, D-pad |
| GameCube pad | stick / C-stick | A | B, X, Y, L, R, Z, D-pad |
| Wii U Pro (libwupc) | sticks | A | as Classic |
| Wii U GamePad (libwiidrc) | sticks | A | as Classic (needs Wii U VC with patched fw.img) |
| DualShock 3 (libsicksaxis) | sticks | Cross | Circle, Square, Triangle, L/R, Start/Select, D-pad |
| USB mouse | mouse | left button | right / middle button (shape), wheel |
| USB keyboard | - | - | every key (letters and digits become glyphs) |

## Options and quitting

* **Options**: hold **Ctrl-Shift-Alt-O** for 3 seconds (keyboard), **+ and -** together (controllers)
  or **all three mouse buttons**. Settings: sounds (None / Laughter / Speech), cursor, force
  UPPERCASE letters (off = random case), faces, visual effects (blur / glow halos), clickless
  mouse drawing, fade away + seconds, clear-after count. Saved to `apps/babysmash/settings.cfg`.
  Navigate with D-pad / arrows / pointer, A / Enter select, B / Esc cancel.
* **Quit**: **Alt-F4**, or hold **HOME** (Wiimote / Classic / GamePad / PS button) or **Start**
  (GameCube) for two seconds. The Wii's front POWER / RESET buttons also work.

## Speech

The original speaks letters, colours, shapes and words with Windows text-to-speech. The Wii can't do
that, so the clips are synthesised on your computer and baked in:

    ./scripts/gen_speech.sh            # letters, digits, colours, shapes -> data/speech.bin
    make                               # embeds it in boot.dol
    ./scripts/gen_speech.sh --words    # optional: ~1800 dictionary words -> speech/words/
                                       # copy `speech/` to apps/babysmash/speech on the SD/USB drive

`ENGINE=say` (default, macOS voice `$VOICE`) sounds natural but Apple's voices must not be
redistributed; `ENGINE=espeak-ng` is robotic but freely redistributable. `data/speech.bin` is
committed as an empty stub; without clips the app falls back to laughter (and the rising jingle for words).

## Not ported (Windows-only)

Multi-monitor windows, transparent background, the keyboard hook / kiosk-mode window handling,
auto-update, font-family choice (letters use a built-in stroke font) and keyboard-language
localisation of spoken names (English only).

## Building

Needs devkitPro with devkitPPC and libogc, plus a few portlibs built into `./portlibs`:

    ./scripts/build_controller_portlibs.sh   # libwiidrc, libwupc, libsicksaxis
    ./scripts/build_grrlib.sh                # GRRLIB
    make                                     # -> boot.dol
    make dist                                # -> dist/apps/babysmash (copy `apps` to the SD card)

`make EXTRA_CFLAGS=-DAUTOTEST` builds a variant that fakes key presses and a click, handy for
checking rendering in Dolphin without a controller.

`scripts/prep_data.sh <path to a babysmash checkout>` regenerates `data/*.bin` (sounds converted
to raw 22.05 kHz mono, and the word list).

## Credits

Original game, sounds and word list: Scott Hanselman, MIT (see `LICENSE.babysmash`).
Controller libraries: [libwiidrc](https://github.com/FIX94/libwiidrc),
[libwupc](https://github.com/FIX94/libwupc), [libsicksaxis](https://github.com/xerpi/libsicksaxis),
[GRRLIB](https://github.com/GRRLIB/GRRLIB).
