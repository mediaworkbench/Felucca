# Felucca

This is a fork of [Felucca Firmware for the M-VAVE FM-1](https://github.com/hugelton/Felucca).
[![License: GPL-3.0-only](https://img.shields.io/badge/license-GPL--3.0--only-blue.svg)](LICENSE)

## Overview

The firmware uses [Felucca-Engines](https://github.com/hugelton/Felucca-Engines) (MIT) for its built-in sound engines and [felucca-bsp](https://github.com/hugelton/felucca-bsp) (MPL-2.0) for the FM-1 hardware layer. Neither repository implies support for Felucca on other hardware.

## New in this fork

On top of upstream Felucca 1.8.1 (the version string is still 1.8.1); build it from this branch ([BUILDING.md](BUILDING.md)).

- **DCO engine (14): a Juno-106 voice.** Up to 4 voices a track. EDIT 1 has WAVE, PW, SUB, NOIS, CUT, RES, KEY and HPF; RANGE
  (16' 8' 4') has a page of its own; the track's LFO / ENV -> SHP move the pulse width (PWM). ENV's third page, VCA (DCO
  tracks), is the Juno's VCA switch: ENV, or GATE: the level stays full while a key is held (the ADSR then moves only the
  filter) and shuts on its release, or ADSR, beyond a Juno: the VCA gets its own envelope on a page VCA ENV, ENV then
  moves only the filter. The switch is kept with RANGE (the web editor shows RANGE as 16 8 4, then with G and A).
  70 presets from the Juno-106 factory patches (brass, leads, winds, strings, organs, keys, basses, plucks, bells). After Ultramaster KR-106 (Kayrock Screenprinting) through
  [fm-106](https://github.com/PatttF/fm-106) (PatttF), both GPL-3.0.
- **BBD chorus.** FX > CHORUS, KNOB 3 (TYPE): STD as before, or the Juno's stereo BBD chorus I, II or I+II.
- **303 engine (15): a TB-303 bass voice** (Open303 by Robin Schmidt, with the Devilfish ranges and the SOFT / RAT drive of
  schwung-303), as [X0X](https://github.com/charlesvestal/fm1-x0x) ported it to the FM-1. EDIT 1: CUT, RES, ENV, DCY; EDIT 2:
  ACC, WAVE (SAW / SQR), DRV, DTYP (SOFT / RAT). One voice; a step's SLIDE, or overlapping keys in LEGATO, glide with the
  track's GLIDE as the slide time; an ACC step (velocity 127) accents. It is the only float code in the firmware (the app is
  built for the FPU; the build fails on float anywhere else).
- **TB-3PO, a line generator for the 303.** On a 303 track ARP opens TB-3PO instead of the arpeggiator: three
  generators (X0X's TB-3PO, the AICD model, ACID), knobs that change the playing line from its next bar, line numbers and
  history, morphing between two lines, and links to the drums and the other tracks. See
  [TB-3PO: the 303 line generator](#tb-3po-the-303-line-generator) below.

- **DrumBrain, a pattern generator for DRUM tracks.** On a DRUM track ARP goes on, after ARP and ARP 2, to two pages with
  the model of [DrumBrain](https://github.com/mganss/DrumBrain) (Michael Ganss, MIT), trained on about 250 808 / 909
  patterns. BRAIN: STYLE (ALL, HOUSE, BREAKS), TEMP (below 100 % the likelier steps, above it the rarer ones), DENS (100 %
  is the model's own; lower thins each lane, the weakest hits first) and CTX (context: how far back it looks; lower is more
  random). BRAIN 2: LANES, the lanes it writes (BEAT is kick, snare and both hats; the others stay as they are, so HATS
  re-rolls only the hats), and BARS, the 1 or 2 bars it generates and repeats to LEN. OCT+ makes a new pattern, OCT- a
  variation that keeps the first half. The knobs are for the next one (NEW gets a star). The steps stay normal, editable
  steps and the track's automation stays; SAVE held undoes.

- **Fewer engines, more room.** SAMPLE (4), SLICE (13), LOFI (3) and VOICE (5) and the built-in sample sets (PIANO,
  FLUTE, SAX, SLICE's BREAK) are left out of the build by default (`FELUCCA_SAMPLE`, `FELUCCA_SLICE`, `FELUCCA_LOFI`,
  `FELUCCA_VOICE` = 0; `=1` brings each back): about 143 KB of flash for new engines and sounds. Their numbers stay
  reserved. GRAIN stays and plays the user sample slots USR1..3 (uploaded with the web editor); its factory sounds use
  USR1. Track 3 starts as DCO STRINGS (was LOFI PULSE LD). The engines: ANALOG, FM6, PHASE, TRIO, WHEEL, GRAIN, PHYS,
  NOISE, DRUM, DCO, 303; on the EDIT layer INIT is the key after them (C5, was G5). A project (or backup) with a track on
  one of the four left out is not loaded (EMPTY); a user preset of one plays as FM6.
- **A steadier second core.** With MENU > SYSTEM > 2ND CORE ON, some FM-1s crashed within seconds (FELUCCA CRASH in
  the render; never with it OFF): at the boot loader's supplies (1.40 V / 1.26 V) and both cores busy, they read flash
  and RAM one bit wrong. The firmware now starts with M-VAVE's own supplies (VDC14 1.60 V, SYSVDD 1.38 V), as
  [X0X](https://github.com/charlesvestal/fm1-x0x) 1.0.9 and [SLOOP](https://github.com/isod89/sloop-fm1) 2.7 do
  ([fm1-x0x#21](https://github.com/charlesvestal/fm1-x0x/issues/21)); the console's `status` shows them (`vdc14` 7,
  `sysvdd` 15). felucca-bsp is at its latest: the flash is never switched off while the second core may run from it.
  A crash screen now stays 10 s (was 4), time for a photo of it. If a unit still crashes with 2ND CORE ON, set it OFF
  (a crash beside the second core turns it off until the next power-off).

Compatibility: the engine order is append-only, so projects and sounds of the engines built load unchanged, and with
all four switches at 1 every earlier golden render is bit for bit as before. Upstream's ADPCM engine (its engine 14, off by default there) is not
available here: 14 is DCO and 15 the 303 (`FELUCCA_ADPCM=1` stops the build). Firmware up to 1.6.1 resets the new DCO favourites setting (PER5) and shows projects that use
the DCO engine as EMPTY.

## TB-3PO: the 303 line generator

TB-3PO writes acid lines into a 303 track's steps: notes, rests, accents, slides and ties. They stay normal steps, so
you can edit them by hand like any other, and the track's automation is kept.

### Quick start

1. Pick the 303 engine on a track and press **ARP**: the GENERATE page opens.
2. Press **OCT+** for a new line ("NEW LINE 4711": every line has a number). Press **OCT-** to mutate it (about a quarter
   of the steps change).
3. Press **PLAY** and turn the knobs: DENS, ACCENT, SLIDE and the others change the line from its next bar. Turned back,
   they give you the same line again.
4. Press **ARP** again to step through the pages. **SAVE** held undoes the last OCT+ / OCT-.

While playing, OCT+ and OCT- land on the next bar, so the change is always in time. In a song they are refused
(STOP TO EDIT): there the steps belong to the song's slots. Everything except the line history is saved with the
project.

### The three generators (MODEL > MODE)

- **TB-3PO**: X0X's generator, ported note for note (from the Phazerville Hemisphere Suite's TB_3PO applet). Random
  notes of the scale, the root often on the beat. Quick and wild.
- **AICD**: lines from [Aicd](https://github.com/mganss/Aicd)'s model (Michael Ganss, MIT), learned from many ABL3 acid
  patterns: each step follows the two before it as it did there. Sounds the most like real acid lines.
- **ACID**: a generator made for this fork. It writes a line the way a player would: a few notes (the root, the fifth,
  then the scale's), each next note by its step from the last as often as in real acid lines, a rhythm from a pattern
  cell, accents in runs, slides towards the root, held notes (ties) and, if you like, a song form (AAAB and so on).

### The pages

**ARP** steps through them in this order. SHAPE shows only in AICD and ACID.

A knob marked **live** changes the playing line at its next bar (at once when stopped). A knob marked **next** is for the
next OCT+ / OCT-: the footer's NEW LINE gets a star until you press it.

**GENERATE**

| Knob | What it does |
|---|---|
| DENS | live. How many steps play (0..100 %). Down removes the off-beats first, up brings notes back. |
| ACCENT | live. How many notes are accented. |
| SLIDE | live. How many notes slide into the next one. |
| OCTS | live. The octave range, 1..3 (TB-3PO, ACID). In AICD this knob is TEMP (next): below 100 % the likelier lines, above it the rarer ones. |

**SCALE**

| Knob | What it does |
|---|---|
| ROOT | live. The key. |
| SCALE | live. MIN, PHRY, HARM, MPEN, DOR, MAJ, PDOM (Phrygian dominant), BLUE, LOCR (Locrian), DARK (1, b2, b5). Notes move to the nearest note of the new scale. In AICD it starts at CHRO (no scale). |
| OCTAVE | live. The octave of the root. |
| MUTATE | Auto MUTATE while playing: every 1..16 passes of the line, or OFF. |

**MODEL**

| Knob | What it does |
|---|---|
| MODE | next. TB-3PO, AICD or ACID. |
| SEED | The line's number, 1..9999 (0: a line without one). Turn it to dial any line; note the number of one you like to get it back later. The same number gives the same line with the same settings (MODE, SHAPE, KEEP and ANSWER included). |
| HIST | Steps back through the last 8 lines (0 = the current one). Not saved with the project. |
| KEEP | For OCT+: OFF, NOTES (the new line plays the old notes in a new rhythm) or RHYTHM (new notes over the old rhythm). |

**SHAPE** (ACID)

| Knob | What it does |
|---|---|
| STYLE | next. AUTO, ROLLING, STAB, HYPNOTIC, TRANCE, DUB: sets NOTES, RHYTHM and TIES while those are on AUTO. |
| NOTES | next. How many different notes the line uses: AUTO (3..5) or 1..7. Few notes sound hypnotic. |
| RHYTHM | next. AUTO, 16THS, OFFBEAT, 3-3-2, SPARSE, ROLL, or MODEL (AICD's rhythm under ACID's notes). |
| TIES | next. How many notes are held over into the next step: AUTO or 0..100 %. |

**SHAPE** (AICD)

| Knob | What it does |
|---|---|
| JOIN | next. ON avoids the model's pattern ends inside a line (fewer abrupt restarts), and a MUTATE tries to lead back into the line. |
| BEST | next. Draws 2..8 lines and keeps the best (the loop joins well, the root on step 1, parts that repeat). 1 = off. |

**FORM**

| Knob | What it does |
|---|---|
| FORM | next (ACID only). The line in four parts: AAAB, AABA, ABAC, or CALL (a phrase and its answer). A repeated A changes a note or two, and the end gets a turnaround. Needs LEN 8 or more. |
| FLAIR | next. Adds ratchets (x2, x3) and pickups that play every 4th pass, on weak steps of a new line. |
| TWIST | Opens the filter in an arc over T.BARS bars, a little more on runs of accents. 0 = off. |
| T.BARS | The length of TWIST's arc, 1..16 bars. |

**PLAY**

| Knob | What it does |
|---|---|
| MORPH | live. Moves the line step by step from line A (yours) to a second line B, 0..100 %. |
| SLIP | live. Shifts the notes against the rhythm (the rhythm stays, the melody slides along it), 0..15. |
| P.LEN | live. Cycles only the first 1..16 notes over the rhythm, like a real 303's separate pitch and time lists. OFF = all. |
| M.TYPE | How auto MUTATE changes the line: DRIFT (it keeps changing), VARY (a variation for one pass, then the line comes back), TURN (only the last 4 steps change). |

On PLAY, **OCT+** draws a new line B and **OCT-** swaps A and B.

**LINK**

| Knob | What it does |
|---|---|
| POCKET | live. Fits the bass around the first DRUM track's kick: DODGE (no note on a kick), LOCK (a note on every kick) or AFTER (no note on a kick, one right after it). Follows the drums as you edit them. |
| ANSWER | next. Against the other 303 track: REST (play where it rests), 3RD, 5TH or OCT (its rhythm, the notes a third / fifth / octave up). Its rhythm replaces KEEP's. |
| ROOT< | live. The root follows a track's lowest note, bar by bar: T1..T4 (a chord or pad track), or OFF. The saved ROOT stays. |
| KEYS | What the keys do on this track: PLAY (play notes as usual), ROOT (a key sets the root, at the next bar) or POOL (the keys held are ACID's notes for the next line). With ROOT or POOL the keys make no sound. |

On LINK, **OCT+** rolls a new drum pattern on the first DRUM track (DrumBrain) and a new bass line around its kick at the
same time ("NEW GROOVE"). SAVE held then undoes the bass only.

### Recipes

- **A line for a whole track**: ACID, STYLE HYPNOTIC, NOTES 2, FORM AABA, MUTATE 8 with M.TYPE VARY. It keeps its
  shape and still moves.
- **A breakdown**: on PLAY, press OCT+ for a sparse line B (lower DENS first), then ride MORPH down and back up.
- **Bass and drums that lock**: put a DRUM track next to the 303, on LINK set POCKET to DODGE and press OCT+.
- **Play the key changes live**: LINK > KEYS ROOT, then press keys while the line runs; or ROOT< on a chord track.
- **Keep a good line**: note its number (MODEL > SEED), or step back with HIST after a few OCT+.

### Good to know

- AICD (and ACID's RHYTHM MODEL) needs the build switch `FELUCCA_AICD` (on by default); LINK's drum groove needs
  DrumBrain (`FELUCCA_DBRAIN`, on by default).
- SLIP, P.LEN and POCKET work on the line as it is. After a MUTATE or a hand edit, turning them back does not always
  restore the line exactly.
- Undo does not cover the drums from LINK's OCT+.
- Older firmware loads a project with ACID as AICD and ignores the new settings.

## AI disclaimer

Felucca is developed with the assistance of AI coding agents (code, tests, documentation, translation, maintenance).

## Credits and licence

Free software: [GPL-3.0-only](LICENSE); the Hügelton Sample Pack is CC0 1.0; the engines in `firmware/engines` are MIT
([Felucca-Engines](https://github.com/hugelton/Felucca-Engines)); the hardware layer in `firmware/bsp` is MPL-2.0
([felucca-bsp](https://github.com/hugelton/felucca-bsp)); the bundled fonts and ported DSP keep their own licences
(Open303 for the 303 engine is MIT; [LICENSES/](LICENSES/), [LICENSING.md](LICENSING.md)). Thanks to everyone whose work and contributions are in it: [CREDITS.md](CREDITS.md).

M-VAVE and FM-1 are trademarks of their respective owners. Felucca is not affiliated with, endorsed by or supported by M-VAVE.

Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
