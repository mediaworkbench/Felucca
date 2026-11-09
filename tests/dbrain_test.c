/* SPDX-License-Identifier: GPL-3.0-only
 * Part of Felucca: free software under the GNU General Public License, version 3 only (see LICENSE) */
/* DrumBrain (firmware/src/dbrain.c, ui_dbrain.c), on the real UI sources (the stubs of tests/ui_test.c):
 * 1. the model in flash against tools/gen_dbrain.py's exact one: every context and its weights; no interior node is
 *    one; the longest-end lookup on random text against a naive search over the contexts.
 * 2. the draw's odds (TEMP 100 % the model's; lower narrower, higher wider); db_gen against a plain port of DrumBrain's
 *    generate() (the same steps: the - between beats, CONTEXT, a kept start); the sources (TOM, BELL by kit); DENS
 *    (100 % nothing, the keep counts, the weak hits first, AC not thinned); the cost.
 * 3. the steps: only LANES written, the rest of a step kept; the velocity rule; repeated to LEN; VARY's first half
 *    kept; a step that keeps a hit keeps its CHANCE and RATCH; automation and locks kept, a lock on an emptied step gone;
 *    IRQs off a step at a time; in a song the graph draws the slot's steps.
 * 4. the UI: ARP on a DRUM track ARP, ARP 2, BRAIN, BRAIN 2; other engines none; OCT+ / OCT-, not the octave; the knobs
 *    and NEW's star; undo (the seed, the TOM source and the star with it); refused in a song.
 * 5. a project: the settings back with the init FM6 patch; unused settings keep the track's FM6 patch; another FM6 area
 *    the defaults; pack / unpack; a knob alone is a change to autosave and PROJECT LOAD (DrumBrain's, TB-3PO's).
 * Built and run by tests/run_tests.sh (argv[1] gen_dbrain.py's reference, build/host/dbrain_ref.txt); again with
 * FELUCCA_DBRAIN=0 (no pages). */
#define UI_TEST_NO_MAIN 1
#define HOST_IRQ_STUBS 1
static int irq_depth, irq_max, irq_sections;      /* db_write's critical sections */
static void fm1_irq_off(void) { if (++irq_depth > irq_max) irq_max = irq_depth; irq_sections++; }
static void fm1_irq_on(void) { irq_depth--; }
#include "ui_test.c"
#ifdef __APPLE__
#include <libproc.h>
#include <sys/resource.h>
#endif

static uint32_t lcg = 0x2545F491u;
static uint32_t rnd(uint32_t n) { lcg = lcg * 1664525u + 1013904223u; return (lcg >> 8) % n; }

#if DBRAIN_ON
/* ------------------------------------------------------- 1. the model --- */
#define MAXCTX 6200
#define SYMS "0125-^"
typedef struct { uint8_t t, n, sym[40], w[4]; double p[4]; } ctx_t;
static ctx_t ctxs[MAXCTX];
static int nctx;

static int load_ref(const char *path)
{
    FILE *f = fopen(path, "r");
    char c[64];
    int t, w[4];
    double p[4];
    nctx = 0;
    if (!f)
        return 0;
    while (fscanf(f, "%d %63s %d %d %d %d %lf %lf %lf %lf", &t, c, &w[0], &w[1], &w[2], &w[3], &p[0], &p[1], &p[2], &p[3]) == 10) {
        ctx_t *x = &ctxs[nctx++];
        uint32_t k, n = c[0] == '.' ? 0u : (uint32_t)strlen(c);
        x->t = (uint8_t)t;
        x->n = (uint8_t)n;
        for (k = 0; k < n; k++)
            x->sym[k] = (uint8_t)(strchr(SYMS, c[k]) - SYMS);
        for (k = 0; k < 4u; k++) {
            x->w[k] = (uint8_t)w[k];
            x->p[k] = p[k];
        }
    }
    fclose(f);
    return nctx;
}
/* DrumBrain's lookup, plainly: the longest end of text, CONTEXT characters at most, in the table's contexts */
static const ctx_t *naive(uint32_t t, const uint8_t *text, uint32_t len, uint32_t ctx)
{
    uint32_t i = len > ctx ? len - ctx : 0u;
    int k;
    for (; i <= len; i++)
        for (k = 0; k < nctx; k++)
            if (ctxs[k].t == t && ctxs[k].n == len - i && !memcmp(ctxs[k].sym, text + i, len - i))
                return &ctxs[k];
    return 0;
}
static int model(const char *ref)
{
    int bad = 0, k, ok = 1, nd = 0;
    uint32_t i, n;
    if (!load_ref(ref))
        return check("dbrain model: the reference (argv[1]) read", 0);
    for (k = 0; k < nctx; k++) {
        const ctx_t *x = &ctxs[k];
        uint32_t d = db_walk(x->t, x->sym, x->n);
        ok &= d != 0u && d <= DB_NDIST && !memcmp(DB_DIST[d - 1u], x->w, 4);
    }
    n = DB_ROOT[DB_NTABLE];
    for (i = 0; i < n; i++)
        nd += (DB_NODE[i] >> 6) != 0u;
    bad += check("dbrain model: every context of the model in flash, its weights exact", ok);
    bad += check("dbrain model: as many nodes with a distribution as contexts (an interior node is none)", nd == nctx);
    ok = 1;
    for (i = 0; i < 1500u; i++) {
        const ctx_t *base = &ctxs[rnd((uint32_t)nctx)], *w;
        uint8_t text[64];
        uint32_t len = base->n, extra = rnd(7), ctx = 1u + rnd(32);
        memcpy(text, base->sym, len);
        while (extra--)
            text[len++] = (uint8_t)rnd(6);
        w = naive(base->t, text, len, ctx);
        ok &= w && !memcmp(DB_DIST[db_find(base->t, text, len, ctx) - 1u], w->w, 4);
    }
    bad += check("dbrain model: the longest end of random text, as a search over the contexts finds it", ok);
    return bad;
}

/* ---------------------------------------------------- 2. the generator --- */
static int odds(void)
{
    int bad = 0, k, ok = 1, seen = 0;
    uint32_t r = 77u;
    for (k = 0; k < nctx && seen < 200; k++) {
        const ctx_t *x = &ctxs[k];
        uint32_t c[4] = {0, 0, 0, 0}, i;
        if ((x->w[0] == 0u) + (x->w[1] == 0u) + (x->w[2] == 0u) + (x->w[3] == 0u) == 4u)
            continue;
        seen++;
        for (i = 0; i < 8000u; i++)
            c[db_draw(db_walk(x->t, x->sym, x->n), 100, &r)]++;
        for (i = 0; i < 4u; i++)
            ok &= fabs(c[i] / 8000.0 - x->p[i]) < 0.03 && (x->p[i] > 0 || !c[i]);
    }
    bad += check("dbrain draw: TEMP 100 % the model's odds, a step it never had never drawn", ok && seen == 200);
    {                                                /* a branching context: its likeliest step's share by TEMP */
        const ctx_t *x = 0;
        uint32_t temps[3] = {25, 100, 200}, c[3], i, j, top = 0;
        for (k = 0; k < nctx && !x; k++)
            if (ctxs[k].w[0] != 255u && ctxs[k].w[1] != 255u && ctxs[k].w[0] != ctxs[k].w[1] && ctxs[k].w[0] + ctxs[k].w[1] > 8)
                x = &ctxs[k];
        for (i = 0; i < 4u; i++)
            if (x->w[i] == 0u) top = i;
        for (j = 0; j < 3u; j++) {
            c[j] = 0;
            for (i = 0; i < 40000u; i++)
                c[j] += db_draw(db_walk(x->t, x->sym, x->n), temps[j], &r) == top;
        }
        bad += check("dbrain draw: TEMP lower narrower, higher wider", c[0] > c[1] && c[1] > c[2]);
    }
    return bad;
}
/* DrumBrain's generate() as it is, on our lookup's plain twin and our draw */
static void port(uint32_t t, uint32_t ctx, uint32_t temp, const uint8_t *kept, uint32_t nkeep, uint8_t *out, uint32_t n, uint32_t *r)
{
    uint8_t text[64];
    uint32_t len = 1, s = 0, i;
    static const uint8_t VAL[4] = {0, 1, 2, 3};
    text[0] = 5;                                     /* ^ */
    for (; s < nkeep; s++) {                         /* getTextFromMidi */
        if (s && s % 4u == 0u)
            text[len++] = 4;
        out[s] = kept[s];
        text[len++] = kept[s];
    }
    while (s < n) {
        const ctx_t *w;
        uint32_t d;
        if ((len - 1u) % 5u == 4u) {
            text[len++] = 4;
            continue;
        }
        w = naive(t, text, len, ctx);
        d = db_walk(t, w->sym, w->n);
        i = db_draw(d, temp, r);
        out[s++] = VAL[i];
        text[len++] = (uint8_t)i;
    }
}
static int same_as_port(void)
{
    uint32_t i, ok = 1;
    db_cfg_t g;
    db_defaults(&g);
    for (i = 0; i < 400u; i++) {
        uint8_t a[DB_NMAX], b[DB_NMAX], kept[DB_NMAX];
        uint32_t t = rnd(DB_NTABLE), n = 1u + rnd(DB_NMAX), nk = rnd(2) ? rnd(n + 1u) : 0u, k, r0 = 1u + rnd(0xFFFFFFu), r1 = r0, r2 = r0;
        g.ctx = (uint8_t)(1u + rnd(32));
        g.temp = (uint8_t)(10u + rnd(191));
        for (k = 0; k < nk; k++)
            kept[k] = (uint8_t)rnd(3);
        db_gen(&g, t, kept, nk, a, n, &r1);
        port(t, g.ctx, g.temp, kept, nk, b, n, &r2);
        ok &= !memcmp(a, b, n) && r1 == r2;
    }
    return check("dbrain generate: step for step DrumBrain's generate() (the -, CONTEXT, TEMP, a kept start)", (int)ok);
}
static int sources(void)
{
    int ok = 1;
    db_cfg_t g;
    track_t *t = &trk[0];
    ui_power_on();
    set_engine_of(t, ENGI_DRUM);
    db_defaults(&g);
    g.style = DB_BREAKS;
    t->p[P_E0] = DK_STD;
    ok &= db_table(&g, t, DV_KICK) == 2u * 12u + 1u && db_table(&g, t, DV_SNARE) == 2u * 12u + 2u &&
          db_table(&g, t, DV_CLAP) == 2u * 12u + 7u && db_table(&g, t, DV_HATC) == 2u * 12u + 11u &&
          db_table(&g, t, DV_HATO) == 2u * 12u + 10u && db_table(&g, t, DV_RIM) == 2u * 12u + 6u;
    ok &= db_table(&g, t, DV_BELL) == 2u * 12u + 8u;
    t->p[P_E0] = DK_10;                              /* a kit whose BELL lane plays a cymbal */
    ok &= db_table(&g, t, DV_BELL) == 2u * 12u + 9u;
    for (g.tom = 0; g.tom < 3u; g.tom++)
        ok &= db_table(&g, t, DV_TOM) == 2u * 12u + 3u + g.tom;
    return check("dbrain sources: each lane's instrument, TOM one of LT MT HT, BELL CB or (a cymbal kit) CY", ok);
}
static int thin(void)
{
    int bad = 0, ok = 1;
    uint32_t i;
    for (i = 0; i < 3000u; i++) {
        uint8_t a[DB_NMAX], b[DB_NMAX];
        uint32_t n = 1u + rnd(DB_NMAX), from = rnd(2) ? rnd(n + 1u) : 0u, dens = 10u + rnd(91), k, hits = 0, left = 0, r = 1u + rnd(99999u), r0 = r;
        for (k = 0; k < n; k++)
            a[k] = (uint8_t)(rnd(3) ? rnd(4) : 0u);
        memcpy(b, a, sizeof a);
        db_thin(b, from, n, 100, &r);
        ok &= !memcmp(a, b, n) && r == r0;           /* (100 %: not a draw) */
        db_thin(b, from, n, dens, &r);
        for (k = 0; k < from; k++)
            ok &= a[k] == b[k];
        for (k = from; k < n; k++) {
            hits += a[k] != 0u;
            left += b[k] != 0u;
            ok &= !b[k] || b[k] == a[k];
        }
        ok &= left == (hits * dens + 99u) / 100u;
    }
    bad += check("dbrain DENS: 100 % nothing (and no draw); the keep count exact, the kept start and the kept hits as they were", ok);
    {                                                /* the strong places stay */
        uint8_t a[16];
        uint32_t r = 5u;
        memset(a, 1, sizeof a);
        db_thin(a, 0, 16, 25, &r);
        bad += check("dbrain DENS: the weakest go first (25 % of 16: the beats)",
                     a[0] && a[4] && a[8] && a[12] && !a[1] && !a[3] && !a[2]);
    }
    {
        track_t *t = &trk[0];
        db_pat_t p;
        uint32_t ac[2] = {0, 0}, hit[2] = {0, 0}, k, i2, r;
        ui_power_on();
        set_engine_of(t, ENGI_DRUM);
        t->p[P_SLEN] = 16;
        db_defaults(&db_cfg[0]);
        for (k = 0; k < 2u; k++) {
            db_cfg[0].dens = k ? 25 : 100;
            for (i = 0; i < 600u; i++) {
                r = 1u + i * 7919u;
                db_line(t, 0, &r, &p);
                for (i2 = 0; i2 < p.n; i2++) {
                    ac[k] += p.ac[i2];
                    hit[k] += (uint32_t)__builtin_popcount(p.hit[i2]);
                }
            }
        }
        bad += check("dbrain DENS: the hits thinned, AC not", hit[1] * 2u < hit[0] && ac[1] * 10u > ac[0] * 8u && ac[1] * 10u < ac[0] * 12u);
    }
    return bad;
}
static uint64_t instr_now(void)                   /* (tests/regress.c's) */
{
#ifdef __APPLE__
    struct rusage_info_v4 ri;
    if (!proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&ri))
        return ri.ri_instructions;
#endif
    return 0;
}
static int cost_and_seed(void)
{
    int bad = 0;
    track_t *t = &trk[0];
    db_pat_t a, b;
    uint32_t r, r2, i;
    uint64_t i0;
    ui_power_on();
    set_engine_of(t, ENGI_DRUM);
    t->p[P_SLEN] = 32;
    db_defaults(&db_cfg[0]);
    db_cfg[0].lanes = DB_L_ALL;
    db_cfg[0].style = DB_ALL;
    r = db_start(&db_cfg[0], 1, 0x1234u);
    r2 = db_start(&db_cfg[0], 1, 0x1234u);
    bad += check("dbrain seed: a new pattern's stream from its seed", r == r2 && db_cfg[0].seed != 0u);
    db_line(t, 0, &r, &a);
    db_line(t, 0, &r2, &b);
    bad += check("dbrain seed: the same seed the same pattern", !memcmp(&a, &b, sizeof a));
    r = 0;
    r = db_start(&db_cfg[0], 0, db_cfg[0].seed);
    bad += check("dbrain seed: a VARY's stream never 0 (xorshift)", r != 0u);
    i0 = instr_now();
    for (i = 0; i < 20u; i++) {
        r = 1u + i;
        db_line(t, 0, &r, &a);
    }
    printf("dbrain: a pattern of ALL lanes, 32 steps, CONTEXT 32: %llu instructions on the host\n", (unsigned long long)((instr_now() - i0) / 20u));
    return bad;
}

/* ---------------------------------------------------- 3. the steps --- */
static void blank(track_t *t)
{
    uint32_t i;
    for (i = 0; i < NSTEP; i++) {
        memset(&t->step[i], 0, sizeof t->step[i]);
        t->step[i].time = ST_REST;
    }
}
static void pat_fill(db_pat_t *p, uint32_t n, uint32_t hit, uint32_t acc)
{
    uint32_t i;
    memset(p, 0, sizeof *p);
    p->n = (uint8_t)n;
    for (i = 0; i < n; i++) {
        p->hit[i] = (uint8_t)hit;
        p->acc[i] = (uint8_t)acc;
        p->ac[i] = (uint8_t)(i & 1u);
    }
}
static int steps(void)
{
    int bad = 0, ok = 1;
    track_t *t = &trk[0];
    db_pat_t p;
    uint32_t i, r, mask;
    uint64_t was;
    ui_power_on();
    set_engine_of(t, ENGI_DRUM);
    t->p[P_SLEN] = 16;

    blank(t);
    for (i = 0; i < 16u; i++) {                      /* a kick and a crash (a note on no lane of the set) on every step */
        t->step[i] = (step_t){{49}, 1, ST_NOTE, 0, 77, 1u, 1u, 50};
    }
    db_defaults(&db_cfg[0]);
    db_cfg[0].lanes = DB_L_HATS;
    mask = DB_LANE_MASK[DB_L_HATS];
    r = 3u;
    db_line(t, 0, &r, &p);
    db_write(t, &p, mask);
    for (i = 0; i < 16u; i++) {
        const step_t *s = &t->step[i];
        ok &= s->n == 1u && s->note[0] == 49u && (s->hit & 1u) && (s->acc & 1u) && s->vel == 77u && s->probability == 50u;
        ok &= (s->hit & mask) == (p.hit[i] & mask) && !(s->acc & ~s->hit);
    }
    bad += check("dbrain steps: LANES HATS writes the hats alone; the kick, its accent, a note, VEL and CHANCE stay", ok);

    ok = 1;
    blank(t);
    pat_fill(&p, 16, 1u, 0u);
    db_write(t, &p, DB_LANE_MASK[DB_L_BEAT]);
    for (i = 0; i < 16u; i++)
        ok &= t->step[i].time == ST_NOTE && t->step[i].hit == 1u && t->step[i].vel == (i & 1u ? 112u : 88u);
    bad += check("dbrain steps: AC the step's velocity (112, else 88) on a step of the set's hits", ok);

    ok = 1;
    blank(t);
    t->p[P_SLEN] = 40;
    db_cfg[0].lanes = DB_L_BEAT;
    db_cfg[0].bars = 1;
    r = 9u;
    db_line(t, 0, &r, &p);
    db_write(t, &p, DB_LANE_MASK[DB_L_BEAT]);
    for (i = 0; i < 40u; i++)
        ok &= t->step[i].hit == t->step[i % 16u].hit;
    for (i = 40u; i < NSTEP; i++)
        ok &= t->step[i].time == ST_REST && !t->step[i].hit;
    bad += check("dbrain steps: N repeated to LEN, nothing past it", ok && p.n == 16u);

    {
        step_t before[32];
        ok = 1;
        t->p[P_SLEN] = 64;
        db_cfg[0].bars = 2;
        r = 21u;
        blank(t);
        db_line(t, 0, &r, &p);
        db_write(t, &p, DB_LANE_MASK[DB_L_BEAT]);
        memcpy(before, t->step, sizeof before);
        r = db_start(&db_cfg[0], 0, 0x777u);
        db_line(t, 1, &r, &p);
        db_write(t, &p, DB_LANE_MASK[DB_L_BEAT]);
        ok &= p.n == 32u && p.from == 16u && !memcmp(t->step, before, 16u * sizeof(step_t));
        for (i = 32u; i < 64u; i++)
            ok &= t->step[i].hit == t->step[i - 32u].hit && (i - 32u >= 16u || t->step[i].vel == before[i - 32u].vel);
        bad += check("dbrain steps: VARY keeps the first half of N (its repeats too)", ok);
    }

    ok = 1;
    blank(t);
    t->p[P_SLEN] = 16;
    for (i = 0; i < 16u; i++)
        t->step[i] = (step_t){{0}, 0, ST_NOTE, (uint8_t)(1u << SF_RATCH_SH), 90, 1u, 0, 50};   /* a kick, RATCH x2, CHANCE 50 */
    pat_fill(&p, 16, 2u, 0u);                        /* the kick off, the snare on */
    db_write(t, &p, DB_LANE_MASK[DB_L_BEAT]);
    for (i = 0; i < 16u; i++)
        ok &= t->step[i].hit == 2u && step_ratchet(&t->step[i]) == 2u && t->step[i].probability == 50u;
    bad += check("dbrain steps: a step that keeps a hit keeps its CHANCE and RATCH when the hit changes lane", ok);

    ok = 1;
    blank(t);
    for (i = 0; i < 16u; i++)
        t->step[i] = (step_t){{0}, 0, ST_NOTE, 0, 90, i < 8u ? 1u : 8u, 0, 50};   /* lanes KICK / HATC */
    motion_reset(t);
    ok &= !motion_set_lock(t, 2, P_E0, 11) && !motion_set_lock(t, 10, P_E1, 22) && !motion_set_event(t, 3, P_REV, 100);
    pat_fill(&p, 16, 1u, 0u);                        /* HATS alone, none: steps 8..15 empty, a kick stays on 0..7 */
    p.hit[0] = 0;
    was = tl_used(t, 16);
    db_write(t, &p, DB_LANE_MASK[DB_L_HATS]);
    tl_unlock(t, was, 16);
    {
        int16_t v;
        ok &= motion_lock_get(t, 2, P_E0, &v) && v == 11;
        ok &= !motion_lock_get(t, 10, P_E1, &v);
        ok &= motion_count(t) == 2u;
        ok &= t->step[10].time == ST_REST && t->step[2].hit == 1u;
    }
    bad += check("dbrain steps: automation and locks stay; a lock on a step that ends empty goes (as SEQ TOOLS')", ok);

    blank(t);
    t->p[P_SLEN] = 24;
    pat_fill(&p, 16, 3u, 1u);
    p.from = 4;
    irq_max = irq_sections = irq_depth = 0;
    db_write(t, &p, DB_LANE_MASK[DB_L_BEAT]);
    bad += check("dbrain steps: IRQs off one step at a time, every step written, none left off",
                 irq_sections == 24 - 4 && irq_max == 1 && irq_depth == 0);

    {                                                /* in a song: the graph's the slot's steps, as the sequencer's */
        static uint16_t song_px[CV_MAX];
        uint32_t k = trk_index(t), n;
        blank(t);
        t->p[P_SLEN] = 16;
        memset(chain.source[0].step[k], 0, sizeof chain.source[0].step[k]);
        for (i = 0; i < 16u; i++) {
            chain.source[0].step[k][i].time = ST_NOTE;
            chain.source[0].step[k][i].hit = (uint8_t)(i % 2u ? 1u : 8u);
        }
        chain.running = 1;
        chain.lane[k] = 0;
        cv_begin(240, 120, T_BG);
        graph_dbrain(t, T_TEXT);
        n = (uint32_t)cv_w * cv_h;
        memcpy(song_px, cv_px, n * sizeof cv_px[0]);
        chain.running = 0;
        memcpy(t->step, chain.source[0].step[k], sizeof chain.source[0].step[k]);
        cv_begin(240, 120, T_BG);
        graph_dbrain(t, T_TEXT);
        ok = !memcmp(song_px, cv_px, n * sizeof cv_px[0]);
        blank(t);
        cv_begin(240, 120, T_BG);
        graph_dbrain(t, T_TEXT);
        ok &= memcmp(song_px, cv_px, n * sizeof cv_px[0]) != 0;
        bad += check("dbrain graph: in a song the slot's steps (what plays), not the track's", ok);
    }
    return bad;
}
#endif

/* ------------------------------------------------------- 4. the UI --- */
static int ui_db(void)
{
    int bad = 0, ok;
    track_t *t;
    ui_power_on();
    t = TSEL;
    set_engine(ENGI_DRUM);
    frame();
    press(B_ARP);
    ok = str_eq(cur_page()->title, "ARP");
    press(B_ARP);
    ok &= str_eq(cur_page()->title, "ARP 2");
#if DBRAIN_ON
    press(B_ARP);
    ok &= str_eq(cur_page()->title, "BRAIN") && cur_page()->graph == GR_DB1;
    press(B_ARP);
    ok &= str_eq(cur_page()->title, "BRAIN 2") && cur_page()->graph == GR_DB2;
    press(B_ARP);
    ok &= str_eq(cur_page()->title, "ARP");
    bad += check("dbrain ui: ARP on a DRUM track: ARP, ARP 2, BRAIN, BRAIN 2, ARP", ok);
    press(B_ARP); press(B_ARP);
    set_engine(0);                                   /* on BRAIN: another engine has none */
    frame();
    ok = str_eq(cur_page()->title, "ARP");
    press(B_ARP);
    ok &= str_eq(cur_page()->title, "ARP 2");
    press(B_ARP);
    ok &= str_eq(cur_page()->title, "ARP");
    bad += check("dbrain ui: another engine: ARP, ARP 2 only", ok);
#if T303_ON
    set_engine(ENGI_303);
    frame();
    ok = cur_page()->graph == GR_TB3GEN;
    press(B_ARP);
    ok &= cur_page()->graph == GR_TB3SCL;
    bad += check("dbrain ui: a 303 track: TB-3PO's pages as before", ok);
#endif

    ui_power_on();
    t = TSEL;
    set_engine(ENGI_DRUM);
    frame();
    t->p[P_SLEN] = 16;
    press(B_ARP); press(B_ARP); press(B_ARP);
    {
        step_t before[NSTEP], after[NSTEP];
        uint32_t oct = (uint32_t)song.octave;
        int any = 0, i;
        ok = cur_page()->graph == GR_DB1 && db_stale[0] == 0u;
        turn(EN_K2, 3);
        ok &= db_cfg[0].temp == 103u && db_stale[0] == 1u;
        turn(EN_K3, -40);
        ok &= db_cfg[0].dens == 60u;
        turn(EN_K4, -2);
        ok &= db_cfg[0].ctx == 30u;
        turn(EN_K1, -1);
        ok &= db_cfg[0].style == DB_ALL;
        press(B_ARP);
        turn(EN_K1, 2);
        ok &= db_cfg[0].lanes == DB_L_ALL;
        turn(EN_K2, -1);
        ok &= db_cfg[0].bars == 1u;
        bad += check("dbrain ui: the knobs set STYLE TEMP DENS CONTEXT, LANES BARS; NEW's star", ok);
        db_defaults(&db_cfg[0]);
        memcpy(before, t->step, sizeof before);
        press(B_OCTUP);
        for (i = 0; i < 16; i++)
            any |= t->step[i].hit != before[i].hit;
        ok = any && (uint32_t)song.octave == oct && db_cfg[0].seed != 0u && db_stale[0] == 0u && !strncmp(ui.msg, "NEW BEAT ", 9);
        bad += check("dbrain ui: OCT+ a new pattern (\"NEW BEAT xxxx\"), not the octave", ok);
        memcpy(after, t->step, sizeof after);
        undo_swap();
        ok = !memcmp(t->step, before, sizeof before);
        undo_swap();
        ok &= !memcmp(t->step, after, sizeof after);
        bad += check("dbrain ui: SAVE held undoes it and brings it back", ok);
        {                                            /* the steps' seed, TOM source and star go with them */
            uint32_t s1, s2;
            uint8_t tm1, tm2;
            db_cfg[0].tom = 1;
            db_stale[0] = 1;
            s1 = db_cfg[0].seed;
            tm1 = db_cfg[0].tom;
            do {                                     /* (until the new pattern's TOM source is another) */
                press(B_OCTUP);
                s2 = db_cfg[0].seed;
                tm2 = db_cfg[0].tom;
                if (tm2 == tm1) { undo_swap(); db_cfg[0].seed = s1; db_cfg[0].tom = tm1; db_stale[0] = 1; frames(32); }
            } while (tm2 == tm1);
            ok = db_stale[0] == 0u && s2 != s1;
            undo_swap();
            ok &= db_cfg[0].seed == s1 && db_cfg[0].tom == tm1 && db_stale[0] == 1u;
            undo_swap();
            ok &= db_cfg[0].seed == s2 && db_cfg[0].tom == tm2 && db_stale[0] == 0u;
            bad += check("dbrain ui: undo brings back the seed, the TOM source and NEW's star; again the new ones", ok);
            {                                        /* another action's undo leaves them */
                tl_undo_take(t);
                undo_swap();
                ok = db_cfg[0].seed == s2 && db_cfg[0].tom == tm2;
                bad += check("dbrain ui: another action's undo leaves them", ok);
                undo_swap();
            }
        }
        (void)motion_set_lock(t, 2, P_E0, 11);
        press(B_OCTDN);
        ok = !strcmp(ui.msg, "VARIED") && motion_count(t) == 1u && (uint32_t)song.octave == oct;
        bad += check("dbrain ui: OCT- a VARY, not the octave; the track's automation stays", ok);
        memcpy(before, t->step, sizeof before);
        chain.running = 1;
        press(B_OCTUP);
        chain.running = 0;
        ok = !memcmp(t->step, before, sizeof before);
        bad += check("dbrain ui: refused in a song", ok);
    }
    {
        ui_power_on();
        set_engine(ENGI_DRUM);
        frame();
        press(B_ARP); press(B_ARP);
        ok = str_eq(help_page() ? cur_page()->title : "", "ARP 2");
        ok &= strstr(help_page(), "BRAIN") != 0;
        bad += check("dbrain ui: ARP 2's hint on a DRUM track goes on to BRAIN", ok);
    }
#else
    press(B_ARP);
    ok &= str_eq(cur_page()->title, "ARP");
    bad += check("dbrain ui: a FELUCCA_DBRAIN=0 build: ARP, ARP 2 only", ok);
#endif
    return bad;
}

/* ---------------------------------------------------- 5. a project --- */
static int project(void)
{
    int bad = 0, ok;
    uint32_t i;
    uint8_t pk[FM6_PACKED], init[FP_SIZE + 1u], own[FP_SIZE + 1u];
    db_cfg_t g;
    ui_power_on();
    song.playing = 0;
    set_engine_of(&trk[0], ENGI_DRUM);
    db_cfg[0] = (db_cfg_t){DB_BREAKS, 150, 40, 7, DB_L_HATS, 1, 2, 0x0ABCDEF1u};
    g = db_cfg[0];
    set_engine_of(&trk[1], ENGI_DRUM);               /* no settings of its own: its FM6 area stays a patch */
    fm6_patch[1][3] ^= 0x15;
    memcpy(own, fm6_patch[1], FP_SIZE);
    ok = !project_save(2);
    db_reset();
    fm6_init();
    project_load(2);
    fm6_unpack(FM6_INIT, init);
    ok = ok && !memcmp(&db_cfg[0], &g, sizeof g) && !memcmp(fm6_patch[0], init, FP_SIZE);
    bad += check("dbrain project: a DRUM track's settings and seed back, its FM6 patch the init one", ok);
    bad += check("dbrain project: settings left alone: the track's own FM6 patch is kept", !memcmp(fm6_patch[1], own, FP_SIZE) && !db_touched(1));
    ok = 1;
    for (i = 0; i < 2000u; i++) {
        uint32_t k;
        db_cfg_t c = {(uint8_t)rnd(DB_NSTYLE), (uint8_t)(10u + rnd(191)), (uint8_t)(10u + rnd(91)), (uint8_t)(1u + rnd(32)),
                      (uint8_t)rnd(DB_NLANES), (uint8_t)(1u + rnd(2)), (uint8_t)rnd(3), (lcg ^ rnd(0xFFFFu)) & 0x7FFFFFFFu};
        db_cfg[0] = c;
        db_pack(0, pk);
        for (k = 0; k < FM6_PACKED; k++)
            ok &= pk[k] < 128u;
        db_reset();
        ok &= db_unpack(0, pk) == 1;
        ok &= !memcmp(&db_cfg[0], &c, sizeof c);
    }
    bad += check("dbrain project: pack / unpack round trip, every byte 0..127", ok);
    db_reset();
    db_cfg[0].style = DB_BREAKS;
    fm6_pack(fm6_patch[0], pk);
    ok = db_unpack(0, pk) == 0 && db_cfg[0].style == DB_HOUSE && !db_touched(0);
    bad += check("dbrain project: an FM6 patch in the area: the defaults", ok);

    ui_power_on();
    set_engine(ENGI_DRUM);
    frame();
    proj_kept_now();
    ok = !proj_changed();
    db_cfg[song.sel % NTRK].temp = 101;
    ok &= proj_changed() != 0;
    db_cfg[song.sel % NTRK].temp = 100;
    ok &= !proj_changed();                           /* (back to the power-on settings: the area a patch again) */
    db_cfg[song.sel % NTRK].style = DB_BREAKS;
    proj_kept_now();
    db_cfg[song.sel % NTRK].lanes = DB_L_HATS;
    ok &= proj_changed() != 0;
    bad += check("dbrain project: a knob alone is a change (autosave, PROJECT LOAD's question)", ok);
#if T303_ON
    ui_power_on();
    set_engine(ENGI_303);
    frame();
    proj_kept_now();
    ok = !proj_changed();
    tb3_cfg[song.sel % NTRK].temp = 150;
    ok &= proj_changed() != 0;
    bad += check("dbrain project: TB-3PO's knob alone too", ok);
#endif
    return bad;
}

int main(int argc, char **argv)
{
    int bad = 0;
#if DBRAIN_ON
    bad += model(argc > 1 ? argv[1] : "build/host/dbrain_ref.txt") + odds() + same_as_port() + sources() + thin() + cost_and_seed() + steps();
#else
    (void)argc, (void)argv;
#endif
    bad += ui_db() + project();
    printf("%s\n", bad ? "DRUMBRAIN TEST FAILED" : "dbrain: all passed");
    return bad != 0;
}
