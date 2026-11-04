# Integrating acidgen into another firmware

This guide is written for an AI coding agent (or a person) adding the acid line generator to a firmware that has a
step sequencer and a monophonic bass voice (a 303 emulation or any synth). Work through the steps in order; each
ends with something to check. Read `src/acidgen.h` alongside: it is the authoritative API.

---

## 0. Before you start: confirm these with the firmware's owner

1. **Licence.** acidgen's code is **GPL-3.0-only**. The target firmware must be distributed under the GPL-3.0 (or a
   licence that lets the combination be GPL-3.0: GPL-3.0-or-later, or permissive code such as MIT / BSD / Apache-2.0
   inside a GPL-3.0 whole). If the target is proprietary or GPL-2.0-only, **stop and tell the owner**: it cannot
   include this code. The AICD model data is MIT (keep its notice).
2. **Flash.** TB-3PO alone needs ~4 KB of code. AICD needs ~24 KB more of const tables plus ~2.5 KB of code (Felucca on
   a 32-bit core built with -Os: +26.5 KB in all). If the image cannot take that, build with `ACIDGEN_AICD=0` (TB-3PO
   only) and say so.
3. **Where the lines go.** The generator only fills an array of steps. The target needs a sequencer with per-step
   note, gate (note / rest, ideally tie), accent and slide. If it has no slide or accent, the lines still work, they
   just lose those (see step 4).

---

## 1. What you are integrating

```
            settings (acid_cfg_t)  +  seed
                      |
     acid_generate()  |  acid_mutate()          <- pure functions on the caller's buffers
                      v
         acid_step_t line[64]  {note, flags}
                      |
     your glue (examples/sequencer_adapter.c)   <- you write this: read / write the target's steps
                      v
         the target's sequencer steps  ->  its 303 voice
```

- `acid_step_t`: `note` (MIDI 0..127) and `flags`: a gate (`ACID_NOTE`, `ACID_REST`, `ACID_TIE`; mask `ACID_GATE`)
  | `ACID_ACC` | `ACID_SLD`.
- A line buffer is **always `ACID_NSTEP` (64) steps**, whatever the line's length `len` (1..64). The generator writes all
  64; steps from `len` on are `ACID_REST`. A target with 16 steps: keep 64-step buffers, pass `len <= 16`, use the
  first `len`.
- Everything follows from `acid_cfg_t` and its `seed`: same input, same line, on any compiler and CPU.
- No global state: one `acid_cfg_t` per track; tracks are independent.

---

## 2. Add the files to the build

Copy into the target (keep the licence headers):

| File | Needed |
|---|---|
| `src/acidgen.h` | yes |
| `src/acidgen.c` | yes |
| `src/acidgen_model.h` | yes with AICD (included by acidgen.c; ~24 KB of `static const` tables: they must land in flash / ROM, not be copied to RAM) |
| `src/acidgen_selftest.c` | recommended for a debug build (step 3) |
| `LICENSES/GPL-3.0.txt`, `LICENSES/MIT-Aicd.txt` | yes (with the target's licences) |

Build requirements:

- C99. `uint32_t` arithmetic must wrap modulo 2^32 (any conforming compiler); `int` at least 32 bits is assumed by
  some intermediate expressions (16-bit `int` targets: not supported without review).
- One `uint64_t` multiply per drawn step (`aicd_next`): on a 32-bit core the compiler emits its 64-bit multiply
  helper (`__aeabi_lmul`, `__muldi3`) or an instruction. If the target links without libgcc / compiler-rt, provide
  that helper.
- No float, no libc, no heap. `-ffast-math` and the like do not matter (no float).
- Options: `-DACIDGEN_AICD=0` for TB-3PO only.
- If the target is a single compilation unit (`#include "x.c"` style, like Felucca), include `acidgen.c` once; its
  internals are `static` and prefixed `acid_` / `aicd_` / `AICD_` / `ACID_`: check for clashes with the target's
  names.

**Check:** the target builds; the map file shows `AICD_REC` (~14 KB) and `AICD_LAST` (~6 KB) in flash, not in RAM /
`.data`.

---

## 3. Prove the port is exact

On the host (needs a C compiler; python3 for the model check):

```sh
sh test/run.sh          # with the host's compiler; must end with: ACIDGEN: ALL PASSED
# the target's (cross) compiler: compile only, with its own flags, warnings as errors
arm-none-eabi-gcc -std=c99 -Wall -Wextra -Werror -Os -Isrc -c src/acidgen.c src/acidgen_selftest.c
```

Without python3, `run.sh` skips the exact model check and says so; install python3 for a full run.

On the target (debug build, once): call `acid_selftest()` and report its result (UART, LED, debugger). It must return
1. It hashes 64 settings × a line and a MUTATE per mode: `acid_selftest_tb3po()` must be `0xCB4CC370` and
`acid_selftest_aicd()` `0x99FE18AC` (Felucca's own tests check the same values). It needs ~1.2 KB of stack and ~5 M
instructions (tens of ms on a 100 MHz core): not in an interrupt, not with a watchdog that short.

If they differ: the compiler or the copy changed something. Check `uint32_t` is 32 bits, the file was copied
byte for byte, `acidgen_model.h` is the one from the package, and no `-fshort-enums` / packed-struct option breaks
`acid_cfg_t`. Do not "fix" the generator to match; find the build difference.

---

## 4. Map a line onto the target's steps

The semantics, which are the 303's (and Felucca's):

| Line step | Means | The target's step |
|---|---|---|
| `ACID_NOTE` | a note `note` | gate on, that pitch |
| `ACID_REST` | silence | gate off. **Keep `note` stored** if the step can hold it: a later MUTATE may turn the rest into a note again with that pitch. If it cannot, that is fine (a MUTATE reads whatever pitch is there). |
| `ACID_TIE` | the note before held on | never generated; it only appears when you read back a hand-edited TIE (a MUTATE may keep or replace it; TIE + SLD before a REST becomes a NOTE, as in X0X). No tie in the target: never produce `ACID_TIE` when reading back. |
| `ACID_ACC` (on a note) | 303 accent | the target's accent flag. No accent flag: velocity 127 (accented) / 96..100 (not: Aicd uses 99, Felucca 96). |
| `ACID_SLD` (on a note) | **slide into the next step**: the gate stays on into the next step and the pitch glides to its note (303 slide); never set when the next step is a REST | the target's slide / legato flag on *this* step. A target whose "slide" flag means "glide into this step" must put it on the *next* step instead. No slide: drop it (or lengthen the gate to tie). |

Write the glue as in `examples/sequencer_adapter.c` (copy and adapt it):

- `line_read(track, line)`: the target's steps → `acid_step_t[64]`. A MUTATE starts from it, so hand edits are
  respected. Steps past the target's own step count: `ACID_REST`.
- `line_write(track, line, old)`: `old == NULL` (a new line): write every step whole, clearing what the generator
  does not set (velocity, probability, ratchet, micro-timing...), velocity 96 (or the target's default) on notes.
  `old != NULL` (a MUTATE): write **only the steps whose note or flags differ from `old`**, and in those only note /
  gate / accent / slide: the step's other parameters stay (this is what keeps a user's per-step edits).

**Check:** generate with `acid_defaults(&cfg, 0)` (seed 0xBEEF), `len` 16, write it, read it back: `line_read` gives
the same 16 steps (note and flags) as `acid_generate` wrote.

---

## 5. Per-track state

```c
typedef struct {
    acid_cfg_t gen;      /* settings + seed: saved with the track / project */
    uint32_t mut_rng;    /* auto MUTATE's random stream: 0 at power-on and on load (= start from the seed) */
    uint32_t passes;     /* passes of the line since PLAY */
    uint8_t stale;       /* settings changed since the line was made (UI hint) */
} my_tb3po_t;
```

Initialise with `acid_defaults(&gen, 0)` (X0X's: DENS 70, ACCENT 40, SLIDE 25, OCTS 2, A minor, OCTAVE 1, MUTATE off,
seed 0xBEEF, mode TB-3PO, TEMP 100, CHRO; AICD's DENS 90, ACCENT 35, SLIDE 32: the model's own shares).

Persistence: `acid_pack(&gen, buf)` writes 26 bytes, each 0..127 (safe in MIDI SysEx); `acid_unpack(&gen, buf)` reads
them back (returns 0 and loads the defaults for anything that is not such a block; a block without the AICD part
loads as TB-3PO). The layout is Felucca's (a 303 track's block in its projects), so settings can be exchanged.
If the target already stores per-track data in a versioned format, add these 26 bytes as a new field: old data
without them → `acid_defaults`.

---

## 6. The actions (UI) and their exact rules

To behave like Felucca (and X0X), reproduce these seeds and streams exactly:

| Action (Felucca's button) | Do |
|---|---|
| **NEW LINE** (OCT+) | `gen.seed = acid_new_seed(entropy)` (Felucca: `ms ^ (ui_frame << 7)`; any changing value), then `acid_generate(&gen, line, len)`, `line_write(..., NULL)`; `stale = 0`. Show the seed's low 16 bits as 4 hex digits ("NEW LINE B5CD"). |
| **MUTATE** (OCT-) | `rng = gen.seed ^ ms` (a fresh stream), `line_read` → copy → `acid_mutate(&gen, line, len, &rng)` → `line_write(..., old)`; `stale = 0`. |
| a knob | change the setting only; the line keeps playing as it is. Set `stale = 1` (Felucca shows "NEW LINE *"), except for MUTATE (auto MUTATE's rate). |
| **auto MUTATE** | in the sequencer, as step 1 of a pass is about to play (before it sounds): on PLAY's first pass `passes = 0`; on each later pass `passes++`, and when `mutate_bars && passes % mutate_bars == 0`: `line_read` → `acid_mutate(&gen, line, len, &mut_rng)` → `line_write(..., old)`. The stream `mut_rng` carries on from MUTATE to MUTATE. Not while a song / chain plays steps that are not this track's own (Felucca refuses there). |
| undo | take the target's undo snapshot of the steps before NEW LINE / MUTATE. |

Concurrency (an interrupt-driven sequencer):

- Work the line out with interrupts **on** (AICD: ~100 k instructions), then write the steps with the sequencer's
  interrupt **off** (or a lock / double buffer). Reading for a MUTATE: copy the steps with it off.
- If the sequencer's own auto MUTATE may run between your read and your write, re-read and redo (Felucca counts auto
  MUTATEs and retries if the count moved; see `ui_tb3po.c tb3_oct` in Felucca).
- Auto MUTATE runs inside the sequencer (in Felucca, the audio interrupt). Its cost is bounded: TB-3PO ~2 k, AICD ~27 k
  instructions (host x86-64, 64 steps: AICD redraws at most 16). If that is too much for the target's interrupt, set
  a flag there and do it in the main loop before the next step plays.
- Stack: ~1 KB (`aicd_next` ~0.5 KB, two 128-byte lines in the caller).

---

## 7. The settings and how to show them

Felucca's pages (ARP on a 303 track; 4 knobs each), a good default layout:

| Page | TB-3PO | AICD |
|---|---|---|
| GENERATE | DENS 0..100 %, ACCENT 0..100 %, SLIDE 0..100 %, OCTS 1..3 | DENS, ACCENT, SLIDE 0..100 % (as TB-3PO's: the share of the steps that are notes, of the notes accented / sliding), TEMP 10..200 % |
| SCALE | ROOT C..B, SCALE `acid_scale_names`, OCTAVE 0..4, MUTATE OFF / 1..16 | ROOT, SCALE `acid_aicd_scale_names` (CHRO first), OCTAVE, MUTATE |
| MODEL | MODE: TB-3PO / AICD | (same) |

- TB-3PO's settings and AICD's are separate fields: switching MODE keeps both.
- Pitch: the root note is `12 * (base_oct + 1) + root` (MIDI). Defaults: A, OCTAVE 1 = MIDI 33 (A1 with C4 = 60).
  TB-3PO spreads notes over OCTS octaves up from it. AICD puts the model's C on the root, its notes from about an
  octave below to two above. Aicd's own untransposed C is MIDI 36: ROOT C, OCTAVE 2 gives Aicd's pitches.
- What AICD's DENS, ACCENT and SLIDE do: the model has a choice at only some steps (after more than half of the step
  pairs it knows, its patterns always went on the same way), so they work in two parts. A bias leans the model's draws
  (3/2 in 1/16 log2 per point off the model's own share, so the model's lines keep their character); then a new line is
  fitted: when its count is off by more than 1 + n / 32 steps (n: 16 for steps, the notes for ACCENT and SLIDE) random-looking
  steps (picked from a hash of the line, not a draw, so TEMP's few lines at 10 % stay few) are turned on or off, only
  into steps the model has; 0 and 100 % are exact. A MUTATE leans but does not fit (the rest of the line stays).
  Measured on 3000 lines of 16, asked 0 / 20 / 50 / 80 / 100 %: notes 6 / 25 / 52 / 82 / 100 %; accented 0 / 19 / 54 /
  77 / 100 %; sliding 0 / 19 / 55 / 80 / 93 % (a slide into a rest does nothing); SCALE MIN → 88 % in the scale (72 %).
  Say so in the target's manual.
- A line view (Felucca's): the line's notes as bars from its lowest to highest note, accents taller, slides drawn into
  the next note, the playing step marked; "AICD A MIN" / "A MIN" and "SEED xxxx" above.

---

## 8. Optional: regenerate the model

`python3 tools/gen_aicd.py src/acidgen_model.h` rebuilds the tables from `model/lm.js` (Python 3, standard library,
<1 s). Only needed if `lm.js` changes. Another model in Aicd's format (a JSON object of 18-character contexts → next
character → probability, steps as `N!a$s#g=\n`, patterns ended by `~\n`) works too; the script checks its own output
and fails on anything it cannot encode. Then rerun `test/run.sh` (the AICD hash and the statistics are this model's:
expect them to change, and update `ACID_SELFTEST_AICD`).

---

## 9. Credits (required)

Add to the target's credits / about screen and its documentation:

- TB-3PO: Phazerville Hemisphere Suite's TB_3PO applet (djphazer and contributors, GPL-3.0), via schwung-tb3po
  (Charles Vestal) and X0X (charlesvestal, GPL-3.0); integer port from Felucca (Hügelton Instruments / Felucca
  contributors, GPL-3.0-only).
- AICD model: Aicd by Michael Ganss, MIT — https://github.com/mganss/Aicd

---

## 10. Done when

- [ ] Licence confirmed compatible (step 0).
- [ ] The target builds; tables in flash (step 2).
- [ ] `test/run.sh` passes on the host; `acid_selftest()` returns 1 on the target (step 3).
- [ ] NEW LINE writes a line that reads back identically; MUTATE changes only some steps and keeps their other
      parameters; hand edits survive a MUTATE (steps 4, 6).
- [ ] Auto MUTATE fires every N passes before step 1 sounds; none with MUTATE OFF (step 6).
- [ ] Settings survive a save / load and a power cycle; old saves load with the defaults (step 5).
- [ ] The 303 voice plays accents and slides as described (step 4); listen to a few lines in each mode.
- [ ] No audio glitch when NEW LINE / MUTATE / auto MUTATE run while playing (step 6).
- [ ] Credits added (step 9).
