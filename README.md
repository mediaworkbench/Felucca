# Felucca

This is a fork of [Felucca Firmware for the M-VAVE FM-1](https://github.com/hugelton/Felucca).
[![License: GPL-3.0-only](https://img.shields.io/badge/license-GPL--3.0--only-blue.svg)](LICENSE)

## Overview

- [Web installer](https://hugelton.github.io/Felucca/)
- [Web editor](https://hugelton.github.io/Felucca/webapp/editor/) ([source](https://github.com/hugelton/Felucca-WebApp))
- [Manual and release notes](https://github.com/hugelton/Felucca/wiki/Home)
- [Building from source](BUILDING.md)
- [Issue tracker](https://github.com/hugelton/Felucca/issues)

The firmware uses [Felucca-Engines](https://github.com/hugelton/Felucca-Engines) (MIT) for its built-in sound engines and [felucca-bsp](https://github.com/hugelton/felucca-bsp) (MPL-2.0) for the FM-1 hardware layer. Neither repository implies support for Felucca on other hardware.

## New in this fork

On top of upstream Felucca 1.8.1 (the version string is still 1.8.1); build it from this branch ([BUILDING.md](BUILDING.md)).

- **DCO engine (14): a Juno-106 voice.** Up to 4 voices a track. EDIT 1 has WAVE, PW, SUB, NOIS, CUT, RES, KEY and HPF; RANGE
  (16' 8' 4') has a page of its own; the track's LFO / ENV -> SHP move the pulse width (PWM). 20 presets from the Juno-106
  factory patches. After Ultramaster KR-106 (Kayrock Screenprinting) through
  [fm-106](https://github.com/PatttF/fm-106) (PatttF), both GPL-3.0.
- **BBD chorus.** FX > CHORUS, KNOB 3 (TYPE): STD as before, or the Juno's stereo BBD chorus I, II or I+II.
- **303 engine (15): a TB-303 bass voice** (Open303 by Robin Schmidt, with the Devilfish ranges and the SOFT / RAT drive of
  schwung-303), as [X0X](https://github.com/charlesvestal/fm1-x0x) ported it to the FM-1. EDIT 1: CUT, RES, ENV, DCY; EDIT 2:
  ACC, WAVE (SAW / SQR), DRV, DTYP (SOFT / RAT). One voice; a step's SLIDE, or overlapping keys in LEGATO, glide with the
  track's GLIDE as the slide time; an ACC step (velocity 127) accents. It is the only float code in the firmware (the app is
  built for the FPU; the build fails on float anywhere else).
- **TB-3PO, a MIDI/pattern generator for the 303.** On a 303 track ARP opens TB-3PO instead of the arpeggiator. GENERATE:
  DENS, ACCENT, SLIDE, OCTS; SCALE: ROOT, SCALE, OCTAVE, MUTATE (every 1..16 passes of the line while playing, or OFF).
  OCT+ writes a new line into the track's 64 steps (notes, accents, slides, rests), OCT- mutates about 25 % of the steps.
  The steps stay normal, editable steps. An integer port of X0X's generator that makes the same notes for the same seed
  and settings (from the Phazerville Hemisphere Suite's TB_3PO applet); a rest keeps a note of its own. DENS, ACCENT,
  SLIDE, OCTS, ROOT, SCALE and OCTAVE act on the line while it plays, from its next bar (at once when stopped): DENS
  silences or brings back steps, ACCENT and SLIDE add or remove them on notes, always the same steps first; ROOT and
  OCTAVE transpose, SCALE moves each note to the nearest one of the scale, OCTS folds or spreads the octaves. Turned
  back, the line is the one it was (hand edits included). TEMP and MODE are for the next OCT+ / OCT-. A third page, MODEL, sets MODE to AICD: the lines
  then come from [Aicd](https://github.com/mganss/Aicd)'s model (Michael Ganss, MIT), an n-gram trained on many ABL3
  acid patterns, so each step follows the two before it as it did in those. In AICD, GENERATE is DENS, ACCENT, SLIDE (as in TB-3PO, 0..100 %: the share of the steps that are notes, and of
  the notes accented or sliding; the defaults are the model's own) and TEMP (below 100 % the likelier lines, above it the rarer ones);
  SCALE starts at CHRO (no bias) and otherwise favours the scale's notes; ROOT and OCTAVE transpose. The three leave the
  model's lines as they are while they are within a step or so of what they say (so TEMP stays in charge), and past it
  turn random steps on or off, only into steps the model has; 0 and 100 % are exact. OCT- draws a run of a quarter of the line
  (16 steps at most) anew, leaning to the three but not fitted (the rest of the line stays).

Compatibility: the engine order is append-only, so existing projects and sounds load unchanged and every earlier golden
render is bit for bit as before. Upstream's ADPCM engine (its engine 14, off by default there) is not
available here: 14 is DCO and 15 the 303 (`FELUCCA_ADPCM=1` stops the build). Firmware up to 1.6.1 resets the new DCO favourites setting (PER5) and shows projects that use
the DCO engine as EMPTY.

## AI disclaimer

Felucca is developed with the assistance of AI coding agents (code, tests, documentation, translation, maintenance).

## Credits and licence

Free software: [GPL-3.0-only](LICENSE); the Hügelton Sample Pack is CC0 1.0; the engines in `firmware/engines` are MIT
([Felucca-Engines](https://github.com/hugelton/Felucca-Engines)); the hardware layer in `firmware/bsp` is MPL-2.0
([felucca-bsp](https://github.com/hugelton/felucca-bsp)); the bundled fonts and ported DSP keep their own licences
(Open303 for the 303 engine is MIT; [LICENSES/](LICENSES/), [LICENSING.md](LICENSING.md)). Thanks to everyone whose work and contributions are in it: [CREDITS.md](CREDITS.md).

M-VAVE and FM-1 are trademarks of their respective owners. Felucca is not affiliated with, endorsed by or supported by M-VAVE.

Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
