/* SPDX-License-Identifier: GPL-3.0-only
 * acidgen: Felucca's 303 acid line generator as a portable library (acidgen.h has the API, INTEGRATION.md the how).
 * Free software under the GNU General Public License, version 3 only (LICENSES/GPL-3.0.txt).
 *
 * The same code as Felucca's firmware/src/tb3po.c (github.com/hugelton/Felucca), its track and UI glue left out:
 *   TB-3PO: X0X's generator (github.com/charlesvestal/fm1-x0x at 54194c5, GPL-3.0-only), itself a port of
 *   schwung-tb3po (Charles Vestal), after the Phazerville Hemisphere Suite's TB_3PO applet (djphazer and contributors,
 *   GPL-3.0). Integer only and the same line as X0X's for the same seed and settings: its random draws in the same
 *   order (xorshift32, u = 24 bits), each float comparison of u / 2^24 against a float d / 100.0f as an exact integer
 *   one against that float x 2^30 (ACID_THR), each (int)(u / 2^24 * len) with the float product's rounding
 *   (acid_pick). test/acidgen_test.c checks it against X0X's float code (test/ref/).
 *   AICD: Aicd's model (Michael Ganss, github.com/mganss/Aicd, MIT: a maximum-likelihood n-gram trained on ABL3
 *   patterns, as it is: tools/gen_aicd.py, acidgen_model.h): each step drawn from what followed the two steps before
 *   it there. DENS ACCENT SLIDE are TB-3PO's 0..100 %, the share of the steps that are notes and of the notes accented /
 *   sliding (defaults 90 / 35 / 32: the model's own): they bias the model's choices where it has some (aicd_bias), and
 *   a new line is then fitted to them (aicd_fit: steps turned on or off, only into steps the model has). SCALE biases
 *   too (CHRO: none), TEMP sharpens (< 100 %) or flattens (> 100 %) the odds; ROOT and OCTAVE transpose (its C on the
 *   root); OCTS is TB-3PO's. A MUTATE draws a run of the line (a quarter, 16 steps at most) on from the two steps
 *   before it, biased but not fitted (the rest of the line stays). */
#include "acidgen.h"

typedef struct { int8_t deg[7]; uint8_t len; } acid_scale_t;
static const acid_scale_t ACID_SCALES[ACID_NSCALES] = {   /* X0X's (schwung-tb3po's), same order */
    {{0, 2, 3, 5, 7, 8, 10}, 7},                       /* MIN */
    {{0, 1, 3, 5, 7, 8, 10}, 7},                       /* PHRY */
    {{0, 2, 3, 5, 7, 8, 11}, 7},                       /* HARM */
    {{0, 3, 5, 7, 10, 0, 0}, 5},                       /* MPEN */
    {{0, 2, 3, 5, 7, 9, 10}, 7},                       /* DOR */
    {{0, 2, 4, 5, 7, 9, 11}, 7},                       /* MAJ */
};
const char *const acid_scale_names[ACID_NSCALES] = {"MIN", "PHRY", "HARM", "MPEN", "DOR", "MAJ"};
const char *const acid_aicd_scale_names[ACID_NSCALES + 1] = {"CHRO", "MIN", "PHRY", "HARM", "MPEN", "DOR", "MAJ"};
                                 /* AICD's SCALE: CHRO (no bias), then ACID_SCALES' */
/* (float)d / 100.0f x 2^30, d = 0..100 (exact: test/acidgen_test.c computes them from the floats) */
static const uint32_t ACID_THR[101] = {
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
#define ACID_THR_35 375809632u    /* 0.35f, 0.25f, 0.5f x 2^30 */
#define ACID_THR_25 268435456u
#define ACID_THR_50 536870912u

void acid_defaults(acid_cfg_t *g, uint32_t seed)   /* X0X tb3po_defaults */
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
    g->mode = ACID_MODE_TB3PO;
    g->temp = 100;
    g->a_dens = ACID_AICD_DENS;
    g->a_acc = ACID_AICD_ACC;
    g->a_sld = ACID_AICD_SLD;
    g->a_scale = 0;
}

static uint32_t acid_u(uint32_t *r)                   /* a draw: xorshift32, its low 24 bits (X0X rng_f's u / 2^24) */
{
    uint32_t x = *r ? *r : 1u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *r = x;
    return x & 0xFFFFFFu;
}
static uint32_t acid_pct(uint32_t v) { return ACID_THR[v <= 100u ? v : 100u]; }
/* (int)(u / 2^24 * len), len 1..7, as the float product rounds: u * len to 24 significant bits (nearest, ties to
 * even), then its whole part; len - 1 at most */
static uint32_t acid_pick(uint32_t u, uint32_t len)
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
static uint32_t acid_clampi(int32_t v, int32_t lo, int32_t hi) { return (uint32_t)(v < lo ? lo : v > hi ? hi : v); }
static const acid_scale_t *acid_scale(const acid_cfg_t *g) { return &ACID_SCALES[g->scale < ACID_NSCALES ? g->scale : 0]; }
static uint8_t acid_note(const acid_cfg_t *g, uint32_t degree, uint32_t oct)
{
    return (uint8_t)acid_clampi(12 * (g->base_oct + 1) + g->root + acid_scale(g)->deg[degree] + 12 * (int32_t)oct, 0, 127);
}
static uint8_t acid_kind(uint32_t *r, const acid_cfg_t *g)   /* X0X roll_kind: NOTE, ACC, SLD (not both) */
{
    uint8_t f = ACID_NOTE;
    if ((acid_u(r) << 6) < acid_pct(g->accent))
        f = ACID_NOTE | ACID_ACC;
    if ((acid_u(r) << 6) < acid_pct(g->slide))
        f = ACID_NOTE | ACID_SLD;
    return f;
}
static void acid_slides(acid_step_t *b, uint32_t len)  /* a slide into a rest does nothing */
{
    uint32_t i;
    for (i = 0; i < len; i++)
        if ((b[i].flags & ACID_SLD) && (b[(i + 1u) % len].flags & ACID_GATE) == ACID_REST)
            b[i].flags = ACID_NOTE;
}

/* a whole line (len steps, ACID_NSTEP written) from g and its seed (X0X tb3po_generate) */
void acid_tb3po_generate(const acid_cfg_t *g, acid_step_t *b, uint32_t len)
{
    const acid_scale_t *sc = acid_scale(g);
    uint32_t range = acid_clampi(g->oct_range, 1, 3), r = g->seed ? g->seed : 1u, i, deg, oct, any = 0;
    len = acid_clampi((int32_t)len, 1, ACID_NSTEP);
    for (i = 0; i < ACID_NSTEP; i++) {
        b[i].flags = ACID_REST;
        b[i].note = acid_note(g, 0, 0);
    }
    for (i = 0; i < len; i++) {
        if ((acid_u(&r) << 6) > acid_pct(g->density))
            continue;                                /* a rest */
        if (i % 4u == 0u && (acid_u(&r) << 6) < ACID_THR_35)
            deg = 0;                                 /* on the beat: the root, often */
        else
            deg = acid_pick(acid_u(&r), sc->len);
        oct = acid_pick(acid_u(&r), range);
        b[i].note = acid_note(g, deg, oct);
        b[i].flags = acid_kind(&r, g);
    }
    acid_slides(b, len);
    for (i = 0; i < len; i++)
        any |= (b[i].flags & ACID_GATE) != ACID_REST;
    if (!any) {
        b[0].note = acid_note(g, 0, 0);
        b[0].flags = ACID_NOTE;
    }
}

/* re-roll ~25 % of the steps, the random stream on from *rng (0: from the seed; X0X tb3po_mutate) */
void acid_tb3po_mutate(const acid_cfg_t *g, acid_step_t *b, uint32_t len, uint32_t *rng)
{
    const acid_scale_t *sc = acid_scale(g);
    uint32_t range = acid_clampi(g->oct_range, 1, 3), r = *rng ? *rng : g->seed ? g->seed : 1u, i, deg;
    len = acid_clampi((int32_t)len, 1, ACID_NSTEP);
    for (i = 0; i < len; i++) {
        if ((acid_u(&r) << 6) >= ACID_THR_25)
            continue;
        if ((acid_u(&r) << 6) < ACID_THR_50) {         /* the step */
            if ((acid_u(&r) << 6) > acid_pct(g->density))
                b[i].flags = ACID_REST;
            else
                b[i].flags = acid_kind(&r, g);
        } else {                                     /* its note */
            deg = acid_pick(acid_u(&r), sc->len);
            b[i].note = acid_note(g, deg, acid_pick(acid_u(&r), range));
        }
    }
    acid_slides(b, len);
    *rng = r;
}

uint32_t acid_new_seed(uint32_t entropy)       /* X0X tb3po_new_seed */
{
    uint32_t r = entropy * 2654435761u + 0x9E3779B9u;
    (void)acid_u(&r);
    (void)acid_u(&r);
    return r ? r : 1u;
}

/* ---------------------------------------------------------- AICD mode --- */
#if ACIDGEN_AICD
#include "acidgen_model.h"
/* The model (tools/gen_aicd.py has its format; acidgen_model.h): a node is two steps (an 18-character context of Aicd's), its edges the
 * steps that followed them, each into the node of the step before and it. A step reads a node's record and its edges'
 * nodes' last symbols (their steps), draws one, walks to its record (7 from a checkpoint at most): ~1.5 k
 * instructions a step on the host, a 16-step MUTATE ~25 k (test/acidgen_test.c). Every weight in 1/16 log2: the
 * record's (1/3 log2 under the likeliest) x 100 / TEMP, the biases on top, then 2^-w. */
#define AICD_OFFSCALE 128        /* SCALE: a note off it 2^-8 as likely */
static const uint16_t AICD_EXP[16] = {65535, 62757, 60097, 57549, 55109, 52773, 50535, 48393, 46341, 44376, 42495,
                                      40693, 38968, 37316, 35734, 34219};   /* 2^16 x 2^(-i/16) (the first 2^16 - 1) */
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
    aicd_seek(AICD_START[((acid_u(r) >> 8) * AICD_NSTART) >> 16], a);
}
/* a share p % (0..100) as a bias in 1/16 log2: 3/2 per point off the model's own share nat, 2^-8 .. 2^8 at the ends
 * (the model alone gets ACC to 13 %..69 %: aicd_fit does the rest) */
static int32_t aicd_lean(uint32_t p, uint32_t nat)
{
    int32_t v = ((int32_t)p - (int32_t)nat) * 3 / 2;
    return 2 * (v < -64 ? -64 : v > 63 ? 63 : v);
}
static void aicd_bias(const acid_cfg_t *g, aicd_bias_t *bi)
{
    uint32_t i, sc = g->a_scale;
    bi->k = 16u * 100u * 256u / AICD_WQ / acid_clampi(g->temp, 10, 200);
    bi->dens = aicd_lean(g->a_dens, ACID_AICD_DENS);
    bi->acc = aicd_lean(g->a_acc, ACID_AICD_ACC);
    bi->sld = aicd_lean(g->a_sld, ACID_AICD_SLD);
    bi->mask = sc ? 0u : 0xFFFu;
    if (sc && sc <= ACID_NSCALES)
        for (i = 0; i < ACID_SCALES[sc - 1u].len; i++)
            bi->mask |= 1u << ACID_SCALES[sc - 1u].deg[i];
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
    x = (uint32_t)(((uint64_t)acid_u(r) * sum) >> 24);
    for (i = 0; x >= (uint32_t)s[i]; i++)
        x -= (uint32_t)s[i];
    aicd_seek(e[i].n, a);
    if (a->last >= AICD_NSTEP)
        return 0;
    *code = AICD_CODE[a->last];
    return 1;
}
static void aicd_put(const acid_cfg_t *g, acid_step_t *b, uint32_t code)   /* a model step into the line */
{
    b->note = (uint8_t)acid_clampi(12 * (g->base_oct + 1) + g->root + (int32_t)(code >> 3) - 16, 0, 127);
    b->flags = (uint8_t)(code & 4u ? ACID_NOTE | (code & 2u ? ACID_ACC : 0u) | (code & 1u ? ACID_SLD : 0u) : ACID_REST);
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
static int aicd_sym(const acid_cfg_t *g, const acid_step_t *b, uint32_t v, uint32_t *sym)
{
    uint32_t gate = b->flags & ACID_GATE, code, lo;
    int32_t o = (int32_t)b->note - 12 * (g->base_oct + 1) - g->root;
    if (gate == ACID_TIE || o < -16 || o > 47)
        return 0;
    code = (uint32_t)(o + 16) << 3 | (gate == ACID_NOTE ? 4u | (b->flags & ACID_ACC ? 2u : 0u) |
                                                         (b->flags & ACID_SLD ? 1u : 0u) : v);
    lo = aicd_lb(code);
    *sym = lo;
    return lo < AICD_NSTEP && AICD_CODE[lo] == code;
}
static int aicd_rest_ok(const acid_cfg_t *g, uint32_t note)   /* the model has a rest on this note (any ACC SLD) */
{
    int32_t o = (int32_t)note - 12 * (g->base_oct + 1) - g->root;
    uint32_t code = (uint32_t)(o + 16) << 3, lo;
    if (o < -16 || o > 47)
        return 0;
    lo = aicd_lb(code);
    return lo < AICD_NSTEP && AICD_CODE[lo] < (code | 4u);
}
/* the node of line steps b2, b1 (a REST any of its ACC SLD the model has), 1; 0: the model never had them */
static int aicd_find(const acid_cfg_t *g, const acid_step_t *b2, const acid_step_t *b1, aicd_at_t *a)
{
    uint32_t v2, v1, x, y;
    for (v2 = 0; v2 < ((b2->flags & ACID_GATE) == ACID_REST ? 4u : 1u); v2++)
        for (v1 = 0; v1 < ((b1->flags & ACID_GATE) == ACID_REST ? 4u : 1u); v1++) {
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
static uint32_t aicd_before(const acid_step_t *b, uint32_t i, uint32_t len)   /* the note of the note before step i */
{
    uint32_t j;
    for (j = 1; j < len; j++)
        if ((b[(i + len - j) % len].flags & ACID_GATE) != ACID_REST)
            return b[(i + len - j) % len].note;
    return b[i].note;
}
static int aicd_is(const acid_cfg_t *g, const acid_step_t *b, uint32_t i, uint32_t len, uint32_t k, int chk)
{
    uint32_t note = (b[i].flags & ACID_GATE) != ACID_REST, rest = (b[(i + 1u) % len].flags & ACID_GATE) == ACID_REST, y;
    acid_step_t t = b[i];
    switch (chk ? k : 99u) {                         /* (t: the step turned, to look up) */
    case 0: t.note = (uint8_t)aicd_before(b, i, len); t.flags = ACID_NOTE; break;
    case 2: t.flags |= ACID_ACC; break;
    case 4: t.flags |= ACID_SLD; break;
    case 6: t.flags = ACID_REST; break;
    case 7: t.flags &= (uint8_t)~ACID_ACC; break;
    case 8: t.flags &= (uint8_t)~ACID_SLD; break;
    default: break;
    }
    switch (k) {
    case 0: return !note && (!chk || aicd_sym(g, &t, 0, &y));
    case 1: return note;
    case 2: return note && !(b[i].flags & ACID_ACC) && (!chk || aicd_sym(g, &t, 0, &y));
    case 3: return note && (b[i].flags & ACID_ACC);
    case 4: return note && !rest && !(b[i].flags & ACID_SLD) && (!chk || aicd_sym(g, &t, 0, &y));
    case 6: return note && !(b[(i + len - 1u) % len].flags & ACID_SLD) && (!chk || aicd_rest_ok(g, t.note));
    case 7: return note && (b[i].flags & ACID_ACC) && (!chk || aicd_sym(g, &t, 0, &y));
    case 8: return note && (b[i].flags & ACID_SLD) && (!chk || aicd_sym(g, &t, 0, &y));
    default: return note && (b[i].flags & ACID_SLD);
    }
}
static uint32_t aicd_count(const acid_cfg_t *g, const acid_step_t *b, uint32_t len, uint32_t k)   /* (not the model's) */
{
    uint32_t i, n = 0;
    for (i = 0; i < len; i++)
        n += (uint32_t)aicd_is(g, b, i, len, k, 0);
    return n;
}
/* a k step of the line turned: which one the line itself says (a hash of it), not a draw, so the same model line is
 * always fitted the same way and TEMP's few lines (10 %) stay few */
static int aicd_turn(const acid_cfg_t *g, acid_step_t *b, uint32_t len, uint32_t k)
{
    uint8_t at[ACID_NSTEP];
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
    case 0: b[i].note = (uint8_t)aicd_before(b, i, len); b[i].flags = ACID_NOTE; break;
    case 6: b[i].flags = ACID_REST; break;
    case 2: b[i].flags |= ACID_ACC; break;
    case 7: b[i].flags &= (uint8_t)~ACID_ACC; break;
    case 4: b[i].flags |= ACID_SLD; break;
    default: b[i].flags &= (uint8_t)~ACID_SLD; break;
    }
    return 1;
}
/* pct % of base steps `on` (a count of kind on), turning random ones of kind `up` / `on` to get there: all of them or
 * none exactly, else within 1 + base / 32 steps of it (the model's own lines, TEMP's too, are kept where they are near) */
static void aicd_fit1(const acid_cfg_t *g, acid_step_t *b, uint32_t len, uint32_t up, uint32_t on,
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
static void aicd_fit(const acid_cfg_t *g, acid_step_t *b, uint32_t len)
{
    uint32_t n;
    aicd_fit1(g, b, len, 0, 1, 6, g->a_dens, len, 1);
    acid_slides(b, len);
    n = aicd_count(g, b, len, 1);
    aicd_fit1(g, b, len, 2, 3, 7, g->a_acc, n, 0);
    aicd_fit1(g, b, len, 4, 5, 8, g->a_sld, n, 0);
}

/* a whole line from g and its seed (TB-3PO's rules after it: no slide into a rest, a note at least) */
void acid_aicd_generate(const acid_cfg_t *g, acid_step_t *b, uint32_t len)
{
    aicd_bias_t bi;
    aicd_at_t a;
    uint32_t r = g->seed ? g->seed : 1u, i, n = 0, any = 0, code, guard;
    len = acid_clampi((int32_t)len, 1, ACID_NSTEP);
    for (i = 0; i < ACID_NSTEP; i++) {
        b[i].flags = ACID_REST;
        b[i].note = acid_note(g, 0, 0);
    }
    aicd_bias(g, &bi);
    aicd_start(&a, &r);
    for (guard = 0; n < len && guard < 4u * ACID_NSTEP; guard++)   /* (pattern ends write nothing) */
        if (aicd_next(&a, &bi, &r, &code))
            aicd_put(g, &b[n++], code);
    acid_slides(b, len);
    aicd_fit(g, b, len);
    for (i = 0; i < len; i++)
        any |= (b[i].flags & ACID_GATE) != ACID_REST;
    if (!any)
        b[0].flags = ACID_NOTE;
}
/* draw a run of the line on from the two steps before it (from a pattern's start if the model never had them), the
 * random stream on from *rng (0: from the seed) */
void acid_aicd_mutate(const acid_cfg_t *g, acid_step_t *b, uint32_t len, uint32_t *rng)
{
    aicd_bias_t bi;
    aicd_at_t a;
    uint32_t r = *rng ? *rng : g->seed ? g->seed : 1u, i0, run, n = 0, code, guard;
    len = acid_clampi((int32_t)len, 1, ACID_NSTEP);
    run = acid_clampi((int32_t)(len + 3u) / 4, 1, 16);
    i0 = ((acid_u(&r) >> 8) * len) >> 16;
    aicd_bias(g, &bi);
    if (!aicd_find(g, &b[(i0 + 2u * len - 2u) % len], &b[(i0 + len - 1u) % len], &a))
        aicd_start(&a, &r);
    for (guard = 0; n < run && guard < 4u * ACID_NSTEP; guard++)
        if (aicd_next(&a, &bi, &r, &code))
            aicd_put(g, &b[(i0 + n++) % len], code);
    acid_slides(b, len);
    *rng = r;
}
#endif

/* the line of g's MODE */
void acid_generate(const acid_cfg_t *g, acid_step_t *line, uint32_t len)
{
#if ACIDGEN_AICD
    if (g->mode == ACID_MODE_AICD) {
        acid_aicd_generate(g, line, len);
        return;
    }
#endif
    acid_tb3po_generate(g, line, len);
}
void acid_mutate(const acid_cfg_t *g, acid_step_t *line, uint32_t len, uint32_t *rng)
{
#if ACIDGEN_AICD
    if (g->mode == ACID_MODE_AICD) {
        acid_aicd_mutate(g, line, len, rng);
        return;
    }
#endif
    acid_tb3po_mutate(g, line, len, rng);
}

/* ---------------------------------------------------------- settings --- */
/* "TB3P", version 1, the eight settings, the seed in five 7-bit bytes; 1, MODE, TEMP (two bytes), AICD's DENS ACCENT
 * SLIDE (+ 64), SCALE; every byte 0..127 (Felucca's tb3_pack: a 303 track's block in its projects) */
#define ACID_PK_VER 1u
void acid_pack(const acid_cfg_t *g, uint8_t *pk)
{
    uint32_t i;
    for (i = 0; i < ACID_PACKED; i++)
        pk[i] = 0;
    pk[0] = 'T'; pk[1] = 'B'; pk[2] = '3'; pk[3] = 'P';
    pk[4] = ACID_PK_VER;
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
int acid_unpack(acid_cfg_t *g, const uint8_t *pk)
{
    uint32_t i, seed = 0;
    if (pk[0] != 'T' || pk[1] != 'B' || pk[2] != '3' || pk[3] != 'P' || pk[4] != ACID_PK_VER) {
        acid_defaults(g, 0);
        return 0;
    }
    g->density = (uint8_t)acid_clampi(pk[5], 0, 100);
    g->accent = (uint8_t)acid_clampi(pk[6], 0, 100);
    g->slide = (uint8_t)acid_clampi(pk[7], 0, 100);
    g->oct_range = (uint8_t)acid_clampi(pk[8], 1, 3);
    g->root = (uint8_t)acid_clampi(pk[9], 0, 11);
    g->scale = (uint8_t)acid_clampi(pk[10], 0, ACID_NSCALES - 1);
    g->base_oct = (uint8_t)acid_clampi(pk[11], 0, 4);
    g->mutate_bars = (uint8_t)acid_clampi(pk[12], 0, 16);
    for (i = 0; i < 5u; i++)
        seed |= (uint32_t)(pk[13u + i] & 127u) << (7u * i);
    g->seed = seed ? seed : 1u;
    g->mode = ACID_MODE_TB3PO;
    g->temp = 100;
    g->a_dens = ACID_AICD_DENS;
    g->a_acc = ACID_AICD_ACC;
    g->a_sld = ACID_AICD_SLD;
    g->a_scale = 0;
    if (pk[18] == 1u) {                              /* MODE and AICD's settings */
        g->mode = (uint8_t)(ACIDGEN_AICD ? acid_clampi(pk[19], 0, 1) : 0);
        g->temp = (uint8_t)acid_clampi(pk[20] | pk[21] << 7, 10, 200);
        g->a_dens = (uint8_t)acid_clampi(pk[22], 0, 100);
        g->a_acc = (uint8_t)acid_clampi(pk[23], 0, 100);
        g->a_sld = (uint8_t)acid_clampi(pk[24], 0, 100);
        g->a_scale = (uint8_t)acid_clampi(pk[25], 0, ACID_NSCALES);
    }
    return 1;
}
