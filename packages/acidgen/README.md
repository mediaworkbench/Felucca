# acidgen — the 303 acid line generator from Felucca

A portable C library that writes TB-303 style acid lines (notes, rests, accents, slides) into a step sequencer.
It is the generator of the [Felucca](https://github.com/hugelton/Felucca) firmware's 303 engine, taken out of its
firmware: the same code, the same lines for the same seed and settings.

Two generators behind one API (`acid_cfg_t.mode`):

| Mode | What it is | Controls |
|---|---|---|
| **TB-3PO** | [X0X](https://github.com/charlesvestal/fm1-x0x)'s TB-3PO (after the Phazerville Hemisphere Suite's TB_3PO applet): rule based | DENS, ACCENT, SLIDE (0..100 %), OCTS (1..3), ROOT, SCALE (6), OCTAVE |
| **AICD** | [Aicd](https://github.com/mganss/Aicd)'s model (Michael Ganss): an n-gram trained on ABL3 acid patterns; each step follows the two before it as it did there | DENS, ACCENT, SLIDE (0..100 %, as TB-3PO's; the defaults are the model's own 90 / 35 / 32), TEMP (10..200 %), SCALE (CHRO = none, or one of the 6), ROOT, OCTAVE |

Both: a whole new line (1..64 steps) from a seed, or a MUTATE of the line as it is (hand edits included).

- C99, `<stdint.h>` only: no libc, no float, no heap, no global state.
- TB-3PO alone is ~4 KB of code. AICD adds ~24 KB of const tables (flash) and ~2.5 KB of code (`ACIDGEN_AICD=0` leaves it out).
- Stack ~1 KB at most. CPU (host x86-64, instructions): a 64-step line ~5 k (TB-3PO) / ~155 k (AICD), a MUTATE ~2 k / ~27 k.

## Files

| Path | What |
|---|---|
| `src/acidgen.h` | the API (read this first) |
| `src/acidgen.c` | the generator (both modes) |
| `src/acidgen_model.h` | AICD's model as tables (generated: `tools/gen_aicd.py`) |
| `src/acidgen_selftest.c` | optional: `acid_selftest()` to run once on the target |
| `examples/sequencer_adapter.c` | the glue to a step sequencer (new line, MUTATE, auto MUTATE, save / load), runnable |
| `INTEGRATION.md` | how to put it into another firmware, step by step (written for an AI coding agent) |
| `model/lm.js`, `model/README.md` | Aicd's model as it is, and where it is from |
| `tools/gen_aicd.py` | `model/lm.js` → `src/acidgen_model.h` |
| `test/run.sh`, `test/acidgen_test.c`, `test/ref/` | the host checks (X0X's float generator as the reference) |
| `LICENSES/` | GPL-3.0 (the code), MIT (Aicd's model) |

## Quick start

```c
#include "acidgen.h"

acid_cfg_t cfg;
acid_step_t line[ACID_NSTEP];               /* always 64 */
acid_defaults(&cfg, 0);                     /* TB-3PO, A minor from A1 */
cfg.seed = acid_new_seed(millis());
acid_generate(&cfg, line, 16);              /* 16 steps: line[0..15], REST after */
/* line[i].note: MIDI note; line[i].flags & ACID_GATE: ACID_NOTE / ACID_REST; ACID_ACC, ACID_SLD */

uint32_t rng = 0;
acid_mutate(&cfg, line, 16, &rng);          /* change some of it */

cfg.mode = ACID_MODE_AICD;                  /* Aicd's model instead */
acid_generate(&cfg, line, 16);
```

Check: `sh test/run.sh` must end with `ACIDGEN: ALL PASSED`.

## Licence

The code is **GPL-3.0-only** (it is derived from X0X's and Phazerville's GPL-3.0 TB-3PO and from Felucca, GPL-3.0-only):
a firmware that includes it must be distributable under the GPL-3.0. The AICD model data is Aicd's, **MIT**
(Copyright (c) 2018 Michael Ganss). Credit both (INTEGRATION.md, step 9).

From Felucca at commit 70f3ef8 plus its AICD mode (2026-11-02, not yet committed there); `acid_selftest()` and
Felucca's tests check the same line hashes (TB-3PO `0xCB4CC370`, AICD `0x99FE18AC`).
