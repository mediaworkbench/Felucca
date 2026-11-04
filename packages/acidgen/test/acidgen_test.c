/* SPDX-License-Identifier: GPL-3.0-only
 * acidgen's host test (test/run.sh builds and runs it): Felucca's tests/tb3po_test.c, the parts that are the
 * generator's.
 * 1. TB-3PO's integer port: the thresholds and acid_pick against the float product (every 24-bit draw, lengths 1..7);
 *    X0X's defaults and new seeds; 40000 lines and 3000 MUTATE chains equal to X0X's float code (test/ref/).
 * 2. AICD: the model in the tables == tools/gen_aicd.py's exact one (argv[1], its reference: every node's edges, the
 *    odds within rounding); a draw's odds those; the lines as Aicd's own sampler's (gate, accent, slide, the share in
 *    A minor, 20000 lines); the biases and TEMP each their way; every step a model step; a MUTATE a run of a quarter.
 * 3. Both: the self-test's hashes (the same lines as Felucca), the settings' bytes (Felucca's layout), a line's rules
 *    (all ACID_NSTEP written, REST past len, never all REST, no SLD into a REST).
 * Built with -ffp-contract=off (the reference's floats as X0X's). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/acidgen.c"
#include "../src/acidgen_selftest.c"

void ref_generate(const uint8_t *cfg, uint32_t seed, int len, uint8_t *notes, uint8_t *flags);
void ref_mutate(const uint8_t *cfg, uint32_t seed, int len, uint8_t *notes, uint8_t *flags, uint32_t *rng);
uint32_t ref_new_seed(uint32_t entropy);
void ref_defaults(uint8_t *cfg, uint32_t *seed);

static int check(const char *what, int ok)
{
    printf("%-100s %s\n", what, ok ? "ok" : "FAIL");
    return !ok;
}
static uint32_t lcg = 0x2545F491u;
static uint32_t rnd(uint32_t n) { lcg = lcg * 1664525u + 1013904223u; return (lcg >> 8) % n; }
static void cfg_set(acid_cfg_t *g, const uint8_t *c, uint32_t seed)
{
    acid_defaults(g, seed);
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
    c[5] = (uint8_t)rnd(ACID_NSCALES);
    c[6] = (uint8_t)rnd(5);
    c[7] = (uint8_t)rnd(17);
}
static int cfg_eq(const acid_cfg_t *a, const acid_cfg_t *b)   /* field by field (not memcmp: the padding) */
{
    return a->density == b->density && a->accent == b->accent && a->slide == b->slide && a->oct_range == b->oct_range &&
           a->root == b->root && a->scale == b->scale && a->base_oct == b->base_oct && a->mutate_bars == b->mutate_bars &&
           a->seed == b->seed && a->mode == b->mode && a->temp == b->temp && a->a_dens == b->a_dens &&
           a->a_acc == b->a_acc && a->a_sld == b->a_sld && a->a_scale == b->a_scale;
}
static uint32_t seed_rand(void) { return rnd(8) ? (lcg ^ (lcg << 13)) | 1u : 1u + rnd(4); }
static void aicd_rand(acid_cfg_t *g)
{
    g->mode = (uint8_t)(ACIDGEN_AICD ? rnd(2) : 0);
    g->temp = (uint8_t)(10u + rnd(191));
    g->a_dens = (uint8_t)rnd(101);
    g->a_acc = (uint8_t)rnd(101);
    g->a_sld = (uint8_t)rnd(101);
    g->a_scale = (uint8_t)rnd(ACID_NSCALES + 1u);
}

/* ------------------------------------------------------- 1. TB-3PO --- */
static int tb3po(void)
{
    int bad = 0, ok = 1;
    uint32_t d, u, len, i, k, n, seed, r1, r2, diffs = 0;
    uint8_t c[8], rn[64], rf[64];
    acid_cfg_t g;
    acid_step_t b[ACID_NSTEP];
    for (d = 0; d <= 100u; d++)
        ok &= (uint32_t)((float)d / 100.0f * 1073741824.0f) == ACID_THR[d];
    ok &= (uint32_t)(0.35f * 1073741824.0f) == ACID_THR_35 && (uint32_t)(0.25f * 1073741824.0f) == ACID_THR_25 &&
          (uint32_t)(0.5f * 1073741824.0f) == ACID_THR_50;
    bad += check("tb3po: the thresholds: (float)d / 100.0f x 2^30 (d 0..100), 0.35f 0.25f 0.5f", ok);
    for (len = 1, ok = 1; len <= 7u; len++)
        for (u = 0; u < (1u << 24); u++) {
            float f = (float)u / (float)0x1000000;
            int r = (int)(f * (float)len);
            r = r >= (int)len ? (int)len - 1 : r;
            ok &= (uint32_t)r == acid_pick(u, len);
        }
    bad += check("tb3po: acid_pick == (int)(u / 2^24 * len) as the float rounds, every u, len 1..7", ok);
    ref_defaults(c, &seed);
    acid_defaults(&g, 0);
    ok = g.density == c[0] && g.accent == c[1] && g.slide == c[2] && g.oct_range == c[3] && g.root == c[4] &&
         g.scale == c[5] && g.base_oct == c[6] && g.mutate_bars == c[7] && g.seed == seed;
    bad += check("tb3po: X0X's defaults (70 / 40 / 25 %, 2 octaves, A minor from A1, seed BEEF)", ok);
    for (i = 0, ok = 1; i < 200000u; i++) {
        uint32_t e = lcg ^ (i * 2654435761u);
        rnd(2);
        ok &= acid_new_seed(e) == ref_new_seed(e);
    }
    bad += check("tb3po: new seeds == X0X's (200000 entropies)", ok);
    for (i = 0, ok = 1; i < 40000u; i++) {
        int l = (int)(rnd(3) ? 1u + rnd(64) : (uint32_t[]){1, 16, 32, 64}[rnd(4)]);
        cfg_rand(c);
        seed = seed_rand();
        ref_generate(c, seed, l, rn, rf);
        cfg_set(&g, c, seed);
        acid_generate(&g, b, (uint32_t)l);
        for (k = 0; k < ACID_NSTEP; k++)
            if (b[k].note != rn[k] || b[k].flags != rf[k]) {
                if (!diffs++)
                    printf("  generate: seed %08x len %d step %u: %u/%u, X0X %u/%u\n", seed, l, k, b[k].note,
                           b[k].flags, rn[k], rf[k]);
                ok = 0;
                break;
            }
    }
    bad += check("tb3po: a line == X0X's tb3po_generate, every step (40000 settings, seeds, lengths)", ok);
    for (i = 0, ok = 1; i < 3000u; i++) {
        int l = (int)(1u + rnd(64));
        cfg_rand(c);
        seed = seed_rand();
        cfg_set(&g, c, seed);
        ref_generate(c, seed, l, rn, rf);
        acid_generate(&g, b, (uint32_t)l);
        r1 = r2 = rnd(4) ? 0u : seed_rand();
        for (n = 0; n < 24u && ok; n++) {
            if (n == 12u) {
                cfg_rand(c);
                cfg_set(&g, c, seed);
            }
            ref_mutate(c, seed, l, rn, rf, &r1);
            acid_mutate(&g, b, (uint32_t)l, &r2);
            ok &= r1 == r2;
            for (k = 0; k < ACID_NSTEP; k++)
                ok &= b[k].note == rn[k] && b[k].flags == rf[k];
        }
    }
    bad += check("tb3po: MUTATE chains == X0X's tb3po_mutate (3000 lines x 24, the stream carried on)", ok);
    return bad;
}

/* --------------------------------------------------------- 2. AICD --- */
#if ACIDGEN_AICD
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
static int aicd_model(const char *ref)
{
    int bad = 0, ok = 1, okw = 1;
    uint32_t node, last, n, k, nodes = 0;
    double tv_max = 0, tv_sum = 0;
    FILE *f = ref ? fopen(ref, "r") : 0;
    if (!f) {                                       /* (no python3: test/run.sh says so) */
        printf("  aicd: no reference given: the exact model check SKIPPED (tools/gen_aicd.py OUT.h REF.txt)\n");
        return 0;
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
        for (k = 0; k < n && n > 1u; k++) {
            double x = -3.0 * log2(p[k] / pmax);
            okw &= x > 31.0 ? e[k].w == 31u : fabs(e[k].w - x) <= 0.5 + 1e-9;
        }
        sq = aicd_odds(e, n, 100, q);
        for (k = 0; k < n; k++)
            tv += fabs(q[k] / sq - p[k]) / 2.0;
        tv_max = tv > tv_max ? tv : tv_max;
        tv_sum += tv;
        nodes++;
    }
    fclose(f);
    printf("  aicd: %u nodes, %u bytes of tables; the odds within %.4f of the model's (mean %.4f)\n", nodes, AICD_BYTES,
           tv_max, tv_sum / nodes);
    bad += check("aicd: every node's edges (their nodes, its last step) == gen_aicd.py's exact model", ok && nodes == AICD_NNODE);
    bad += check("aicd: the weights round(-3 log2(p / the likeliest's)); a draw's odds within 0.05 of the model's",
                 okw && tv_max < 0.05 && tv_sum / nodes < 0.01);
    return bad;
}
static int aicd_draws(void)
{
    int ok = 1;
    uint32_t t, i, k, n, r = 0x1234567u;
    aicd_bias_t bi;
    acid_cfg_t c;
    acid_defaults(&c, 1);
    c.mode = ACID_MODE_AICD;
    aicd_bias(&c, &bi);
    for (t = 0; t < 6u; t++) {
        aicd_at_t a0, a, tg[AICD_MAXE];
        aicd_edge_t e[AICD_MAXE];
        double q[AICD_MAXE], sum;
        uint32_t cnt[AICD_MAXE] = {0}, pos, code;
        aicd_seek(t == 0 ? AICD_START[rnd(AICD_NSTART)] : rnd(AICD_NNODE), &a0);
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
            for (k = 0; k < n && (tg[k].n != a.n || tg[k].pos != a.pos); k++)
                ;
            ok &= k < n;
            if (k < n)
                cnt[k]++;
        }
        for (k = 0; k < n; k++) {
            double pe = q[k] / sum, sd = sqrt(pe * (1 - pe) / 200000.0) + 1e-9;
            ok &= fabs(cnt[k] / 200000.0 - pe) / sd < 5.0;
        }
    }
    return check("aicd: a draw's edges as often as their odds (a start, random nodes: within 5 sd)", ok);
}
typedef struct { double gate, acc, sld, inmin; uint32_t distinct; } stats_t;
static int cmp_u32(const void *a, const void *b)
{
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return x < y ? -1 : x > y;
}
static stats_t stats(const acid_cfg_t *c0, uint32_t nlines, uint32_t len, int *valid)
{
    static uint32_t hs[20000];
    stats_t st = {0, 0, 0, 0, 0};
    uint32_t i, k, tot = 0, ng = 0, acc = 0, sld = 0, inm = 0, v, y;
    acid_cfg_t c = *c0;
    for (i = 0; i < nlines; i++) {
        acid_step_t b[ACID_NSTEP];
        c.seed = acid_new_seed(i * 7919u + 13u);
        acid_generate(&c, b, len);
        for (k = 0; k < len; k++) {
            int known = 0;
            for (v = 0; v < ((b[k].flags & ACID_GATE) == ACID_REST ? 4u : 1u); v++)
                known |= aicd_sym(&c, &b[k], v, &y);
            *valid &= known;
            tot++;
            if ((b[k].flags & ACID_GATE) == ACID_NOTE) {
                uint32_t pc = (uint32_t)(b[k].note + 120 - 12 * (c.base_oct + 1) - c.root) % 12u;
                ng++;
                acc += (b[k].flags & ACID_ACC) != 0;
                sld += (b[k].flags & ACID_SLD) != 0;
                inm += pc == 0 || pc == 2 || pc == 3 || pc == 5 || pc == 7 || pc == 8 || pc == 10;
            }
        }
        hs[i] = acid_line_hash(b, len);
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
/* Aicd's own sampler (its acid.js at temperature 1, 20000 lines of 16): gate 0.9028, ACC 0.3530 and SLD 0.3177 of the
 * notes (after "no slide into a rest"), the minor scale's 0.7132 */
static int aicd_lines(void)
{
    int bad = 0, valid = 1, ok;
    acid_cfg_t c;
    stats_t s0, s;
    uint32_t d10, d100, d200;
    acid_defaults(&c, 1);
    c.mode = ACID_MODE_AICD;
    s0 = stats(&c, 20000, 16, &valid);
    printf("  aicd: gate %.4f, ACC %.4f, SLD %.4f, minor %.4f (Aicd: 0.9028 0.3530 0.3177 0.7132)\n", s0.gate, s0.acc,
           s0.sld, s0.inmin);
    ok = fabs(s0.gate - 0.9028) < 0.01 && fabs(s0.acc - 0.3530) < 0.02 && fabs(s0.sld - 0.3177) < 0.015 &&
         fabs(s0.inmin - 0.7132) < 0.015;
    bad += check("aicd: the lines as Aicd's own (gate, ACC, SLD, the notes in the minor scale; 20000 lines)", ok);
    ok = 1;                                         /* DENS ACCENT SLIDE: the share asked for (0 and 100 % too) */
    printf("  aicd: asked / got (gate, ACC, SLD):");
    {
        static const uint32_t PCT[] = {0, 20, 50, 80, 100};
        uint32_t k;
        for (k = 0; k < sizeof PCT / sizeof PCT[0]; k++) {
            double want = PCT[k] / 100.0, tol = 0.06;   /* (within a step of 16, or 14 notes) */
            stats_t g1, a1, l1;
            c.a_dens = (uint8_t)PCT[k];
            g1 = stats(&c, 3000, 16, &valid);
            ok &= fabs(g1.gate - (PCT[k] ? want : 1.0 / 16)) < tol;
            c.a_dens = ACID_AICD_DENS;
            c.a_acc = (uint8_t)PCT[k];
            a1 = stats(&c, 3000, 16, &valid);
            ok &= fabs(a1.acc - want) < tol;
            c.a_acc = ACID_AICD_ACC;
            c.a_sld = (uint8_t)PCT[k];
            l1 = stats(&c, 3000, 16, &valid);
            ok &= PCT[k] == 100 ? l1.sld > 0.85 : fabs(l1.sld - want) < tol;   /* (a slide into a rest does nothing) */
            c.a_sld = ACID_AICD_SLD;
            printf(" %u: %.2f %.2f %.2f;", PCT[k], g1.gate, a1.acc, l1.sld);
        }
    }
    c.a_scale = 1;
    s = stats(&c, 3000, 16, &valid);
    ok &= s.inmin > s0.inmin + 0.12;
    printf(" SCALE MIN: %.3f in the minor scale\n", s.inmin);
    c.a_scale = 0;
    bad += check("aicd: DENS ACCENT SLIDE 0..100 %: that share of the steps / notes (0 and 100 % too); SCALE MIN more "
                 "in it", ok);
    c.temp = 10;
    d10 = stats(&c, 20000, 4, &valid).distinct;
    c.temp = 100;
    d100 = stats(&c, 20000, 4, &valid).distinct;
    c.temp = 200;
    d200 = stats(&c, 20000, 4, &valid).distinct;
    printf("  aicd: TEMP 10 / 100 / 200 %%: %u / %u / %u distinct 4-step lines of 20000\n", d10, d100, d200);
    ok = d10 * 3u < d100 * 2u && d100 + 200u < d200;
    c.temp = 10;                                    /* (the fit keeps it: 16-step lines, 7.5 k of 20000 at 10 %, ~19 k at 100 %) */
    d10 = stats(&c, 20000, 16, &valid).distinct;
    c.temp = 100;
    d100 = stats(&c, 20000, 16, &valid).distinct;
    printf("  aicd: TEMP 10 / 100 %%: %u / %u distinct 16-step lines of 20000\n", d10, d100);
    ok &= d10 * 2u < d100;
    bad += check("aicd: TEMP: 10 % fewer lines (the likeliest), 200 % more than 100 %", ok);
    bad += check("aicd: every step a model step (transposed by ROOT / OCTAVE)", valid);
    return bad;
}
#endif

/* ------------------------------------------------------- 3. both --- */
static int both(void)
{
    int bad = 0, ok = 1, okr = 1;
    uint32_t i, k;
    uint8_t pk[ACID_PACKED];
    acid_cfg_t g, h;
    static const uint8_t DEF[ACID_PACKED] = {'T', 'B', '3', 'P', 1, 70, 40, 25, 2, 9, 0, 1, 0, 0x6F, 0x7D, 2, 0, 0,
                                             1, 0, 100, 0, 90, 35, 32, 0};
    printf("  selftest: TB-3PO %08x, AICD %08x\n", acid_selftest_tb3po(),
#if ACIDGEN_AICD
           acid_selftest_aicd()
#else
           0u
#endif
    );
    bad += check("both: the self-test's hashes: Felucca's lines (TB-3PO CB4CC370, AICD 99FE18AC)", acid_selftest());
    acid_defaults(&g, 0);
    acid_pack(&g, pk);
    ok = !memcmp(pk, DEF, sizeof pk);
    for (i = 0; i < 3000u; i++) {
        uint8_t c[8];
        cfg_rand(c);
        cfg_set(&g, c, i ? lcg | 1u : 0xFFFFFFFFu);
        aicd_rand(&g);
        acid_pack(&g, pk);
        for (k = 0; k < ACID_PACKED; k++)
            ok &= pk[k] <= 127u;
        memset(&h, 0, sizeof h);
        ok &= acid_unpack(&h, pk) && cfg_eq(&h, &g);
    }
    pk[2] = 'X';
    ok &= !acid_unpack(&h, pk) && h.density == 70u && h.seed == 0xBEEFu;
    acid_pack(&g, pk);
    memset(pk + 18, 0, 8);
    ok &= acid_unpack(&h, pk) && h.mode == ACID_MODE_TB3PO && h.temp == 100u && h.a_dens == ACID_AICD_DENS && h.a_acc == ACID_AICD_ACC &&
           h.a_sld == ACID_AICD_SLD && !h.a_scale;
    bad += check("both: the settings' 26 bytes (Felucca's layout, the defaults' exactly), back exactly, each <= 127; "
                 "not TB3P: the defaults; no AICD part: TB-3PO", ok);
    for (i = 0; i < 4000u; i++) {                   /* a line's rules, both modes */
        acid_step_t b[ACID_NSTEP], was[ACID_NSTEP];
        uint8_t c[8];
        uint32_t len = 1u + rnd(64), any = 0, r = rnd(2) ? 0u : seed_rand(), run, i0, ins;
        cfg_rand(c);
        cfg_set(&g, c, seed_rand());
        aicd_rand(&g);
        acid_generate(&g, b, len);
        for (k = 0; k < ACID_NSTEP; k++) {
            uint32_t gate = b[k].flags & ACID_GATE;
            okr &= b[k].note <= 127u && gate != ACID_TIE && (gate == ACID_NOTE || !(b[k].flags & (ACID_ACC | ACID_SLD)));
            okr &= k < len || b[k].flags == ACID_REST;
            okr &= !(b[k].flags & ACID_SLD) || (b[(k + 1u) % len].flags & ACID_GATE) != ACID_REST;
            any |= k < len && gate == ACID_NOTE;
        }
        okr &= any;
        memcpy(was, b, sizeof b);
        acid_mutate(&g, b, len, &r);
        okr &= r != 0;
        for (k = len; k < ACID_NSTEP; k++)
            okr &= b[k].note == was[k].note && b[k].flags == was[k].flags;
        if (g.mode != ACID_MODE_AICD)
            continue;
        run = (len + 3u) / 4u > 16u ? 16u : (len + 3u) / 4u;   /* AICD: a run (and the step before it) */
        for (i0 = 0, ins = 0; i0 < len && !ins; i0++) {
            uint32_t out = 0;
            for (k = 0; k < len; k++)
                out += (b[k].note != was[k].note || b[k].flags != was[k].flags) && (k + len - i0) % len >= run &&
                       k != (i0 + len - 1u) % len;
            ins = !out;
        }
        okr &= ins;
    }
    bad += check("both: a line: all 64 written, REST past len, no TIE, ACC / SLD on notes only, no SLD into a REST, a "
                 "note at least; a MUTATE: nothing past len, AICD's a run", okr);
    return bad;
}

int main(int argc, char **argv)
{
    int bad = tb3po() + both();
#if ACIDGEN_AICD
    bad += aicd_model(argc > 1 ? argv[1] : 0) + aicd_draws() + aicd_lines();
#else
    (void)argc, (void)argv;
#endif
    printf("%s\n", bad ? "ACIDGEN TEST FAILED" : "acidgen: all passed");
    return bad != 0;
}
