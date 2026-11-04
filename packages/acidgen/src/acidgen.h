/* SPDX-License-Identifier: GPL-3.0-only
 * acidgen: Felucca's 303 acid line generator (TB-3PO + AICD) as a portable library. Free software under the GNU
 * General Public License, version 3 only (LICENSES/GPL-3.0.txt). The AICD model data is Aicd's (Michael Ganss, MIT:
 * LICENSES/MIT-Aicd.txt).
 *
 * Two generators behind one API, picked by acid_cfg_t.mode:
 *   ACID_MODE_TB3PO  X0X's TB-3PO (github.com/charlesvestal/fm1-x0x, after the Phazerville Hemisphere Suite's TB_3PO
 *                    applet): rule based, DENS / ACCENT / SLIDE probabilities, a scale, an octave range. An integer
 *                    port that makes exactly X0X's float lines for the same seed and settings.
 *   ACID_MODE_AICD   Aicd's model (github.com/mganss/Aicd): a maximum-likelihood n-gram trained on ABL3 acid patterns;
 *                    each step drawn from what followed the two steps before it there. Biases (DENS ACCENT SLIDE, a
 *                    scale) and a temperature lean its choices.
 *
 * No OS, no libc, no float, no heap; only <stdint.h>. Everything follows from the settings and the seed: the same
 * input, the same line, on any compiler (test/acidgen_test.c checks the lines against X0X's reference and a hash).
 * Reentrant: no global state (only const tables), each call works on the caller's buffers, so tracks may be generated
 * concurrently. The caller must still keep its sequencer from playing a line while it is written (INTEGRATION.md). */
#ifndef ACIDGEN_H
#define ACIDGEN_H
#include <stdint.h>

#ifndef ACIDGEN_AICD
#define ACIDGEN_AICD 1           /* 0: TB-3PO only (no model tables: ~24 KB less flash) */
#endif

#define ACID_NSTEP 64u           /* a line's buffer: always ACID_NSTEP steps (past len: REST) */
#define ACID_NSCALES 6u          /* MIN PHRY HARM MPEN DOR MAJ */
#define ACID_PACKED 26u          /* acid_pack's bytes */

/* a step's flags: the gate (one of three) | ACC | SLD */
#define ACID_REST 0u             /* no note; note keeps a pitch (a later MUTATE may turn it into a note again) */
#define ACID_NOTE 1u             /* a note */
#define ACID_TIE 2u              /* the note before held on (never generated; a hand edit read back: a MUTATE may keep
                                  * or replace it; TIE | SLD before a REST becomes a NOTE, as in X0X) */
#define ACID_GATE 3u             /* (the gate's mask) */
#define ACID_ACC 4u              /* accent (only with ACID_NOTE) */
#define ACID_SLD 8u              /* slide into the next step (303: the gate held, the pitch glides; only with ACID_NOTE,
                                  * never into a REST) */
typedef struct { uint8_t note, flags; } acid_step_t;   /* note: MIDI 0..127 */

#define ACID_MODE_TB3PO 0u
#define ACID_MODE_AICD 1u
#define ACID_AICD_DENS 90u       /* AICD's DENS ACCENT SLIDE defaults: the model's own shares (steps that are notes, ACC, SLD of the notes) */
#define ACID_AICD_ACC 35u
#define ACID_AICD_SLD 32u
typedef struct {                 /* a line's settings: the line follows from them and seed. Field order is fixed (the
                                  * tests use positional initialisers) */
    uint8_t density, accent, slide;   /* TB-3PO: 0..100 % (X0X's 70 / 40 / 25) */
    uint8_t oct_range;           /* TB-3PO: 1..3 octaves */
    uint8_t root, scale;         /* root 0 = C .. 11 = B (both modes); scale TB-3PO's: 0..ACID_NSCALES - 1 */
    uint8_t base_oct;            /* 0..4: the root note is 12 (base_oct + 1) + root (both modes; X0X's A1 = 33) */
    uint8_t mutate_bars;         /* 1..16: auto MUTATE every that many passes, 0 off (the caller's sequencer does it) */
    uint32_t seed;
    uint8_t mode;                /* ACID_MODE_TB3PO, ACID_MODE_AICD */
    uint8_t temp;                /* AICD: 10..200 %, 100 = the model's own odds */
    uint8_t a_dens, a_acc, a_sld; /* AICD: DENS ACCENT SLIDE 0..100 % (ACID_AICD_DENS.. = the model's own shares) */
    uint8_t a_scale;             /* AICD: 0 = CHRO (no bias), 1..ACID_NSCALES = acid_scale_names[a_scale - 1] */
} acid_cfg_t;

extern const char *const acid_scale_names[ACID_NSCALES];        /* "MIN" "PHRY" "HARM" "MPEN" "DOR" "MAJ" */
extern const char *const acid_aicd_scale_names[ACID_NSCALES + 1]; /* "CHRO", then the same */

/* X0X's defaults (70 / 40 / 25 %, 2 octaves, A minor from A1, MUTATE off, seed: seed or 0xBEEF), MODE TB-3PO,
 * AICD's defaults (DENS 90 / ACCENT 35 / SLIDE 32 %, TEMP 100, CHRO) */
void acid_defaults(acid_cfg_t *g, uint32_t seed);
/* a new seed from any entropy (a timer, a frame count): X0X's (never 0) */
uint32_t acid_new_seed(uint32_t entropy);

/* a whole new line of len (1..ACID_NSTEP) steps in g's mode from g->seed: all ACID_NSTEP of line written (past len
 * REST). Never all REST (step 0 a note then). */
void acid_generate(const acid_cfg_t *g, acid_step_t *line, uint32_t len);
/* MUTATE line in place (hand edits in it are read as they are): TB-3PO re-rolls ~25 % of the steps, AICD draws a run
 * of (len + 3) / 4 steps (16 at most) on from the two steps before it (and may drop the SLD of the step before the run
 * when that becomes a REST). *rng: the random stream, carried from call to call; 0 = start it from g->seed. */
void acid_mutate(const acid_cfg_t *g, acid_step_t *line, uint32_t len, uint32_t *rng);

/* each mode's own (acid_generate / acid_mutate pick by g->mode) */
void acid_tb3po_generate(const acid_cfg_t *g, acid_step_t *line, uint32_t len);
void acid_tb3po_mutate(const acid_cfg_t *g, acid_step_t *line, uint32_t len, uint32_t *rng);
#if ACIDGEN_AICD
void acid_aicd_generate(const acid_cfg_t *g, acid_step_t *line, uint32_t len);
void acid_aicd_mutate(const acid_cfg_t *g, acid_step_t *line, uint32_t len, uint32_t *rng);
#endif

/* the settings as ACID_PACKED bytes, each 0..127 (MIDI SysEx safe): "TB3P", 1, the eight TB-3PO settings, the seed
 * in five 7-bit bytes, 1, MODE, TEMP (two bytes), AICD's DENS ACCENT SLIDE (0..100), its SCALE. Felucca's own layout (a
 * 303 track's block in its projects), so the two exchange settings */
void acid_pack(const acid_cfg_t *g, uint8_t *out);
/* back (each value clamped into its range): 1; not "TB3P" version 1: the defaults, 0. A block without the AICD part
 * (byte 18 not 1): MODE TB-3PO, AICD's defaults */
int acid_unpack(acid_cfg_t *g, const uint8_t *in);

/* src/acidgen_selftest.c (optional): 1 when this build makes Felucca's lines (hashes of 64 settings x a line and a
 * MUTATE per mode: 0xCB4CC370 TB-3PO, 0x99FE18AC AICD). Run it once on the target: ~5 M instructions (host), ~1.2 KB
 * of stack */
int acid_selftest(void);
uint32_t acid_selftest_tb3po(void);
#if ACIDGEN_AICD
uint32_t acid_selftest_aicd(void);
#endif

#endif
