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
 * 6. a project: a 303 track's settings and seed saved and loaded, its FM6 patch the init one, other tracks' kept.
 * 7. MODE AICD (1.6.2): the model in flash against tools/gen_aicd.py's exact one (every node's edges, the odds within
 *    rounding); a draw's odds those; Aicd's lines (gate, accent, slide, the scale's share as its own sampler's, 20000
 *    lines); the biases and TEMP each their way; every step a model step; a MUTATE a run of a quarter of the line at
 *    most; the cost; the UI (MODEL, the knobs, OCT+ / OCT-), auto MUTATE, a project; the same lines (a hash).
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
    c[5] = (uint8_t)rnd(TB3_NSCALES);
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
    ok &= cur_page()->graph == (AICD_ON ? GR_TB3MOD : GR_TB3GEN) && (!AICD_ON || str_eq(cur_page()->title, "MODEL"));
    if (AICD_ON)
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
    for (k = 0; k < 4u; k++)
        hx[k] = "0123456789ABCDEF"[(tb3_cfg[0].seed >> (12u - 4u * k)) & 15u];
    hx[4] = 0;
    ok = line_is(t, &tb3_cfg[0]) && !seq_is_empty(t) && song.octave == 0 && tb3_cfg[0].seed != 0xBEEFu &&
         !strncmp(ui.msg, "NEW LINE ", 9) && str_eq(ui.msg + 9, hx);
    bad += check("tb3po ui: OCT+ a new line (a new seed, \"NEW LINE xxxx\"), not the octave", ok);
    turn(EN_K1, 5);
    ok = tb3_cfg[0].density == 75u && tb3_rt[0].stale && line_is(t, &(tb3_cfg_t){70, 40, 25, 2, 9, 0, 1, 0,
                                                                                        tb3_cfg[0].seed});
    press(B_ARP);
    turn(EN_K4, 2);
    ok &= tb3_cfg[0].mutate_bars == 2u && cur_page()->graph == GR_TB3SCL;
    tb3_rt[0].stale = 0;
    turn(EN_K4, 1);
    ok &= tb3_cfg[0].mutate_bars == 3u && !tb3_rt[0].stale;
    turn(EN_K1, 3);
    ok &= tb3_cfg[0].root == 11u && tb3_rt[0].stale;   /* (A + 3: ROOT 0..11 does not wrap, B) */
    bad += check("tb3po ui: the knobs set up the next line (the line stays, NEW LINE's star; MUTATE no star)", ok);
    {
        step_t line[NSTEP];
        memcpy(line, t->step, sizeof line);
        press(B_OCTDN);
        ok = msg_is("MUTATED") && memcmp(line, t->step, sizeof line) && !tb3_rt[0].stale && song.octave == 0;
    }
    hold(B_SAVE);
    ok &= !memcmp(before, t->step, sizeof before);
    bad += check("tb3po ui: OCT- a MUTATE (not the octave); SAVE held undoes it (loads in a row: one undo)", ok);
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
    ok = ok && cfg_eq(&tb3_cfg[0], &g) && trk[0].eng_req == ENGI_303 && !memcmp(v, init, FP_SIZE) &&
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
        memcpy(line, t->step, sizeof line);
        press(B_OCTDN);
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

int main(int argc, char **argv)
{
    int bad = pieces() + same_lines() + steps() + ui_tb3() + auto_mutate() + project();
#if AICD_ON
    bad += aicd_model(argc > 1 ? argv[1] : 0) + aicd_draws() + aicd_lines() + aicd_mut() + aicd_ui();
#else
    (void)argc, (void)argv;
#endif
    printf("%s\n", bad ? "TB-3PO TEST FAILED" : "tb3po: all passed");
    return bad != 0;
}
