/* SPDX-License-Identifier: GPL-3.0-only
 * Part of Felucca: free software under the GNU General Public License, version 3 only (see LICENSE) */
/* TB-3PO: the 303 engine's line generator (eng_303.c), what ARP opens on a 303 track (ui_tb3po.c). Ported from X0X's
 * seq/tb3po.c (github.com/charlesvestal/fm1-x0x at 54194c5, GPL-3.0-only), itself a port of schwung-tb3po (Charles
 * Vestal), after the Phazerville Hemisphere Suite's TB_3PO applet (djphazer and contributors, GPL-3.0). Included by
 * seq.c (auto MUTATE as a pass starts), used by the UI and project.c.
 *
 * Integer only (Felucca's floats are the 303's: build.py float_check), and the same line as X0X's for the same seed and
 * settings: its random draws in the same order (xorshift32, u = 24 bits), each float comparison of u / 2^24 against a
 * float d / 100.0f as an exact integer one against that float x 2^30 (TB3_THR: every such float is a whole number of
 * 2^-30 from 0.01 up), each (int)(u / 2^24 * len) with the float product's rounding (tb3_pick). tests/tb3po_test.c
 * checks it against X0X's float code over many seeds and settings.
 *
 * The line goes into the track's steps (tb3_write): a NOTE step one note, ACC / SLD, velocity 96; a rest a REST step that
 * keeps its note in note[0] (n 0: it plays nothing), which a MUTATE may turn into a note again, as X0X's rests keep
 * theirs. A MUTATE reads the steps back (tb3_read: hand edits included, a chord as its first note, a TIE as a TIE) and
 * writes only the steps it changed, their other fields (RATCH, NUDGE, CHANCE, VEL) kept. LEN is the track's.
 * The settings and the seed per track (tb3_cfg): a project keeps them in a 303 track's FM6 area (tb3_pack, project.c),
 * 0 = X0X's defaults (70 % / 40 % / 25 %, 2 octaves, A minor from A1, seed BEEF).
 * 1.8.2: DENS ACCENT SLIDE OCTS ROOT SCALE OCTAVE act on the line as it plays, at its next bar (tb3_live, tb3_bar),
 * TEMP / MODE the next line's only; a new line's rests keep notes of their own (tb3_rest_notes: X0X's notes as they are).
 *
 * MODE AICD (1.6.2, FELUCCA_AICD): the line from Aicd's model instead (Michael Ganss, github.com/mganss/Aicd, MIT:
 * a maximum-likelihood n-gram trained on ABL3 patterns, as it is: tools/gen_aicd.py, build/gen/felucca_aicd.h): each
 * step drawn from what followed the two steps before it there. DENS ACCENT SLIDE are TB-3PO's 0..100 %, the share
 * of the steps that are notes and of the notes accented / sliding (defaults 90 / 35 / 32: the model's own): they bias
 * the model's choices where it has some (aicd_bias), and a new line is then fitted to them exactly (aicd_fit: random
 * steps turned on or off). SCALE biases too (CHRO: none), TEMP sharpens (< 100 %) or flattens (> 100 %) the odds;
 * ROOT and OCTAVE transpose (its C on the root); OCTS is TB-3PO's. A MUTATE draws a run of the line (a quarter, 16
 * steps at most) on from the two steps before it, biased but not fitted (the rest of the line stays). */
#define TB3_NSCALES 6
#define TB3_REST 0u              /* a line's step: gate (X0X's G_REST G_NOTE G_TIE) | ACC | SLD */
#define TB3_NOTE 1u
#define TB3_TIE 2u
#define TB3_GATE 3u
#define TB3_ACC 4u
#define TB3_SLD 8u
typedef struct {                 /* a track's TB-3PO settings (X0X tb3po_cfg_t): the line follows from them and seed */
    uint8_t density, accent, slide;   /* 0..100 % */
    uint8_t oct_range;           /* 1..3 */
    uint8_t root, scale;         /* 0 = C .. 11 = B; TB3_SCALE_NAMES */
    uint8_t base_oct;            /* 0..4: the root's octave, note 12 (base_oct + 1) + root */
    uint8_t mutate_bars;         /* 1..16: MUTATE every that many passes of the line while playing, 0 off */
    uint32_t seed;
    uint8_t mode;                /* TB3_TB3PO (X0X's), TB3_AICD (1.6.2) */
    uint8_t temp;                /* AICD: TEMP 10..200 %, 100 the model's own odds */
    uint8_t a_dens, a_acc, a_sld; /* AICD: DENS ACCENT SLIDE, 0..100 % (AICD_DENS.. the model's own) */
    uint8_t a_scale;             /* AICD: SCALE, 0 CHRO (none), 1.. TB3_SCALES' */
} tb3_cfg_t;
#define TB3_TB3PO 0u
#define TB3_AICD 1u
#define AICD_DENS 90u            /* AICD's DENS ACCENT SLIDE defaults: the model's own shares (gate, ACC, SLD of the notes) */
#define AICD_ACC 35u
#define AICD_SLD 32u
typedef struct { uint8_t note, flags; } tb3_step_t;
typedef struct {                 /* 1.8.2: the knobs that act on the playing line (tb3_live), of the track's MODE */
    uint8_t dens, acc, sld;      /* 0..100 % */
    uint8_t octs;                /* 1..3 (AICD: 0, not a knob) */
    uint8_t root, scale, base;   /* scale 0 CHRO (AICD's), 1.. TB3_SCALES[scale - 1] */
} tb3_knobs_t;
typedef struct {                 /* a track's TB-3PO state that is not stored */
    uint32_t rng;                /* auto MUTATE's random stream (X0X mut_rng; 0: from the seed) */
    uint32_t passes;             /* passes of the line since PLAY */
    uint8_t stale;               /* a setting changed since the line was written (the screen's NEW LINE *) */
    uint8_t pending;             /* 1.8.2: a live knob turned, for the next bar (tb3_bar) */
    uint8_t have;                /* anchor holds a line */
    tb3_knobs_t k0, kapp;        /* the knobs anchor was written with / those last applied */
    uint32_t wrote;              /* tb3_lhash of the steps as TB-3PO last wrote them (else: edited, a new anchor) */
    tb3_step_t anchor[NSTEP];    /* the line the live knobs start from (OCT+, OCT-, auto MUTATE, a hand edit) */
} tb3_rt_t;
static tb3_cfg_t tb3_cfg[NTRK];
static tb3_rt_t tb3_rt[NTRK];
static uint32_t tb3_auto_n;      /* auto MUTATEs so far (the screen redraws on one: ui_graph.c graph_signature) */

typedef struct { int8_t deg[7]; uint8_t len; } tb3_scale_t;
static const tb3_scale_t TB3_SCALES[TB3_NSCALES] = {   /* X0X's (schwung-tb3po's), same order */
    {{0, 2, 3, 5, 7, 8, 10}, 7},                       /* MIN */
    {{0, 1, 3, 5, 7, 8, 10}, 7},                       /* PHRY */
    {{0, 2, 3, 5, 7, 8, 11}, 7},                       /* HARM */
    {{0, 3, 5, 7, 10, 0, 0}, 5},                       /* MPEN */
    {{0, 2, 3, 5, 7, 9, 10}, 7},                       /* DOR */
    {{0, 2, 4, 5, 7, 9, 11}, 7},                       /* MAJ */
};
static const char *const TB3_SCALE_NAMES[TB3_NSCALES] = {"MIN", "PHRY", "HARM", "MPEN", "DOR", "MAJ"};
static const char *const TB3_ASCALE_NAMES[TB3_NSCALES + 1] = {"CHRO", "MIN", "PHRY", "HARM", "MPEN", "DOR", "MAJ"};
                                 /* AICD's SCALE: CHRO (no bias), then TB3_SCALES' */
/* (float)d / 100.0f x 2^30, d = 0..100 (exact: tools-free, tests/tb3po_test.c computes them from the floats) */
static const uint32_t TB3_THR[101] = {
    0, 10737418, 21474836, 32212254, 42949672, 53687092, 64424508, 75161928, 85899344, 96636768, 107374184,
    118111600, 128849016, 139586432, 150323856, 161061280, 171798688, 182536112, 193273536, 204010944, 214748368,
    225485776, 236223200, 246960624, 257698032, 268435456, 279172864, 289910304, 300647712, 311385120, 322122560,
    332859968, 343597376, 354334816, 365072224, 375809632, 386547072, 397284480, 408021888, 418759296, 429496736,
    440234144, 450971552, 461708992, 472446400, 483183808, 493921248, 504658656, 515396064, 526133504, 536870912,
    547608320, 558345728, 569083136, 579820608, 590558016, 601295424, 612032832, 622770240, 633507648, 644245120,
    654982528, 665719936, 676457344, 687194752, 697932160, 708669632, 719407040, 730144448, 740881856, 751619264,
    762356672, 773094144, 783831552, 794568960, 805306368, 816043776, 826781184, 837518592, 848256064, 858993472,
    869730880, 880468288, 891205696, 901943104, 912680576, 923417984, 934155392, 944892800, 955630208, 966367616,
    977105088, 987842496, 998579904, 1009317312, 1020054720, 1030792128, 1041529600, 1052267008, 1063004416,
    1073741824,
};
#define TB3_THR_35 375809632u    /* 0.35f, 0.25f, 0.5f x 2^30 */
#define TB3_THR_25 268435456u
#define TB3_THR_50 536870912u

static void tb3_defaults(tb3_cfg_t *g, uint32_t seed)   /* X0X tb3po_defaults */
{
    g->density = 70;
    g->accent = 40;
    g->slide = 25;
    g->oct_range = 2;
    g->root = 9;
    g->scale = 0;
    g->base_oct = 1;
    g->mutate_bars = 0;
    g->seed = seed ? seed : 0xBEEF;
    g->mode = TB3_TB3PO;
    g->temp = 100;
    g->a_dens = AICD_DENS;
    g->a_acc = AICD_ACC;
    g->a_sld = AICD_SLD;
    g->a_scale = 0;
}

static uint32_t tb3_u(uint32_t *r)                   /* a draw: xorshift32, its low 24 bits (X0X rng_f's u / 2^24) */
{
    uint32_t x = *r ? *r : 1u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *r = x;
    return x & 0xFFFFFFu;
}
static uint32_t tb3_pct(uint32_t v) { return TB3_THR[v <= 100u ? v : 100u]; }
/* (int)(u / 2^24 * len), len 1..7, as the float product rounds: u * len to 24 significant bits (nearest, ties to
 * even), then its whole part; len - 1 at most */
static uint32_t tb3_pick(uint32_t u, uint32_t len)
{
    uint32_t p = u * len, s = 0, q, rem, half;
    while ((p >> s) >= (1u << 24))
        s++;
    if (s) {
        q = p >> s;
        rem = p & ((1u << s) - 1u);
        half = 1u << (s - 1u);
        if (rem > half || (rem == half && (q & 1u)))
            q++;
        p = q << s;
    }
    p >>= 24;
    return p >= len ? len - 1u : p;
}
static uint32_t tb3_clampi(int32_t v, int32_t lo, int32_t hi) { return (uint32_t)(v < lo ? lo : v > hi ? hi : v); }
static const tb3_scale_t *tb3_scale(const tb3_cfg_t *g) { return &TB3_SCALES[g->scale < TB3_NSCALES ? g->scale : 0]; }
static uint8_t tb3_note(const tb3_cfg_t *g, uint32_t degree, uint32_t oct)
{
    return (uint8_t)tb3_clampi(12 * (g->base_oct + 1) + g->root + tb3_scale(g)->deg[degree] + 12 * (int32_t)oct, 0, 127);
}
static uint8_t tb3_kind(uint32_t *r, const tb3_cfg_t *g)   /* X0X roll_kind: NOTE, ACC, SLD (not both) */
{
    uint8_t f = TB3_NOTE;
    if ((tb3_u(r) << 6) < tb3_pct(g->accent))
        f = TB3_NOTE | TB3_ACC;
    if ((tb3_u(r) << 6) < tb3_pct(g->slide))
        f = TB3_NOTE | TB3_SLD;
    return f;
}
static void tb3_slides(tb3_step_t *b, uint32_t len)  /* a slide into a rest does nothing */
{
    uint32_t i;
    for (i = 0; i < len; i++)
        if ((b[i].flags & TB3_SLD) && (b[(i + 1u) % len].flags & TB3_GATE) == TB3_REST)
            b[i].flags = TB3_NOTE;
}

/* a whole line (len steps, NSTEP written) from g and its seed (X0X tb3po_generate) */
static void tb3_generate(const tb3_cfg_t *g, tb3_step_t *b, uint32_t len)
{
    const tb3_scale_t *sc = tb3_scale(g);
    uint32_t range = tb3_clampi(g->oct_range, 1, 3), r = g->seed ? g->seed : 1u, i, deg, oct, any = 0;
    len = tb3_clampi((int32_t)len, 1, NSTEP);
    for (i = 0; i < NSTEP; i++) {
        b[i].flags = TB3_REST;
        b[i].note = tb3_note(g, 0, 0);
    }
    for (i = 0; i < len; i++) {
        if ((tb3_u(&r) << 6) > tb3_pct(g->density))
            continue;                                /* a rest */
        if (i % 4u == 0u && (tb3_u(&r) << 6) < TB3_THR_35)
            deg = 0;                                 /* on the beat: the root, often */
        else
            deg = tb3_pick(tb3_u(&r), sc->len);
        oct = tb3_pick(tb3_u(&r), range);
        b[i].note = tb3_note(g, deg, oct);
        b[i].flags = tb3_kind(&r, g);
    }
    tb3_slides(b, len);
    for (i = 0; i < len; i++)
        any |= (b[i].flags & TB3_GATE) != TB3_REST;
    if (!any) {
        b[0].note = tb3_note(g, 0, 0);
        b[0].flags = TB3_NOTE;
    }
}

/* re-roll ~25 % of the steps, the random stream on from *rng (0: from the seed; X0X tb3po_mutate) */
static void tb3_mutate(const tb3_cfg_t *g, tb3_step_t *b, uint32_t len, uint32_t *rng)
{
    const tb3_scale_t *sc = tb3_scale(g);
    uint32_t range = tb3_clampi(g->oct_range, 1, 3), r = *rng ? *rng : g->seed ? g->seed : 1u, i, deg;
    len = tb3_clampi((int32_t)len, 1, NSTEP);
    for (i = 0; i < len; i++) {
        if ((tb3_u(&r) << 6) >= TB3_THR_25)
            continue;
        if ((tb3_u(&r) << 6) < TB3_THR_50) {         /* the step */
            if ((tb3_u(&r) << 6) > tb3_pct(g->density))
                b[i].flags = TB3_REST;
            else
                b[i].flags = tb3_kind(&r, g);
        } else {                                     /* its note */
            deg = tb3_pick(tb3_u(&r), sc->len);
            b[i].note = tb3_note(g, deg, tb3_pick(tb3_u(&r), range));
        }
    }
    tb3_slides(b, len);
    *rng = r;
}

static uint32_t tb3_new_seed(uint32_t entropy)       /* X0X tb3po_new_seed */
{
    uint32_t r = entropy * 2654435761u + 0x9E3779B9u;
    (void)tb3_u(&r);
    (void)tb3_u(&r);
    return r ? r : 1u;
}

#if AICD_ON || DBRAIN_ON            /* (AICD's weights; a DRUM track's BRAIN pages, dbrain.c, too) */
static const uint16_t AICD_EXP[16] = {65535, 62757, 60097, 57549, 55109, 52773, 50535, 48393, 46341, 44376, 42495,
                                      40693, 38968, 37316, 35734, 34219};   /* 2^16 x 2^(-i/16) (the first 2^16 - 1) */
#endif

/* ---------------------------------------------------------- AICD mode --- */
#if AICD_ON
#include "felucca_aicd.h"
/* The model (tools/gen_aicd.py has its format): a node is two steps (an 18-character context of Aicd's), its edges the
 * steps that followed them, each into the node of the step before and it. A step reads a node's record and its edges'
 * nodes' last symbols (their steps), draws one, walks to its record (7 from a checkpoint at most): ~1.5 k
 * instructions a step on the host, a 16-step MUTATE ~25 k (tests/tb3po_test.c). Every weight in 1/16 log2: the
 * record's (1/3 log2 under the likeliest) x 100 / TEMP, the biases on top, then 2^-w. */
#define AICD_OFFSCALE 128        /* SCALE: a note off it 2^-8 as likely */
typedef struct { uint16_t n; uint8_t w; } aicd_edge_t;   /* an edge: its node, weight */
typedef struct { uint32_t pos, last, n; } aicd_at_t;  /* node n: its record's edges at bit pos, its last symbol (step) */
typedef struct { uint32_t k, mask; int32_t dens, acc, sld; } aicd_bias_t;

static uint32_t aicd_peek(uint32_t pos)              /* 25 bits of AICD_REC from bit pos, LSB first */
{
    const uint8_t *p = &AICD_REC[pos >> 3];
    return ((uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24) >> (pos & 7u);
}
static uint32_t aicd_lastof(uint32_t n)              /* node n's last symbol (its step) */
{
    uint32_t pos = n * AICD_SYMBITS;
    const uint8_t *p = &AICD_LAST[pos >> 3];
    return (((uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16) >> (pos & 7u)) & ((1u << AICD_SYMBITS) - 1u);
}
static uint32_t aicd_get(uint32_t *pos, uint32_t w)  /* w (0..24) bits */
{
    uint32_t v = aicd_peek(*pos) & ((1u << w) - 1u);
    *pos += w;
    return v;
}
static uint32_t aicd_ones3(uint32_t *pos)           /* 0, 10, 110, 111: 0..3 */
{
    uint32_t v = aicd_peek(*pos), q = 0;
    while (q < 3u && ((v >> q) & 1u))
        q++;
    *pos += q < 3u ? q + 1u : 3u;
    return q;
}
static uint32_t aicd_gap(uint32_t *pos)              /* z zeros, a one, z bits: 1 << z | them (z 6 at most) */
{
    uint32_t z = 0, v = aicd_peek(*pos);
    while (!(v & 1u)) {
        v >>= 1;
        z++;
    }
    *pos += z + 1u;
    return 1u << z | aicd_get(pos, z);
}
static uint32_t aicd_bl(uint32_t n)                  /* bits for an index below n */
{
    uint32_t b = 0;
    while ((1u << b) < n)
        b++;
    return b;
}
/* the rest of a record, its last symbol last: its edges into e (0: past them), their count */
static uint32_t aicd_body(uint32_t *pos, uint32_t last, aicd_edge_t *e)
{
    uint32_t bnd = aicd_get(pos, 1), n = aicd_ones3(pos), tot, m = 0, i, j = 0, jw, w, g0 = AICD_GRP[last];
    n = n < 3u ? n + 1u : aicd_get(pos, 6);
    tot = bnd + n;
    if (tot > 1u)
        m = aicd_get(pos, aicd_bl(tot));
    if (bnd) {                                       /* a pattern end: its node anywhere */
        uint32_t t = aicd_get(pos, AICD_NODEBITS);
        w = tot > 1u && m ? aicd_get(pos, AICD_WBITS) : 0u;
        if (e) {
            e[0].n = (uint16_t)t;
            e[0].w = (uint8_t)w;
        }
    }
    jw = aicd_bl(AICD_GRP[last + 1u] - g0);
    for (i = 0; i < n; i++) {                        /* the steps: their nodes in group last, by index */
        j = i ? j + aicd_gap(pos) : aicd_get(pos, jw);
        w = tot > 1u && i + bnd != m ? aicd_get(pos, AICD_WBITS) : 0u;
        if (e) {
            e[bnd + i].n = (uint16_t)(g0 + j);
            e[bnd + i].w = (uint8_t)w;
        }
    }
    return tot;
}
/* node n: its record's edges (from its checkpoint, past those before it) */
static void aicd_seek(uint32_t n, aicd_at_t *a)
{
    uint32_t pos = AICD_CK[n >> 3] * 8u, m;
    for (m = n & ~7u; m < n; m++)
        (void)aicd_body(&pos, aicd_lastof(m), 0);
    a->pos = pos;
    a->last = aicd_lastof(n);
    a->n = n;
}
static void aicd_start(aicd_at_t *a, uint32_t *r)    /* a pattern's start, as Aicd picks it (evenly) */
{
    aicd_seek(AICD_START[((tb3_u(r) >> 8) * AICD_NSTART) >> 16], a);
}
/* a share p % (0..100) as a bias in 1/16 log2: 3/2 per point off the model's own share nat, 2^-8 .. 2^8 at the ends
 * (the model alone gets ACC to 13 %..69 %: aicd_fit does the rest) */
static int32_t aicd_lean(uint32_t p, uint32_t nat)
{
    int32_t v = ((int32_t)p - (int32_t)nat) * 3 / 2;
    return 2 * (v < -64 ? -64 : v > 63 ? 63 : v);
}
static void aicd_bias(const tb3_cfg_t *g, aicd_bias_t *bi)
{
    uint32_t i, sc = g->a_scale;
    bi->k = 16u * 100u * 256u / AICD_WQ / tb3_clampi(g->temp, 10, 200);
    bi->dens = aicd_lean(g->a_dens, AICD_DENS);
    bi->acc = aicd_lean(g->a_acc, AICD_ACC);
    bi->sld = aicd_lean(g->a_sld, AICD_SLD);
    bi->mask = sc ? 0u : 0xFFFu;
    if (sc && sc <= TB3_NSCALES)
        for (i = 0; i < TB3_SCALES[sc - 1u].len; i++)
            bi->mask |= 1u << TB3_SCALES[sc - 1u].deg[i];
}
/* a line on from node *a: 1 a step (its code into *code), 0 a pattern end (Aicd skips its "~"); *a the node after */
static int aicd_next(aicd_at_t *a, const aicd_bias_t *bi, uint32_t *r, uint32_t *code)
{
    aicd_edge_t e[AICD_MAXE];
    uint16_t sym[AICD_MAXE];
    int32_t s[AICD_MAXE], lo = 0x7FFFFFFF;
    uint32_t pos = a->pos, n = aicd_body(&pos, a->last, e), i, sum = 0, x;
    if (!n) {                                        /* (a dead end: none in this model) */
        aicd_start(a, r);
        return 0;
    }
    for (i = 0; i < n; i++) {                        /* the edges' nodes' steps */
        int32_t v = (int32_t)((e[i].w * bi->k) >> 8);
        uint32_t l = aicd_lastof(e[i].n);
        sym[i] = (uint16_t)l;
        if (l < AICD_NSTEP && (AICD_CODE[l] & 4u)) { /* a note: DENS, ACCENT, SLIDE, SCALE */
            uint32_t c = AICD_CODE[l];
            v -= bi->dens + (c & 2u ? bi->acc : 0) + (c & 1u ? bi->sld : 0);
            if (!((bi->mask >> (((c >> 3) + 8u) % 12u)) & 1u))   /* (its offset from the root: c >> 3, less 16) */
                v += AICD_OFFSCALE;
        }
        s[i] = v;
        lo = v < lo ? v : lo;
    }
    for (i = 0; i < n; i++) {
        uint32_t d = (uint32_t)(s[i] - lo);
        s[i] = d >= 16u * 17u ? 0 : (int32_t)(AICD_EXP[d & 15u] >> (d >> 4));
        sum += (uint32_t)s[i];
    }
    x = (uint32_t)(((uint64_t)tb3_u(r) * sum) >> 24);
    for (i = 0; x >= (uint32_t)s[i]; i++)
        x -= (uint32_t)s[i];
    aicd_seek(e[i].n, a);
    if (a->last >= AICD_NSTEP)
        return 0;
    *code = AICD_CODE[a->last];
    return 1;
}
static void aicd_put(const tb3_cfg_t *g, tb3_step_t *b, uint32_t code)   /* a model step into the line */
{
    b->note = (uint8_t)tb3_clampi(12 * (g->base_oct + 1) + g->root + (int32_t)(code >> 3) - 16, 0, 127);
    b->flags = (uint8_t)(code & 4u ? TB3_NOTE | (code & 2u ? TB3_ACC : 0u) | (code & 1u ? TB3_SLD : 0u) : TB3_REST);
}
static uint32_t aicd_lb(uint32_t code)               /* the first model step of code or more */
{
    uint32_t lo = 0, hi = AICD_NSTEP;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2u;
        if (AICD_CODE[mid] < code)
            lo = mid + 1u;
        else
            hi = mid;
    }
    return lo;
}
/* line step b as a model step's symbol (v: a REST's ACC SLD, 0..3: the steps keep none), 0: none such */
static int aicd_sym(const tb3_cfg_t *g, const tb3_step_t *b, uint32_t v, uint32_t *sym)
{
    uint32_t gate = b->flags & TB3_GATE, code, lo;
    int32_t o = (int32_t)b->note - 12 * (g->base_oct + 1) - g->root;
    if (gate == TB3_TIE || o < -16 || o > 47)
        return 0;
    code = (uint32_t)(o + 16) << 3 | (gate == TB3_NOTE ? 4u | (b->flags & TB3_ACC ? 2u : 0u) |
                                                         (b->flags & TB3_SLD ? 1u : 0u) : v);
    lo = aicd_lb(code);
    *sym = lo;
    return lo < AICD_NSTEP && AICD_CODE[lo] == code;
}
static int aicd_rest_ok(const tb3_cfg_t *g, uint32_t note)   /* the model has a rest on this note (any ACC SLD) */
{
    int32_t o = (int32_t)note - 12 * (g->base_oct + 1) - g->root;
    uint32_t code = (uint32_t)(o + 16) << 3, lo;
    if (o < -16 || o > 47)
        return 0;
    lo = aicd_lb(code);
    return lo < AICD_NSTEP && AICD_CODE[lo] < (code | 4u);
}
/* the node of line steps b2, b1 (a REST any of its ACC SLD the model has), 1; 0: the model never had them */
static int aicd_find(const tb3_cfg_t *g, const tb3_step_t *b2, const tb3_step_t *b1, aicd_at_t *a)
{
    uint32_t v2, v1, x, y;
    for (v2 = 0; v2 < ((b2->flags & TB3_GATE) == TB3_REST ? 4u : 1u); v2++)
        for (v1 = 0; v1 < ((b1->flags & TB3_GATE) == TB3_REST ? 4u : 1u); v1++) {
            uint32_t lo, hi;
            if (!aicd_sym(g, b2, v2, &x) || !aicd_sym(g, b1, v1, &y))
                continue;
            for (lo = AICD_GRP[x], hi = AICD_GRP[x + 1u]; lo < hi;) {   /* group x by last symbol */
                uint32_t mid = (lo + hi) / 2u;
                if (aicd_lastof(mid) < y)
                    lo = mid + 1u;
                else
                    hi = mid;
            }
            if (lo < AICD_GRP[x + 1u] && aicd_lastof(lo) == y) {
                aicd_seek(lo, a);
                return 1;
            }
        }
    return 0;
}

/* the fit: the steps of line b (len) that are k: 0 a rest, 1 a note, 2 a note without ACC, 3 with, 4 a note without SLD
 * that is not before a rest, 5 with, 6 a note (as 1), 7 with ACC (as 3), 8 with SLD (as 5); for 0, 2, 4, 6, 7 and 8 only
 * if the step they turn it into is one of the model's too (its rests have notes: a rest on any note is not; 6 not after a slide, which would end in a rest) */
static uint32_t aicd_before(const tb3_step_t *b, uint32_t i, uint32_t len)   /* the note of the note before step i */
{
    uint32_t j;
    for (j = 1; j < len; j++)
        if ((b[(i + len - j) % len].flags & TB3_GATE) != TB3_REST)
            return b[(i + len - j) % len].note;
    return b[i].note;
}
static int aicd_is(const tb3_cfg_t *g, const tb3_step_t *b, uint32_t i, uint32_t len, uint32_t k, int chk)
{
    uint32_t note = (b[i].flags & TB3_GATE) != TB3_REST, rest = (b[(i + 1u) % len].flags & TB3_GATE) == TB3_REST, y;
    tb3_step_t t = b[i];
    switch (chk ? k : 99u) {                         /* (t: the step turned, to look up) */
    case 0: t.note = (uint8_t)aicd_before(b, i, len); t.flags = TB3_NOTE; break;
    case 2: t.flags |= TB3_ACC; break;
    case 4: t.flags |= TB3_SLD; break;
    case 6: t.flags = TB3_REST; break;
    case 7: t.flags &= (uint8_t)~TB3_ACC; break;
    case 8: t.flags &= (uint8_t)~TB3_SLD; break;
    default: break;
    }
    switch (k) {
    case 0: return !note && (!chk || aicd_sym(g, &t, 0, &y));
    case 1: return note;
    case 2: return note && !(b[i].flags & TB3_ACC) && (!chk || aicd_sym(g, &t, 0, &y));
    case 3: return note && (b[i].flags & TB3_ACC);
    case 4: return note && !rest && !(b[i].flags & TB3_SLD) && (!chk || aicd_sym(g, &t, 0, &y));
    case 6: return note && !(b[(i + len - 1u) % len].flags & TB3_SLD) && (!chk || aicd_rest_ok(g, t.note));
    case 7: return note && (b[i].flags & TB3_ACC) && (!chk || aicd_sym(g, &t, 0, &y));
    case 8: return note && (b[i].flags & TB3_SLD) && (!chk || aicd_sym(g, &t, 0, &y));
    default: return note && (b[i].flags & TB3_SLD);
    }
}
static uint32_t aicd_count(const tb3_cfg_t *g, const tb3_step_t *b, uint32_t len, uint32_t k)   /* (not the model's) */
{
    uint32_t i, n = 0;
    for (i = 0; i < len; i++)
        n += (uint32_t)aicd_is(g, b, i, len, k, 0);
    return n;
}
/* a k step of the line turned: which one the line itself says (a hash of it), not a draw, so the same model line is
 * always fitted the same way and TEMP's few lines (10 %) stay few */
static int aicd_turn(const tb3_cfg_t *g, tb3_step_t *b, uint32_t len, uint32_t k)
{
    uint8_t at[NSTEP];
    uint32_t n = 0, i, h;
    for (i = 0; i < len; i++)
        if (aicd_is(g, b, i, len, k, 1))
            at[n++] = (uint8_t)i;
    if (!n)
        return 0;
    h = 2166136261u ^ k;
    for (i = 0; i < len; i++)
        h = (h ^ (b[i].note | (uint32_t)b[i].flags << 8)) * 16777619u;
    i = at[(h >> 8) % n];
    switch (k) {
    case 0: b[i].note = (uint8_t)aicd_before(b, i, len); b[i].flags = TB3_NOTE; break;
    case 6: b[i].flags = TB3_REST; break;
    case 2: b[i].flags |= TB3_ACC; break;
    case 7: b[i].flags &= (uint8_t)~TB3_ACC; break;
    case 4: b[i].flags |= TB3_SLD; break;
    default: b[i].flags &= (uint8_t)~TB3_SLD; break;
    }
    return 1;
}
/* pct % of base steps `on` (a count of kind on), turning random ones of kind `up` / `on` to get there: all of them or
 * none exactly, else within 1 + base / 32 steps of it (the model's own lines, TEMP's too, are kept where they are near) */
static void aicd_fit1(const tb3_cfg_t *g, tb3_step_t *b, uint32_t len, uint32_t up, uint32_t on,
                      uint32_t down, uint32_t pct, uint32_t base, uint32_t min)
{
    uint32_t c = aicd_count(g, b, len, on), tot = c + aicd_count(g, b, len, up), tgt = (pct * base + 50u) / 100u;
    if (tgt < min)
        tgt = min;
    if (tgt > min && tgt < tot) {
        uint32_t slack = 1u + base / 32u;
        if (c + slack < tgt)
            tgt -= slack;
        else if (c > tgt + slack)
            tgt += slack;
        else
            return;
    }
    while (c < tgt && aicd_turn(g, b, len, up))
        c++;
    while (c > tgt && aicd_turn(g, b, len, down))
        c--;
}
/* the line to DENS (of the steps), then ACCENT and SLIDE (of the notes; no slide into a rest) as the knobs say */
static void aicd_fit(const tb3_cfg_t *g, tb3_step_t *b, uint32_t len)
{
    uint32_t n;
    aicd_fit1(g, b, len, 0, 1, 6, g->a_dens, len, 1);
    tb3_slides(b, len);
    n = aicd_count(g, b, len, 1);
    aicd_fit1(g, b, len, 2, 3, 7, g->a_acc, n, 0);
    aicd_fit1(g, b, len, 4, 5, 8, g->a_sld, n, 0);
}

/* a whole line from g and its seed (TB-3PO's rules after it: no slide into a rest, a note at least) */
static void aicd_generate(const tb3_cfg_t *g, tb3_step_t *b, uint32_t len)
{
    aicd_bias_t bi;
    aicd_at_t a;
    uint32_t r = g->seed ? g->seed : 1u, i, n = 0, any = 0, code, guard;
    len = tb3_clampi((int32_t)len, 1, NSTEP);
    for (i = 0; i < NSTEP; i++) {
        b[i].flags = TB3_REST;
        b[i].note = tb3_note(g, 0, 0);
    }
    aicd_bias(g, &bi);
    aicd_start(&a, &r);
    for (guard = 0; n < len && guard < 4u * NSTEP; guard++)   /* (pattern ends write nothing) */
        if (aicd_next(&a, &bi, &r, &code))
            aicd_put(g, &b[n++], code);
    tb3_slides(b, len);
    aicd_fit(g, b, len);
    for (i = 0; i < len; i++)
        any |= (b[i].flags & TB3_GATE) != TB3_REST;
    if (!any)
        b[0].flags = TB3_NOTE;
}
/* draw a run of the line on from the two steps before it (from a pattern's start if the model never had them), the
 * random stream on from *rng (0: from the seed) */
static void aicd_mutate(const tb3_cfg_t *g, tb3_step_t *b, uint32_t len, uint32_t *rng)
{
    aicd_bias_t bi;
    aicd_at_t a;
    uint32_t r = *rng ? *rng : g->seed ? g->seed : 1u, i0, run, n = 0, code, guard;
    len = tb3_clampi((int32_t)len, 1, NSTEP);
    run = tb3_clampi((int32_t)(len + 3u) / 4, 1, 16);
    i0 = ((tb3_u(&r) >> 8) * len) >> 16;
    aicd_bias(g, &bi);
    if (!aicd_find(g, &b[(i0 + 2u * len - 2u) % len], &b[(i0 + len - 1u) % len], &a))
        aicd_start(&a, &r);
    for (guard = 0; n < run && guard < 4u * NSTEP; guard++)
        if (aicd_next(&a, &bi, &r, &code))
            aicd_put(g, &b[(i0 + n++) % len], code);
    tb3_slides(b, len);
    *rng = r;
}
#endif

/* a hash of seed, step i and a kind (the live knobs' fixed choices: not a draw of the line's stream) */
static uint32_t tb3_h(uint32_t seed, uint32_t i, uint32_t kind)
{
    uint32_t x = seed * 2654435761u ^ (i + 1u) * 0x85EBCA6Bu ^ kind * 0xC2B2AE35u;
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    return x ^ (x >> 16);
}
/* 1.8.2: the rests' notes (X0X's keep the root): each its own, as a note's rules draw it (the root on the beat
 * ~35 %, a degree, an octave of OCTS), so DENS up (tb3_live) brings in a line, not the root over and over */
static void tb3_rest_notes(const tb3_cfg_t *g, tb3_step_t *b)
{
    const tb3_scale_t *sc = tb3_scale(g);
    uint32_t range = tb3_clampi(g->oct_range, 1, 3), i, u, deg;
    for (i = 0; i < NSTEP; i++)
        if ((b[i].flags & TB3_GATE) == TB3_REST) {
            u = tb3_h(g->seed, i, 'R');
            deg = i % 4u == 0u && (u & 255u) < 90u ? 0u : (((u >> 8) & 0xFFFFu) * sc->len) >> 16;
            b[i].note = tb3_note(g, deg, ((u >> 24) * range) >> 8);
        }
}

/* the line of g's MODE: a new one (OCT+), a MUTATE */
static void tb3_gen(const tb3_cfg_t *g, tb3_step_t *b, uint32_t len)
{
#if AICD_ON
    if (g->mode == TB3_AICD) {
        aicd_generate(g, b, len);
        return;
    }
#endif
    tb3_generate(g, b, len);
    tb3_rest_notes(g, b);
}
static void tb3_mut(const tb3_cfg_t *g, tb3_step_t *b, uint32_t len, uint32_t *rng)
{
#if AICD_ON
    if (g->mode == TB3_AICD) {
        aicd_mutate(g, b, len, rng);
        return;
    }
#endif
    tb3_mutate(g, b, len, rng);
}

/* ------------------------------------------------- the track's steps --- */
static void tb3_read(const step_t *s, tb3_step_t *b)  /* the steps as a line (what a MUTATE starts from) */
{
    uint32_t i;
    for (i = 0; i < NSTEP; i++) {
        uint8_t f = s[i].time == ST_TIE ? TB3_TIE : s[i].time == ST_NOTE && s[i].n ? TB3_NOTE : TB3_REST;
        if (f != TB3_REST)
            f |= (uint8_t)((s[i].flags & SF_ACCENT ? TB3_ACC : 0u) | (s[i].flags & SF_SLIDE ? TB3_SLD : 0u));
        b[i].note = s[i].note[0] & 127u;
        b[i].flags = f;
    }
}
/* line step b into step s: all of it (fresh: OCT+), else its note, time and ACC / SLD (the rest of the step kept) */
static void tb3_put(step_t *s, const tb3_step_t *b, int fresh)
{
    uint32_t g = b->flags & TB3_GATE;
    if (fresh) {
        s->flags = 0;
        s->vel = 0;
        s->probability = 0;
    }
    s->hit = s->acc = 0;
    s->note[0] = b->note;
    s->flags = (uint8_t)((s->flags & ~(SF_ACCENT | SF_SLIDE)) | (g != TB3_REST && (b->flags & TB3_ACC) ? SF_ACCENT : 0u) |
                         (g != TB3_REST && (b->flags & TB3_SLD) ? SF_SLIDE : 0u));
    if (g == TB3_REST) {
        s->time = ST_REST;
        s->n = 0;                                    /* (its note kept in note[0]: a later MUTATE may play it) */
    } else {
        s->time = g == TB3_TIE ? ST_TIE : ST_NOTE;
        s->n = s->n && !fresh ? s->n : 1u;
        if (!s->vel)
            s->vel = 96;
    }
}
/* the line b into the steps: fresh (every step), else only those that differ from old (tb3_read of them) */
static void tb3_write(step_t *s, const tb3_step_t *b, const tb3_step_t *old)
{
    uint32_t i;
    for (i = 0; i < NSTEP; i++)
        if (!old || b[i].note != old[i].note || b[i].flags != old[i].flags)
            tb3_put(&s[i], &b[i], !old);
}

/* ------------------------------------------- live knobs (1.8.2) --- */
/* DENS ACCENT SLIDE OCTS ROOT SCALE OCTAVE act on the line that plays, at its next bar (tb3_bar): the line as the
 * anchor (the last OCT+ / OCT- / auto MUTATE's, or the steps as edited) with the knobs moved from the anchor's (k0) to
 * now's. A function of the anchor and the knobs only: back to k0, back to the anchor; never a new draw. DENS turns the
 * steps on / off, ACCENT / SLIDE their flags, in a fixed order of the seed (tb3_order: the same steps go first every
 * time); the anchor's share moved towards none / all (tb3_share). Pitches: ROOT OCTAVE transpose, SCALE snaps each step
 * to its nearest degree, OCTS folds / spreads the octaves (each step's own hash). A rest keeps its note: DENS up plays
 * it. Steps TB-3PO did not write (tb3_lhash != wrote): a new anchor, the knobs it was at kapp. */
static tb3_knobs_t tb3_knobs(const tb3_cfg_t *g)
{
    tb3_knobs_t k;
    int a = AICD_ON && g->mode == TB3_AICD;
    k.dens = a ? g->a_dens : g->density;
    k.acc = a ? g->a_acc : g->accent;
    k.sld = a ? g->a_sld : g->slide;
    k.octs = a ? 0u : g->oct_range;
    k.root = g->root;
    k.scale = (uint8_t)(a ? g->a_scale : g->scale + 1u);
    k.base = g->base_oct;
    return k;
}
static uint32_t tb3_lhash(const tb3_step_t *b)       /* a line's FNV-1a */
{
    uint32_t h = 2166136261u, i;
    for (i = 0; i < NSTEP; i++)
        h = (h ^ (b[i].note | (uint32_t)b[i].flags << 8)) * 16777619u;
    return h;
}
static void tb3_order(uint32_t seed, uint32_t kind, uint8_t *o)   /* the steps in a fixed order (Fisher-Yates) */
{
    uint32_t r = tb3_h(seed, 0, kind), i, j;
    uint8_t x;
    for (i = 0; i < NSTEP; i++)
        o[i] = (uint8_t)i;
    for (i = NSTEP - 1u; i; i--) {
        j = (tb3_u(&r) * (i + 1u)) >> 24;
        x = o[i];
        o[i] = o[j];
        o[j] = x;
    }
}
/* how many of tot are on at knob k: the anchor's c0 of tot0 (at k0) moved in proportion towards none (k < k0) or all
 * (k > k0); k0 keeps the anchor's share, 0 / 100 % none / all, a step of the knob never a jump (rounded) */
static uint32_t tb3_share(uint32_t c0, uint32_t tot0, uint32_t tot, uint32_t k0, uint32_t k)
{
    uint32_t num, den;
    if (!tot0) {
        num = k;
        den = 100u;
    } else if (k == k0) {
        num = c0;
        den = tot0;
    } else if (k < k0) {
        num = c0 * k;
        den = tot0 * k0;
    } else {
        num = c0 * (100u - k0) + (tot0 - c0) * (k - k0);
        den = tot0 * (100u - k0);
    }
    return (tot * num + den / 2u) / den;
}
static int tb3_gated(const tb3_step_t *s) { return (s->flags & TB3_GATE) != TB3_REST; }
static int tb3_has(const tb3_step_t *s, uint32_t f) { return f ? (s->flags & f) != 0u : tb3_gated(s); }
static int tb3_elig(const tb3_step_t *b, uint32_t i, uint32_t len, uint32_t f)   /* f (0: the gate) can be on step i */
{
    return !f || (tb3_gated(&b[i]) && (f == TB3_ACC || tb3_gated(&b[(i + 1u) % len])));   /* (a slide: into a note) */
}
/* flag f (0: the gate, else ACC / SLD) of b's steps to the share knob k says (the anchor a's at k0), in seed's order */
static void tb3_fit(const tb3_step_t *a, tb3_step_t *b, uint32_t len, uint32_t f, uint32_t k0, uint32_t k,
                    uint32_t seed)
{
    uint8_t o[NSTEP];
    uint32_t i, j, c0 = 0, t0 = 0, c = 0, t = 0, n, x;
    int less;
    for (i = 0; i < len; i++) {
        if (tb3_elig(a, i, len, f)) {
            t0++;
            c0 += (uint32_t)tb3_has(&a[i], f);
        }
        if (tb3_elig(b, i, len, f)) {
            t++;
            c += (uint32_t)tb3_has(&b[i], f);
        }
    }
    n = tb3_share(c0, t0, t, k0, k);
    if (n == c)
        return;
    less = n < c;
    tb3_order(seed, f + 1u, o);
    for (j = 0, i = 0; j < NSTEP; j++) {             /* fewer: the first n in order stay; more: the first off come on */
        x = o[j];
        if (x >= len || !tb3_elig(b, x, len, f) || tb3_has(&b[x], f) != less)
            continue;
        if (less) {
            if (i++ < n)
                continue;
        } else if (c++ >= n)
            break;
        if (!f)
            b[x].flags = less ? TB3_REST : TB3_NOTE;
        else
            b[x].flags = (uint8_t)(less ? b[x].flags & ~f : b[x].flags | f);
    }
}
/* a note from the anchor's knobs k0 to k (h: the step's hash) */
static uint8_t tb3_repitch(uint32_t note, const tb3_knobs_t *k0, const tb3_knobs_t *k, uint32_t h)
{
    int32_t off = (int32_t)note - (12 * (k0->base + 1) + k0->root), o, w, best, d, bd, i;
    o = off >= 0 ? off / 12 : -((11 - off) / 12);
    w = off - 12 * o;
    if (k->scale != k0->scale && k->scale) {         /* the nearest degree (12: the root above; ties down) */
        const tb3_scale_t *sc = &TB3_SCALES[(k->scale - 1u) % TB3_NSCALES];
        for (best = 12, bd = 12 - w, i = 0; i < sc->len; i++) {
            d = sc->deg[i] > w ? sc->deg[i] - w : w - sc->deg[i];
            if (d < bd || (d == bd && sc->deg[i] < best)) {
                bd = d;
                best = sc->deg[i];
            }
        }
        o += best == 12;
        w = best % 12;
    }
    if (k->octs != k0->octs && k->octs && k0->octs) {   /* fewer: folded in; more: a share moves up */
        int32_t x = (int32_t)(h % k->octs);
        if (!(o < k->octs && x < k0->octs))
            o = x;
    }
    return (uint8_t)tb3_clampi(12 * (k->base + 1) + k->root + 12 * o + w, 0, 127);
}
/* the line the knobs k make of anchor a (made at k0): b, len steps (the pitches of all NSTEP) */
static void tb3_live_line(const tb3_step_t *a, const tb3_knobs_t *k0, const tb3_knobs_t *k, uint32_t seed,
                          uint32_t len, tb3_step_t *b)
{
    uint32_t i;
    len = tb3_clampi((int32_t)len, 1, NSTEP);
    memcpy(b, a, NSTEP * sizeof *b);
    tb3_fit(a, b, len, 0, k0->dens, k->dens, seed);
    tb3_fit(a, b, len, TB3_ACC, k0->acc, k->acc, seed);
    tb3_fit(a, b, len, TB3_SLD, k0->sld, k->sld, seed);
    for (i = 0; i < NSTEP; i++)
        b[i].note = tb3_repitch(a[i].note, k0, k, tb3_h(seed, i, 'O'));
}
/* the line track k's steps hold (just written) is the anchor, at the knobs now (OCT+ / OCT- / auto MUTATE drew it) */
static void tb3_anchor(uint32_t k, const step_t *s)
{
    tb3_rt_t *rt = &tb3_rt[k % NTRK];
    tb3_read(s, rt->anchor);
    rt->k0 = rt->kapp = tb3_knobs(&tb3_cfg[k % NTRK]);
    rt->wrote = tb3_lhash(rt->anchor);
    rt->have = 1;
    rt->pending = 0;
}
static void tb3_rebase(uint32_t k)                   /* no anchor (a load, MODE): the steps one, at the knobs now */
{
    tb3_rt[k % NTRK].have = tb3_rt[k % NTRK].pending = 0;
    tb3_rt[k % NTRK].kapp = tb3_knobs(&tb3_cfg[k % NTRK]);
}
/* the knobs into track t's steps (the audio ISR's at a bar, tb3_bar; the UI's stopped, IRQs off) */
static void tb3_live(track_t *t)
{
    uint32_t k = trk_index(t);
    tb3_rt_t *rt = &tb3_rt[k];
    tb3_knobs_t kn = tb3_knobs(&tb3_cfg[k]);
    tb3_step_t cur[NSTEP], b[NSTEP];
    rt->pending = 0;
    tb3_read(t->step, cur);
    if (!rt->have || tb3_lhash(cur) != rt->wrote) {  /* edited (or loaded): those steps the anchor */
        memcpy(rt->anchor, cur, sizeof cur);
        rt->k0 = rt->kapp;
        rt->have = 1;
    }
    tb3_live_line(rt->anchor, &rt->k0, &kn, tb3_cfg[k].seed, (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP), b);
    tb3_write(t->step, b, cur);
    tb3_read(t->step, cur);
    rt->wrote = tb3_lhash(cur);
    rt->kapp = kn;
}

/* track t's next line b from its steps old (tb3_read of them): a new line (fresh: OCT+), else OCT- / auto MUTATE's
 * with *rng */
static void tb3_line(const track_t *t, const tb3_step_t *old, tb3_step_t *b, int fresh, uint32_t *rng)
{
    uint32_t k = trk_index(t), len = (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP);
    if (fresh)
        tb3_gen(&tb3_cfg[k], b, len);
    else {
        memcpy(b, old, NSTEP * sizeof *b);
        tb3_mut(&tb3_cfg[k], b, len, rng);
    }
}
/* the steps track t plays from its TB-3PO, at once: the audio ISR's (seq_enter: auto MUTATE) and the tests' (the UI
 * works the line out with the ISR on: ui_tb3po.c tb3_oct) */
static void tb3_apply(track_t *t, int fresh, uint32_t *rng)
{
    tb3_step_t b[NSTEP], old[NSTEP];
    if (!fresh && tb3_rt[trk_index(t)].pending)
        tb3_live(t);                                 /* (a MUTATE from the line the knobs say) */
    tb3_read(t->step, old);
    tb3_line(t, old, b, fresh, rng);
    tb3_write(t->step, b, fresh ? 0 : old);
    tb3_anchor(trk_index(t), t->step);
}

/* a pass of t's line starts (seq.c seq_enter, step 1; top: PLAY's or a section's first, which counts from 0): every
 * mutate_bars passes a MUTATE before it plays, X0X's auto_mutate (not in a song: its steps are a slot's then) */
static __attribute__((noinline)) void tb3_pass(track_t *t, uint32_t top)
{
    uint32_t k = trk_index(t), m = tb3_cfg[k].mutate_bars;
    if (top) {
        tb3_rt[k].passes = 0;
        return;
    }
    tb3_rt[k].passes++;
    if (m && !chain.running && t->eng_req % NENGINES == ENGI_303 && tb3_rt[k].passes % m == 0u) {
        tb3_apply(t, 0, &tb3_rt[k].rng);             /* (NEW LINE's star stays as it was, as X0X's) */
        tb3_auto_n++;
    }
}
/* a bar of t's line starts (seq.c seq_enter: every 16th step, step 1 too; a 303 track): its auto MUTATE, then (1.8.2) a
 * live knob turned since (not in a song: it waits) */
static __attribute__((noinline)) void tb3_bar(track_t *t, uint32_t top)
{
    if (!t->seq_idx)
        tb3_pass(t, top);
    if (tb3_rt[trk_index(t)].pending && !chain.running) {
        tb3_live(t);
        tb3_auto_n++;                                /* (the screen redraws; the UI's OCT- sees the steps changed) */
    }
}

/* ------------------------------------------------------------ project --- */
/* a 303 track's FM6 area in a project (project.c: its FM6 patch is the init one then): "TB3P", version 1, the eight
 * settings, the seed in five 7-bit bytes; 1.6.2: 1, MODE, TEMP (two bytes), AICD's DENS ACCENT SLIDE (0..100), SCALE
 * (before: zeros there, MODE TB-3PO and AICD's defaults); zeros; every byte 0..127 (proj_pack) */
#define TB3_PK_VER 1u
static void tb3_pack(uint32_t k, uint8_t *pk)
{
    const tb3_cfg_t *g = &tb3_cfg[k % NTRK];
    uint32_t i;
    memset(pk, 0, FM6_PACKED);
    pk[0] = 'T'; pk[1] = 'B'; pk[2] = '3'; pk[3] = 'P';
    pk[4] = TB3_PK_VER;
    pk[5] = g->density; pk[6] = g->accent; pk[7] = g->slide; pk[8] = g->oct_range;
    pk[9] = g->root; pk[10] = g->scale; pk[11] = g->base_oct; pk[12] = g->mutate_bars;
    for (i = 0; i < 5u; i++)
        pk[13u + i] = (uint8_t)((g->seed >> (7u * i)) & 127u);
    pk[18] = 1;
    pk[19] = g->mode;
    pk[20] = g->temp & 127u;
    pk[21] = (uint8_t)(g->temp >> 7);
    pk[22] = g->a_dens;
    pk[23] = g->a_acc;
    pk[24] = g->a_sld;
    pk[25] = g->a_scale;
}
/* track k's settings from a project's FM6 area: 1 = it holds them (each value into its range), else the defaults, 0 */
static int tb3_unpack(uint32_t k, const uint8_t *pk)
{
    tb3_cfg_t *g = &tb3_cfg[k % NTRK];
    uint32_t i, seed = 0;
    tb3_rt[k % NTRK].rng = tb3_rt[k % NTRK].passes = tb3_rt[k % NTRK].stale = 0;
    if (pk[0] != 'T' || pk[1] != 'B' || pk[2] != '3' || pk[3] != 'P' || pk[4] != TB3_PK_VER) {
        tb3_defaults(g, 0);
        tb3_rebase(k);
        return 0;
    }
    g->density = (uint8_t)tb3_clampi(pk[5], 0, 100);
    g->accent = (uint8_t)tb3_clampi(pk[6], 0, 100);
    g->slide = (uint8_t)tb3_clampi(pk[7], 0, 100);
    g->oct_range = (uint8_t)tb3_clampi(pk[8], 1, 3);
    g->root = (uint8_t)tb3_clampi(pk[9], 0, 11);
    g->scale = (uint8_t)tb3_clampi(pk[10], 0, TB3_NSCALES - 1);
    g->base_oct = (uint8_t)tb3_clampi(pk[11], 0, 4);
    g->mutate_bars = (uint8_t)tb3_clampi(pk[12], 0, 16);
    for (i = 0; i < 5u; i++)
        seed |= (uint32_t)(pk[13u + i] & 127u) << (7u * i);
    g->seed = seed ? seed : 1u;
    g->mode = TB3_TB3PO;
    g->temp = 100;
    g->a_dens = AICD_DENS;
    g->a_acc = AICD_ACC;
    g->a_sld = AICD_SLD;
    g->a_scale = 0;
    if (pk[18] == 1u) {                              /* 1.6.2: MODE and AICD's settings */
        g->mode = (uint8_t)(AICD_ON ? tb3_clampi(pk[19], 0, 1) : 0);
        g->temp = (uint8_t)tb3_clampi(pk[20] | pk[21] << 7, 10, 200);
        g->a_dens = (uint8_t)tb3_clampi(pk[22], 0, 100);
        g->a_acc = (uint8_t)tb3_clampi(pk[23], 0, 100);
        g->a_sld = (uint8_t)tb3_clampi(pk[24], 0, 100);
        g->a_scale = (uint8_t)tb3_clampi(pk[25], 0, TB3_NSCALES);
    }
    tb3_rebase(k);
    return 1;
}
static void tb3_reset(void)                           /* power-on: every track X0X's defaults */
{
    uint32_t k;
    for (k = 0; k < NTRK; k++) {
        tb3_defaults(&tb3_cfg[k], 0);
        tb3_rt[k].rng = tb3_rt[k].passes = tb3_rt[k].stale = 0;
        tb3_rebase(k);
    }
}
