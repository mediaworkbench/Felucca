/* SPDX-License-Identifier: GPL-3.0-only
 * Part of Felucca: free software under the GNU General Public License, version 3 only (see LICENSE) */
/* TB-3PO (firmware/src/tb3po.c, ui_tb3po.c), on the real UI sources (the stubs of tests/ui_test.c), against X0X's float
 * generator (tests/ref/: X0X's tb3po.c as it is, its own unit, x0x_tb3po_ref.c):
 * 1. the integer port's pieces: the thresholds (d / 100.0f, 0.35f, 0.25f, 0.5f x 2^30) and tb3_pick against the float
 *    product, for every 24-bit draw and every length 1..7.
 * 2. the same line as X0X's: X0X's defaults; new seeds; generate over random settings (the corners too) and lengths;
 *    chains of MUTATEs (the random stream carried on as X0X carries it).
 * 3. the line in the steps: written and read back; a MUTATE writes only the steps it changes, keeping the rest of each
 *    (VEL, CHANCE, RATCH, NUDGE, chords); a project's FM6 area round trip (pack / unpack, a bad block: the defaults).
 * 4. the UI: ARP on a 303 track opens GENERATE, then SCALE (the arp's pages on another engine); OCT+ a new line (its
 *    seed shown), OCT- a MUTATE, neither the octave; the knobs (NEW LINE's star, not MUTATE's); SAVE held undoes; in a
 *    song refused. The arp does nothing on a 303 track (its mode kept), back on another engine.
 * 5. the sequencer: auto MUTATE every N passes, before step 1 plays, as X0X's (the same steps); none at 0, in a song.
 *    5b (1.8.2): the live knobs: the knobs as they were the line as it is; DENS ACCENT SLIDE 0..100 none .. all, each
 *    value's steps the last's and more, the rest of the line kept; ROOT OCTAVE transpose, SCALE snaps, OCTS folds /
 *    spreads; a new line's rests have notes; while playing at the next bar, in a song after it; a hand edit the anchor.
 * 6. a project: a 303 track's settings and seed saved and loaded, its FM6 patch the init one, other tracks' kept.
 * 7. MODE AICD (1.6.2): the model in flash against tools/gen_aicd.py's exact one (every node's edges, the odds within
 *    rounding); a draw's odds those; Aicd's lines (gate, accent, slide, the scale's share as its own sampler's, 20000
 *    lines); the biases and TEMP each their way; every step a model step; a MUTATE a run of a quarter of the line at
 *    most; the cost; the UI (MODEL, the knobs, OCT+ / OCT-), auto MUTATE, a project; the same lines (a hash).
 * 8. what the audit found (B1..B9): automation kept, no note 0, no stray slide, undo's seed, DENS's order, the editor told.
 * Built and run by tests/run_tests.sh (with tests/ref/x0x_tb3po_ref.c, -ffp-contract=off; argv[1] gen_aicd.py's
 * reference, build/host/aicd_ref.txt). */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"
#ifdef __APPLE__
#include <libproc.h>
#include <sys/resource.h>
#endif

void ref_generate(const uint8_t *cfg, uint32_t seed, int len, uint8_t *notes, uint8_t *flags);
void ref_mutate(const uint8_t *cfg, uint32_t seed, int len, uint8_t *notes, uint8_t *flags, uint32_t *rng);
uint32_t ref_new_seed(uint32_t entropy);
void ref_defaults(uint8_t *cfg, uint32_t *seed);

static uint32_t lcg = 0x2545F491u;
static uint32_t rnd(uint32_t n) { lcg = lcg * 1664525u + 1013904223u; return (lcg >> 8) % n; }

static void cfg_set(tb3_cfg_t *g, const uint8_t *c, uint32_t seed)
{
    g->density = c[0]; g->accent = c[1]; g->slide = c[2]; g->oct_range = c[3];
    g->root = c[4]; g->scale = c[5]; g->base_oct = c[6]; g->mutate_bars = c[7];
    g->seed = seed;
}
static void cfg_rand(uint8_t *c)                  /* random settings, the corners often */
{
    static const uint8_t EDGE[] = {0, 1, 99, 100};
    uint32_t k;
    for (k = 0; k < 3u; k++)
        c[k] = (uint8_t)(rnd(4) ? rnd(101) : EDGE[rnd(4)]);
    c[3] = (uint8_t)(1u + rnd(3));
    c[4] = (uint8_t)rnd(12);
    c[5] = (uint8_t)rnd(6u);                       /* (X0X's six scales) */
    c[6] = (uint8_t)rnd(5);
    c[7] = (uint8_t)rnd(17);
}
static uint32_t seed_rand(void) { return rnd(8) ? (lcg ^ (lcg << 13)) | 1u : 1u + rnd(4); }
static int cfg_eq(const tb3_cfg_t *a, const tb3_cfg_t *b)   /* field by field (not memcmp: 1.6.2's fields pad it) */
{
    return a->density == b->density && a->accent == b->accent && a->slide == b->slide && a->oct_range == b->oct_range &&
           a->root == b->root && a->scale == b->scale && a->base_oct == b->base_oct && a->mutate_bars == b->mutate_bars &&
           a->seed == b->seed && a->mode == b->mode && a->temp == b->temp && a->a_dens == b->a_dens &&
           a->a_acc == b->a_acc && a->a_sld == b->a_sld && a->a_scale == b->a_scale;
}
static void aicd_rand(tb3_cfg_t *g)               /* random AICD settings (MODE either) */
{
    g->mode = (uint8_t)rnd(2);
    g->temp = (uint8_t)(10u + rnd(191));
    g->a_dens = (uint8_t)rnd(101);
    g->a_acc = (uint8_t)rnd(101);
    g->a_sld = (uint8_t)rnd(101);
    g->a_scale = (uint8_t)rnd(TB3_NSCALES + 1u);
}

/* ---------------------------------------------------- 1. the pieces --- */
static int pieces(void)
{
    int bad = 0, ok = 1;
    uint32_t d, u, len;
    for (d = 0; d <= 100u; d++)
        ok &= (uint32_t)((float)d / 100.0f * 1073741824.0f) == TB3_THR[d];
    ok &= (uint32_t)(0.35f * 1073741824.0f) == TB3_THR_35 && (uint32_t)(0.25f * 1073741824.0f) == TB3_THR_25 &&
          (uint32_t)(0.5f * 1073741824.0f) == TB3_THR_50;
    bad += check("tb3po: the thresholds: (float)d / 100.0f x 2^30 (d 0..100), 0.35f 0.25f 0.5f", ok);
    ok = 1;
    for (len = 1; len <= 7u; len++)
        for (u = 0; u < (1u << 24); u++) {
            float f = (float)u / (float)0x1000000;   /* X0X rng_f, then roll_degree / roll_oct */
            int r = (int)(f * (float)len);
            r = r >= (int)len ? (int)len - 1 : r;
            if ((uint32_t)r != tb3_pick(u, len)) {
                if (ok)
                    printf("  pick u %u len %u: %d, tb3_pick %u\n", u, len, r, tb3_pick(u, len));
                ok = 0;
            }
        }
    bad += check("tb3po: tb3_pick == (int)(u / 2^24 * len) as the float rounds, every u, len 1..7", ok);
    return bad;
}

/* ----------------------------------------------- 2. X0X's lines --- */
static int same_lines(void)
{
    int bad = 0, ok = 1;
    uint32_t i, k, n, seed, r1, r2, diffs = 0;
    uint8_t c[8], rn[64], rf[64];
    tb3_cfg_t g;
    tb3_step_t b[NSTEP];
    ref_defaults(c, &seed);
    tb3_defaults(&g, 0);
    ok = g.density == c[0] && g.accent == c[1] && g.slide == c[2] && g.oct_range == c[3] && g.root == c[4] &&
         g.scale == c[5] && g.base_oct == c[6] && g.mutate_bars == c[7] && g.seed == seed;
    bad += check("tb3po: X0X's defaults (70 / 40 / 25 %, 2 octaves, A minor from A1, seed BEEF)", ok);
    for (i = 0, ok = 1; i < 200000u; i++) {
        uint32_t e = lcg ^ (i * 2654435761u);
        rnd(2);
        ok &= tb3_new_seed(e) == ref_new_seed(e);
    }
    bad += check("tb3po: new seeds == X0X's (200000 entropies)", ok);
    for (i = 0, ok = 1; i < 40000u; i++) {          /* generate */
        int len = (int)(rnd(3) ? 1u + rnd(64) : (uint32_t[]){1, 16, 32, 64}[rnd(4)]);
        cfg_rand(c);
        seed = seed_rand();
        ref_generate(c, seed, len, rn, rf);
        cfg_set(&g, c, seed);
        tb3_generate(&g, b, (uint32_t)len);
        for (k = 0; k < NSTEP; k++)
            if (b[k].note != rn[k] || b[k].flags != rf[k]) {
                if (!diffs++)
                    printf("  generate: seed %08x len %d step %u: %u/%u, X0X %u/%u\n", seed, len, k, b[k].note,
                           b[k].flags, rn[k], rf[k]);
                ok = 0;
                break;
            }
    }
    bad += check("tb3po: generate == X0X's tb3po_generate, every step (40000 settings, seeds, lengths)", ok);
    for (i = 0, ok = 1; i < 3000u; i++) {           /* MUTATE chains */
        int len = (int)(1u + rnd(64));
        cfg_rand(c);
        seed = seed_rand();
        cfg_set(&g, c, seed);
        ref_generate(c, seed, len, rn, rf);
        tb3_generate(&g, b, (uint32_t)len);
        r1 = r2 = rnd(4) ? 0u : seed_rand();
        for (n = 0; n < 24u && ok; n++) {
            if (n == 12u) {                          /* (settings changed halfway: the knobs before an OCT-) */
                cfg_rand(c);
                cfg_set(&g, c, seed);
            }
            ref_mutate(c, seed, len, rn, rf, &r1);
            tb3_mutate(&g, b, (uint32_t)len, &r2);
            ok &= r1 == r2;
            for (k = 0; k < NSTEP; k++)
                if (b[k].note != rn[k] || b[k].flags != rf[k]) {
                    if (!diffs++)
                        printf("  mutate %u: seed %08x len %d step %u\n", n, seed, len, k);
                    ok = 0;
                    break;
                }
        }
    }
    bad += check("tb3po: MUTATE chains == X0X's tb3po_mutate (3000 lines x 24, the stream carried on)", ok);
    return bad;
}

/* ------------------------------------------------- 3. the steps --- */
static int steps(void)
{
    int bad = 0, ok = 1, ok2 = 1, ok3 = 1;
    uint32_t i, k, r;
    uint8_t c[8], pk[FM6_PACKED];
    tb3_cfg_t g;
    tb3_step_t b[NSTEP], back[NSTEP];
    step_t s[NSTEP], was[NSTEP];
    for (i = 0; i < 500u; i++) {
        uint32_t len = 1u + rnd(64);
        cfg_rand(c);
        cfg_set(&g, c, seed_rand());
        tb3_generate(&g, b, len);
        memset(s, 0x5A, sizeof s);
        tb3_write(s, b, 0);
        tb3_read(s, back);
        for (k = 0; k < NSTEP; k++) {
            ok &= back[k].note == b[k].note && back[k].flags == b[k].flags;
            ok &= s[k].probability == 0 && !s[k].hit && !(s[k].flags & ~(SF_ACCENT | SF_SLIDE)) &&
                  (s[k].time == ST_REST ? !s[k].n && !s[k].vel : s[k].n == 1u && s[k].vel == 96u);
        }
        for (k = 0; k < NSTEP; k++) {               /* the rest of the steps: VEL, CHANCE, RATCH, NUDGE, a chord */
            if (s[k].time == ST_REST)
                continue;
            s[k].vel = (uint8_t)(1u + rnd(127));
            s[k].probability = (uint8_t)rnd(101);
            step_set_ratchet(&s[k], 1u + rnd(4));
            step_set_nudge(&s[k], (int32_t)rnd(16) - 8);
            if (!rnd(4)) {
                s[k].n = 3;
                s[k].note[1] = 64;
                s[k].note[2] = 67;
            }
        }
        memcpy(was, s, sizeof s);
        tb3_read(s, back);
        memcpy(b, back, sizeof b);
        r = 0;
        tb3_mutate(&g, b, len, &r);
        tb3_write(s, b, back);
        for (k = 0; k < NSTEP; k++) {
            if (b[k].note == back[k].note && b[k].flags == back[k].flags)
                ok2 &= !memcmp(&s[k], &was[k], sizeof s[k]);   /* untouched */
            else if ((b[k].flags & TB3_GATE) != TB3_REST && was[k].time != ST_REST)
                ok2 &= s[k].vel == was[k].vel && s[k].probability == was[k].probability &&
                       step_ratchet(&s[k]) == step_ratchet(&was[k]) && step_nudge(&s[k]) == step_nudge(&was[k]);
        }
        tb3_read(s, back);
        for (k = 0; k < NSTEP; k++)
            ok2 &= back[k].note == b[k].note && back[k].flags == b[k].flags;
    }
    bad += check("tb3po: a line into the steps and back (NOTE 1 note VEL 96, REST keeps its note, n 0)", ok);
    bad += check("tb3po: a MUTATE writes only the steps it changes, keeping their VEL CHANCE RATCH NUDGE", ok2);
    for (i = 0; i < 2000u; i++) {                   /* the project's FM6 area */
        cfg_rand(c);
        cfg_set(&tb3_cfg[1], c, i ? lcg | 1u : 0xFFFFFFFFu);
        aicd_rand(&tb3_cfg[1]);
        if (!i)
            tb3_cfg[1].temp = 200, tb3_cfg[1].a_dens = 0, tb3_cfg[1].a_acc = 100, tb3_cfg[1].a_scale = TB3_NSCALES;
        tb3_cfg[1].mode &= AICD_ON;
        g = tb3_cfg[1];
        tb3_pack(1, pk);
        for (k = 0; k < FM6_PACKED; k++)
            ok3 &= pk[k] <= 127u;
        memset(&tb3_cfg[1], 0, sizeof tb3_cfg[1]);
        ok3 &= tb3_unpack(1, pk) && cfg_eq(&tb3_cfg[1], &g);
        rnd(2);
    }
    pk[2] = 'X';
    ok3 &= !tb3_unpack(1, pk) && tb3_cfg[1].density == 70u && tb3_cfg[1].seed == 0xBEEFu;
    ok3 &= !tb3_unpack(1, FM6_INIT);
    tb3_pack(1, pk);                                /* (a block before 1.6.2: zeros after the seed) */
    memset(pk + 18, 0, 8);
    tb3_cfg[1].mode = 1, tb3_cfg[1].temp = 50, tb3_cfg[1].a_sld = 9, tb3_cfg[1].a_scale = 3;
    ok3 &= tb3_unpack(1, pk) && tb3_cfg[1].mode == TB3_TB3PO && tb3_cfg[1].temp == 100u && tb3_cfg[1].a_dens == AICD_DENS &&
           tb3_cfg[1].a_acc == AICD_ACC && tb3_cfg[1].a_sld == AICD_SLD && !tb3_cfg[1].a_scale;
    bad += check("tb3po: a 303 track's FM6 area: every byte <= 127, back exactly (the 32-bit seed, MODE, AICD's); "
                 "else the defaults; a block before 1.6.2: TB-3PO, AICD's defaults", ok3);
    return bad;
}

/* --------------------------------------------------------- 4. the UI --- */
static int line_is(const track_t *t, const tb3_cfg_t *g)   /* the steps are g's fresh line (its MODE's) */
{
    tb3_step_t b[NSTEP], s[NSTEP];
    tb3_gen(g, b, (uint32_t)t->p[P_SLEN]);
    tb3_read(t->step, s);
    return !memcmp(b, s, sizeof b);
}
static int ui_tb3(void)
{
    int bad = 0, ok;
    uint32_t k;
    char hx[8];
    track_t *t;
    step_t before[NSTEP];
    ui_power_on();
    t = TSEL;
    set_engine(ENGI_303);
    frame();
    press(B_ARP);
    ok = !ui.home && cur_page()->graph == GR_TB3GEN && str_eq(cur_page()->title, "GENERATE");
    press(B_ARP);
    ok &= cur_page()->graph == GR_TB3SCL;
    press(B_ARP);
    ok &= cur_page()->graph == GR_TB3MOD && str_eq(cur_page()->title, "MODEL");
    press(B_ARP);                                   /* (FORM, PLAY, LINK) */
    press(B_ARP);
    press(B_ARP);
    press(B_ARP);
    ok &= cur_page()->graph == GR_TB3GEN;
    set_engine(0);
    frame();
    ok &= str_eq(cur_page()->title, "ARP");          /* (GENERATE hidden now: the family's first shown) */
    press(B_ARP);
    ok &= str_eq(cur_page()->title, "ARP 2");
    set_engine(ENGI_303);
    frame();
    ok &= cur_page()->graph == GR_TB3GEN;
    bad += check("tb3po ui: ARP on a 303 track: GENERATE, SCALE, MODEL, GENERATE; another engine: ARP, ARP 2 (and back)",
                 ok);

    ui_power_on();
    t = TSEL;
    set_engine(ENGI_303);
    t->p[P_SLEN] = 16;
    press(B_ARP);
    memcpy(before, t->step, sizeof before);         /* (the steps before: an undo goes back to them) */
    press(B_OCTUP);
    snprintf(hx, sizeof hx, "%u", (unsigned)tb3_cfg[0].line);
    ok = tb3_cfg[0].line >= 1u && tb3_cfg[0].line <= 9999u && tb3_cfg[0].seed == tb3_new_seed(tb3_cfg[0].line) && line_is(t, &tb3_cfg[0]) && !seq_is_empty(t) && song.octave == 0 && tb3_cfg[0].seed != 0xBEEFu &&
         !strncmp(ui.msg, "NEW LINE ", 9) && str_eq(ui.msg + 9, hx);
    bad += check("tb3po ui: OCT+ a new line (a new number, \"NEW LINE 4711\"), not the octave", ok);
    {
        tb3_step_t l0[NSTEP], cur[NSTEP], want[NSTEP];
        tb3_knobs_t k0, k1;
        tb3_read(t->step, l0);
        k0 = tb3_knobs(&tb3_cfg[0]);
        turn(EN_K1, 5);                              /* (stopped: at once) */
        k1 = tb3_knobs(&tb3_cfg[0]);
        tb3_live_line(l0, &k0, &k1, tb3_cfg[0].seed, 16, want);
        tb3_read(t->step, cur);
        ok = tb3_cfg[0].density == 75u && !tb3_rt[0].stale && !tb3_rt[0].pending && !memcmp(cur, want, sizeof cur) &&
             memcmp(cur, l0, sizeof cur);
        turn(EN_K1, -5);
        tb3_read(t->step, cur);
        ok &= tb3_cfg[0].density == 70u && !memcmp(cur, l0, sizeof cur);
        press(B_ARP);
        turn(EN_K4, 2);
        ok &= tb3_cfg[0].mutate_bars == 2u && cur_page()->graph == GR_TB3SCL;
        tb3_read(t->step, cur);
        ok &= !tb3_rt[0].stale && !memcmp(cur, l0, sizeof cur);
        turn(EN_K1, 3);
        tb3_read(t->step, cur);
        ok &= tb3_cfg[0].root == 11u && !tb3_rt[0].stale;   /* (A + 3: ROOT 0..11 does not wrap, B) */
        for (k = 0; k < NSTEP; k++)
            ok &= cur[k].flags == l0[k].flags && cur[k].note == l0[k].note + 2u;
    }
    bad += check("tb3po ui: 1.8.2: DENS / ROOT act on the line (stopped: at once, back when turned back; no star), "
                 "MUTATE not", ok);
    {
        step_t line[NSTEP];
        memcpy(line, t->step, sizeof line);
        press(B_OCTDN);
        ok = msg_is("MUTATED") && memcmp(line, t->step, sizeof line) && !tb3_rt[0].stale && song.octave == 0;
        hold(B_SAVE);
        ok &= !memcmp(line, t->step, sizeof line);   /* (a knob between: not a load in a row) */
    }
    bad += check("tb3po ui: OCT- a MUTATE (not the octave); SAVE held undoes it", ok);
    ui_power_on();                                  /* (the undo took the 303 too: the loads in a row) */
    t = TSEL;
    set_engine(ENGI_303);
    press(B_ARP);
    chain.running = 1;
    memcpy(before, t->step, sizeof before);
    press(B_OCTUP);
    ok = msg_is("STOP TO EDIT") && !memcmp(before, t->step, sizeof before);
    chain.running = 0;
    bad += check("tb3po ui: in a song OCT+ / OCT- are refused (the steps are a slot's)", ok);

    ui_power_on();                                  /* the arp: inert on a 303 track, its mode kept */
    t = TSEL;
    set_engine(ENGI_303);
    t->engine = ENGI_303;                           /* (the audio side's switch, at once) */
    eng_mem_claim(t);
    t->p[P_AMODE] = AM_UP;
    input_on(t, 60, 100);
    ok = !t->nheld && t->v[0].active && arp_mode(t) == 0 && t->p[P_AMODE] == AM_UP;
    input_off(t, 60);
    set_engine(0);
    ok &= arp_mode(t) == AM_UP;
    bad += check("tb3po ui: the arp does nothing on a 303 track (the keys play), its mode back on another engine", ok);
    return bad;
}

/* -------------------------------------------------- 5. auto MUTATE --- */
static int auto_mutate(void)
{
    int bad = 0, ok = 1;
    uint32_t b, wraps = 0, last, r = 0, m, n0;
    tb3_step_t want[NSTEP], cur[NSTEP];
    track_t *t;
    ui_power_on();
    t = &trk[0];
    set_engine_of(t, ENGI_303);
    t->engine = ENGI_303;
    eng_mem_claim(t);
    t->p[P_SLEN] = 4;
    tb3_cfg[0].mutate_bars = 2;
    r = 0;
    tb3_apply(t, 1, &r);
    tb3_read(t->step, want);
    song.playing = 1;
    seq_start();
    last = t->seq_idx;
    n0 = tb3_auto_n;
    r = 0;
    for (b = 0; b < 20000u && wraps < 7u; b++) {
        seq_tick(t, CTL);
        if (t->seq_idx == 0u && last != 0u) {
            if (b) {                                 /* (block 0: PLAY's step 1, passes from 0) */
                wraps++;
                if (wraps % 2u == 0u)
                    tb3_mutate(&tb3_cfg[0], want, 4, &r);   /* X0X: every 2nd pass, before step 1 plays */
                tb3_read(t->step, cur);
                ok &= !memcmp(cur, want, sizeof cur);
            }
        }
        last = t->seq_idx;
    }
    m = tb3_auto_n - n0;
    bad += check("tb3po seq: MUTATE 2: a MUTATE every 2nd pass, before its step 1, X0X's steps (6 passes: 3)",
                 ok && wraps == 7u && m == 3u);
    ui_power_on();
    t = &trk[0];
    set_engine_of(t, ENGI_303);
    t->p[P_SLEN] = 2;
    tb3_cfg[0].mutate_bars = 0;
    r = 0;
    tb3_apply(t, 1, &r);
    tb3_read(t->step, want);
    song.playing = 1;
    seq_start();
    for (b = 0; b < 3000u; b++)
        seq_tick(t, CTL);
    tb3_read(t->step, cur);
    ok = !memcmp(cur, want, sizeof cur);
    tb3_cfg[0].mutate_bars = 1;
    chain.running = 1;
    for (b = 0; b < 3000u; b++)
        seq_tick(t, CTL);
    chain.running = 0;
    tb3_read(t->step, cur);
    ok &= !memcmp(cur, want, sizeof cur);
    bad += check("tb3po seq: MUTATE OFF: the line as it is; in a song no MUTATE", ok);
    return bad;
}

/* -------------------------------------------- 5b. the live knobs (1.8.2) --- */
static uint32_t live_n(const tb3_step_t *b, uint32_t len, uint32_t f)   /* steps of b (< len) with f (0: the gate) */
{
    uint32_t i, n = 0;
    for (i = 0; i < len; i++)
        n += (uint32_t)tb3_has(&b[i], f);
    return n;
}
static int32_t live_off(const tb3_knobs_t *k, uint32_t note) { return (int32_t)note - 12 * (k->base + 1) - k->root; }
static int live_in(uint32_t scale, int32_t off)   /* off (semitones from the root) in TB3_SCALES[scale - 1] */
{
    const tb3_scale_t *sc = &TB3_SCALES[scale - 1u];
    int32_t w = ((off % 12) + 12) % 12, i;
    for (i = 0; i < sc->len; i++)
        if (sc->deg[i] == w)
            return 1;
    return 0;
}
static int live(void)
{
    int bad = 0, okid = 1, okd = 1, oka = 1, oks = 1, okp = 1, oksc = 1, oko = 1, ok;
    uint32_t it, i, v, len;
    uint8_t c[8];
    for (it = 0; it < 3000u; it++) {
        tb3_cfg_t g;
        tb3_step_t a[NSTEP], b[NSTEP], p[NSTEP];
        tb3_knobs_t k0, k;
        tb3_defaults(&g, 0);
        cfg_rand(c);
        cfg_set(&g, c, seed_rand());
        if (AICD_ON && it % 4u == 3u) {
            aicd_rand(&g);
            g.mode = TB3_AICD;
        }
        len = 1u + rnd(NSTEP);
        tb3_gen(&g, a, len);
        k0 = tb3_knobs(&g);
        tb3_live_line(a, &k0, &k0, g.seed, len, b);
        okid &= !memcmp(a, b, sizeof b);
        for (v = 0; v <= 100u; v++) {                /* DENS: the gates only, in the seed's order, none .. all */
            k = k0;
            k.dens = (uint8_t)v;
            memcpy(p, b, sizeof p);
            tb3_live_line(a, &k0, &k, g.seed, len, b);
            okd &= live_n(b, len, 0) == tb3_share(live_n(a, len, 0), len, len, k0.dens, v);
            okd &= v || v == k0.dens || !live_n(b, len, 0);
            okd &= v < 100u || v == k0.dens || live_n(b, len, 0) == len;
            for (i = 0; i < NSTEP; i++) {
                okd &= b[i].note == a[i].note && (i < len || !memcmp(&b[i], &a[i], sizeof b[i]));
                okd &= !v || !tb3_gated(&p[i]) || tb3_gated(&b[i]);   /* (more: those of less, and some) */
            }
        }
        for (v = 0; v <= 100u; v++) {                /* ACCENT: the notes' ACC only, none .. every note */
            k = k0;
            k.acc = (uint8_t)v;
            memcpy(p, b, sizeof p);
            tb3_live_line(a, &k0, &k, g.seed, len, b);
            oka &= v || v == k0.acc || !live_n(b, len, TB3_ACC);
            oka &= v < 100u || v == k0.acc || live_n(b, len, TB3_ACC) == live_n(a, len, 0);
            for (i = 0; i < NSTEP; i++) {
                oka &= b[i].note == a[i].note && (b[i].flags & ~TB3_ACC) == (a[i].flags & ~TB3_ACC);
                oka &= !v || !(p[i].flags & TB3_ACC) || (b[i].flags & TB3_ACC);
            }
        }
        for (v = 0; v <= 100u; v++) {                /* SLIDE: into a note only; none (into a note) .. all */
            uint32_t into = 0, sl = 0;
            k = k0;
            k.sld = (uint8_t)v;
            tb3_live_line(a, &k0, &k, g.seed, len, b);
            for (i = 0; i < len; i++) {
                int e = tb3_gated(&b[i]) && tb3_gated(&b[(i + 1u) % len]);
                into += (uint32_t)e;
                sl += (uint32_t)(e && (b[i].flags & TB3_SLD));
                oks &= e || (b[i].flags & TB3_SLD) == (a[i].flags & TB3_SLD);   /* (not into a note: as it was) */
                oks &= b[i].note == a[i].note && (b[i].flags & ~TB3_SLD) == (a[i].flags & ~TB3_SLD);
            }
            oks &= v == k0.sld || ((v || !sl) && (v < 100u || sl == into));
        }
        k = k0;                                      /* ROOT, OCTAVE: transposed, the rhythm kept */
        k.root = (uint8_t)rnd(12);
        k.base = (uint8_t)rnd(5);
        tb3_live_line(a, &k0, &k, g.seed, len, b);
        for (i = 0; i < NSTEP; i++) {
            int32_t n = (int32_t)a[i].note + 12 * (k.base - k0.base) + k.root - k0.root;
            okp &= b[i].flags == a[i].flags && (n < 0 || n > 127 || b[i].note == (uint32_t)n);
        }
        k = k0;                                      /* SCALE: every step on it, the common degrees kept */
        k.scale = (uint8_t)(1u + rnd(TB3_NSCALES));
        tb3_live_line(a, &k0, &k, g.seed, len, b);
        for (i = 0; i < NSTEP; i++) {
            int32_t o0 = live_off(&k0, a[i].note), o1 = live_off(&k, b[i].note);
            oksc &= b[i].flags == a[i].flags;
            if (k.scale != k0.scale && b[i].note > 0u && b[i].note < 127u)
                oksc &= live_in(k.scale, o1) && (!live_in(k.scale, o0) || b[i].note == a[i].note) &&
                        (o1 - o0 <= 3 && o0 - o1 <= 3);   /* (DARK, 1 b2 b5: its gaps are the widest) */
            if (!k.scale || k.scale == k0.scale)
                oksc &= b[i].note == a[i].note;
        }
        if (k0.octs) {                               /* OCTS: within the range now (a new line: within its own) */
            k = k0;
            k.octs = (uint8_t)(1u + rnd(3));
            tb3_live_line(a, &k0, &k, g.seed, len, b);
            for (i = 0; i < NSTEP; i++) {
                int32_t o = live_off(&k0, b[i].note);
                oko &= b[i].flags == a[i].flags && (o - ((o % 12) + 12) % 12) / 12 < (int32_t)k.octs &&
                       ((o % 12) + 12) % 12 == ((live_off(&k0, a[i].note) % 12) + 12) % 12;
                oko &= k.octs != k0.octs || b[i].note == a[i].note;
            }
        }
    }
    bad += check("tb3po live: the knobs as they were: the line as it is (3000 lines, both MODEs)", okid);
    bad += check("tb3po live: DENS 0..100: none .. every step, the anchor's share at its own, each the last's and more, "
                 "the pitches kept", okd);
    bad += check("tb3po live: ACCENT 0..100: none .. every note, each the last's and more, the rest kept", oka);
    bad += check("tb3po live: SLIDE 0..100: none .. every note into a note, those into a rest as they were", oks);
    bad += check("tb3po live: ROOT / OCTAVE transpose every step (its note kept by a rest too), the rhythm kept", okp);
    bad += check("tb3po live: SCALE: every step on it (its nearest degree), those on it already kept; CHRO none", oksc);
    bad += check("tb3po live: OCTS: every step within the range, its degree kept; the same OCTS the same line", oko);

    {                                                /* the rests' notes: of the scale, not all the root */
        tb3_cfg_t g;
        tb3_step_t a[NSTEP];
        uint32_t roots = 0, rests = 0;
        ok = 1;
        for (it = 0; it < 200u; it++) {
            tb3_defaults(&g, 0x1234u + it);
            g.density = 30;
            tb3_gen(&g, a, NSTEP);
            for (i = 0; i < NSTEP; i++)
                if (!tb3_gated(&a[i])) {
                    tb3_knobs_t kg = tb3_knobs(&g);
                    int32_t o = live_off(&kg, a[i].note);
                    rests++;
                    roots += (uint32_t)(o == 0);
                    ok &= live_in(1u + g.scale, o) && o >= 0 && o < 12 * g.oct_range;
                }
        }
        bad += check("tb3po live: a new line's rests: notes of their own (the scale, OCTS), not the root over and over",
                     ok && rests > 1000u && roots * 3u < rests);
    }

    {                                                /* the sequencer: at the next bar; in a song not */
        track_t *t;
        tb3_step_t a[NSTEP], want[NSTEP], cur[NSTEP];
        tb3_knobs_t k0, k1;
        uint32_t r = 0, b, last, hit = 0;
        ui_power_on();
        t = &trk[0];
        set_engine_of(t, ENGI_303);
        t->engine = ENGI_303;
        eng_mem_claim(t);
        t->p[P_SLEN] = 32;
        tb3_apply(t, 1, &r);
        tb3_read(t->step, a);
        k0 = tb3_knobs(&tb3_cfg[0]);
        song.playing = 1;
        seq_start();
        ok = 1;
        for (b = 0; b < 40000u && t->seq_idx != 3u; b++)
            seq_tick(t, CTL);
        tb3_cfg[0].density = 20;
        tb3_cfg[0].root = 2;
        tb3_rt[0].pending = 1;
        k1 = tb3_knobs(&tb3_cfg[0]);
        tb3_live_line(a, &k0, &k1, tb3_cfg[0].seed, 32, want);
        last = t->seq_idx;
        for (b = 0; b < 40000u && !hit; b++) {
            seq_tick(t, CTL);
            tb3_read(t->step, cur);
            if (t->seq_idx != last && t->seq_idx == 16u) {
                hit = 1;
                ok &= !memcmp(cur, want, sizeof cur) && !tb3_rt[0].pending;
            } else
                ok &= !memcmp(cur, a, sizeof cur);
            last = t->seq_idx;
        }
        ok &= hit;
        tb3_cfg[0].density = 70;                     /* in a song: it waits */
        tb3_rt[0].pending = 1;
        chain.running = 1;
        for (b = 0; b < 6000u; b++)
            seq_tick(t, CTL);
        tb3_read(t->step, cur);
        ok &= !memcmp(cur, want, sizeof cur) && tb3_rt[0].pending;
        chain.running = 0;
        for (b = 0; b < 6000u; b++)
            seq_tick(t, CTL);
        k1 = tb3_knobs(&tb3_cfg[0]);
        tb3_live_line(a, &k0, &k1, tb3_cfg[0].seed, 32, want);
        tb3_read(t->step, cur);
        ok &= !memcmp(cur, want, sizeof cur) && !tb3_rt[0].pending;
        song.playing = 0;
        bad += check("tb3po live seq: a knob turned while playing: the line as it was until the next bar (step 17), "
                     "then the knobs'; in a song after it", ok);

        t->step[5].note[0] = (uint8_t)(t->step[5].note[0] + 1u);   /* a hand edit: the anchor from it */
        t->step[5].time = ST_NOTE;
        t->step[5].n = 1;
        tb3_read(t->step, a);
        k0 = tb3_knobs(&tb3_cfg[0]);
        tb3_cfg[0].root = 3;
        tb3_live(t);
        tb3_read(t->step, cur);
        ok = 1;
        for (i = 0; i < NSTEP; i++)
            ok &= cur[i].flags == a[i].flags && (a[i].note > 126u || cur[i].note == a[i].note + 1u);
        bad += check("tb3po live: after a hand edit the edited steps are the anchor (the edit kept, transposed)", ok);
    }
    return bad;
}

/* ----------------------------------------------------- 6. a project --- */
static int project(void)
{
    int bad = 0, ok;
    uint8_t v[FP_SIZE + 1u], init[FP_SIZE + 1u], own2[FP_SIZE + 1u];
    tb3_cfg_t g;
    ui_power_on();
    song.playing = 0;
    set_engine_of(&trk[0], ENGI_303);
    tb3_cfg[0] = (tb3_cfg_t){55, 66, 77, 3, 4, 5, 2, 9, 0xDEADBEEFu, AICD_ON, 150, 20, 30, 63, 2};
    tb3_cfg[0].keep = 2; tb3_cfg[0].style = 3; tb3_cfg[0].notes = 4; tb3_cfg[0].rhythm = 5; tb3_cfg[0].ties = 9;
    tb3_cfg[0].join = 1; tb3_cfg[0].best = 6; tb3_cfg[0].form = 2; tb3_cfg[0].flair = 33; tb3_cfg[0].twist = 44;
    tb3_cfg[0].tbars = 12; tb3_cfg[0].morph = 55; tb3_cfg[0].slip = 7; tb3_cfg[0].plen = 9; tb3_cfg[0].mtype = 2;
    tb3_cfg[0].pocket = 3; tb3_cfg[0].answer = 4; tb3_cfg[0].rootfol = 2; tb3_cfg[0].keys = 1;
    tb3_cfg[0].pool = 0xABCu; tb3_cfg[0].line = 9876; tb3_cfg[0].seed_b = 0xCAFEF00Du;
    g = tb3_cfg[0];
    tb3_cfg[1].density = 12;                        /* (track 2 not a 303: not saved, the defaults back) */
    fm6_patch[1][3] ^= 0x15;                        /* track 2's own FM6 patch */
    memcpy(own2, fm6_patch[1], FP_SIZE);
    ok = !project_save(2);
    tb3_reset();
    fm6_init();
    project_load(2);
    fm6_unpack(FM6_INIT, init);
    memcpy(v, fm6_patch[0], FP_SIZE);
    ok = ok && cfg_eq(&tb3_cfg[0], &g) && !memcmp(&tb3_cfg[0].keep, &g.keep,
                                                  offsetof(tb3_cfg_t, seed_b) + sizeof g.seed_b - offsetof(tb3_cfg_t, keep)) &&
         trk[0].eng_req == ENGI_303 && !memcmp(v, init, FP_SIZE) &&
         tb3_cfg[1].density == 70u && !memcmp(fm6_patch[1], own2, FP_SIZE);
    bad += check("tb3po project: a 303 track's settings and seed back, its FM6 patch the init one, track 2's own kept", ok);
    return bad;
}

/* ---------------------------------------------------------- 7. AICD --- */
#if AICD_ON
static uint64_t instr_now(void)                   /* (tests/regress.c's) */
{
#ifdef __APPLE__
    struct rusage_info_v4 ri;
    if (!proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&ri))
        return ri.ri_instructions;
#endif
    return 0;
}
static void aicd_cfg(tb3_cfg_t *g, uint32_t seed) /* AICD, its defaults (A from A1: the base note 33) */
{
    tb3_defaults(g, seed);
    g->mode = TB3_AICD;
}
static double aicd_odds(const aicd_edge_t *e, uint32_t n, uint32_t temp, double *q)   /* a draw's odds, their sum */
{
    uint32_t k = 16u * 100u * 256u / AICD_WQ / temp, i, lo = ~0u;
    double sum = 0;
    for (i = 0; i < n; i++)
        lo = (e[i].w * k) >> 8 < lo ? (e[i].w * k) >> 8 : lo;
    for (i = 0; i < n; i++) {
        uint32_t d = ((e[i].w * k) >> 8) - lo;
        q[i] = d >= 16u * 17u ? 0 : (double)(AICD_EXP[d & 15u] >> (d >> 4));
        sum += q[i];
    }
    return sum;
}

/* 7a. the model in flash == gen_aicd.py's (its reference: "node last n" then n x "node p") */
static int aicd_model(const char *ref)
{
    int bad = 0, ok = 1, okw = 1, ok2 = 1;
    uint32_t node, last, n, k, nodes = 0, total = 0;
    double tv_max = 0, tv_sum = 0;
    FILE *f = ref ? fopen(ref, "r") : 0;
    for (k = 0; k < AICD_NSYM; k++)
        ok2 &= AICD_GRP[k] <= AICD_GRP[k + 1u];
    for (k = 1; k < AICD_NSTEP; k++)
        ok2 &= AICD_CODE[k - 1] < AICD_CODE[k];
    for (k = 0; k < 16u; k++)
        ok2 &= fabs(AICD_EXP[k] - 65536.0 * pow(2.0, -(double)k / 16.0)) <= 1.0;
    for (k = 0; k < AICD_NSTART; k++)
        ok2 &= AICD_START[k] < AICD_NNODE;
    bad += check("aicd: the tables: every node in a group, the steps sorted, 2^-i/16 in 16 bits",
                 ok2 && AICD_GRP[AICD_NSYM] == AICD_NNODE);
    if (!f) {
        printf("  aicd: no reference (%s): run tools/gen_aicd.py OUT.h REF.txt\n", ref ? ref : "argv[1]");
        return bad + 1;
    }
    while (fscanf(f, "%u %u %u", &node, &last, &n) == 3) {
        aicd_at_t a;
        aicd_edge_t e[AICD_MAXE];
        double p[AICD_MAXE], q[AICD_MAXE], pmax = 0, sq, tv = 0;
        uint32_t pos, m, en;
        aicd_seek(node, &a);
        pos = a.pos;
        m = aicd_body(&pos, a.last, e);
        ok &= node == nodes && a.last == last && m == n && n <= AICD_MAXE;
        for (k = 0; k < n; k++) {
            if (fscanf(f, "%u %lf", &en, &p[k]) != 2)
                ok = 0;
            ok &= e[k].n == en;
            pmax = p[k] > pmax ? p[k] : pmax;
        }
        for (k = 0; k < n && n > 1u; k++) {          /* the weights: 1/3 log2 under the likeliest, 31 at most */
            double x = -3.0 * log2(p[k] / pmax);
            okw &= x > 31.0 ? e[k].w == 31u : fabs(e[k].w - x) <= 0.5 + 1e-9;
        }
        sq = aicd_odds(e, n, 100, q);
        for (k = 0; k < n; k++)
            tv += fabs(q[k] / sq - p[k]) / 2.0;
        tv_max = tv > tv_max ? tv : tv_max;
        tv_sum += tv;
        nodes++;
        total += n;
    }
    fclose(f);
    printf("  aicd: %u nodes, %u edges, %u bytes; the odds within %.4f of the model's (mean %.4f)\n", nodes, total,
           AICD_BYTES, tv_max, tv_sum / nodes);
    bad += check("aicd: every node's edges (their nodes, its last step) == gen_aicd.py's exact model", ok && nodes == AICD_NNODE);
    bad += check("aicd: the weights: round(-3 log2(p / the likeliest's)); a draw's odds within 0.05 of the model's (mean 0.01)",
                 okw && tv_max < 0.05 && tv_sum / nodes < 0.01);
    return bad;
}

/* 7b. a draw from a node: as often as its odds say (the busiest node, a pattern's end, random ones; 200000 each) */
static int aicd_draws(void)
{
    int bad = 0, ok = 1;
    uint32_t t, i, k, n, best = 0, bn = 0, r = 0x1234567u;
    aicd_bias_t bi;
    tb3_cfg_t c;
    aicd_cfg(&c, 1);
    aicd_bias(&c, &bi);
    for (i = 0; i < AICD_NNODE; i++) {              /* the node with the most edges */
        aicd_at_t a;
        uint32_t pos;
        aicd_seek(i, &a);
        pos = a.pos;
        n = aicd_body(&pos, a.last, 0);
        if (n > best)
            best = n, bn = i;
    }
    for (t = 0; t < 6u; t++) {
        aicd_at_t a0, a, tg[AICD_MAXE];
        aicd_edge_t e[AICD_MAXE];
        double q[AICD_MAXE], sum, worst = 0;
        uint32_t cnt[AICD_MAXE] = {0}, pos, code;
        aicd_seek(t == 0 ? bn : t == 1 ? AICD_START[rnd(AICD_NSTART)] : rnd(AICD_NNODE), &a0);
        pos = a0.pos;
        n = aicd_body(&pos, a0.last, e);
        if (!n)
            continue;
        sum = aicd_odds(e, n, 100, q);
        for (k = 0; k < n; k++)
            aicd_seek(e[k].n, &tg[k]);
        for (i = 0; i < 200000u; i++) {
            a = a0;
            (void)aicd_next(&a, &bi, &r, &code);
            for (k = 0; k < n && (tg[k].n != a.n || tg[k].last != a.last || tg[k].pos != a.pos); k++)
                ;
            ok &= k < n;
            if (k < n)
                cnt[k]++;
        }
        for (k = 0; k < n; k++) {
            double pe = q[k] / sum, sd = sqrt(pe * (1 - pe) / 200000.0) + 1e-9, z = fabs(cnt[k] / 200000.0 - pe) / sd;
            worst = z > worst ? z : worst;
        }
        ok &= worst < 5.0;
        if (t == 0)
            printf("  aicd: the busiest node, %u edges: within %.1f sd of its odds\n", n, worst);
    }
    bad += check("aicd: a draw's edges as often as their odds (the busiest node, a start, random ones: within 5 sd)", ok);
    return bad;
}

typedef struct { double gate, acc, sld, inmin; uint32_t distinct; } aicd_stats_t;
static uint32_t line_hash(const tb3_step_t *b, uint32_t len)
{
    uint32_t h = 2166136261u, i;
    for (i = 0; i < len; i++)
        h = (h ^ (b[i].note | (uint32_t)b[i].flags << 8)) * 16777619u;
    return h;
}
static int cmp_u32(const void *a, const void *b)
{
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return x < y ? -1 : x > y;
}
/* lines of len from N seeds: the share of notes, of ACC / SLD among them, of A minor's among them; distinct lines */
static aicd_stats_t aicd_stats(const tb3_cfg_t *c0, uint32_t nlines, uint32_t len, int *valid)
{
    static uint32_t hs[20000];
    aicd_stats_t st = {0, 0, 0, 0, 0};
    uint32_t i, k, tot = 0, ng = 0, acc = 0, sld = 0, inm = 0, v, y;
    tb3_cfg_t c = *c0;
    for (i = 0; i < nlines; i++) {
        tb3_step_t b[NSTEP];
        c.seed = tb3_new_seed(i * 7919u + 13u);
        aicd_generate(&c, b, len);
        for (k = 0; k < len; k++) {
            int known = 0;
            for (v = 0; v < ((b[k].flags & TB3_GATE) == TB3_REST ? 4u : 1u); v++)
                known |= aicd_sym(&c, &b[k], v, &y);
            *valid &= known;
            tot++;
            if ((b[k].flags & TB3_GATE) == TB3_NOTE) {
                uint32_t pc = (uint32_t)(b[k].note + 120 - 33) % 12u;   /* (from A) */
                ng++;
                acc += (b[k].flags & TB3_ACC) != 0;
                sld += (b[k].flags & TB3_SLD) != 0;
                inm += pc == 0 || pc == 2 || pc == 3 || pc == 5 || pc == 7 || pc == 8 || pc == 10;
            }
        }
        for (k = len; k < NSTEP; k++)
            *valid &= b[k].flags == TB3_REST;
        hs[i] = line_hash(b, len);
    }
    qsort(hs, nlines, sizeof hs[0], cmp_u32);
    for (i = 0; i < nlines; i++)
        st.distinct += !i || hs[i] != hs[i - 1];
    st.gate = (double)ng / tot;
    st.acc = (double)acc / ng;
    st.sld = (double)sld / ng;
    st.inmin = (double)inm / ng;
    return st;
}
/* 7c. Aicd's lines: as its own sampler's (tools: its acid.js at temperature 1, 20000 lines of 16: gate 0.9028, ACC
 * 0.3530 and SLD 0.3177 of the notes, after TB-3PO's "no slide into a rest", A minor's 0.7132); each bias its way */
static int aicd_lines(void)
{
    int bad = 0, valid = 1, ok;
    tb3_cfg_t c;
    aicd_stats_t s0, s, s1, s2;
    aicd_cfg(&c, 1);
    s0 = aicd_stats(&c, 20000, 16, &valid);
    printf("  aicd: gate %.4f, ACC %.4f, SLD %.4f, A minor %.4f, %u distinct of 20000 (Aicd: 0.9028 0.3530 0.3177 0.7132)\n",
           s0.gate, s0.acc, s0.sld, s0.inmin, s0.distinct);
    ok = fabs(s0.gate - 0.9028) < 0.01 && fabs(s0.acc - 0.3530) < 0.02 && fabs(s0.sld - 0.3177) < 0.015 &&
         fabs(s0.inmin - 0.7132) < 0.015;
    bad += check("aicd: the lines as Aicd's own (gate, ACC, SLD, the notes in A minor; 20000 lines)", ok);
    bad += check("aicd: every step a model step, transposed (A from A1); after LEN all REST", valid);
    ok = 1;                                         /* DENS ACCENT SLIDE: the share asked for (0 and 100 % too) */
    printf("  aicd: asked / got (gate, ACC, SLD):");
    {
        static const uint32_t PCT[] = {0, 20, 50, 80, 100};
        uint32_t k;
        for (k = 0; k < NELEM(PCT); k++) {
            double want = PCT[k] / 100.0, tol = 0.06;   /* (within a step of 16, or 14 notes) */
            c.a_dens = (uint8_t)PCT[k];
            s = aicd_stats(&c, 3000, 16, &valid);
            ok &= fabs(s.gate - (PCT[k] ? want : 1.0 / 16)) < tol;
            c.a_dens = AICD_DENS;
            c.a_acc = (uint8_t)PCT[k];
            s1 = aicd_stats(&c, 3000, 16, &valid);
            ok &= fabs(s1.acc - want) < tol;
            c.a_acc = AICD_ACC;
            c.a_sld = (uint8_t)PCT[k];
            s2 = aicd_stats(&c, 3000, 16, &valid);
            ok &= PCT[k] == 100 ? s2.sld > 0.85 : fabs(s2.sld - want) < tol;   /* (a slide into a rest does nothing) */
            c.a_sld = AICD_SLD;
            printf(" %u: %.2f %.2f %.2f;", PCT[k], s.gate, s1.acc, s2.sld);
        }
    }
    c.a_scale = 1;                                  /* MIN */
    s = aicd_stats(&c, 3000, 16, &valid);
    ok &= s.inmin > s0.inmin + 0.12;
    printf(" SCALE MIN: %.3f in A minor\n", s.inmin);
    bad += check("aicd: DENS ACCENT SLIDE 0..100 %: that share of the steps / notes (0 and 100 % too); SCALE MIN more "
                 "in it (where the model has a choice)", ok);
    c.a_scale = 0;
    {
        uint32_t d10, d100, d200;
        c.temp = 10;
        d10 = aicd_stats(&c, 20000, 4, &valid).distinct;
        c.temp = 100;
        d100 = aicd_stats(&c, 20000, 4, &valid).distinct;
        c.temp = 200;
        d200 = aicd_stats(&c, 20000, 4, &valid).distinct;
        printf("  aicd: TEMP 10 / 100 / 200 %%: %u / %u / %u distinct 4-step lines of 20000\n", d10, d100, d200);
        ok = d10 * 3u < d100 * 2u && d100 + 200u < d200;
        c.temp = 10;                                /* (the fit keeps it: 16-step lines, 7.5 k of 20000 at 10 %, ~19 k at 100 %) */
        d10 = aicd_stats(&c, 20000, 16, &valid).distinct;
        c.temp = 100;
        d100 = aicd_stats(&c, 20000, 16, &valid).distinct;
        printf("  aicd: TEMP 10 / 100 %%: %u / %u distinct 16-step lines of 20000\n", d10, d100);
        ok &= d10 * 2u < d100;
    }
    bad += check("aicd: TEMP: 10 % fewer lines (the likeliest), 200 % more than 100 %", ok);
    bad += check("aicd: every step of the biased lines a model step too", valid);
    return bad;
}

/* 7d. a MUTATE: a run of (LEN + 3) / 4 steps (16 at most) changed at most, the rest as it was; model steps; the cost */
static int aicd_mut(void)
{
    int bad = 0, ok = 1;
    uint32_t i, k, i0, run, changed = 0;
    uint64_t t0, gen_i = 0, mut_i = 0;
    tb3_cfg_t c;
    aicd_cfg(&c, 1);
    for (i = 0; i < 3000u; i++) {
        uint32_t len = rnd(3) ? 1u + rnd(64) : 64u, r = rnd(4) ? seed_rand() : 0u, diff[NSTEP], any;
        tb3_step_t b[NSTEP], was[NSTEP];
        aicd_rand(&c);
        c.mode = TB3_AICD;
        c.root = (uint8_t)rnd(12);
        c.base_oct = (uint8_t)rnd(5);
        c.seed = seed_rand();
        aicd_generate(&c, b, len);
        memcpy(was, b, sizeof b);
        aicd_mutate(&c, b, len, &r);
        run = (len + 3u) / 4u > 16u ? 16u : (len + 3u) / 4u;
        for (k = 0; k < NSTEP; k++) {
            uint32_t g = b[k].flags & TB3_GATE;
            diff[k] = b[k].note != was[k].note || b[k].flags != was[k].flags;
            ok &= !diff[k] || (k < len && (g == TB3_NOTE || g == TB3_REST));
            changed += diff[k];
        }
        for (i0 = 0, any = 0; i0 < len && !any; i0++) {   /* a window of run steps (round the end) holds them all, */
            uint32_t out = 0;                           /* and the step before it (its SLD into a REST now dropped) */
            for (k = 0; k < len; k++)
                out += diff[k] && (k + len - i0) % len >= run + 1u && !(k == (i0 + len - 1u) % len);
            any = !out;
        }
        ok &= any && r != 0;
    }
    bad += check("aicd: a MUTATE changes a run of (LEN + 3) / 4 steps (16 at most), nothing past LEN; the stream on", ok);
    printf("  aicd: a MUTATE changed %.2f steps a line on average\n", changed / 3000.0);
    aicd_cfg(&c, 1);
    t0 = instr_now();
    for (i = 0; i < 300u; i++) {
        tb3_step_t b[NSTEP];
        c.seed = 1u + i;
        aicd_generate(&c, b, 64);
    }
    gen_i = instr_now() - t0;
    t0 = instr_now();
    for (i = 0; i < 300u; i++) {
        tb3_step_t b[NSTEP];
        uint32_t r = 77u + i;
        c.seed = 1u + i;
        memset(b, 0, sizeof b);
        aicd_mutate(&c, b, 64, &r);
    }
    mut_i = instr_now() - t0;
    if (gen_i) {
        printf("  aicd: %llu instructions a 64-step line, %llu a MUTATE of one (host)\n",
               (unsigned long long)(gen_i / 300u), (unsigned long long)(mut_i / 300u));
        bad += check("aicd: the cost: a line < 200000 instructions (the UI: IRQs on), a MUTATE < 40000 (the ISR's auto)",
                     gen_i / 300u < 200000u && mut_i / 300u < 40000u);
    }
    return bad;
}

/* 7e. the UI, auto MUTATE, a project; the same lines as before (a hash) */
static int aicd_ui(void)
{
    int bad = 0, ok;
    uint32_t i, h = 2166136261u, b, wraps = 0, last, r, n0;
    track_t *t;
    tb3_cfg_t c;
    tb3_step_t want[NSTEP], cur[NSTEP];
    ui_power_on();
    t = TSEL;
    set_engine(ENGI_303);
    t->p[P_SLEN] = 16;
    press(B_ARP);
    press(B_ARP);
    press(B_ARP);
    ok = cur_page()->graph == GR_TB3MOD && tb3_cfg[0].mode == TB3_TB3PO;
    turn(EN_K2, 3);                                 /* (KNOB 2..4 nothing here) */
    turn(EN_K1, 1);
    ok &= tb3_cfg[0].mode == TB3_AICD && tb3_rt[0].stale && tb3_cfg[0].density == 70u;
    press(B_ARP);                                   /* (SHAPE FORM PLAY LINK) */
    press(B_ARP);
    press(B_ARP);
    press(B_ARP);
    press(B_ARP);
    turn(EN_K1, 5);
    turn(EN_K2, -3);
    turn(EN_K3, 2);
    turn(EN_K4, -20);
    ok &= cur_page()->graph == GR_TB3GEN && tb3_cfg[0].a_dens == AICD_DENS + 5u && tb3_cfg[0].a_acc == AICD_ACC - 3u &&
          tb3_cfg[0].a_sld == AICD_SLD + 2u &&
          tb3_cfg[0].temp == 80u && tb3_cfg[0].density == 70u && tb3_cfg[0].accent == 40u && tb3_cfg[0].oct_range == 2u;
    press(B_ARP);
    turn(EN_K2, 2);
    ok &= tb3_cfg[0].a_scale == 2u && tb3_cfg[0].scale == 0u;   /* PHRY (TB-3PO's SCALE as it was) */
    bad += check("aicd ui: MODEL: MODE (KNOB 1); in AICD GENERATE DENS ACCENT SLIDE TEMP, SCALE from CHRO, their own", ok);
    press(B_OCTUP);
    ok = line_is(t, &tb3_cfg[0]) && !strncmp(ui.msg, "NEW LINE ", 9) && !tb3_rt[0].stale;
    {
        step_t line[NSTEP];
        tb3_step_t m[NSTEP], o[NSTEP];
        for (n0 = 0; n0 < 8u; n0++) {               /* (the model may draw the same run again: many of its steps are forced) */
            memcpy(line, t->step, sizeof line);
            press(B_OCTDN);
            if (memcmp(line, t->step, sizeof line))
                break;
        }
        tb3_read(t->step, m);
        ok &= msg_is("MUTATED") && memcmp(line, t->step, sizeof line);
        tb3_read(line, o);
        for (i = 0, b = 0; i < NSTEP; i++)
            b += m[i].note != o[i].note || m[i].flags != o[i].flags;
        ok &= b <= 5u;                              /* (a run of 4, the SLD before it into a REST now) */
    }
    bad += check("aicd ui: OCT+ AICD's line (its seed, the settings'), OCT- a MUTATE of a run of 4 (LEN 16)", ok);

    ui_power_on();                                  /* auto MUTATE: AICD's, every 2nd pass, before its step 1 */
    t = &trk[0];
    set_engine_of(t, ENGI_303);
    t->engine = ENGI_303;
    eng_mem_claim(t);
    t->p[P_SLEN] = 8;
    tb3_cfg[0].mode = TB3_AICD;
    tb3_cfg[0].mutate_bars = 2;
    r = 0;
    tb3_apply(t, 1, &r);
    tb3_read(t->step, want);
    song.playing = 1;
    seq_start();
    last = t->seq_idx;
    n0 = tb3_auto_n;
    r = 0;
    ok = 1;
    for (b = 0; b < 40000u && wraps < 7u; b++) {
        seq_tick(t, CTL);
        if (t->seq_idx == 0u && last != 0u && b) {
            wraps++;
            if (wraps % 2u == 0u)
                aicd_mutate(&tb3_cfg[0], want, 8, &r);
            tb3_read(t->step, cur);
            ok &= !memcmp(cur, want, sizeof cur);
        }
        last = t->seq_idx;
    }
    bad += check("aicd seq: MUTATE 2 in AICD: AICD's MUTATE every 2nd pass, before its step 1 (6 passes: 3)",
                 ok && wraps == 7u && tb3_auto_n - n0 == 3u);

    for (i = 0; i < 64u; i++) {                     /* the same lines as before: settings, seeds, lengths */
        tb3_step_t l[NSTEP];
        uint32_t rr = i * 2654435761u;
        aicd_cfg(&c, 0x51EDu + i * 7u);
        c.temp = (uint8_t)(10u + i * 3u);
        c.a_dens = (uint8_t)(i * 5u % 101u);
        c.a_acc = (uint8_t)(i * 11u % 101u);
        c.a_sld = (uint8_t)(i * 13u % 101u);
        c.a_scale = (uint8_t)(i % 7u);
        c.root = (uint8_t)(i % 12u);
        c.base_oct = (uint8_t)(i % 5u);
        aicd_generate(&c, l, 1u + i);
        h = (h ^ line_hash(l, NSTEP)) * 16777619u;
        aicd_mutate(&c, l, 1u + i, &rr);
        h = (h ^ line_hash(l, NSTEP) ^ rr) * 16777619u;
    }
    printf("  aicd: the lines' hash %08x\n", h);
    bad += check("aicd: the same lines and MUTATEs as before (64 settings, seeds, lengths: their hash)", h == 0x99FE18ACu);
    return bad;
}
#endif

/* -------------------------------------------- 8. what the audit found --- */
static int fixes(void)
{
    int bad = 0, ok;
    uint32_t i, it;
    track_t *t;
    tb3_step_t cur[NSTEP];

    ui_power_on();
    t = TSEL;
    set_engine(ENGI_303);
    t->p[P_SLEN] = 16;
    press(B_ARP);
    press(B_OCTUP);
    ok = motion_put(t, 3, P_PAN, 10, 0) == 0 && motion_count(t) == 1u;
    press(B_OCTDN);
    ok &= motion_count(t) == 1u;
    press(B_OCTUP);
    ok &= motion_count(t) == 1u;
    bad += check("tb3po fix: OCT+ / OCT- keep the track's automation and locks", ok);
    {
        uint32_t s1 = tb3_cfg[0].seed, s0;
        step_t after[NSTEP];
        press(B_OCTUP);
        s0 = tb3_cfg[0].seed;
        memcpy(after, t->step, sizeof after);
        hold(B_SAVE);
        ok = tb3_cfg[0].seed == s1 && memcmp(after, t->step, sizeof after);
        hold(B_SAVE);
        ok &= tb3_cfg[0].seed == s0 && !memcmp(after, t->step, sizeof after);
        bad += check("tb3po fix: SAVE held undoes the line and its seed, again redoes both", ok);
    }

    ui_power_on();
    t = TSEL;
    set_engine(ENGI_303);
    t->p[P_SLEN] = 16;
    press(B_ARP);
    ok = seq_is_empty(t);
    turn(EN_K1, 30);                                 /* DENS up on steps never written */
    tb3_read(t->step, cur);
    for (i = 0, it = 0; i < 16u; i++)
        if (tb3_gated(&cur[i])) {
            it++;
            ok &= cur[i].note >= 12u;
        }
    ok &= it > 0u;
    bad += check("tb3po fix: DENS up on an empty track plays notes of its own, never note 0", ok);
    ui_power_on();
    t = TSEL;
    set_engine(ENGI_303);
    t->p[P_SLEN] = 16;
    press(B_ARP);
    ok = 1;
    for (it = 0; it < 12u; it++)
        press(B_OCTDN);
    tb3_read(t->step, cur);
    for (i = 0; i < 16u; i++)
        ok &= !tb3_gated(&cur[i]) || cur[i].note >= 12u;
    bad += check("tb3po fix: MUTATE on an empty track never plays note 0", ok);

    {
        tb3_cfg_t g;
        tb3_step_t a[NSTEP], b[NSTEP];
        tb3_knobs_t k0, k;
        uint32_t lvl[2] = {0, 0}, nl[2] = {0, 0};
        ok = 1;
        for (it = 0; it < 2000u; it++) {
            tb3_defaults(&g, 0x5000u + it);
            g.density = 80;
            g.slide = 40;
            tb3_gen(&g, a, 16);
            k0 = k = tb3_knobs(&g);
            k.dens = 40;
            k.sld = 0;
            tb3_live_line(a, &k0, &k, g.seed, 16, b);
            for (i = 0; i < 16u; i++)
                ok &= !(b[i].flags & TB3_SLD);
            k = k0;
            k.dens = 30;
            tb3_live_line(a, &k0, &k, g.seed, 16, b);
            for (i = 0; i < 16u; i++)
                if (tb3_gated(&a[i])) {
                    uint32_t x = tb3_level(i) == 0u ? 0u : tb3_level(i) == 4u ? 1u : 2u;
                    if (x < 2u) {
                        nl[x]++;
                        lvl[x] += (uint32_t)tb3_gated(&b[i]);
                    }
                }
        }
        bad += check("tb3po fix: SLIDE 0 with DENS down leaves no slide", ok);
        bad += check("tb3po fix: DENS down keeps step 1 and thins the off-beats first",
                     lvl[0] * 100u / nl[0] > 90u && lvl[1] * 100u / nl[1] < 40u);
    }
    {
        tb3_step_t a[NSTEP];
        memset(a, 0, sizeof a);
        for (i = 0; i < NSTEP; i++) {
            a[i].note = 40;
            a[i].flags = TB3_NOTE;
        }
        a[3].flags = TB3_TIE;
        bad += check("tb3po fix: ACCENT never picks a TIE, SLIDE still may",
                     !tb3_elig(a, 3, 16, TB3_ACC) && tb3_elig(a, 2, 16, TB3_ACC) && tb3_elig(a, 3, 16, TB3_SLD));
    }
    {
        uint32_t n0;
        tb3_poll();
        fm1_ms += 2000u;
        tb3_poll();
        sync_reload = 0;
        n0 = tb3_auto_n;
        tb3_auto_n = n0 + 1u;
        fm1_ms += 2000u;
        tb3_poll();
        ok = sync_reload != 0;
        sync_reload = 0;
        tb3_auto_n = n0 + 2u;
        tb3_poll();
        ok &= !sync_reload;                          /* (at most once a second) */
        sync_reload = 0;
        bad += check("tb3po fix: an auto MUTATE tells the editor, not more than once a second", ok);
    }
#if AICD_ON
    {
        tb3_cfg_t g;
        tb3_step_t b;
        tb3_defaults(&g, 1);
        g.base_oct = 0;
        g.root = 0;
        aicd_put(&g, &b, (1u << 3) | 4u);            /* (the model's lowest: offset -15, note -3) */
        bad += check("tb3po fix: AICD at OCTAVE 0: under note 0 an octave up, not all clamped to 0", b.note == 9u);
    }
    ui_power_on();
    t = TSEL;
    set_engine(ENGI_303);
    t->p[P_SLEN] = 16;
    press(B_ARP);
    press(B_OCTUP);
    song.playing = 1;
    turn(EN_K1, -20);
    ok = tb3_rt[0].pending != 0;
    {
        tb3_knobs_t k0 = tb3_rt[0].k0, k = tb3_knobs(&tb3_cfg[0]);
        tb3_step_t want[NSTEP];
        tb3_live_line(tb3_rt[0].anchor, &k0, &k, tb3_cfg[0].seed, 16, want);
        press(B_ARP);
        press(B_ARP);
        turn(EN_K1, 1);                              /* MODE, with the knob's bar still to come */
        tb3_read(t->step, cur);
        ok &= !tb3_rt[0].pending && tb3_cfg[0].mode == TB3_AICD && !memcmp(cur, want, sizeof cur);
    }
    song.playing = 0;
    bad += check("tb3po fix: MODE changed within a bar: the knob turned reaches the line first", ok);
#endif
    return bad;
}

/* -------------------------------------------------- 9. the workflow --- */
static int workflow(void)
{
    int bad = 0, ok;
    uint32_t b, i;
    track_t *t;
    tb3_step_t cur[NSTEP], a0[NSTEP], want[NSTEP];

    ui_power_on();
    t = TSEL;
    set_engine(ENGI_303);
    t->engine = ENGI_303;
    eng_mem_claim(t);
    t->p[P_SLEN] = 32;
    press(B_ARP);
    press(B_OCTUP);
    tb3_read(t->step, a0);
    song.playing = 1;
    seq_start();
    for (b = 0; b < 40000u && t->seq_idx != 3u; b++)
        seq_tick(t, CTL);
    {
        uint32_t last, hit = 0;
        press(B_OCTUP);
        ok = tb3_next[0].state == TB3Q_READY;
        memcpy(want, tb3_next[0].line, sizeof want);
        last = t->seq_idx;
        for (b = 0; b < 40000u && !hit; b++) {
            seq_tick(t, CTL);
            tb3_read(t->step, cur);
            if (t->seq_idx != last && t->seq_idx == 16u) {
                hit = 1;
                ok &= tb3_lhash(cur) == tb3_lhash(want);
            } else
                ok &= !memcmp(cur, a0, sizeof cur);
            last = t->seq_idx;
        }
        ok &= hit;
    }
    frame();
    ok &= tb3_next[0].state == TB3Q_NONE;
    bad += check("tb3po flow: OCT+ while playing: the line as it was until the next bar (step 17), then the new one", ok);

    press(B_OCTDN);
    ok = tb3_next[0].state == TB3Q_READY;
    hold(B_SAVE);
    ok &= tb3_next[0].state == TB3Q_NONE;
    bad += check("tb3po flow: a line waiting for its bar: SAVE held takes it back", ok);

    press(B_OCTUP);
    ok = tb3_next[0].state == TB3Q_READY;
    memcpy(want, tb3_next[0].line, sizeof want);
    song.playing = 0;
    frame();
    tb3_read(t->step, cur);
    ok &= tb3_next[0].state == TB3Q_NONE && tb3_lhash(cur) == tb3_lhash(want);
    bad += check("tb3po flow: a line waiting when the transport stops: written at once", ok);

    /* SEED: a number is a line */
    ui_power_on();
    t = TSEL;
    set_engine(ENGI_303);
    t->p[P_SLEN] = 16;
    press(B_ARP);
    press(B_ARP);
    press(B_ARP);
    ok = cur_page()->graph == GR_TB3MOD;
    turn(EN_K2, 40);
    {
        uint16_t no = tb3_cfg[0].line;
        tb3_read(t->step, a0);
        ok &= no >= 1u && tb3_cfg[0].seed == tb3_new_seed(no);
        press(B_OCTUP);
        tb3_cfg[0].line = no;
        tb3_cfg[0].seed = tb3_new_seed(no);
        tb3_gen(&tb3_cfg[0], want, 16);
        tb3_read(t->step, cur);
        ok &= memcmp(cur, a0, sizeof cur) != 0;
        for (i = 0; i < 16u; i++)
            ok &= want[i].note == a0[i].note && want[i].flags == a0[i].flags;
    }
    bad += check("tb3po flow: SEED: a number is always the same line", ok);

    /* HIST: back through the last lines, and to the line it left */
    ui_power_on();
    t = TSEL;
    set_engine(ENGI_303);
    t->p[P_SLEN] = 16;
    press(B_ARP);
    press(B_ARP);
    press(B_ARP);
    {
        tb3_step_t l1[NSTEP], l2[NSTEP], l3[NSTEP];
        uint32_t s2;
        press(B_OCTUP);
        tb3_read(t->step, l1);
        press(B_OCTUP);
        tb3_read(t->step, l2);
        s2 = tb3_cfg[0].seed;
        press(B_OCTUP);
        tb3_read(t->step, l3);
        turn(EN_K3, 1);
        tb3_read(t->step, cur);
        ok = !memcmp(cur, l2, sizeof cur) && tb3_cfg[0].seed == s2 && tb3_hist[0].at == 1u;
        turn(EN_K3, 1);
        tb3_read(t->step, cur);
        ok &= !memcmp(cur, l1, sizeof cur);
        turn(EN_K3, -2);
        tb3_read(t->step, cur);
        ok &= !memcmp(cur, l3, sizeof cur) && tb3_hist[0].at == 0u;
        turn(EN_K3, 7);                              /* (only as far as there are lines: 3) */
        ok &= tb3_hist[0].at == 3u;
        turn(EN_K3, -3);
    }
    bad += check("tb3po flow: HIST: the last lines back and forth, the seed with them, the line it left", ok);

    /* KEEP */
    {
        tb3_cfg_t g;
        tb3_step_t old[NSTEP], b1[NSTEP];
        uint8_t list[NSTEP];
        uint32_t n = 0, j = 0;
        tb3_defaults(&g, 0x77u);
        tb3_gen(&g, old, 16);
        g.seed = 0x99u;
        tb3_gen(&g, b1, 16);
        tb3_keep(2, old, b1, 16);
        ok = 1;
        for (i = 0; i < NSTEP; i++)
            ok &= b1[i].flags == old[i].flags;
        bad += check("tb3po flow: KEEP RHYTHM: the old gates, accents and slides", ok);
        for (i = 0; i < 16u; i++)
            if ((old[i].flags & TB3_GATE) == TB3_NOTE)
                list[n++] = old[i].note;
        tb3_gen(&g, b1, 16);
        tb3_keep(1, old, b1, 16);
        ok = n > 0u;
        for (i = 0; i < 16u; i++)
            if ((b1[i].flags & TB3_GATE) == TB3_NOTE)
                ok &= b1[i].note == list[j++ % n];
        bad += check("tb3po flow: KEEP NOTES: the old notes in order over the new rhythm", ok);
    }

    /* old projects: no extended block, all off */
    {
        uint8_t pk[FM6_PACKED];
        tb3_cfg[2].keep = 2;
        tb3_cfg[2].line = 77;
        tb3_pack(2, pk);
        memset(pk + 26, 0, FM6_PACKED - 26u);
        tb3_cfg[2].keep = 1;
        ok = tb3_unpack(2, pk) && !tb3_cfg[2].keep && !tb3_cfg[2].line;
        tb3_reset();
        bad += check("tb3po flow: a project from before the extended settings: all off", ok);
    }
    return bad;
}

/* 10. MODE ACID: repeatable, inside its pool, its shares exact, near the corpus; FORM; the pages */
static tb3_cfg_t acid_cfg(uint32_t seed)
{
    tb3_cfg_t g;
    tb3_defaults(&g, seed);
    g.mode = TB3_ACID;
    return g;
}
static int acid_valid(const tb3_step_t *b, uint32_t len)   /* no orphan TIE, no slide but into a note, none on step 1 */
{
    uint32_t i, ok = 1;
    for (i = 0; i < len; i++) {
        uint32_t g = b[i].flags & TB3_GATE, nx = b[(i + 1u) % len].flags & TB3_GATE;
        if (g == TB3_TIE && (!i || !(b[i - 1u].flags & TB3_GATE) || (b[i].flags & (TB3_ACC | TB3_SLD))))
            ok = 0;
        if ((b[i].flags & TB3_SLD) && nx != TB3_NOTE)
            ok = 0;
    }
    return (int)ok;
}
static uint32_t acid_pcs(const tb3_cfg_t *g, const tb3_step_t *b, uint32_t len)   /* its pitch classes over the root, a mask */
{
    uint32_t i, m = 0;
    for (i = 0; i < len; i++)
        if ((b[i].flags & TB3_GATE) == TB3_NOTE)
            m |= 1u << (((uint32_t)b[i].note + 120u - g->root) % 12u);
    return m;
}
static uint32_t pop12(uint32_t m)
{
    uint32_t n = 0;
    for (; m; m &= m - 1u)
        n++;
    return n;
}
static int acid(void)
{
    int bad = 0, ok;
    uint32_t i, j, seed, len;
    tb3_cfg_t g;
    tb3_step_t a[NSTEP], b[NSTEP];
    static const uint8_t LENS[] = {1, 2, 3, 7, 8, 12, 16, 17, 32, 63, 64};

    ok = 1;
    for (seed = 1; seed < 400u; seed++) {
        g = acid_cfg(seed * 2654435761u | 1u);
        g.style = (uint8_t)(seed % ACID_NSTYLE);
        g.form = (uint8_t)(seed % ACID_NFORM);
        g.rhythm = (uint8_t)(seed % ACID_NRHYTHM);
        g.scale = (uint8_t)(seed % TB3_NSCALES);
        len = LENS[seed % sizeof LENS];
        tb3_gen(&g, a, len);
        tb3_gen(&g, b, len);
        ok &= !memcmp(a, b, sizeof a);
    }
    bad += check("acid: the same seed and settings, the same line", ok);

    ok = 1;
    for (seed = 1; seed < 3000u; seed++) {
        uint32_t notes, c = 0, sl = 0, on = 0, hi = 0;
        g = acid_cfg(seed * 2654435761u | 1u);
        g.density = (uint8_t)(seed * 7u % 101u);
        g.accent = (uint8_t)(seed * 13u % 101u);
        g.slide = (uint8_t)(seed * 31u % 101u);
        g.style = (uint8_t)(seed % ACID_NSTYLE);
        g.form = (uint8_t)(seed % ACID_NFORM);
        g.rhythm = (uint8_t)(seed % ACID_NRHYTHM);
        g.ties = (uint8_t)(seed % ACID_NTIES);
        g.scale = (uint8_t)(seed % TB3_NSCALES);
        g.oct_range = (uint8_t)(1u + seed % 3u);
        len = LENS[seed % sizeof LENS];
        tb3_gen(&g, b, len);
        ok &= acid_valid(b, len);
        for (i = 0; i < len; i++) {
            on += (b[i].flags & TB3_GATE) != TB3_REST;
            c += (b[i].flags & TB3_ACC) != 0u;
            sl += (b[i].flags & TB3_SLD) != 0u;
            if (b[i].flags & TB3_GATE)
                hi = b[i].note > hi ? b[i].note : hi;
        }
        notes = acid_count(b, len);
        ok &= on >= 1u && c == (g.accent * notes + 50u) / 100u && sl <= (g.slide * notes + 50u) / 100u + 1u;
        ok &= hi < (uint32_t)(12 * (g.base_oct + 1) + g.root + 12 * g.oct_range);
    }
    bad += check("acid: ties and slides valid, ACCENT exact, SLIDE at most the knob's, inside OCTS (any LEN, FORM, RHYTHM)", ok);

    ok = 1;
    for (seed = 1; seed < 500u; seed++) {
        uint32_t want = 1u + seed % 7u, r = 0, sc;
        g = acid_cfg(seed * 40503u | 1u);
        g.notes = (uint8_t)want;
        g.ties = 1;
        g.rhythm = AR_16TH;
        g.scale = (uint8_t)(seed % TB3_NSCALES);
        sc = TB3_SCALES[g.scale].len;
        tb3_gen(&g, b, 64);
        ok &= pop12(acid_pcs(&g, b, 64)) <= (want < sc ? want : sc) && (acid_pcs(&g, b, 64) & 1u);
        {
            uint32_t pool = acid_pcs(&g, b, 64);
            acid_t c;
            uint32_t m = 0;
            acid_ctx(&g, &c);
            for (i = 0; i < c.npool; i++)
                m |= 1u << c.pool[i];
            ok &= !(pool & ~m);
            for (j = 0; j < 32u; j++)
                tb3_mut(&g, b, 64, &r);
            ok &= !(acid_pcs(&g, b, 64) & ~m) && acid_valid(b, 64);
        }
    }
    bad += check("acid: NOTES holds, the root in it, 32 MUTATEs stay inside the pool", ok);

    {   /* the shape of the lines, against the corpus's (AICD: repeats 23 %, octaves 17 %, root 28 %) and TB-3PO's 7 % / 7 % */
        uint32_t n = 0, rep = 0, oct = 0, root = 0, b1 = 0, b0 = 0, m1 = 0, m0 = 0;
        for (seed = 1; seed < 2000u; seed++) {
            uint32_t r = 0, last = 999;
            g = acid_cfg(seed * 2654435761u | 1u);
            tb3_gen(&g, b, 64);
            for (i = 0; i < 64u; i++) {
                if ((b[i].flags & TB3_GATE) != TB3_NOTE)
                    continue;
                n++;
                root += (b[i].note + 120u - g.root) % 12u == 0u;
                if (last != 999u) {
                    uint32_t d = b[i].note > last ? b[i].note - last : last - b[i].note;
                    rep += !d;
                    oct += d && d % 12u == 0u;
                }
                last = b[i].note;
                if (!(i & 3u)) {
                    b0++;
                    b1 += (b[i].note + 120u - g.root) % 12u == 0u;
                }
            }
            for (j = 0; j < 32u; j++)
                tb3_mut(&g, b, 64, &r);
            for (i = 0; i < 64u; i += 4u)
                if ((b[i].flags & TB3_GATE) == TB3_NOTE) {
                    m0++;
                    m1 += (b[i].note + 120u - g.root) % 12u == 0u;
                }
        }
        printf("acid: repeats %u %%, octaves %u %%, root %u %%; root on the beats %u %%, after 32 MUTATEs %u %%\n",
               100u * rep / n, 100u * oct / n, 100u * root / n, 100u * b1 / b0, 100u * m1 / m0);
        bad += check("acid: repeats and octaves near the corpus's, the root stays on the beats after 32 MUTATEs",
                     100u * rep / n >= 15u && 100u * rep / n <= 35u && 100u * oct / n >= 9u && 100u * oct / n <= 27u &&
                     100u * root / n >= 20u && 100u * m1 / m0 >= 30u && 100u * b1 / b0 >= 40u);
    }

    {   /* FORM: at LEN 64 parts of 16; A repeats nearly exactly, a B doesn't */
        uint32_t aa = 0, ab = 0, n = 0;
        for (seed = 1; seed < 400u; seed++) {
            g = acid_cfg(seed * 2654435761u | 1u);
            g.form = 1;
            tb3_gen(&g, b, 64);
            for (i = 0; i < 12u; i++) {                 /* (the turnaround is in the last part) */
                aa += b[i].flags == b[16u + i].flags && (!(b[i].flags & TB3_GATE) || b[i].note == b[16u + i].note);
                ab += b[i].flags == b[48u + i].flags && (!(b[i].flags & TB3_GATE) || b[i].note == b[48u + i].note);
                n++;
            }
        }
        printf("acid: FORM AAAB: A against A %u %%, A against B %u %%\n", 100u * aa / n, 100u * ab / n);
        bad += check("acid: FORM AAAB repeats its A, its B is another", 100u * aa / n >= 75u && 100u * ab / n <= 50u);
    }

    ok = 1;                                         /* the live knobs on a line with ties: reversible, no orphan */
    for (seed = 1; seed < 500u; seed++) {
        tb3_knobs_t k0, k;
        tb3_step_t l[NSTEP];
        uint32_t d, o = 0;
        g = acid_cfg(seed * 2654435761u | 1u);
        g.ties = 8;
        tb3_gen(&g, a, 32);
        k0 = tb3_knobs(&g);
        tb3_live_line(a, &k0, &k0, g.seed, 32, l);
        ok &= !memcmp(a, l, sizeof a);
        k = k0;
        for (d = 0; d <= 100u; d += 10u) {
            k.dens = (uint8_t)d;
            tb3_live_line(a, &k0, &k, g.seed, 32, l);
            for (i = 1; i < 32u; i++)
                o += (l[i].flags & TB3_GATE) == TB3_TIE && !(l[i - 1u].flags & TB3_GATE);
        }
        ok &= !o;
    }
    bad += check("acid: DENS turned back gives the line back, never leaves a TIE after a rest", ok);

    {   /* a project keeps it, in either build; an older MODE byte's clamp */
        uint8_t pk[FM6_PACKED];
        ui_power_on();
        tb3_cfg[1] = acid_cfg(77);
        tb3_cfg[1].style = 3; tb3_cfg[1].notes = 2; tb3_cfg[1].rhythm = 5; tb3_cfg[1].ties = 11; tb3_cfg[1].form = 4;
        tb3_cfg[1].scale = 9;
        tb3_pack(1, pk);
        tb3_defaults(&tb3_cfg[1], 0);
        ok = tb3_unpack(1, pk) && tb3_cfg[1].mode == TB3_ACID && tb3_cfg[1].style == 3u && tb3_cfg[1].notes == 2u &&
             tb3_cfg[1].rhythm == 5u && tb3_cfg[1].ties == 11u && tb3_cfg[1].form == 4u && tb3_cfg[1].scale == 9u;
        pk[19] = 1;
        ok &= tb3_unpack(1, pk) && tb3_cfg[1].mode == (AICD_ON ? TB3_AICD : TB3_TB3PO);
        bad += check("acid: a project keeps MODE ACID and its settings (a build without AICD: AICD's lines load as TB-3PO)", ok);
    }

    {   /* the pages: only in ACID, in order, the knobs; MODE past AICD where it isn't built */
        track_t *t;
        ui_power_on();
        t = TSEL;
        set_engine(ENGI_303);
        t->p[P_SLEN] = 16;
        press(B_ARP);
        press(B_ARP);
        press(B_ARP);
        ok = cur_page()->graph == GR_TB3MOD;
        press(B_ARP);
        ok &= cur_page()->graph == GR_TB3FRM;           /* (TB-3PO: no SHAPE) */
        press(B_ARP);
        ok &= cur_page()->graph == GR_TB3PLY;
        press(B_ARP);
        ok &= cur_page()->graph == GR_TB3LNK;
        press(B_ARP);
        ok &= cur_page()->graph == GR_TB3GEN;
        press(B_ARP);
        press(B_ARP);
        turn(EN_K1, 2);                                 /* to ACID (AICD_ON: through AICD) */
        ok &= tb3_cfg[0].mode == TB3_ACID && tb3_rt[0].stale;
        press(B_ARP);
        ok &= cur_page()->graph == GR_TB3SHP;
        turn(EN_K1, 3);
        turn(EN_K3, 2);
        ok &= tb3_cfg[0].style == 3u && tb3_cfg[0].rhythm == 2u;
        press(B_ARP);
        ok &= cur_page()->graph == GR_TB3FRM;
        turn(EN_K1, 1);
        ok &= tb3_cfg[0].form == 1u;
        press(B_ARP);
        ok &= cur_page()->graph == GR_TB3PLY;
        press(B_ARP);
        press(B_ARP);
        ok &= cur_page()->graph == GR_TB3GEN;
        press(B_OCTUP);
        ok &= tb3_rt[0].have && acid_valid(tb3_rt[0].anchor, 16);
        bad += check("acid ui: SHAPE and FORM in MODE ACID only, their knobs, OCT+ writes a valid line", ok);
    }
    return bad;
}

/* 11. AICD's SHAPE: JOIN, BEST, a MUTATE that rejoins; off: as before (aicd_lines' hash) */
static int aicd_shape(void)
{
#if AICD_ON
    int bad = 0, ok;
    uint32_t i, j, seed, n;
    tb3_cfg_t g;
    tb3_step_t a[NSTEP], b[NSTEP];
    {   /* every node, JOIN on: a step whenever the model has one; its odds never all zero */
        aicd_bias_t bi;
        uint32_t off = 0, on = 0, draws = 0, forced = 0;
        tb3_defaults(&g, 7);
        g.mode = TB3_AICD;
        aicd_bias(&g, &bi);
        for (seed = 1; seed < 400u; seed++) {
            uint32_t r = seed * 2654435761u | 1u, code;
            aicd_at_t x, y;
            uint32_t r2 = r;
            aicd_start(&x, &r);
            y = x;
            r2 = r;
            for (j = 0; j < 128u; j++) {
                bi.join = 0;
                off += !aicd_next(&x, &bi, &r, &code);
                bi.join = 1;
                on += !aicd_next(&y, &bi, &r2, &code);
                draws++;
            }
        }
        {
            aicd_at_t x;
            uint32_t r = 99u, code, nodes = AICD_GRP[AICD_NSTEP + 1u], m, only = 0;
            aicd_edge_t e[AICD_MAXE];
            bi.join = 1;
            for (m = 0; m < nodes; m++) {
                uint32_t pos, c, st = 0;
                aicd_seek(m, &x);
                pos = x.pos;
                c = aicd_body(&pos, x.last, e);
                for (j = 0; j < c; j++)
                    st += aicd_lastof(e[j].n) < AICD_NSTEP;
                if (c && !st)
                    only++;
                for (j = 0; j < 8u; j++) {
                    aicd_at_t z = x;
                    int got = aicd_next(&z, &bi, &r, &code);
                    forced += !got && st;
                }
            }
            printf("aicd: JOIN: pattern ends per 128 steps %u -> %u; %u of %u nodes have only an end (those stay)\n",
                   off * 128u / draws, on * 128u / draws, only, nodes);
            ok = !forced;
        }
        bad += check("aicd JOIN: every node with a step on offer never ends the pattern (all nodes, no all-zero odds)", ok && on * 3u < off);
    }
    ok = 1;
    for (seed = 1; seed < 300u; seed++) {
        tb3_defaults(&g, seed * 2654435761u | 1u);
        g.mode = TB3_AICD;
        tb3_gen(&g, a, 32);
        g.best = 1;
        tb3_gen(&g, b, 32);
        ok &= !memcmp(a, b, sizeof a);
    }
    bad += check("aicd BEST 1: the one line, as before", ok);
    ok = 1;
    {
        uint32_t s1 = 0, s8 = 0, off = 0;
        for (seed = 1; seed < 300u; seed++) {
            uint32_t v;
            tb3_defaults(&g, seed * 2654435761u | 1u);
            g.mode = TB3_AICD;
            aicd_generate(&g, a, 16);
            v = aicd_score(&g, a, 16);
            s1 += v;
            g.best = 8;
            tb3_gen(&g, b, 16);
            ok &= aicd_score(&g, b, 16) >= v;
            s8 += aicd_score(&g, b, 16);
            tb3_gen(&g, a, 16);
            ok &= !memcmp(a, b, sizeof a);
            off += (((uint32_t)b[0].note + 120u - g.root) % 12u == 0u) && tb3_gated(&b[0]);
        }
        printf("aicd: BEST 8: mean score %u -> %u (x100), the root on step 1 in %u %%\n", s1 * 100u / 299u, s8 * 100u / 299u, off * 100u / 299u);
        bad += check("aicd BEST 8: never below the one line's score, repeatable, better on average", ok && s8 > s1);
    }
    {   /* a MUTATE that rejoins */
        uint32_t k0 = 0, k1 = 0, tot = 0;
        for (seed = 1; seed < 800u; seed++) {
            uint32_t r0 = seed * 40503u | 1u, r1 = r0, x0, x1, run, at;
            tb3_defaults(&g, seed * 2654435761u | 1u);
            g.mode = TB3_AICD;
            tb3_gen(&g, a, 32);
            memcpy(b, a, sizeof a);
            x0 = r0;
            aicd_mutate(&g, a, 32, &r0);
            g.join = 1;
            x1 = r1;
            aicd_mutate(&g, b, 32, &r1);
            run = 8u;
            at = ((tb3_u(&x0) >> 8) * 32u) >> 16;
            (void)x1;
            k0 += (uint32_t)aicd_joins(&g, a, (at + run) % 32u, 32);
            k1 += (uint32_t)aicd_joins(&g, b, (at + run) % 32u, 32);
            tot++;
        }
        printf("aicd: a MUTATE leading back into the line as the model knows: %u %% -> %u %% (JOIN)\n", k0 * 100u / tot, k1 * 100u / tot);
        bad += check("aicd JOIN: a MUTATE rejoins the line more often", k1 > k0);
    }
    {   /* JOIN's auto MUTATE: not in the ISR, drawn by the main loop, on the bar after */
        track_t *t;
        tb3_step_t cur[NSTEP], was[NSTEP];
        ui_power_on();
        t = TSEL;
        set_engine(ENGI_303);
        t->p[P_SLEN] = 16;
        tb3_cfg[0].mode = TB3_AICD;
        tb3_cfg[0].join = 1;
        tb3_cfg[0].mutate_bars = 1;
        { uint32_t r = 0; tb3_apply(t, 1, &r); }
        song.playing = 1;
        tb3_read(t->step, was);
        tb3_pass(t, 0);
        tb3_read(t->step, cur);
        ok = tb3_rt[0].want == 1u && !memcmp(was, cur, sizeof was);
        tb3_poll();
        tb3_read(t->step, cur);
        ok &= !tb3_rt[0].want && tb3_next[0].state == TB3Q_READY && !memcmp(was, cur, sizeof was);
        tb3_commit(t);
        tb3_poll();
        ok &= tb3_next[0].state == TB3Q_NONE;
        tb3_cfg[0].join = 0;
        tb3_read(t->step, was);
        tb3_pass(t, 0);
        tb3_read(t->step, cur);
        ok &= !tb3_rt[0].want && tb3_next[0].state == TB3Q_NONE;
        song.playing = 0;
        bad += check("aicd JOIN: an auto MUTATE drawn by the main loop, landing on a bar; JOIN off: as before, in the pass", ok);
    }
    {   /* the page */
        track_t *t;
        ui_power_on();
        t = TSEL;
        set_engine(ENGI_303);
        t->p[P_SLEN] = 16;
        press(B_ARP);
        press(B_ARP);
        press(B_ARP);
        turn(EN_K1, 1);
        press(B_ARP);
        ok = cur_page()->graph == GR_TB3SHP && tb3_cfg[0].mode == TB3_AICD;
        turn(EN_K1, 1);
        turn(EN_K2, 5);
        turn(EN_K3, 3);
        ok &= tb3_cfg[0].join == 1u && tb3_cfg[0].best == 6u && tb3_rt[0].stale;
        press(B_ARP);
        ok &= cur_page()->graph == GR_TB3FRM;
        turn(EN_K1, 1);                                 /* (FORM: ACID's only) */
        ok &= !tb3_cfg[0].form;
        bad += check("aicd ui: SHAPE has JOIN and BEST; on FORM, FORM itself is ACID's", ok);
    }
    return bad;
#else
    return 0;
#endif
}

/* 12. Performance: SLIP / P.LEN, M.TYPE, FLAIR, MORPH, KEYS, TWIST */
static void perf_line(track_t *t, uint32_t len, uint32_t seed)
{
    uint32_t r = 0;
    t->p[P_SLEN] = (int16_t)len;
    tb3_cfg[trk_index(t)].seed = seed;
    tb3_apply(t, 1, &r);
    tb3_rt[trk_index(t)].pending = 0;
}
static int perf(void)
{
    int bad = 0, ok;
    uint32_t i, j, seed;
    tb3_step_t a[NSTEP], b[NSTEP], c[NSTEP];
    track_t *t;

    ok = 1;                                         /* SLIP / P.LEN */
    for (seed = 1; seed < 400u; seed++) {
        tb3_cfg_t g;
        uint8_t l0[NSTEP], l1[NSTEP];
        uint32_t n = 0, m = 0, slip = seed % 16u, plen = seed % 17u;
        tb3_defaults(&g, seed * 2654435761u | 1u);
        tb3_gen(&g, a, 32);
        for (i = 3; i < 32u; i += 7u)
            if ((a[i].flags & TB3_GATE) == TB3_NOTE && (a[i - 1u].flags & TB3_GATE))
                a[i].flags = TB3_TIE;               /* (a TIE takes no note) */
        memcpy(b, a, sizeof b);
        tb3_slip(b, 32, slip, plen);
        for (i = 0; i < 32u; i++) {
            ok &= b[i].flags == a[i].flags;
            if ((a[i].flags & TB3_GATE) == TB3_NOTE) {
                l0[n++] = a[i].note;
                l1[m++] = b[i].note;
            } else {
                ok &= b[i].note == a[i].note;       /* (a rest, a TIE: their own) */
            }
        }
        for (i = 0; i < n; i++) {
            uint32_t w = plen && plen < n ? plen : n;
            ok &= l1[i] == l0[(i % w + slip) % n];
        }
        memcpy(b, a, sizeof b);
        tb3_slip(b, 32, 0, 0);
        ok &= !memcmp(a, b, sizeof a);
    }
    bad += check("play: SLIP turns the note list, P.LEN cycles it; gates, TIEs and rests as they were; 0 / OFF nothing", ok);

    ui_power_on();
    t = TSEL;
    set_engine(ENGI_303);
    tb3_cfg[0].mutate_bars = 1;
    tb3_cfg[0].mtype = 1;                            /* VARY */
    perf_line(t, 16, 0x1234u);
    tb3_read(t->step, a);
    tb3_pass(t, 0);
    tb3_read(t->step, b);
    ok = memcmp(a, b, sizeof a) && tb3_rt[0].varied;
    tb3_pass(t, 0);
    tb3_read(t->step, c);
    ok &= !memcmp(a, c, sizeof a) && !tb3_rt[0].varied;
    tb3_pass(t, 0);                                  /* a variation, and a hand edit in it: kept */
    tb3_read(t->step, b);
    t->step[3].note[0] = 77;
    t->step[3].time = ST_NOTE;
    t->step[3].n = 1;
    tb3_pass(t, 0);
    tb3_read(t->step, c);
    ok &= c[3].note == 77u && !memcmp(c, b, 3u * sizeof *b);
    bad += check("play: M.TYPE VARY: a variation for one pass, then the line exactly; a step edited in it kept", ok);

    ok = 1;
    ui_power_on();
    t = TSEL;
    set_engine(ENGI_303);
    j = 0;
    for (seed = 1; seed < 60u; seed++) {            /* TURN: only the last 4 */
        uint32_t ch = 0;
        tb3_cfg[0].mutate_bars = 1;
        tb3_cfg[0].mtype = 2;
        tb3_cfg[0].mode = seed % 3u == 1u ? AICD_ON : seed % 3u == 2u ? TB3_ACID : TB3_TB3PO;
        perf_line(t, 16, seed * 2654435761u | 1u);
        tb3_rt[0].have = 0;
        tb3_read(t->step, a);
        tb3_pass(t, 0);
        tb3_read(t->step, b);
        for (i = 0; i < 12u; i++)
            ok &= a[i].note == b[i].note && (a[i].flags & TB3_GATE) == (b[i].flags & TB3_GATE) &&
                  (a[i].flags & TB3_ACC) == (b[i].flags & TB3_ACC);
        for (; i < 16u; i++)
            ch += a[i].note != b[i].note || a[i].flags != b[i].flags;
        j += ch;
    }
    bad += check("play: M.TYPE TURN: only the last 4 steps redrawn (each MODE)", ok && j > 30u);

    {   /* FLAIR */
        uint32_t rat = 0, cond = 0, wrong = 0, tot = 0;
        ui_power_on();
        t = TSEL;
        set_engine(ENGI_303);
        for (seed = 1; seed < 100u; seed++) {
            step_t was[NSTEP];
            tb3_cfg[0].flair = 60;
            tb3_cfg[0].slide = 25;
            perf_line(t, 32, seed * 2654435761u | 1u);
            memcpy(was, t->step, sizeof was);
            tb3_read(t->step, a);
            for (i = 0; i < 32u; i++) {
                uint32_t on = step_ratchet(&t->step[i]) > 1u || step_cond(&t->step[i]);
                rat += step_ratchet(&t->step[i]) > 1u;
                cond += step_cond(&t->step[i]) == 9u;
                tot += on;
                wrong += on && (tb3_level(i) < 3u || (a[i].flags & (TB3_GATE | TB3_SLD)) != TB3_NOTE);
            }
            perf_line(t, 32, seed * 2654435761u | 1u);
            wrong += memcmp(was, t->step, sizeof was) != 0;
            tb3_cfg[0].flair = 0;
            perf_line(t, 32, seed * 2654435761u | 1u);
            for (i = 0; i < 32u; i++)
                wrong += step_ratchet(&t->step[i]) > 1u || step_cond(&t->step[i]);
        }
        bad += check("play: FLAIR: ratchets and 4:4 pickups on weak plain notes only, the same each time, none at 0", !wrong && rat > cond && cond && tot);
    }

    ui_power_on();                                  /* MORPH */
    t = TSEL;
    set_engine(ENGI_303);
    t->p[P_SLEN] = 16;
    perf_line(t, 16, 0xABCDu);
    press(B_ARP);
    press(B_ARP);
    press(B_ARP);
    press(B_ARP);
    press(B_ARP);
    ok = cur_page()->graph == GR_TB3PLY;
    tb3_read(t->step, a);
    turn(EN_K1, 100);
    frame();
    tb3_read(t->step, b);
    {
        uint32_t same = 0;
        for (i = 0; i < 16u; i++)
            same += tb3_b[0][i].flags == b[i].flags && tb3_b[0][i].note == b[i].note;
        printf("play: MORPH 100: %u of 16 steps are line B's\n", same);
        ok &= tb3_cfg[0].morph == 100u && tb3_rt[0].bok && same >= 14u;
    }
    turn(EN_K1, -100);
    frame();
    tb3_read(t->step, c);
    ok &= !memcmp(a, c, sizeof a);
    bad += check("play: MORPH 0..100: line A to line B (stopped: at once); back: line A exactly", ok);

    ok = 1;
    turn(EN_K1, 100);
    frame();
    tb3_read(t->step, b);
    press(B_OCTDN);                                  /* A <> B */
    frame();
    tb3_read(t->step, c);
    ok &= tb3_cfg[0].morph == 100u;
    turn(EN_K1, -100);
    frame();
    tb3_read(t->step, c);
    {
        uint32_t same = 0;
        for (i = 0; i < 16u; i++)
            same += c[i].note == b[i].note && c[i].flags == b[i].flags;
        ok &= same >= 14u;
    }
    press(B_OCTDN);
    frame();
    tb3_read(t->step, c);
    ok &= !memcmp(a, c, 16u * sizeof *a);
    press(B_OCTUP);                                  /* a new B: A stays */
    frame();
    tb3_read(t->step, c);
    ok &= !memcmp(a, c, 16u * sizeof *a) && str_eq(ui.msg, "NEW B");
    bad += check("play: MORPH: OCT- swaps A and B and back, OCT+ a new B, A untouched", ok);

    ok = 1;
    turn(EN_K1, 50);
    frame();
    tb3_read(t->step, b);
    t->step[5].note[0] = 81;
    t->step[5].time = ST_NOTE;
    t->step[5].n = 1;
    turn(EN_K2, 1);                                  /* SLIP: the line is read again */
    frame();
    ok &= tb3_cfg[0].morph == 0u;
    bad += check("play: a hand edit under MORPH makes that line A (MORPH back to 0)", ok);

    {   /* a project: B from its seed, on the main loop */
        uint8_t pk[FM6_PACKED];
        uint32_t want;
        ui_power_on();
        t = TSEL;
        set_engine(ENGI_303);
        perf_line(t, 16, 0x7777u);
        tb3_cfg[0].seed_b = 0xBEEFu;
        tb3_cfg[0].morph = 40;
        tb3_b_make(0);
        memcpy(a, tb3_b[0], sizeof a);
        want = tb3_lhash(a);
        tb3_pack(0, pk);
        tb3_cfg[0].morph = 0;
        memset(tb3_b[0], 0, sizeof tb3_b[0]);
        ok = tb3_unpack(0, pk) && !tb3_rt[0].bok && tb3_cfg[0].morph == 40u;
        tb3_poll();
        ok &= tb3_rt[0].bok && tb3_lhash(tb3_b[0]) == want;
        bad += check("play: a project's MORPH: line B drawn again from its seed", ok);
    }

    {   /* KEYS */
        uint32_t k;
        ui_power_on();
        t = TSEL;
        set_engine(ENGI_303);
        perf_line(t, 16, 0x4242u);
        tb3_read(t->step, a);
        tb3_cfg[0].keys = 1;
        tb3_cfg[0].root = 9;
        k = 3;
        fm1_in.notes = 1u << k;
        keyboard_block();
        ok = tb3_cfg[0].root == kb_map(t, k) % 12u && kb_note[k] == KB_SILENT && !t->seq_n;
        fm1_in.notes = 0;
        keyboard_block();
        tb3_poll();
        tb3_read(t->step, b);
        ok &= tb3_rt[0].pending == 0u;
        for (i = 0; i < NSTEP; i++)
            ok &= (b[i].note + 120 - a[i].note - (tb3_cfg[0].root + 12 - 9)) % 12 == 0;
        tb3_cfg[0].keys = 2;
        tb3_cfg[0].root = 0;
        tb3_cfg[0].pool = 0;
        tb3_cfg[0].mode = TB3_ACID;
        fm1_in.notes = 1u << 1;
        keyboard_block();
        fm1_in.notes = 1u << 1 | 1u << 5;
        keyboard_block();
        j = tb3_cfg[0].pool;
        ok &= pop12(j) == 2u && tb3_rt[0].stale;
        fm1_in.notes = 0;
        keyboard_block();
        fm1_in.notes = 1u << 8;
        keyboard_block();
        ok &= pop12(tb3_cfg[0].pool) == 1u;
        fm1_in.notes = 0;
        keyboard_block();
        {
            uint32_t m = tb3_cfg[0].pool;
            tb3_cfg_t g = tb3_cfg[0];
            g.seed = 0x5151u;
            tb3_gen(&g, b, 32);
            ok &= !(acid_pcs(&g, b, 32) & ~m);
        }
        bad += check("play: KEYS: ROOT moves the line, none sounds; POOL: the keys held are the note pool", ok);
    }

    {   /* TWIST */
        uint32_t hi = 0, top_at = 0;
        ui_power_on();
        t = TSEL;
        set_engine(ENGI_303);
        perf_line(t, 16, 0x1111u);
        for (i = 0; i < 64u; i++)
            t->step[i].flags &= (uint8_t)~SF_ACCENT;
        for (i = 0; i < 32u; i++) {
            t->seq_idx = (uint16_t)(i % 16u);
            tb3_twist(t, !i);
        }
        ok = tb3_tw[0] == 0;
        tb3_cfg[0].twist = 100;
        tb3_cfg[0].tbars = 2;
        for (i = 0; i < 32u; i++) {
            t->seq_idx = (uint16_t)(i % 16u);
            tb3_twist(t, !i);
            if (!i)
                ok &= tb3_tw[0] == 0;
            if ((uint32_t)tb3_tw[0] > hi) {
                hi = (uint32_t)tb3_tw[0];
                top_at = i;
            }
        }
        ok &= top_at == 16u && hi == 10200u;
        t->seq_idx = 0;
        tb3_twist(t, 0);
        ok &= tb3_tw[0] == 0;
        t->step[4].flags |= SF_ACCENT;
        t->step[5].flags |= SF_ACCENT;
        t->seq_idx = 5;
        tb3_twist(t, 0);
        ok &= tb3_tw[0] > 2500;
        tb3_cfg[0].twist = 0;
        tb3_twist(t, 0);
        ok &= tb3_tw[0] == 0;
        bad += check("play: TWIST: 0 at OFF, an arc over T.BARS back to 0, a lift on accent runs", ok);
    }
    return bad;
}

/* 13. Tracks that listen: POCKET, LINK's groove, ANSWER, ROOT< */
static void kick_on(track_t *d, uint32_t i, uint32_t on)
{
    grid_hit(d, i, DV_KICK, on);
}
static int linked(void)
{
    int bad = 0, ok;
    uint32_t i, p;
    track_t *t, *d;
    tb3_step_t a[NSTEP], b[NSTEP];

    ui_power_on();
    t = &trk[0];
    d = &trk[1];
    song.sel = 0;
    set_engine_of(t, ENGI_303);
    set_engine_of(d, ENGI_DRUM);
    d->p[P_SLEN] = 16;
    for (i = 0; i < 16u; i++)
        kick_on(d, i, !(i & 3u));                  /* four on the floor */
    tb3_cfg[0].density = 90;
    perf_line(t, 32, 0x2468u);
    tb3_read(t->step, a);
    ok = 1;
    for (p = 1; p <= 3u; p++) {
        tb3_cfg[0].pocket = (uint8_t)p;
        tb3_kick_take(0);
        tb3_live(t);
        tb3_read(t->step, b);
        for (i = 0; i < 32u; i++) {
            uint32_t kk = !(i & 3u);
            if (p == 1u || p == 3u)
                ok &= !kk || !tb3_gated(&b[i]);
            if (p == 2u)
                ok &= !kk || tb3_gated(&b[i]);
            if (p == 3u && (i & 3u) == 1u)
                ok &= tb3_gated(&b[i]);
        }
    }
    tb3_cfg[0].pocket = 0;
    tb3_rt[0].pending = 1;
    tb3_live(t);
    tb3_read(t->step, b);
    ok &= !memcmp(a, b, sizeof a);
    bad += check("link: POCKET DODGE / LOCK / ANSWER the first DRUM track's kick (its LEN repeated); OFF: the line back", ok);

    tb3_cfg[0].pocket = 1;                          /* the drums edited: the main loop sees it within a second */
    tb3_kick_take(0);
    tb3_live(t);
    kick_on(d, 6, 1);
    fm1_ms += 1100u;
    tb3_poll();
    tb3_read(t->step, b);
    ok = !tb3_gated(&b[6]) && !tb3_gated(&b[22]) && !tb3_rt[0].pending;
    tb3_cfg[0].pocket = 0;
    bad += check("link: POCKET follows the drums as they are edited (stopped: at once)", ok);

#if DBRAIN_ON
    {
        step_t was[NSTEP];
        memcpy(was, d->step, sizeof was);
        tb3_cfg[0].pocket = 1;
        press(B_ARP);
        while (cur_page()->graph != GR_TB3LNK)
            press(B_ARP);
        press(B_OCTUP);
        tb3_read(t->step, b);
        ok = memcmp(was, d->step, sizeof was) != 0 && !strncmp(ui.msg, "NEW GROOVE ", 11);
        for (i = 0; i < 32u; i++)
            ok &= !((step_lanes(&d->step[i % 16u]) >> DV_KICK) & 1u) || !tb3_gated(&b[i]);
        tb3_cfg[0].pocket = 0;
        bad += check("link: LINK's OCT+: a new drum pattern and a new line around its kick", ok);
    }
#endif

    {   /* ANSWER */
        track_t *o = &trk[2];
        tb3_step_t ob[NSTEP];
        uint32_t m;
        ui_power_on();
        song.sel = 0;
        t = &trk[0];
        set_engine_of(t, ENGI_303);
        set_engine_of(o, ENGI_303);
        perf_line(o, 16, 0x9999u);
        tb3_read(o->step, ob);
        ok = 1;
        for (m = 1; m <= 4u; m++) {
            tb3_cfg[0].answer = (uint8_t)m;
            perf_line(t, 16, 0x1357u);
            tb3_read(t->step, b);
            for (i = 0; i < 16u; i++) {
                if (m == 1u)
                    ok &= !(tb3_gated(&ob[i]) && tb3_gated(&b[i]));
                else {
                    ok &= tb3_gated(&ob[i]) == tb3_gated(&b[i]) || ((ob[i].flags & TB3_GATE) == TB3_TIE);
                    if ((b[i].flags & TB3_GATE) == TB3_NOTE) {
                        int32_t dlt = (int32_t)b[i].note - (int32_t)ob[i].note;
                        ok &= m == 4u ? dlt == 12 : m == 3u ? dlt >= 6 && dlt <= 8 : dlt >= 3 && dlt <= 4;
                    }
                }
            }
        }
        tb3_read(o->step, b);
        ok &= !memcmp(ob, b, sizeof ob);
        bad += check("link: ANSWER REST / 3RD / 5TH / OCT against the other 303 track, which stays as it is", ok);
    }

    {   /* ROOT< */
        track_t *c = &trk[3];
        ui_power_on();
        song.sel = 0;
        t = &trk[0];
        set_engine_of(t, ENGI_303);
        set_engine_of(c, 0);
        c->p[P_SLEN] = 16;
        for (i = 0; i < 16u; i++)
            c->step[i].time = ST_REST, c->step[i].n = 0;
        c->step[0].time = ST_NOTE, c->step[0].n = 2, c->step[0].note[0] = 64, c->step[0].note[1] = 55;   /* E, G under it */
        tb3_cfg[0].root = 9;
        perf_line(t, 16, 0x8642u);
        tb3_read(t->step, a);
        tb3_cfg[0].rootfol = 4;
        c->seq_idx = 0;
        t->seq_idx = 0;
        tb3_bar(t, 0);
        tb3_read(t->step, b);
        ok = tb3_rt[0].fol == 7u && tb3_knobs(&tb3_cfg[0]).root == 7u;
        for (i = 0; i < 16u; i++)
            ok &= (b[i].note + 12u - a[i].note) % 12u == 10u && b[i].flags == a[i].flags;
        ok &= tb3_cfg[0].root == 9u;               /* (runtime only: the project's ROOT stays) */
        tb3_cfg[0].rootfol = 4;
        c->step[0].note[1] = 59;                    /* (B under the E now: the lowest) */
        tb3_bar(t, 0);
        ok &= tb3_rt[0].fol == 11u;
        bad += check("link: ROOT< follows the source track's lowest note at each bar, the ROOT setting stays", ok);
    }
    return bad;
}

/* 14. Review fixes (after Phase 5): POCKET after a MUTATE, ROOT< for new lines, TWIST once stopped, A<>B's number,
 * M.TYPE with JOIN, LINK's footer */
static int review(void)
{
    int bad = 0, ok;
    uint32_t i, on, p;
    track_t *t, *d, *c;
    tb3_step_t b[NSTEP], e[NSTEP];

    ui_power_on();                                  /* POCKET: a MUTATE's new notes, VARY's too */
    t = &trk[0];
    d = &trk[1];
    song.sel = 0;
    set_engine_of(t, ENGI_303);
    set_engine_of(d, ENGI_DRUM);
    d->p[P_SLEN] = 16;
    for (i = 0; i < 16u; i++)
        kick_on(d, i, !(i & 3u));
    ok = 1;
    for (p = 0; p < 3u; p++) {
        tb3_cfg[0].density = 90;
        tb3_cfg[0].mtype = (uint8_t)p;
        tb3_cfg[0].mutate_bars = 1;
        perf_line(t, 32, 0x2468u + p);
        tb3_cfg[0].pocket = 1;
        tb3_kick_take(0);
        tb3_live(t);
        for (i = 0; i < 6u; i++) {
            t->seq_idx = 0;
            tb3_bar(t, 0);
            tb3_read(t->step, b);
            for (on = 0; on < 32u; on++)
                ok &= (on & 3u) || !tb3_gated(&b[on]);
        }
        tb3_cfg[0].pocket = 0;
    }
    kick_on(d, 6, 1);                               /* the drums edited after MUTATEs: followed */
    tb3_cfg[0].pocket = 1;
    tb3_cfg[0].mtype = 0;
    fm1_ms += 1100u;
    tb3_poll();
    tb3_read(t->step, b);
    ok &= !tb3_gated(&b[6]) && !tb3_gated(&b[22]);
    tb3_cfg[0].pocket = 0;
    bad += check("review: POCKET holds after auto MUTATEs (DRIFT, VARY, TURN) and follows the drums after them", ok);

    ui_power_on();                                  /* ROOT<: a new line, a MUTATE in the followed key */
    t = &trk[0];
    c = &trk[3];
    song.sel = 0;
    set_engine_of(t, ENGI_303);
    set_engine_of(c, 0);
    c->p[P_SLEN] = 16;
    for (i = 0; i < 16u; i++)
        c->step[i].time = ST_REST, c->step[i].n = 0;
    c->step[0].time = ST_NOTE, c->step[0].n = 1, c->step[0].note[0] = 64;
    tb3_cfg[0].root = 9;
    tb3_cfg[0].rootfol = 4;
    tb3_rt[0].fol = 4;
    perf_line(t, 16, 0x1234u);
    tb3_poll();
    tb3_read(t->step, b);
    {
        tb3_cfg_t g = tb3_cfg[0];
        uint32_t m = 0, r = 0, sc = 0;
        g.root = 4;
        tb3_gen(&g, e, 16);
        ok = tb3_cfg[0].root == 9u;
        for (i = 0; i < 16u; i++)
            ok &= !tb3_gated(&b[i]) || (b[i].note == e[i].note && b[i].flags == e[i].flags);
        for (i = 0; i < TB3_SCALES[g.scale].len; i++)
            sc |= 1u << TB3_SCALES[g.scale].deg[i];
        for (p = 0; p < 20u; p++)
            tb3_apply(t, 0, &r);
        tb3_poll();
        tb3_read(t->step, b);
        for (i = 0; i < 16u; i++)
            if (tb3_gated(&b[i]))
                m |= 1u << ((b[i].note + 120u - 4u) % 12u);
        ok &= !(m & ~sc);
    }
    bad += check("review: ROOT< following: a new line and 20 MUTATEs in the followed key, ROOT as it was", ok);

    ui_power_on();                                  /* TWIST: nothing left once stopped or off */
    t = &trk[0];
    set_engine_of(t, ENGI_303);
    tb3_cfg[0].twist = 100;
    song.playing = 1;
    for (i = 0; i < 10u; i++) {
        t->seq_idx = (uint16_t)i;
        tb3_twist(t, !i);
    }
    ok = tb3_tw[0] != 0;
    tb3_poll();
    ok &= tb3_tw[0] != 0;                           /* (playing, on: as seq_enter set it) */
    tb3_cfg[0].twist = 0;
    tb3_poll();
    ok &= tb3_tw[0] == 0;
    tb3_cfg[0].twist = 100;
    tb3_twist(t, 0);
    song.playing = 0;
    tb3_poll();
    ok &= tb3_tw[0] == 0;
    bad += check("review: TWIST leaves no cutoff offset once stopped or turned off", ok);

    ui_power_on();                                  /* A<>B: B has no number */
    t = TSEL;
    set_engine(ENGI_303);
    perf_line(t, 16, 0xABCDu);
    tb3_cfg[0].line = 4711;
    while (cur_page()->graph != GR_TB3PLY)
        press(B_ARP);
    press(B_OCTDN);
    frame();
    ok = tb3_cfg[0].line == 0u;
    bad += check("review: A<>B: the header no longer shows A's number for B", ok);

#if AICD_ON
    ui_power_on();                                  /* M.TYPE VARY in AICD with JOIN: in the pass, not the main loop's */
    t = TSEL;
    set_engine(ENGI_303);
    tb3_cfg[0].mode = TB3_AICD;
    tb3_cfg[0].join = 1;
    tb3_cfg[0].mtype = 1;
    tb3_cfg[0].mutate_bars = 1;
    perf_line(t, 16, 0x5555u);
    tb3_read(t->step, e);
    for (p = 0; p < 8u && !tb3_rt[0].varied; p++) {
        tb3_pass(t, 0);
        if (!tb3_rt[0].varied)
            tb3_pass(t, 0);
    }
    ok = tb3_rt[0].varied && !tb3_rt[0].want;
    tb3_pass(t, 0);
    tb3_read(t->step, b);
    ok &= !memcmp(b, e, sizeof b);
    bad += check("review: M.TYPE VARY works in AICD with JOIN on (the variation, then the line back)", ok);
#endif

    ui_power_on();                                  /* LINK's footer: NEW GROOVE only with drums to roll */
    song.sel = 0;
    set_engine_of(&trk[0], ENGI_303);
    for (i = 1; i < NTRK; i++)
        set_engine_of(&trk[i], 0);
    ok = !tb3_groove_ok(0);
    set_engine_of(&trk[2], ENGI_DRUM);
    ok &= tb3_groove_ok(0) == DBRAIN_ON;
    bad += check("review: LINK's OCT+ says NEW GROOVE only with a DRUM track (and DrumBrain built)", ok);

    ui_power_on();                                  /* (2nd pass) a line waiting at STOP lands while the menu is open */
    t = TSEL;
    set_engine(ENGI_303);
    perf_line(t, 16, 0x3131u);
    song.playing = 1;
    press(B_ARP);
    press(B_OCTUP);
    ok = tb3_next[0].state == TB3Q_READY;
    song.playing = 0;
    ui.menu = 1;
    frame();
    ok &= tb3_next[0].state == TB3Q_NONE && line_is(t, &tb3_cfg[0]);
    ui.menu = 0;
    bad += check("review: a line waiting when the transport stops is written even with the menu open", ok);

    ui_power_on();                                  /* (2nd pass) undo after A<>B: line B back from its seed */
    t = TSEL;
    set_engine(ENGI_303);
    perf_line(t, 16, 0x4444u);
    while (cur_page()->graph != GR_TB3PLY)
        press(B_ARP);
    press(B_OCTUP);                                 /* a line B */
    frame();
    {
        uint32_t sa = tb3_cfg[0].seed, sb = tb3_cfg[0].seed_b;
        tb3_step_t lb[NSTEP], la[NSTEP];
        memcpy(lb, tb3_b[0], sizeof lb);
        tb3_read(t->step, la);
        press(B_OCTDN);                             /* A <> B */
        frame();
        ok = tb3_cfg[0].seed == sb && tb3_cfg[0].seed_b == sa;
        undo_swap();
        frame();
        tb3_read(t->step, b);
        ok &= tb3_cfg[0].seed == sa && tb3_cfg[0].seed_b == sb && !memcmp(b, la, sizeof b);
        tb3_cfg[0].morph = 1;                       /* (B is drawn again on demand: the main loop) */
        tb3_poll();
        ok &= tb3_rt[0].bok && !memcmp(tb3_b[0], lb, sizeof lb);
    }
    bad += check("review: undo after A<>B brings back line A and line B (from its seed)", ok);

    ui_power_on();                                  /* (2nd pass) SEED: 0 is no number; turning down to it dials nothing */
    t = TSEL;
    set_engine(ENGI_303);
    perf_line(t, 16, 0x7070u);
    tb3_cfg[0].line = 1;
    press(B_ARP);
    press(B_ARP);
    press(B_ARP);
    tb3_read(t->step, e);
    turn(EN_K2, -1);
    tb3_read(t->step, b);
    ok = cur_page()->graph == GR_TB3MOD && tb3_cfg[0].line == 1u && !memcmp(b, e, sizeof b);
    bad += check("review: SEED shows 0 for a line without a number; turning to 0 dials nothing", ok);
    return bad;
}

int main(int argc, char **argv)
{
    int bad = pieces() + same_lines() + steps() + ui_tb3() + auto_mutate() + live() + project() + fixes() + workflow() + acid() + aicd_shape() + perf() + linked() + review();
#if AICD_ON
    bad += aicd_model(argc > 1 ? argv[1] : 0) + aicd_draws() + aicd_lines() + aicd_mut() + aicd_ui();
#else
    (void)argc, (void)argv;
#endif
    printf("%s\n", bad ? "TB-3PO TEST FAILED" : "tb3po: all passed");
    return bad != 0;
}
