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
 * While playing OCT+ / OCT- / SEED / HIST land at the next bar (tb3_next, tb3_commit); a line has a number (SEED, 1..9999).
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
 * steps at most) on from the two steps before it, biased but not fitted (the rest of the line stays). SHAPE: JOIN steers
 * round the pattern ends inside a line (a MUTATE rejoins the line), BEST the highest scoring of 2..8 draws (aicd_line). */
#define TB3_NSCALES 10            /* X0X's six, then (appended, stored by index) PDOM BLUE LOCR DARK */
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
    uint8_t mode;                /* TB3_TB3PO (X0X's), TB3_AICD (1.6.2), TB3_ACID */
    uint8_t temp;                /* AICD: TEMP 10..200 %, 100 the model's own odds */
    uint8_t a_dens, a_acc, a_sld; /* AICD: DENS ACCENT SLIDE, 0..100 % (AICD_DENS.. the model's own) */
    uint8_t a_scale;             /* AICD: SCALE, 0 CHRO (none), 1.. TB3_SCALES' */
    /* the extended settings (tb3_pack, byte 26 on): 0 off / auto in every one, so an older project plays as it did */
    uint8_t keep;                /* OCT+ keeps: 0 nothing, 1 the notes, 2 the rhythm */
    uint8_t style, notes, rhythm, ties;   /* MODE ACID */
    uint8_t join, best;          /* AICD */
    uint8_t form, flair, twist, tbars;
    uint8_t morph, slip, plen, mtype;
    uint8_t pocket, answer, rootfol, keys;
    uint16_t pool;               /* the note pool, a bit per semitone over the root; 0 none */
    uint16_t line;               /* SEED: the line's number 1..9999, seed = tb3_new_seed(it); 0 a seed from before numbers */
    uint32_t seed_b;             /* line B's (MORPH) */
} tb3_cfg_t;
#define TB3_TB3PO 0u
#define TB3_AICD 1u
#define TB3_ACID 2u              /* a third generator (acid_generate); TB-3PO's knobs and rules for the live ones */
#define AICD_DENS 90u            /* AICD's DENS ACCENT SLIDE defaults: the model's own shares (gate, ACC, SLD of the notes) */
#define AICD_ACC 35u
#define AICD_SLD 32u
typedef struct { uint8_t note, flags; } tb3_step_t;
typedef struct {                 /* 1.8.2: the knobs that act on the playing line (tb3_live), of the track's MODE */
    uint8_t dens, acc, sld;      /* 0..100 % */
    uint8_t octs;                /* 1..3 (AICD: 0, not a knob) */
    uint8_t root, scale, base;   /* scale 0 CHRO (AICD's), 1.. TB3_SCALES[scale - 1] */
    uint8_t morph, slip, plen;   /* MORPH 0..100 % towards line B; the notes against the gates (tb3_slip) */
    uint8_t pocket;              /* POCKET: 0 off, 1 DODGE, 2 LOCK, 3 ANSWER the kick (tb3_pocket) */
} tb3_knobs_t;
typedef struct {                 /* a track's TB-3PO state that is not stored */
    uint32_t rng;                /* auto MUTATE's random stream (X0X mut_rng; 0: from the seed) */
    uint32_t passes;             /* passes of the line since PLAY */
    uint8_t stale;               /* a setting changed since the line was written (the screen's NEW LINE *) */
    uint8_t pending;             /* 1.8.2: a live knob turned, for the next bar (tb3_bar) */
    uint8_t have;                /* anchor holds a line */
    uint8_t want;                /* an auto MUTATE for the main loop to draw (AICD with JOIN: tb3_pass, tb3_poll) */
    uint8_t varied;              /* M.TYPE VARY: the line that plays is a variation, the next pass brings the anchor's back */
    uint8_t bok;                 /* tb3_b holds line B (seed_b's); a load: not yet (tb3_poll) */
    uint32_t tw_pos;             /* TWIST: steps since PLAY */
    tb3_knobs_t kb;              /* the knobs line B was drawn at */
    uint8_t fol;                 /* ROOT<: the root its source track's bar asks for (tb3_bar), 0xFF none */
    uint64_t kick;               /* POCKET: bit i, a kick on the first DRUM track under step i (the main loop's, ui_tb3po.c) */
    tb3_knobs_t k0, kapp;        /* the knobs anchor was written with / those last applied */
    uint32_t wrote;              /* tb3_lhash of the steps as TB-3PO last wrote them (else: edited, a new anchor) */
    tb3_step_t anchor[NSTEP];    /* the line the live knobs start from (OCT+, OCT-, auto MUTATE, a hand edit) */
} tb3_rt_t;
static tb3_cfg_t tb3_cfg[NTRK];
static tb3_rt_t tb3_rt[NTRK];
/* a line waiting for the bar while playing (ui_tb3po.c tb3_emit -> tb3_bar): OCT+, OCT-, SEED, HIST land on the beat */
enum { TB3Q_NONE, TB3Q_READY, TB3Q_DONE };
typedef struct {
    uint8_t state, fresh;        /* fresh: every step written (OCT+, SEED), else only those that differ */
    tb3_knobs_t k;               /* the knobs it was made at */
    tb3_step_t line[NSTEP];
} tb3_next_t;
static tb3_next_t tb3_next[NTRK];
#define TB3_NHIST 8u              /* the lines HIST goes back through, a track's (in the pool: the main loop's only) */
typedef struct {
    tb3_step_t line[TB3_NHIST][NSTEP], live[NSTEP];   /* the last ones; the line HIST left */
    uint32_t seed[TB3_NHIST], live_seed;
    uint16_t no[TB3_NHIST], live_no;
    uint8_t n, head, at;         /* how many, where the next goes, HIST (0: now) */
} tb3_hist_t;
static tb3_hist_t tb3_hist[NTRK] __attribute__((section(".pool")));
static tb3_step_t tb3_b[NTRK][NSTEP] __attribute__((section(".pool")));   /* line B, MORPH's other end (the audio ISR reads it) */
static uint32_t tb3_auto_n;      /* auto MUTATEs so far (the screen redraws on one: ui_graph.c graph_signature) */

typedef struct { int8_t deg[7]; uint8_t len; } tb3_scale_t;
static const tb3_scale_t TB3_SCALES[TB3_NSCALES] = {   /* X0X's (schwung-tb3po's), same order */
    {{0, 2, 3, 5, 7, 8, 10}, 7},                       /* MIN */
    {{0, 1, 3, 5, 7, 8, 10}, 7},                       /* PHRY */
    {{0, 2, 3, 5, 7, 8, 11}, 7},                       /* HARM */
    {{0, 3, 5, 7, 10, 0, 0}, 5},                       /* MPEN */
    {{0, 2, 3, 5, 7, 9, 10}, 7},                       /* DOR */
    {{0, 2, 4, 5, 7, 9, 11}, 7},                       /* MAJ */
    {{0, 1, 4, 5, 7, 8, 10}, 7},                       /* PDOM: Phrygian dominant */
    {{0, 3, 5, 6, 7, 10, 0}, 6},                       /* BLUE */
    {{0, 1, 3, 5, 6, 8, 10}, 7},                       /* LOCR */
    {{0, 1, 6, 0, 0, 0, 0}, 3},                        /* DARK: 1 b2 b5 */
};
static const char *const TB3_SCALE_NAMES[TB3_NSCALES] = {"MIN", "PHRY", "HARM", "MPEN", "DOR", "MAJ",
                                                        "PDOM", "BLUE", "LOCR", "DARK"};
static const char *const TB3_ASCALE_NAMES[TB3_NSCALES + 1] = {"CHRO", "MIN", "PHRY", "HARM", "MPEN", "DOR", "MAJ",
                                                             "PDOM", "BLUE", "LOCR", "DARK"};
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
    g->keep = g->style = g->notes = g->rhythm = g->ties = g->join = g->best = 0;
    g->form = g->flair = g->twist = g->tbars = g->morph = g->slip = g->plen = g->mtype = 0;
    g->pocket = g->answer = g->rootfol = g->keys = 0;
    g->pool = g->line = 0;
    g->seed_b = 0;
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
static void tb3_slides(tb3_step_t *b, uint32_t len)  /* no slide into a rest (seq.c: it would only hold the note its whole step) */
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

static uint32_t tb3_h(uint32_t seed, uint32_t i, uint32_t kind);

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
typedef struct { uint32_t k, mask, join; int32_t dens, acc, sld; } aicd_bias_t;

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
    bi->join = g->join;
    bi->dens = aicd_lean(g->a_dens, AICD_DENS);
    bi->acc = aicd_lean(g->a_acc, AICD_ACC);
    bi->sld = aicd_lean(g->a_sld, AICD_SLD);
    bi->mask = sc ? 0u : 0xFFFu;
    if (sc && sc <= TB3_NSCALES)
        for (i = 0; i < TB3_SCALES[sc - 1u].len; i++)
            bi->mask |= 1u << TB3_SCALES[sc - 1u].deg[i];
}
/* the model has only a pattern end after node n (JOIN steers round them: a seam else) */
static int aicd_dead(uint32_t n)
{
    aicd_at_t a;
    aicd_edge_t e[AICD_MAXE];
    uint32_t pos, c, i;
    aicd_seek(n, &a);
    pos = a.pos;
    c = aicd_body(&pos, a.last, e);
    for (i = 0; i < c; i++)
        if (aicd_lastof(e[i].n) < AICD_NSTEP)
            return 0;
    return 1;
}
/* a line on from node *a: 1 a step (its code into *code), 0 a pattern end (Aicd skips its "~"); *a the node after */
static int aicd_next(aicd_at_t *a, const aicd_bias_t *bi, uint32_t *r, uint32_t *code)
{
    aicd_edge_t e[AICD_MAXE];
    uint16_t sym[AICD_MAXE];
    uint8_t rank[AICD_MAXE];
    int32_t s[AICD_MAXE], lo = 0x7FFFFFFF;
    uint32_t pos = a->pos, n = aicd_body(&pos, a->last, e), i, sum = 0, x, drop = 0;
    if (!n) {                                        /* (a dead end: none in this model) */
        aicd_start(a, r);
        return 0;
    }
    if (bi->join)                                    /* JOIN: a step that can go on, else a step, an end only if alone */
        for (i = 0; i < n; i++) {
            rank[i] = aicd_lastof(e[i].n) >= AICD_NSTEP ? 0u : aicd_dead(e[i].n) ? 1u : 2u;
            drop = rank[i] > drop ? rank[i] : drop;
        }
    for (i = 0; i < n; i++) {                        /* the edges' nodes' steps */
        int32_t v = (int32_t)((e[i].w * bi->k) >> 8);
        uint32_t l = aicd_lastof(e[i].n);
        if (bi->join && rank[i] < drop) {            /* (far above lo: its odds 0) */
            s[i] = 0x3FFFFFFF;
            continue;
        }
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
    int32_t n = 12 * (g->base_oct + 1) + g->root + (int32_t)(code >> 3) - 16;
    b->note = (uint8_t)tb3_clampi(n < 0 ? n + 12 : n, 0, 127);   /* (OCTAVE 0: below note 0 an octave up, not all on note 0) */
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
/* the model has step s after node a (a REST any of its ACC SLD) */
static int aicd_has_edge(const tb3_cfg_t *g, const aicd_at_t *a, const tb3_step_t *s)
{
    aicd_edge_t e[AICD_MAXE];
    uint32_t pos = a->pos, n = aicd_body(&pos, a->last, e), i, v, y;
    for (i = 0; i < n; i++)
        for (v = 0; v < ((s->flags & TB3_GATE) == TB3_REST ? 4u : 1u); v++)
            if (aicd_sym(g, s, v, &y) && aicd_lastof(e[i].n) == y)
                return 1;
    return 0;
}
/* the model knows step i of b after the two before it */
static int aicd_joins(const tb3_cfg_t *g, const tb3_step_t *b, uint32_t i, uint32_t len)
{
    aicd_at_t a;
    return len > 2u && aicd_find(g, &b[(i + 2u * len - 2u) % len], &b[(i + len - 1u) % len], &a) && aicd_has_edge(g, &a, &b[i]);
}
/* BEST's score: the loop joining as the model knows, the root on step 1, the line repeating at 4 and 8 steps */
static uint32_t aicd_score(const tb3_cfg_t *g, const tb3_step_t *b, uint32_t len)
{
    uint32_t i, rep = 0, sc = aicd_joins(g, b, 0, len) ? 8u : 0u;
    if ((b[0].flags & TB3_GATE) != TB3_REST && ((uint32_t)b[0].note + 120u - g->root) % 12u == 0u)
        sc += 4u;
    for (i = 0; i + 8u < len; i++)
        rep += (uint32_t)(b[i].flags == b[i + 4u].flags && b[i].note == b[i + 4u].note) +
               (uint32_t)(b[i].flags == b[i + 8u].flags && b[i].note == b[i + 8u].note);
    return sc + rep * 8u / len;
}
/* a new line of MODE AICD: BEST 2..8 draws that many and keeps the highest score (the first of equals), each its own seed
 * of the line's; 0, 1: the one line. The main loop's only */
static void aicd_line(const tb3_cfg_t *g, tb3_step_t *b, uint32_t len)
{
    tb3_cfg_t m = *g;
    tb3_step_t c[NSTEP];
    uint32_t i, sc, top = 0;
    aicd_generate(g, b, len);
    if (g->best < 2u)
        return;
    len = tb3_clampi((int32_t)len, 1, NSTEP);
    top = aicd_score(g, b, len);
    for (i = 1; i < g->best; i++) {
        m.seed = tb3_h(g->seed, i, 'C') | 1u;
        aicd_generate(&m, c, len);
        sc = aicd_score(g, c, len);
        if (sc > top) {
            top = sc;
            memcpy(b, c, sizeof c);
        }
    }
}
/* draw a run of the line on from the two steps before it (from a pattern's start if the model never had them), the
 * random stream on from *rng (0: from the seed). JOIN: up to 4 draws, kept the first that leads back into the line as the
 * model knows (auto MUTATE then in the main loop: tb3_pass, tb3_poll) */
static void aicd_run(const tb3_cfg_t *g, tb3_step_t *b, uint32_t len, uint32_t i0, uint32_t run, uint32_t *rp)
{
    aicd_bias_t bi;
    aicd_at_t a;
    tb3_step_t t[NSTEP], *w = g->join ? t : b;
    uint32_t r = *rp, n, code, guard, tries = g->join ? 4u : 1u;
    aicd_bias(g, &bi);
    for (;;) {
        if (g->join)
            memcpy(t, b, sizeof t);
        if (!aicd_find(g, &b[(i0 + 2u * len - 2u) % len], &b[(i0 + len - 1u) % len], &a))
            aicd_start(&a, &r);
        for (n = 0, guard = 0; n < run && guard < 4u * NSTEP; guard++)
            if (aicd_next(&a, &bi, &r, &code))
                aicd_put(g, &w[(i0 + n++) % len], code);
        if (!--tries || aicd_joins(g, w, (i0 + run) % len, len))
            break;
    }
    if (g->join)
        memcpy(b, t, sizeof t);
    tb3_slides(b, len);
    *rp = r;
}
static void aicd_mutate(const tb3_cfg_t *g, tb3_step_t *b, uint32_t len, uint32_t *rng)
{
    uint32_t r = *rng ? *rng : g->seed ? g->seed : 1u, i0, run;
    len = tb3_clampi((int32_t)len, 1, NSTEP);
    run = tb3_clampi((int32_t)(len + 3u) / 4, 1, 16);
    i0 = ((tb3_u(&r) >> 8) * len) >> 16;
    aicd_run(g, b, len, i0, run, &r);
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
static uint8_t tb3_rest_note(const tb3_cfg_t *g, uint32_t i)
{
    const tb3_scale_t *sc = tb3_scale(g);
    uint32_t range = tb3_clampi(g->oct_range, 1, 3), u = tb3_h(g->seed, i, 'R');
    uint32_t deg = i % 4u == 0u && (u & 255u) < 90u ? 0u : (((u >> 8) & 0xFFFFu) * sc->len) >> 16;
    return tb3_note(g, deg, ((u >> 24) * range) >> 8);
}
static void tb3_rest_notes(const tb3_cfg_t *g, tb3_step_t *b)
{
    uint32_t i;
    for (i = 0; i < NSTEP; i++)
        if ((b[i].flags & TB3_GATE) == TB3_REST)
            b[i].note = tb3_rest_note(g, i);
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
/* steps never written (power-on's zeros: a rest on note 0) get a note of their own, as a new line's rests have: DENS up
 * or a MUTATE would otherwise play them at 8 Hz. In the steps, so the line read back stays the same whatever the knobs do
 * next. (A line of TB-3PO's has no note under 12.) */
static void tb3_fill_rests(const tb3_cfg_t *g, step_t *s)
{
    uint32_t i;
    for (i = 0; i < NSTEP; i++)
        if (!s[i].n && !s[i].hit && !s[i].note[0] && s[i].time != ST_TIE)
            s[i].note[0] = tb3_rest_note(g, i);
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
    k.morph = g->morph;
    k.slip = g->slip;
    k.plen = g->plen;
    k.pocket = g->pocket;
    {
        uint32_t i = (uint32_t)(g - tb3_cfg);        /* (a track's own settings: ROOT< follows; a copy's does not) */
        if (i < NTRK && g->rootfol && tb3_rt[i].fol < 12u)
            k.root = tb3_rt[i].fol;
    }
    return k;
}
/* the settings track k draws its lines with: while ROOT< follows (tb3_follow), the followed root as ROOT (runtime only:
 * the stored ROOT stays), so a new line, a MUTATE, rest notes, line B and ANSWER are in the key the live knobs play */
static const tb3_cfg_t *tb3_gcfg(uint32_t k, tb3_cfg_t *tmp)
{
    const tb3_cfg_t *g = &tb3_cfg[k % NTRK];
    if (!g->rootfol || tb3_rt[k % NTRK].fol >= 12u)
        return g;
    *tmp = *g;
    tmp->root = tb3_rt[k % NTRK].fol;
    return tmp;
}
static uint32_t tb3_lhash(const tb3_step_t *b)       /* a line's FNV-1a */
{
    uint32_t h = 2166136261u, i;
    for (i = 0; i < NSTEP; i++)
        h = (h ^ (b[i].note | (uint32_t)b[i].flags << 8)) * 16777619u;
    return h;
}
static uint32_t tb3_level(uint32_t i)                /* a step's weight in the bar, 0 the strongest (1, 9, 5 13, even, odd) */
{
    i &= 15u;
    return !i ? 0u : i == 8u ? 1u : !(i & 3u) ? 2u : !(i & 1u) ? 3u : 4u;
}
/* the steps in a fixed order (Fisher-Yates); the gate's (kind 1, DENS) strongest first, the seed's within a level, so DENS
 * down thins the off-beats first as DrumBrain's does */
static void tb3_order(uint32_t seed, uint32_t kind, uint8_t *o)
{
    uint8_t t[NSTEP], x;
    uint32_t r = tb3_h(seed, 0, kind), i, j, l, n = 0;
    for (i = 0; i < NSTEP; i++)
        o[i] = (uint8_t)i;
    for (i = NSTEP - 1u; i; i--) {
        j = (tb3_u(&r) * (i + 1u)) >> 24;
        x = o[i];
        o[i] = o[j];
        o[j] = x;
    }
    if (kind != 1u)
        return;
    for (l = 0; l < 5u; l++)
        for (i = 0; i < NSTEP; i++)
            if (tb3_level(o[i]) == l)
                t[n++] = o[i];
    memcpy(o, t, NSTEP);
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
    if (f == TB3_ACC)
        return (b[i].flags & TB3_GATE) == TB3_NOTE;      /* (a TIE sounds no accent: seq.c) */
    return !f || (tb3_gated(&b[i]) && tb3_gated(&b[(i + 1u) % len]));   /* (a slide: into a note) */
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
/* SLIP / P.LEN: the notes of b's NOTE steps as a list of their own, as a real 303's pitch and time lists: turned by slip,
 * plen of them (0: all) cycled over the gates. A TIE takes none, a rest keeps its own */
static void tb3_slip(tb3_step_t *b, uint32_t len, uint32_t slip, uint32_t plen)
{
    uint8_t list[NSTEP];
    uint32_t i, j = 0, n = 0, m;
    if (!slip && !plen)
        return;
    for (i = 0; i < len; i++)
        if ((b[i].flags & TB3_GATE) == TB3_NOTE)
            list[n++] = b[i].note;
    if (!n)
        return;
    m = plen && plen < n ? plen : n;
    for (i = 0; i < len; i++)
        if ((b[i].flags & TB3_GATE) == TB3_NOTE)
            b[i].note = list[(j++ % m + slip) % n];
}
/* POCKET against kick (bit i: step i): DODGE no note on a kick, LOCK a note on each, ANSWER none on a kick and one
 * just after it */
static void tb3_pocket(tb3_step_t *b, uint32_t len, uint32_t mode, uint64_t kick)
{
    uint32_t i;
    for (i = 0; i < len; i++) {
        uint32_t kk = (uint32_t)(kick >> i) & 1u, after = (uint32_t)(kick >> ((i + len - 1u) % len)) & 1u;
        if (kk && mode != 2u && tb3_gated(&b[i]))
            b[i].flags = TB3_REST;
        else if (!tb3_gated(&b[i]) && ((kk && mode == 2u) || (!kk && after && mode == 3u)))
            b[i].flags = TB3_NOTE;
    }
}
/* the line the knobs k make of anchor a (made at k0): b, len steps (the pitches of all NSTEP). MORPH: k->morph % of the
 * steps from line lb (0: none, drawn at kb), the seed's order, the same steps first every time; POCKET: on kick */
static void tb3_live_x(const tb3_step_t *a, const tb3_knobs_t *k0, const tb3_knobs_t *k, uint32_t seed,
                       uint32_t len, tb3_step_t *b, const tb3_step_t *lb, const tb3_knobs_t *kb, uint64_t kick)
{
    tb3_step_t m[NSTEP];
    uint32_t i;
    len = tb3_clampi((int32_t)len, 1, NSTEP);
    if (lb && k->morph) {
        uint8_t o[NSTEP];
        uint32_t c = (uint32_t)k->morph * len / 100u;
        memcpy(m, a, sizeof m);
        tb3_order(seed, 'M', o);
        for (i = 0; i < NSTEP && c; i++)
            if (o[i] < len) {
                m[o[i]] = lb[o[i]];
                m[o[i]].note = tb3_repitch(lb[o[i]].note, kb, k0, tb3_h(seed, o[i], 'O'));
                c--;
            }
        for (i = 0; i < len; i++)                    /* (where the two lines meet: no slide into a rest, no TIE after one) */
            if ((m[i].flags & TB3_GATE) == TB3_TIE && !tb3_gated(&m[(i + len - 1u) % len]))
                m[i].flags = TB3_REST;
        for (i = 0; i < len; i++)
            if ((m[i].flags & TB3_SLD) && !tb3_gated(&m[(i + 1u) % len]))
                m[i].flags &= (uint8_t)~TB3_SLD;
        a = m;
    }
    memcpy(b, a, NSTEP * sizeof *b);
    tb3_fit(a, b, len, 0, k0->dens, k->dens, seed);
    if (k->pocket)                                   /* (whenever on: a MUTATE's new notes, the drums edited) */
        tb3_pocket(b, len, k->pocket, kick);
    tb3_fit(a, b, len, TB3_ACC, k0->acc, k->acc, seed);
    tb3_fit(a, b, len, TB3_SLD, k0->sld, k->sld, seed);
    for (i = 0; i < len; i++)                        /* DENS made a rest after a slide: no slide (the anchor's own stay) */
        if ((b[i].flags & TB3_SLD) && !tb3_gated(&b[(i + 1u) % len]) && tb3_gated(&a[(i + 1u) % len]))
            b[i].flags &= (uint8_t)~TB3_SLD;
    for (i = 0; i < len; i++) {                      /* a note and its TIEs are one: DENS took the note, the TIE goes too */
        if ((b[i].flags & TB3_GATE) == TB3_TIE && !tb3_gated(&b[(i + len - 1u) % len]) && tb3_gated(&a[(i + len - 1u) % len]))
            b[i].flags = TB3_REST;
    }
    for (i = 0; i < NSTEP; i++)
        b[i].note = tb3_repitch(a[i].note, k0, k, tb3_h(seed, i, 'O'));
    if (k->slip != k0->slip || k->plen != k0->plen)
        tb3_slip(b, len, k->slip, k->plen);
    for (i = 1; i < len; i++)                        /* (a TIE on the note it holds stays on it: OCTS moves them as one) */
        if ((b[i].flags & TB3_GATE) == TB3_TIE && a[i].note == a[i - 1u].note)
            b[i].note = b[i - 1u].note;
}
static void tb3_live_line(const tb3_step_t *a, const tb3_knobs_t *k0, const tb3_knobs_t *k, uint32_t seed,
                          uint32_t len, tb3_step_t *b)
{
    tb3_live_x(a, k0, k, seed, len, b, 0, 0, 0);
}
/* the anchor is at knobs kn, but MORPH and POCKET are still to come (it is line A, drawn without the kick) and on a new
 * line SLIP / P.LEN too: a MUTATE's line has them in it already. The next bar applies them (stopped: tb3_poll) */
static void tb3_k0(uint32_t k, const tb3_knobs_t *kn, int fresh)
{
    tb3_rt_t *rt = &tb3_rt[k % NTRK];
    rt->k0 = rt->kapp = *kn;
    rt->k0.morph = 0;
    if (fresh)
        rt->k0.slip = rt->k0.plen = 0;
    rt->pending = (uint8_t)(kn->morph || kn->pocket || rt->k0.slip != kn->slip || rt->k0.plen != kn->plen);
}
/* the line track k's steps hold (just written) is the anchor, at the knobs now (OCT+ / OCT- / auto MUTATE drew it) */
static void tb3_anchor(uint32_t k, const step_t *s, int fresh)
{
    tb3_rt_t *rt = &tb3_rt[k % NTRK];
    tb3_knobs_t kn = tb3_knobs(&tb3_cfg[k % NTRK]);
    tb3_read(s, rt->anchor);
    rt->wrote = tb3_lhash(rt->anchor);
    rt->have = 1;
    rt->varied = 0;
    tb3_k0(k, &kn, fresh);
}
static void tb3_rebase(uint32_t k)                   /* no anchor (a load, MODE): the steps one, at the knobs now */
{
    tb3_rt[k % NTRK].have = tb3_rt[k % NTRK].pending = tb3_rt[k % NTRK].want = tb3_rt[k % NTRK].varied = 0;
    tb3_rt[k % NTRK].bok = 0;
    tb3_rt[k % NTRK].fol = 0xFF;
    tb3_rt[k % NTRK].kapp = tb3_knobs(&tb3_cfg[k % NTRK]);
    tb3_next[k % NTRK].state = TB3Q_NONE;
}
/* the knobs into track t's steps (the audio ISR's at a bar, tb3_bar; the UI's stopped, IRQs off) */
static void tb3_live(track_t *t)
{
    uint32_t k = trk_index(t);
    tb3_rt_t *rt = &tb3_rt[k];
    tb3_knobs_t kn = tb3_knobs(&tb3_cfg[k]);
    tb3_step_t cur[NSTEP], b[NSTEP];
    tb3_cfg_t tmp;
    rt->pending = 0;
    tb3_fill_rests(tb3_gcfg(k, &tmp), t->step);
    tb3_read(t->step, cur);
    if (!rt->have || tb3_lhash(cur) != rt->wrote) {  /* edited (or loaded): those steps the anchor */
        memcpy(rt->anchor, cur, sizeof cur);
        rt->k0 = rt->kapp;
        rt->k0.morph = 0;
        rt->have = 1;
        if (kn.morph) {                              /* (a hand edit under MORPH: that line is A now) */
            tb3_cfg[k].morph = 0;
            kn.morph = 0;
        }
    }
    tb3_live_x(rt->anchor, &rt->k0, &kn, tb3_cfg[k].seed, (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP), b,
               rt->bok && tb3_cfg[k].seed_b ? tb3_b[k] : 0, &rt->kb, rt->kick);
    tb3_write(t->step, b, cur);
    tb3_read(t->step, cur);
    rt->wrote = tb3_lhash(cur);
    rt->kapp = kn;
}

/* the notes list[0..n) in order over b's NOTE steps (len), again from the start past the end: a TIE takes none, a rest
 * keeps its own (the 303's pitch list over its time list) */
static void tb3_lay_pitches(const uint8_t *list, uint32_t n, tb3_step_t *b, uint32_t len)
{
    uint32_t i, j = 0;
    if (!n)
        return;
    for (i = 0; i < len; i++)
        if ((b[i].flags & TB3_GATE) == TB3_NOTE)
            b[i].note = list[j++ % n];
}
/* KEEP on a new line b: 1 the old notes over its rhythm, 2 its notes under the old rhythm */
static void tb3_keep(uint32_t keep, const tb3_step_t *old, tb3_step_t *b, uint32_t len)
{
    uint8_t list[NSTEP];
    uint32_t i, n = 0;
    if (keep == 1u) {
        for (i = 0; i < len; i++)
            if ((old[i].flags & TB3_GATE) == TB3_NOTE)
                list[n++] = old[i].note;
        tb3_lay_pitches(list, n, b, len);
    } else if (keep == 2u) {
        for (i = 0; i < NSTEP; i++)
            b[i].flags = old[i].flags;
    }
}

/* ---------------------------------------------------------- ACID mode --- */
/* A line built as a player would: a few notes, a rhythm, then accents and slides where they tell. On TB-3PO's own knobs
 * (DENS ACCENT SLIDE OCTS ROOT SCALE OCTAVE: tb3_knobs as is, so they act live as there); its own: STYLE NOTES RHYTHM TIES
 * FORM (SHAPE, FORM), each AUTO / OFF at 0. Everything from the seed and the settings: the same line again.
 *  NOTES: the pool, the root, the scale's fifth, then its degrees in the seed's order (g->pool, if set, instead). Each next
 *   note by its interval to the last, as often as in Aicd's corpus (acid_pick: repeats 23 %, octaves 17 %, the rest
 *   ACID_IV), pulled to the root on steps 1, 9 and the beats; TB-3PO's are nearly memoryless (repeats 7 %, octaves 7 %).
 *  RHYTHM: DENS % of the steps, those a 16-step table (ACID_CELL) weighs most, a little jitter; MODEL: AICD's gates, accents
 *   and slides under the pool's notes. TIES: a note after a note held on, never on step 1.
 *  ACCENT / SLIDE: exactly the knob's share of the notes; accents on strong steps and in runs (ACC + SLD allowed), slides
 *   where the line moves towards the root or by a small step.
 *  FORM: the line as four parts of LEN / 4 (16 steps at LEN 64): AAAB AABA ABAC, CALL (ABAB, B A's rhythm with an answer);
 *   an A again changes n / 8 notes, the last 2..4 steps are a turnaround. X0X's and Aicd's lines do not change.
 * A MUTATE re-rolls ~25 % of the steps as TB-3PO's, inside the pool and with the pull (auto MUTATE: in the audio ISR).
 * Any MODE: PLAY (MORPH SLIP P.LEN M.TYPE: tb3_live_x, tb3_auto_type), FORM's FLAIR TWIST (tb3_flair, tb3_twist), LINK:
 * POCKET (the first DRUM track's kick, tb3_pocket), ANSWER (the first other 303 track, tb3_answer, new lines), ROOT<
 * (a track's lowest note at each bar, tb3_follow), KEYS (tb3_key_in). */
enum { AR_AUTO, AR_16TH, AR_OFFB, AR_332, AR_SPARSE, AR_ROLL, AR_MODEL };
#define ACID_NRHYTHM (6u + (uint32_t)AICD_ON)
#define ACID_NSTYLE 6u
#define ACID_NFORM 5u
#define ACID_NTIES 12u           /* AUTO, then 0 % .. 100 % by tens */
static const uint8_t ACID_CELL[5][16] = {
    {9, 6, 7, 6, 8, 6, 7, 6, 9, 6, 7, 6, 8, 6, 7, 6},  /* 16THS */
    {1, 2, 9, 3, 1, 2, 9, 3, 1, 2, 9, 3, 1, 3, 9, 4},  /* OFFBEAT */
    {9, 2, 1, 9, 2, 1, 9, 1, 9, 2, 1, 9, 2, 1, 9, 1},  /* 3-3-2 */
    {9, 0, 2, 1, 4, 0, 1, 2, 7, 0, 2, 1, 3, 1, 2, 3},  /* SPARSE */
    {5, 7, 8, 7, 5, 7, 8, 7, 5, 7, 8, 7, 6, 8, 9, 9},  /* ROLL */
};
typedef struct { uint8_t notes, rhythm, ties, rep, oct; } acid_style_t;
static const acid_style_t ACID_STYLE[ACID_NSTYLE] = {  /* what AUTO means: notes (0: 3..5), rhythm, TIES %, repeats %, octaves % */
    {0, AR_AUTO, 8, 23, 17},                           /* AUTO: the corpus's */
    {4, AR_ROLL, 10, 18, 20},                          /* ROLLING */
    {3, AR_OFFB, 0, 28, 10},                           /* STAB */
    {2, AR_16TH, 5, 45, 20},                           /* HYPNOTIC */
    {5, AR_332, 5, 12, 14},                            /* TRANCE */
    {3, AR_SPARSE, 30, 30, 12},                        /* DUB */
};
static const uint8_t ACID_FORM[ACID_NFORM - 1u][4] = {{0, 0, 0, 1}, {0, 0, 1, 0}, {0, 1, 0, 2}, {0, 1, 0, 1}};
/* a note's odds by its interval (1..11 semitones) to the last, x 10 per note up or down: Aicd's corpus (% of its intervals
 * / 2); over an octave 11 each; unison and octaves have their own share */
static const uint8_t ACID_IV[12] = {0, 13, 28, 28, 14, 19, 9, 17, 8, 13, 14, 7};
typedef struct { uint8_t pool[7], npool, range, rhythm, ties, rep, oct; } acid_t;

static int32_t acid_base(const tb3_cfg_t *g) { return 12 * (g->base_oct + 1) + g->root; }
static uint8_t acid_note(const tb3_cfg_t *g, int32_t semi) { return (uint8_t)tb3_clampi(acid_base(g) + semi, 0, 127); }
static void acid_ctx(const tb3_cfg_t *g, acid_t *c)
{
    const acid_style_t *st = &ACID_STYLE[g->style % ACID_NSTYLE];
    const tb3_scale_t *sc = tb3_scale(g);
    uint32_t h = tb3_h(g->seed, 0, 'P'), r = h, want, i, j, n = 1u;
    uint8_t o[7], x;
    c->range = (uint8_t)tb3_clampi(g->oct_range, 1, 3);
    c->rep = st->rep;
    c->oct = st->oct;
    c->ties = g->ties ? (uint8_t)((g->ties - 1u) * 10u) : st->ties;
    c->rhythm = g->rhythm ? g->rhythm : st->rhythm ? st->rhythm : (uint8_t)(AR_16TH + (h >> 8) % 5u);
    c->npool = 0;
    if (g->pool) {                                   /* (the keys' pool) */
        for (i = 0; i < 12u && c->npool < 7u; i++)
            if ((g->pool >> i) & 1u)
                c->pool[c->npool++] = (uint8_t)i;
        return;
    }
    want = g->notes ? g->notes : st->notes ? st->notes : 3u + h % 3u;
    c->pool[0] = 0;
    for (i = 0; i < sc->len; i++) {
        o[i] = (uint8_t)sc->deg[i];
        if (o[i] == 7u && want > 1u)
            c->pool[n++] = 7;
    }
    for (i = sc->len - 1u; i; i--) {
        j = tb3_u(&r) % (i + 1u);
        x = o[i];
        o[i] = o[j];
        o[j] = x;
    }
    for (i = 0; i < sc->len && n < want; i++)
        if (o[i] && o[i] != 7u)
            c->pool[n++] = o[i];
    c->npool = (uint8_t)n;
}
static uint32_t acid_w(int32_t prev, int32_t n)
{
    int32_t d = prev > n ? prev - n : n - prev;
    return !d || d % 12 == 0 ? 0u : d > 12 ? 11u : ACID_IV[d];
}
/* the next note (semitones over the root) after prev, on step i */
static int32_t acid_pick(const acid_t *c, int32_t prev, uint32_t i, uint32_t *r)
{
    uint32_t p = !(i & 15u) ? 60u : (i & 15u) == 8u ? 40u : !(i & 3u) ? 20u : 0u, u, j, o, sum = 0, x;
    int in = 0;
    if (tb3_u(r) % 100u < p)
        return c->pool[0] + (c->range > 1u && !(tb3_u(r) & 3u) ? 12 : 0);   /* (the root, or the keys' lowest: POOL) */
    if (prev >= 0 && prev < 12 * (int32_t)c->range)
        for (j = 0; j < c->npool; j++)
            in |= c->pool[j] == prev % 12;
    u = tb3_u(r) % 100u;
    if (in && u < c->rep)
        return prev;
    if (in && u < (uint32_t)c->rep + c->oct) {
        int up = prev + 12 < 12 * (int32_t)c->range, down = prev >= 12;
        if (up && down)
            return prev + (tb3_u(r) & 1u ? 12 : -12);
        if (up || down)
            return prev + (up ? 12 : -12);
    }
    for (o = 0; o < c->range; o++)
        for (j = 0; j < c->npool; j++)
            sum += acid_w(prev, c->pool[j] + 12 * (int32_t)o);
    if (!sum)
        return in ? prev : c->pool[0];
    x = tb3_u(r) % sum;
    for (o = 0; o < c->range; o++)
        for (j = 0; j < c->npool; j++) {
            int32_t n = c->pool[j] + 12 * (int32_t)o;
            uint32_t w = acid_w(prev, n);
            if (x < w)
                return n;
            x -= w;
        }
    return c->pool[0];
}
/* want more of b's first n steps get flag f (0: a note), the highest keys first (key 0: none) */
static void acid_top(tb3_step_t *b, const uint16_t *key, uint32_t n, uint32_t want, uint32_t f)
{
    uint32_t i, best;
    while (want--) {
        for (best = n, i = 0; i < n; i++)
            if (key[i] && !tb3_has(&b[i], f) && (best == n || key[i] > key[best]))
                best = i;
        if (best == n)
            return;
        b[best].flags = (uint8_t)(f ? b[best].flags | f : TB3_NOTE);
    }
}
static uint32_t acid_count(const tb3_step_t *b, uint32_t n)   /* the notes (a TIE is the note before it) */
{
    uint32_t i, c = 0;
    for (i = 0; i < n; i++)
        c += (b[i].flags & TB3_GATE) == TB3_NOTE;
    return c;
}
static int acid_can(const tb3_step_t *b, uint32_t i, uint32_t n, uint32_t f)   /* f: only a note; a slide into a note */
{
    return (b[i].flags & TB3_GATE) == TB3_NOTE && (f == TB3_ACC || (b[(i + 1u) % n].flags & TB3_GATE) == TB3_NOTE);
}
static void acid_flags(const tb3_cfg_t *g, tb3_step_t *b, uint32_t n, uint32_t *r)
{
    uint16_t key[NSTEP];
    uint32_t i, notes = acid_count(b, n), want = (g->accent * notes + 50u) / 100u;
    int32_t base = acid_base(g);
    for (i = 0; i < n; i++)
        key[i] = acid_can(b, i, n, TB3_ACC) ? (uint16_t)(1u + (4u - tb3_level(i)) * 24u + tb3_u(r) % 64u) : 0u;
    acid_top(b, key, n, (want + 1u) / 2u, TB3_ACC);
    for (i = 1; i < n; i++)                          /* the rest near those: accents come in runs */
        if (key[i] && (b[i - 1u].flags & TB3_ACC))
            key[i] = (uint16_t)(key[i] + 96u);
    acid_top(b, key, n, want / 2u, TB3_ACC);
    for (i = 0; i < n; i++) {
        int32_t a = (int32_t)b[i].note - base, x = (int32_t)b[(i + 1u) % n].note - base, d = x - a;
        key[i] = acid_can(b, i, n, TB3_SLD)
                     ? (uint16_t)(1u + tb3_u(r) % 64u + ((x < 0 ? -x : x) < (a < 0 ? -a : a) ? 64u : 0u) +
                                  ((d < 0 ? -d : d) <= 5 ? 32u : 0u))
                     : 0u;
    }
    acid_top(b, key, n, (g->slide * notes + 50u) / 100u, TB3_SLD);
}
/* one part, n steps: the gates, ties, notes, accents and slides (b: NSTEP long) */
static void acid_cell(const tb3_cfg_t *g, const acid_t *c, tb3_step_t *b, uint32_t n, uint32_t *r)
{
    uint16_t key[NSTEP];
    uint32_t i, model = 0;
    int32_t prev = 0;
    for (i = 0; i < NSTEP; i++)
        b[i].flags = TB3_REST, b[i].note = acid_note(g, 0);
#if AICD_ON
    if (c->rhythm == AR_MODEL) {
        tb3_cfg_t m = *g;
        m.mode = TB3_AICD;
        m.seed = tb3_u(r) << 8 | 1u;
        m.a_dens = g->density;
        m.a_acc = g->accent;
        m.a_sld = g->slide;
        m.a_scale = 0;
        m.temp = 100;
        m.join = 0;
        aicd_generate(&m, b, n);
        model = 1u;
    }
#endif
    if (!model) {
        const uint8_t *w = ACID_CELL[(c->rhythm ? c->rhythm - 1u : 0u) % 5u];
        for (i = 0; i < n; i++)
            key[i] = (uint16_t)(1u + w[i & 15u] * 16u + tb3_u(r) % 32u);
        acid_top(b, key, n, (g->density * n + 50u) / 100u, 0);
    }
    for (i = 1; i < n; i++)
        if ((b[i].flags & TB3_GATE) == TB3_NOTE && (i & 15u) && tb3_gated(&b[i - 1u]) &&
            ((b[i - 1u].flags & TB3_GATE) != TB3_TIE || !(tb3_u(r) & 3u)) && tb3_u(r) % 100u < c->ties)
            b[i].flags = TB3_TIE;
    for (i = 0; i < n; i++) {
        if ((b[i].flags & TB3_GATE) == TB3_NOTE)
            prev = acid_pick(c, prev, i, r);
        b[i].note = acid_note(g, prev);
    }
    if (!model)
        acid_flags(g, b, n, r);
}
/* what a line must be, whatever was done to it: a TIE after a note and plain, on its note; a slide into a note */
static void acid_clean(tb3_step_t *b, uint32_t len)
{
    uint32_t i;
    for (i = 0; i < len; i++)
        if ((b[i].flags & TB3_GATE) == TB3_TIE) {
            const tb3_step_t *p = &b[(i + len - 1u) % len];
            b[i].flags = tb3_gated(p) && i ? TB3_TIE : TB3_REST;
            b[i].note = p->note;
        }
    for (i = 0; i < len; i++)
        if ((b[i].flags & TB3_SLD) && (b[(i + 1u) % len].flags & TB3_GATE) != TB3_NOTE)
            b[i].flags &= (uint8_t)~TB3_SLD;
}
static void acid_trim(tb3_step_t *b, uint32_t len, uint32_t f, uint32_t pct, uint32_t seed)   /* f to pct % of the notes */
{
    uint8_t o[NSTEP];
    uint32_t i, c = 0, want = (pct * acid_count(b, len) + 50u) / 100u;
    for (i = 0; i < len; i++)
        c += (b[i].flags & f) != 0u;
    if (c == want)
        return;
    tb3_order(seed, f + 1u, o);
    for (i = 0; i < NSTEP && c != want; i++) {
        uint32_t x = o[i];
        if (x >= len || !acid_can(b, x, len, f))
            continue;
        if (c < want && !(b[x].flags & f)) {
            b[x].flags |= (uint8_t)f;
            c++;
        } else if (c > want && (b[x].flags & f)) {
            b[x].flags &= (uint8_t)~f;
            c--;
        }
    }
}
/* FORM: the parts in their order, an A that returns a little changed, the end a turnaround */
static void acid_form(const tb3_cfg_t *g, const acid_t *c, tb3_step_t *b, uint32_t len, uint32_t n, uint32_t *r)
{
    tb3_step_t part[3][NSTEP];
    const uint8_t *f = ACID_FORM[(g->form - 1u) % (ACID_NFORM - 1u)];
    int32_t base = acid_base(g);
    uint32_t i, j, s, turn, seen = 0;
    for (i = 0; i < 3u; i++)
        acid_cell(g, c, part[i], n, r);
    if (g->form == 4u) {                             /* CALL: B is A with its second half answered, on the root */
        memcpy(part[1], part[0], sizeof part[1]);
        for (i = n / 2u; i < n; i++) {
            if ((part[1][i].flags & TB3_GATE) == TB3_NOTE) {
                part[1][i].note = acid_note(g, acid_pick(c, (int32_t)part[1][i].note - base, i, r));
            }
        }
        for (i = n; i-- > n / 2u;)
            if ((part[1][i].flags & TB3_GATE) == TB3_NOTE) {
                part[1][i].note = acid_note(g, c->pool[0]);
                break;
            }
    }
    for (i = 0; i < len; i++)
        b[i] = part[f[(i / n) & 3u]][i % n];
    for (s = 0; s * n < len; s++) {
        uint32_t l = f[s & 3u];
        if ((seen >> l) & 1u)
            for (j = n / 8u; j; j--) {
                uint32_t x = s * n + tb3_u(r) % n;
                if (x < len && (b[x].flags & TB3_GATE) == TB3_NOTE)
                    b[x].note = acid_note(g, acid_pick(c, (int32_t)b[x].note - base, x, r));
            }
        seen |= 1u << l;
    }
    turn = 2u + tb3_u(r) % 3u;
    turn = turn > n / 2u ? n / 2u : turn;
    for (i = len - turn; i < len; i++) {
        if (tb3_u(r) % 100u < g->density) {
            b[i].flags = TB3_NOTE;
            b[i].note = acid_note(g, acid_pick(c, (int32_t)b[(i + len - 1u) % len].note - base, i, r));
        } else {
            b[i].flags = TB3_REST;
        }
    }
}
static void acid_generate(const tb3_cfg_t *g, tb3_step_t *b, uint32_t len)
{
    acid_t c;
    uint32_t r = g->seed ? g->seed : 1u, i, any = 0;
    len = tb3_clampi((int32_t)len, 1, NSTEP);
    acid_ctx(g, &c);
    if (g->form && len >= 8u) {
        for (i = 0; i < NSTEP; i++)
            b[i].flags = TB3_REST;
        acid_form(g, &c, b, len, len / 4u, &r);
    } else {
        acid_cell(g, &c, b, len, &r);
    }
    acid_clean(b, len);
    for (i = 0; i < len; i++)
        any |= tb3_gated(&b[i]);
    if (!any) {                                      /* (a note at least, as TB-3PO's) */
        b[0].flags = TB3_NOTE;
        b[0].note = acid_note(g, c.pool[0]);
    }
    acid_trim(b, len, TB3_ACC, g->accent, g->seed);
    acid_trim(b, len, TB3_SLD, g->slide, g->seed);
    for (i = 0; i < NSTEP; i++)                      /* a rest's note: of the pool, DENS up plays it */
        if (i >= len || !tb3_gated(&b[i])) {
            uint32_t u = tb3_h(g->seed, i, 'R');
            b[i].flags = TB3_REST;
            b[i].note = acid_note(g, c.pool[(((u >> 8) & 0xFFFFu) * c.npool) >> 16] + 12 * (int32_t)(((u >> 24) * c.range) >> 8));
        }
}
/* re-roll ~25 % of the steps, inside the pool (the stream on from *rng; 0: from the seed) */
static void acid_mutate(const tb3_cfg_t *g, tb3_step_t *b, uint32_t len, uint32_t *rng)
{
    acid_t c;
    uint32_t r = *rng ? *rng : g->seed ? g->seed : 1u, i, f;
    int32_t base = acid_base(g), prev = 0;
    len = tb3_clampi((int32_t)len, 1, NSTEP);
    acid_ctx(g, &c);
    for (i = len; i--;)                              /* (the first step's last note: the line's end) */
        if (tb3_gated(&b[i])) {
            prev = (int32_t)b[i].note - base;
            break;
        }
    for (i = 0; i < len; i++) {
        if (tb3_u(&r) % 100u < 25u) {
            if (tb3_u(&r) & 1u) {                    /* the step */
                if (tb3_u(&r) % 100u >= g->density) {
                    b[i].flags = TB3_REST;
                } else {
                    f = TB3_NOTE;
                    f |= tb3_u(&r) % 100u < g->accent ? TB3_ACC : 0u;
                    f |= tb3_u(&r) % 100u < g->slide ? TB3_SLD : 0u;
                    b[i].flags = (uint8_t)f;
                    b[i].note = acid_note(g, acid_pick(&c, prev, i, &r));
                }
            } else if ((b[i].flags & TB3_GATE) == TB3_NOTE) {   /* its note */
                b[i].note = acid_note(g, acid_pick(&c, prev, i, &r));
            }
        }
        if (tb3_gated(&b[i]))
            prev = (int32_t)b[i].note - base;
    }
    acid_clean(b, len);
    *rng = r;
}

/* the line of g's MODE: a new one (OCT+), a MUTATE */
static void tb3_gen(const tb3_cfg_t *g, tb3_step_t *b, uint32_t len)
{
    if (g->mode == TB3_ACID) {
        acid_generate(g, b, len);
        return;
    }
#if AICD_ON
    if (g->mode == TB3_AICD) {
        aicd_line(g, b, len);
        return;
    }
#endif
    tb3_generate(g, b, len);
    tb3_rest_notes(g, b);
}
static void tb3_mut(const tb3_cfg_t *g, tb3_step_t *b, uint32_t len, uint32_t *rng)
{
    if (g->mode == TB3_ACID) {
        acid_mutate(g, b, len, rng);
        return;
    }
#if AICD_ON
    if (g->mode == TB3_AICD) {
        aicd_mutate(g, b, len, rng);
        return;
    }
#endif
    tb3_mutate(g, b, len, rng);
}

/* note moved by `by` degrees of g's scale (AICD's CHRO: 3 semitones a degree, 7 for two) */
static uint8_t tb3_harm(const tb3_cfg_t *g, uint32_t note, uint32_t by)
{
    const tb3_scale_t *sc;
    uint32_t sci = g->mode == TB3_AICD ? g->a_scale : g->scale + 1u, d, i;
    int32_t base = 12 * (g->base_oct + 1) + g->root, off = (int32_t)note - base, o, w;
    o = off >= 0 ? off / 12 : -((11 - off) / 12);
    w = off - 12 * o;
    if (!sci || sci > TB3_NSCALES)
        return (uint8_t)tb3_clampi((int32_t)note + (by == 2u ? 3 : 7), 0, 127);
    sc = &TB3_SCALES[sci - 1u];
    for (d = 0, i = 0; i < sc->len; i++)             /* the degree at or under it */
        if (sc->deg[i] <= w)
            d = i;
    i = d + by;
    o += (int32_t)(i / sc->len);
    w = sc->deg[i % sc->len] + (w - sc->deg[d]);
    return (uint8_t)tb3_clampi(base + 12 * o + w, 0, 127);
}
/* ANSWER on new line b of track t (drawn with g), against the first other 303 track: REST plays where it rests; 3RD 5TH
 * OCT its rhythm, its notes a third / fifth (of t's scale) / an octave up: they replace KEEP's rhythm. Read only: the other
 * line stays */
static void tb3_answer(const track_t *t, const tb3_cfg_t *g, tb3_step_t *b, uint32_t len)
{
    static const uint8_t BY[5] = {0, 0, 2, 4, 7};
    uint32_t k = trk_index(t), m = g->answer, o, ol, i;
    tb3_step_t ob[NSTEP];
    if (!m)
        return;
    for (o = 0; o < NTRK && (o == k || trk[o].eng_req % NENGINES != ENGI_303); o++)
        ;
    if (o >= NTRK)
        return;
    ol = (uint32_t)clamp(trk[o].p[P_SLEN], 1, NSTEP);
    tb3_read(trk[o].step, ob);
    for (i = 0; i < len; i++) {
        const tb3_step_t *x = &ob[i % ol];
        if (m == 1u) {
            if (tb3_gated(x))
                b[i].flags = TB3_REST;
            continue;
        }
        b[i].flags = x->flags;
        if (tb3_gated(x))
            b[i].note = BY[m] == 7u ? (uint8_t)tb3_clampi(x->note + 12, 0, 127) : tb3_harm(g, x->note, BY[m]);
    }
    for (i = 0; i < len; i++)                        /* (no slide into a rest, no TIE after one) */
        if ((b[i].flags & TB3_GATE) == TB3_TIE && !tb3_gated(&b[(i + len - 1u) % len]))
            b[i].flags = TB3_REST;
    for (i = 0; i < len; i++)
        if ((b[i].flags & TB3_SLD) && !tb3_gated(&b[(i + 1u) % len]))
            b[i].flags &= (uint8_t)~TB3_SLD;
}

/* track t's next line b from its steps old (tb3_read of them): a new line (fresh: OCT+), else OCT- / auto MUTATE's
 * with *rng */
static void tb3_line(const track_t *t, const tb3_step_t *old, tb3_step_t *b, int fresh, uint32_t *rng)
{
    uint32_t k = trk_index(t), len = (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP);
    tb3_cfg_t tmp;
    const tb3_cfg_t *g = tb3_gcfg(k, &tmp);
    if (fresh) {
        tb3_gen(g, b, len);
        tb3_keep(g->keep, old, b, len);
        tb3_answer(t, g, b, len);                    /* (after KEEP: ANSWER's rhythm is the other track's) */
    } else {
        memcpy(b, old, NSTEP * sizeof *b);
        tb3_mut(g, b, len, rng);
    }
}
/* FLAIR on new line b's step i: weak NOTE steps, not sliding or held on, FLAIR % of them (the seed's): a ratchet (x2, x3) or
 * the 4:4 condition, a pickup every fourth pass. In the step, so a song plays it too; a condition that fails releases */
static void tb3_flair(const tb3_cfg_t *g, step_t *s, const tb3_step_t *b, uint32_t i, uint32_t len)
{
    uint32_t u;
    if (!g->flair || i >= len || tb3_level(i) < 3u || (b[i].flags & (TB3_GATE | TB3_SLD)) != TB3_NOTE ||
        (b[(i + 1u) % len].flags & TB3_GATE) == TB3_TIE)
        return;
    u = tb3_h(g->seed, i, 'F');
    if (u % 100u >= g->flair)
        return;
    if ((u >> 8) & 3u)
        step_set_ratchet(s, 2u + ((u >> 10) & 1u));
    else
        s->probability = COND_FIRST + 8u;            /* (4:4: core.h) */
}
/* the steps track t plays from its TB-3PO, at once: the audio ISR's (seq_enter: auto MUTATE) and the tests' (the UI
 * works the line out with the ISR on: ui_tb3po.c tb3_oct) */
static void tb3_apply(track_t *t, int fresh, uint32_t *rng)
{
    tb3_step_t b[NSTEP], old[NSTEP];
    if (!fresh && tb3_rt[trk_index(t)].pending)
        tb3_live(t);                                 /* (a MUTATE from the line the knobs say) */
    if (!fresh) {
        tb3_cfg_t tmp;
        tb3_fill_rests(tb3_gcfg(trk_index(t), &tmp), t->step);
    }
    tb3_read(t->step, old);
    tb3_line(t, old, b, fresh, rng);
    tb3_write(t->step, b, fresh ? 0 : old);
    if (fresh) {
        uint32_t i;
        for (i = 0; i < NSTEP; i++)
            tb3_flair(&tb3_cfg[trk_index(t)], &t->step[i], b, i, (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP));
    }
    tb3_anchor(trk_index(t), t->step, fresh);
}

/* the waiting line into t's steps (the bar: audio ISR; stopped since: the main loop, IRQs off) */
static void tb3_commit(track_t *t)
{
    uint32_t k = trk_index(t);
    tb3_next_t *q = &tb3_next[k];
    tb3_step_t cur[NSTEP];
    uint8_t p = tb3_rt[k].pending;                   /* (a knob turned since: on top of it, tb3_bar) */
    tb3_read(t->step, cur);
    tb3_write(t->step, q->line, q->fresh ? 0 : cur);
    if (q->fresh) {
        uint32_t i;
        for (i = 0; i < NSTEP; i++)
            tb3_flair(&tb3_cfg[k], &t->step[i], q->line, i, (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP));
    }
    tb3_anchor(k, t->step, q->fresh);
    tb3_k0(k, &q->k, q->fresh);
    tb3_rt[k].pending |= p;
    q->state = TB3Q_DONE;
    tb3_auto_n++;
}

/* TURN (M.TYPE): only the line's last 4 steps drawn again, what comes before them left as it is */
static void tb3_turn(const tb3_cfg_t *g, tb3_step_t *b, uint32_t len, uint32_t *rng)
{
    uint32_t n = len < 4u ? len : 4u, i;
#if AICD_ON
    if (g->mode == TB3_AICD) {
        uint32_t r = *rng ? *rng : g->seed ? g->seed : 1u;
        aicd_run(g, b, len, len - n, n, &r);
        *rng = r;
        return;
    }
#endif
    {
        tb3_step_t c[NSTEP];
        memcpy(c, b, sizeof c);
        for (i = 0; i < 3u; i++)                     /* (a MUTATE moves a quarter of the steps: three, most of them) */
            tb3_mut(g, c, len, rng);
        memcpy(&b[len - n], &c[len - n], n * sizeof *b);
    }
    if (g->mode == TB3_ACID)
        acid_clean(b, len);
    else
        tb3_slides(b, len);
}
/* auto MUTATE's other types: VARY a line for one pass, the anchor untouched (tb3_live brings it back unless the steps were
 * edited since); TURN its last steps. In the audio ISR: AICD's without JOIN's redraws (only DRIFT has them) */
static void tb3_auto_type(track_t *t, uint32_t type)
{
    uint32_t k = trk_index(t), len = (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP);
    tb3_step_t old[NSTEP], b[NSTEP];
    tb3_cfg_t tmp, g;
    g = *tb3_gcfg(k, &tmp);
    g.join = 0;                                      /* (JOIN's redraws are the main loop's: here AICD's plain run) */
    tb3_live(t);                                     /* (an anchor, the line the knobs say) */
    tb3_fill_rests(&g, t->step);
    tb3_read(t->step, old);
    memcpy(b, old, sizeof b);
    if (type == 1u)
        tb3_mut(&g, b, len, &tb3_rt[k].rng);
    else
        tb3_turn(&g, b, len, &tb3_rt[k].rng);
    if (type == 1u && g.pocket)                      /* (VARY keeps the anchor: the kick's rule on the variation itself) */
        tb3_pocket(b, len, g.pocket, tb3_rt[k].kick);
    tb3_write(t->step, b, old);
    if (type == 1u) {
        tb3_read(t->step, old);
        tb3_rt[k].wrote = tb3_lhash(old);
        tb3_rt[k].varied = 1;
    } else {
        tb3_anchor(k, t->step, 0);
    }
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
    if (tb3_rt[k].varied && !chain.running) {        /* VARY's pass is over: the line again */
        tb3_rt[k].varied = 0;
        tb3_live(t);
        tb3_auto_n++;
        return;
    }
    if (m && !chain.running && t->eng_req % NENGINES == ENGI_303 && tb3_rt[k].passes % m == 0u) {
        if (AICD_ON && tb3_cfg[k].mode == TB3_AICD && tb3_cfg[k].join && !tb3_cfg[k].mtype) {   /* (up to 4 draws: not in
                                                                                                * the ISR; a bar on) */
            tb3_rt[k].want = 1;
            return;
        }
        if (tb3_cfg[k].mtype)
            tb3_auto_type(t, tb3_cfg[k].mtype);
        else
            tb3_apply(t, 0, &tb3_rt[k].rng);         /* (NEW LINE's star stays as it was, as X0X's) */
        tb3_auto_n++;
    }
}
/* ROOT<: the pitch class of the lowest note in the next 16 steps of t's source track (T1..T4; 0xFF: none, itself) */
static uint32_t tb3_follow(const track_t *t)
{
    uint32_t src = tb3_cfg[trk_index(t)].rootfol, sl, j, n, lo = 0xFFu;
    const track_t *s;
    if (!src || src - 1u >= NTRK || src - 1u == trk_index(t))
        return 0xFFu;
    s = &trk[src - 1u];
    sl = (uint32_t)clamp(s->p[P_SLEN], 1, NSTEP);
    for (j = 0; j < 16u; j++) {
        const step_t *x = &s->step[(s->seq_idx + j) % sl];
        if (x->time == ST_NOTE)
            for (n = 0; n < x->n && n < 4u; n++)
                lo = (x->note[n] & 127u) < lo ? x->note[n] & 127u : lo;
    }
    return lo < 128u ? lo % 12u : 0xFFu;
}
/* a bar of t's line starts (seq.c seq_enter: every 16th step, step 1 too; a 303 track): its auto MUTATE, then (1.8.2) a
 * live knob turned since (not in a song: it waits) */
static __attribute__((noinline)) void tb3_bar(track_t *t, uint32_t top)
{
    if (!t->seq_idx)
        tb3_pass(t, top);
    if (tb3_cfg[trk_index(t)].rootfol && !chain.running) {   /* ROOT<: its source's coming bar, its lowest note */
        uint32_t f = tb3_follow(t);
        if (f < 12u && f != tb3_rt[trk_index(t)].fol) {
            tb3_rt[trk_index(t)].fol = (uint8_t)f;
            tb3_rt[trk_index(t)].pending = 1;
        }
    }
    if (tb3_next[trk_index(t)].state == TB3Q_READY && !chain.running)
        tb3_commit(t);
    if (tb3_rt[trk_index(t)].pending && !chain.running) {
        tb3_live(t);
        tb3_auto_n++;                                /* (the screen redraws; the UI's OCT- sees the steps changed) */
    }
}

/* TWIST: this step's cutoff offset on t (seq.c seq_enter, every step of a 303 track): an arc up and back over T.BARS bars,
 * and a lift while accents run on. Integer; written before the second core starts its block; 0 when off: as before */
static __attribute__((noinline)) void tb3_twist(track_t *t, uint32_t top)
{
    uint32_t k = trk_index(t), bars = tb3_cfg[k].tbars ? tb3_cfg[k].tbars : 4u, span = bars * 16u, p, tri, amt = tb3_cfg[k].twist;
    uint32_t i = t->seq_idx, len = (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP);
    if (!amt) {
        tb3_tw[k] = 0;
        return;
    }
    tb3_rt[k].tw_pos = top && !chain.running ? 0u : tb3_rt[k].tw_pos + 1u;
    p = tb3_rt[k].tw_pos % span;
    tri = p < span / 2u ? p * 2u : (span - p) * 2u;
    p = amt * 102u * tri / span;                     /* (100 %: 40 CUT steps at the top) */
    if ((t->step[i].flags & SF_ACCENT) && (t->step[(i + len - 1u) % len].flags & SF_ACCENT))
        p += amt * 25u;                              /* (10 steps more) */
    tb3_tw[k] = (int16_t)p;
}
/* KEYS (seq.c keyboard_block, the audio ISR): ROOT: the key's note the root, at the next bar as the knob's (while ROOT<
 * follows a track, that track's root wins); POOL: the keys held are MODE ACID's note pool for the next line (a key pressed
 * alone starts a new one; TB-3PO's and AICD's lines have no pool: the keys only stay silent there) */
static void tb3_key_in(uint32_t k, uint32_t note, int alone)
{
    tb3_cfg_t *g = &tb3_cfg[k];
    uint32_t pc = note % 12u;
    if (g->keys == 1u) {
        if (g->root != pc) {
            g->root = (uint8_t)pc;
            tb3_rt[k].pending = 1;
        }
        return;
    }
    if (alone)
        g->pool = 0;
    g->pool |= (uint16_t)(1u << ((pc + 12u - g->root) % 12u));
    tb3_rt[k].stale = 1;
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
    pk[26] = 1;                                      /* the extended settings (older firmware stops at 25) */
    pk[27] = g->keep; pk[28] = g->style; pk[29] = g->notes; pk[30] = g->rhythm; pk[31] = g->ties;
    pk[32] = g->join; pk[33] = g->best; pk[34] = g->form; pk[35] = g->flair; pk[36] = g->twist; pk[37] = g->tbars;
    pk[38] = g->morph; pk[39] = g->slip; pk[40] = g->plen; pk[41] = g->mtype;
    pk[42] = g->pocket; pk[43] = g->answer; pk[44] = g->rootfol; pk[45] = g->keys;
    for (i = 0; i < 5u; i++)
        pk[46u + i] = (uint8_t)((g->seed_b >> (7u * i)) & 127u);
    pk[51] = (uint8_t)(g->pool & 127u);
    pk[52] = (uint8_t)(g->pool >> 7);
    pk[53] = (uint8_t)(g->line & 127u);
    pk[54] = (uint8_t)(g->line >> 7);
}
/* track k's settings from a project's FM6 area: 1 = it holds them (each value into its range), else the defaults, 0 */
static int tb3_unpack(uint32_t k, const uint8_t *pk)
{
    tb3_cfg_t *g = &tb3_cfg[k % NTRK];
    uint32_t i, seed = 0;
    tb3_hist[k % NTRK].n = tb3_hist[k % NTRK].at = tb3_hist[k % NTRK].head = 0;   /* (another project: not its lines) */
    tb3_rt[k % NTRK].rng = tb3_rt[k % NTRK].passes = tb3_rt[k % NTRK].stale = 0;
    tb3_rt[k % NTRK].tw_pos = 0;
    tb3_tw[k % NTRK] = 0;
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
        g->mode = (uint8_t)tb3_clampi(pk[19], 0, 2);
        if (g->mode == TB3_AICD && !AICD_ON)
            g->mode = TB3_TB3PO;
        g->temp = (uint8_t)tb3_clampi(pk[20] | pk[21] << 7, 10, 200);
        g->a_dens = (uint8_t)tb3_clampi(pk[22], 0, 100);
        g->a_acc = (uint8_t)tb3_clampi(pk[23], 0, 100);
        g->a_sld = (uint8_t)tb3_clampi(pk[24], 0, 100);
        g->a_scale = (uint8_t)tb3_clampi(pk[25], 0, TB3_NSCALES);
    }
    g->keep = g->style = g->notes = g->rhythm = g->ties = g->join = g->best = 0;
    g->form = g->flair = g->twist = g->tbars = g->morph = g->slip = g->plen = g->mtype = 0;
    g->pocket = g->answer = g->rootfol = g->keys = 0;
    g->pool = g->line = 0;
    g->seed_b = 0;
    if (pk[26] == 1u) {                              /* the extended settings (before: zeros, all off) */
        g->keep = (uint8_t)tb3_clampi(pk[27], 0, 2);
        g->style = (uint8_t)tb3_clampi(pk[28], 0, ACID_NSTYLE - 1);
        g->notes = (uint8_t)tb3_clampi(pk[29], 0, 7);
        g->rhythm = (uint8_t)tb3_clampi(pk[30], 0, ACID_NRHYTHM - 1);
        g->ties = (uint8_t)tb3_clampi(pk[31], 0, ACID_NTIES - 1);
        g->join = (uint8_t)tb3_clampi(pk[32], 0, 1);
        g->best = (uint8_t)tb3_clampi(pk[33], 0, 8);
        g->form = (uint8_t)tb3_clampi(pk[34], 0, ACID_NFORM - 1);
        g->flair = (uint8_t)tb3_clampi(pk[35], 0, 100);
        g->twist = (uint8_t)tb3_clampi(pk[36], 0, 100);
        g->tbars = (uint8_t)tb3_clampi(pk[37], 0, 16);
        g->morph = (uint8_t)tb3_clampi(pk[38], 0, 100);
        g->slip = (uint8_t)tb3_clampi(pk[39], 0, 15);
        g->plen = (uint8_t)tb3_clampi(pk[40], 0, 16);
        g->mtype = (uint8_t)tb3_clampi(pk[41], 0, 2);
        g->pocket = (uint8_t)tb3_clampi(pk[42], 0, 3);
        g->answer = (uint8_t)tb3_clampi(pk[43], 0, 4);
        g->rootfol = (uint8_t)tb3_clampi(pk[44], 0, 4);
        g->keys = (uint8_t)tb3_clampi(pk[45], 0, 2);
        for (i = 0; i < 5u; i++)
            g->seed_b |= (uint32_t)(pk[46u + i] & 127u) << (7u * i);
        g->pool = (uint16_t)((pk[51] | pk[52] << 7) & 0xFFFu);
        g->line = (uint16_t)tb3_clampi(pk[53] | pk[54] << 7, 0, 9999);
    }
    tb3_rebase(k);
    return 1;
}
static void tb3_reset(void)                           /* power-on: every track X0X's defaults */
{
    uint32_t k;
    for (k = 0; k < NTRK; k++) {
        tb3_defaults(&tb3_cfg[k], 0);
        tb3_hist[k].n = tb3_hist[k].at = tb3_hist[k].head = 0;
        tb3_rt[k].rng = tb3_rt[k].passes = tb3_rt[k].stale = 0;
        tb3_rt[k].tw_pos = 0;
        tb3_tw[k] = 0;
        tb3_rebase(k);
    }
}
