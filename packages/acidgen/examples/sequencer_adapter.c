/* SPDX-License-Identifier: GPL-3.0-only
 * acidgen example: the glue between the generator and a step sequencer, as Felucca has it (its tb3po.c tb3_read /
 * tb3_put / tb3_write / tb3_pass, ui_tb3po.c tb3_oct). A made-up sequencer (my_step_t, my_track_t) stands in for the
 * target's: map each function onto its own types. Build and run (test/run.sh does):
 *   cc -DACIDGEN_EXAMPLE_MAIN -Isrc examples/sequencer_adapter.c src/acidgen.c -o example && ./example
 */
#include "acidgen.h"

/* ------------------------------------------------ the target's model --- */
typedef struct {
    uint8_t note;                /* MIDI note (kept on a rest: see ACID_REST) */
    uint8_t gate;                /* 0 rest, 1 note, 2 tie (the note before held on) */
    uint8_t accent, slide;       /* 303 accent; slide into the next step */
    uint8_t vel;                 /* 1..127 */
    uint8_t ratchet;             /* (anything else a step has: a MUTATE keeps it) */
} my_step_t;
typedef struct {
    my_step_t step[ACID_NSTEP];
    uint32_t len;                /* 1..ACID_NSTEP */
    acid_cfg_t gen;              /* the generator's settings, saved with the track (acid_pack) */
    uint32_t mut_rng;            /* auto MUTATE's random stream (0: from the seed); not saved */
    uint32_t passes;             /* passes of the line since PLAY */
    uint8_t stale;               /* settings changed since the line was made (show a "*" by NEW LINE) */
    volatile uint32_t auto_n;    /* auto MUTATEs so far (the sequencer's ISR counts them: a MUTATE retries on one) */
} my_track_t;

/* stand-ins for the target's services */
static uint32_t my_millis;       /* a ms clock (entropy for seeds) */
static void irq_off(void) {}     /* keep the sequencer (audio / timer ISR) from reading a half-written line */
static void irq_on(void) {}

/* ---------------------------------------------------- the step glue --- */
/* the track's steps as a line (what a MUTATE starts from: hand edits included; a TIE stays a TIE) */
static void line_read(const my_track_t *t, acid_step_t *line)
{
    uint32_t i;
    for (i = 0; i < ACID_NSTEP; i++) {
        const my_step_t *s = &t->step[i];
        uint8_t f = s->gate == 2u ? ACID_TIE : s->gate == 1u ? ACID_NOTE : ACID_REST;
        if (f != ACID_REST)
            f |= (uint8_t)((s->accent ? ACID_ACC : 0u) | (s->slide ? ACID_SLD : 0u));
        line[i].note = s->note & 127u;
        line[i].flags = f;
    }
}
/* line step b into step s: the whole step (fresh: a new line), else only its note, gate, accent, slide (the rest of
 * the step, its velocity, ratchet..., kept) */
static void step_put(my_step_t *s, const acid_step_t *b, int fresh)
{
    uint32_t g = b->flags & ACID_GATE;
    if (fresh) {
        s->vel = 0;
        s->ratchet = 0;
    }
    s->note = b->note;                               /* (a REST keeps its note: a later MUTATE may play it) */
    s->gate = (uint8_t)(g == ACID_TIE ? 2u : g == ACID_NOTE ? 1u : 0u);
    s->accent = g != ACID_REST && (b->flags & ACID_ACC);
    s->slide = g != ACID_REST && (b->flags & ACID_SLD);
    if (g != ACID_REST && !s->vel)
        s->vel = 96;                                 /* (Felucca's: an accent is the flag, not the velocity) */
}
/* the line into the steps: every step (old 0), else only those that differ from old (line_read of them before) */
static void line_write(my_track_t *t, const acid_step_t *line, const acid_step_t *old)
{
    uint32_t i;
    for (i = 0; i < ACID_NSTEP; i++)
        if (!old || line[i].note != old[i].note || line[i].flags != old[i].flags)
            step_put(&t->step[i], &line[i], !old);
}

/* ---------------------------------------------------------- actions --- */
/* NEW LINE (Felucca: OCT+): a new seed, a whole line. Worked out with interrupts on (AICD: ~100 k instructions), the
 * steps written with them off */
static void act_new_line(my_track_t *t)
{
    acid_step_t line[ACID_NSTEP];
    t->gen.seed = acid_new_seed(my_millis);
    acid_generate(&t->gen, line, t->len);
    irq_off();
    line_write(t, line, 0);
    irq_on();
    t->stale = 0;
}
/* MUTATE (Felucca: OCT-): a fresh stream from the seed and the time. If the sequencer's auto MUTATE changed the steps
 * between the read and the write, again from the steps as they are now (Felucca's tb3_oct) */
static void act_mutate(my_track_t *t)
{
    acid_step_t line[ACID_NSTEP], old[ACID_NSTEP];
    uint32_t r0 = t->gen.seed ^ my_millis, r, i, n;
    for (;;) {
        irq_off();                                   /* (a consistent copy: the ISR's auto MUTATE writes steps) */
        line_read(t, old);
        n = t->auto_n;
        irq_on();
        for (i = 0; i < ACID_NSTEP; i++)
            line[i] = old[i];
        r = r0;
        acid_mutate(&t->gen, line, t->len, &r);
        irq_off();
        if (n == t->auto_n)
            break;                                   /* (with the ISR still off: write) */
        irq_on();
    }
    line_write(t, line, old);
    irq_on();
    t->stale = 0;
}
/* a knob turned: the setting is the next NEW LINE's / MUTATE's; the line plays on as it is */
static void act_set_density(my_track_t *t, uint8_t v)
{
    t->gen.density = v;
    t->stale = 1;
}
/* the sequencer, as step 1 of a pass is about to play (in its ISR; top: PLAY's first pass): auto MUTATE every
 * mutate_bars passes, before step 1 sounds. AICD's MUTATE is ~25 k instructions at 64 steps (host) */
static void seq_pass_start(my_track_t *t, int top)
{
    acid_step_t line[ACID_NSTEP], old[ACID_NSTEP];
    uint32_t i;
    if (top) {
        t->passes = 0;
        return;
    }
    t->passes++;
    if (!t->gen.mutate_bars || t->passes % t->gen.mutate_bars)
        return;
    line_read(t, old);
    for (i = 0; i < ACID_NSTEP; i++)
        line[i] = old[i];
    acid_mutate(&t->gen, line, t->len, &t->mut_rng);
    line_write(t, line, old);
    t->auto_n++;
}
/* settings in storage (26 bytes, each 0..127): Felucca's own block, so the two exchange settings */
static void track_save(const my_track_t *t, uint8_t *blob) { acid_pack(&t->gen, blob); }
static void track_load(my_track_t *t, const uint8_t *blob)
{
    (void)acid_unpack(&t->gen, blob);                /* (not a block: the defaults) */
    t->mut_rng = t->passes = t->stale = 0;
}

#ifdef ACIDGEN_EXAMPLE_MAIN
#include <stdio.h>
static void show(const my_track_t *t, const char *what)
{
    static const char *const NN[12] = {"C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-"};
    uint32_t i;
    printf("%-26s", what);
    for (i = 0; i < t->len; i++) {
        const my_step_t *s = &t->step[i];
        if (s->gate == 1u)
            printf(" %s%d%c%c", NN[s->note % 12u], s->note / 12 - 1, s->accent ? '!' : '.', s->slide ? '~' : '.');
        else
            printf(" %s", s->gate == 2u ? "----" : " .. ");
    }
    printf("\n");
}
int main(void)
{
    static my_track_t t;
    uint8_t blob[ACID_PACKED];
    uint32_t p;
    acid_defaults(&t.gen, 0);
    t.len = 16;
    my_millis = 1234;
    act_new_line(&t);
    show(&t, "TB-3PO new line");
    my_millis = 777;
    act_mutate(&t);
    show(&t, "TB-3PO MUTATE");
    act_set_density(&t, 100);
#if ACIDGEN_AICD
    t.gen.mode = ACID_MODE_AICD;
    t.gen.root = 0;                                  /* C, from C2 = MIDI 36: Aicd's own pitch */
    t.gen.base_oct = 2;
    t.gen.a_scale = 1;                               /* lean on C minor */
    my_millis = 99;
    act_new_line(&t);
    show(&t, "AICD new line (C, MIN)");
    t.gen.mutate_bars = 2;
    seq_pass_start(&t, 1);
    for (p = 0; p < 4u; p++)
        seq_pass_start(&t, 0);                       /* passes 1..4: MUTATEs before passes 2 and 4 */
    show(&t, "AICD after 4 passes (2)");
#endif
    track_save(&t, blob);
    track_load(&t, blob);
    printf("settings back: mode %u seed %08x (key: ! accent, ~ slide into the next, .. rest)\n", t.gen.mode, t.gen.seed);
    (void)p;
    return 0;
}
#endif
