/* SPDX-License-Identifier: GPL-3.0-only
 * Part of Felucca: free software under the GNU General Public License, version 3 only (see LICENSE) */
/* DrumBrain on a DRUM track (FELUCCA_DBRAIN; the pages, the keys and the writing of the steps: ui_dbrain.c): the model
 * of Michael Ganss's DrumBrain (github.com/mganss/DrumBrain, MIT: assets/drumbrain/, tools/gen_dbrain.py,
 * build/gen/felucca_dbrain.h), a Lempel-Ziv continuation dictionary per style and instrument, as it is. A port of its
 * generate(): the text starts with ^, a - between beats; a step is drawn from the longest end of the text (CONTEXT
 * characters at most) that the dictionary has. TEMP 100 % and DENS 100 % are the model's own odds (within rounding).
 * Each instrument on its own, as DrumBrain's: DRUM's 8 lanes take BD SD CP CH OH RS, one of LT MT HT for the TOM lane
 * (picked per new pattern) and CB for BELL (CY on a kit whose BELL lane plays a cymbal). AC (index 0) is the step's
 * velocity, a high hit or a flam its lane's accent (the flam as DrumBrain plays it with FLAM off).
 *
 * BARS: N = 16 or 32 steps (at most LEN) are generated and repeated to LEN: the model is of 1 and 2 bar patterns, its
 * odds drift further out. DENS (ours, the model's tables untouched): thins each lane's new hits after the draw, the
 * weakest first (off the beat, then low, then the seed's): 58 % of the model's contexts have one continuation, which
 * no bias on the odds moves. VARY (DrumBrain's Continue): keeps the first half of N and goes on from it.
 *
 * Worked out in the main loop, the audio ISRs running; nothing here runs in an ISR. Integer only.
 * The settings per track (db_cfg): a project keeps them in a DRUM track's FM6 area (db_pack, project.c). */
enum { DB_ALL, DB_HOUSE, DB_BREAKS, DB_NSTYLE };
enum { DB_L_BEAT, DB_L_BEATC, DB_L_ALL, DB_L_KICK, DB_L_SNARE, DB_L_HATS, DB_L_PERC, DB_NLANES };
#define DB_BEATS 16u
#define DB_NMAX (2u * DB_BEATS)       /* BARS 2 */
#define DB_NINST 12u                  /* in the model, AC first */
#define DB_I_AC 0u
#define DB_I_TOM 3u                   /* LT MT HT */
#define DB_I_CB 8u
#define DB_I_CY 9u

static const char *const DB_STYLE_NAMES[DB_NSTYLE] = {"ALL", "HOUSE", "BREAKS"};
static const char *const DB_LANES_NAMES[DB_NLANES] = {"BEAT", "BEAT+C", "ALL", "KICK", "SNARE", "HATS", "PERC"};
/* the lanes a set generates: the others keep what they have */
#define DB_B(l) (1u << (l))
static const uint8_t DB_LANE_MASK[DB_NLANES] = {
    DB_B(DV_KICK) | DB_B(DV_SNARE) | DB_B(DV_HATC) | DB_B(DV_HATO),
    DB_B(DV_KICK) | DB_B(DV_SNARE) | DB_B(DV_HATC) | DB_B(DV_HATO) | DB_B(DV_CLAP),
    0xFF,
    DB_B(DV_KICK),
    DB_B(DV_SNARE),
    DB_B(DV_HATC) | DB_B(DV_HATO),
    DB_B(DV_CLAP) | DB_B(DV_TOM) | DB_B(DV_RIM) | DB_B(DV_BELL),
};

typedef struct {
    uint8_t style;               /* DB_ALL .. */
    uint8_t temp;                /* TEMP 10..200 %, 100 the model's own odds */
    uint8_t dens;                /* DENS 10..100 %, the share of each lane's hits kept */
    uint8_t ctx;                 /* CONTEXT 1..32 characters of the text, its - too, as DrumBrain's */
    uint8_t lanes;               /* LANES: DB_L_* */
    uint8_t bars;                /* BARS 1, 2 */
    uint8_t tom;                 /* 0..2: LT MT HT on the TOM lane, the new pattern's (VARY goes on with it) */
    uint32_t seed;               /* 0: no pattern yet */
} db_cfg_t;
static db_cfg_t db_cfg[NTRK];
static uint8_t db_stale[NTRK];   /* the knobs no longer match the pattern (the footer's star) */

typedef struct {                 /* N steps of the lanes the set generates */
    uint8_t n, from;             /* N; the steps below from are kept (VARY) */
    uint8_t hit[DB_NMAX], acc[DB_NMAX];   /* bit l: lane l hits, is accented */
    uint8_t ac[DB_NMAX];         /* AC on the step */
} db_pat_t;

static void db_defaults(db_cfg_t *g)
{
    g->style = DB_HOUSE;         /* DrumBrain's own */
    g->temp = 100;
    g->dens = 100;
    g->ctx = 32;
    g->lanes = DB_L_BEAT;
    g->bars = 2;
    g->tom = 0;
    g->seed = 0;
}
static void db_reset(void)       /* power-on: every track's defaults */
{
    uint32_t k;
    for (k = 0; k < NTRK; k++) {
        db_defaults(&db_cfg[k]);
        db_stale[k] = 0;
    }
}
static uint32_t db_len(const db_cfg_t *g, const track_t *t)   /* N: the steps generated */
{
    uint32_t n = (uint32_t)tb3_clampi(g->bars, 1, 2) * DB_BEATS, len = (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP);
    return n < len ? n : len;
}
/* the random stream of a new pattern (fresh: a new seed and TOM source from entropy) or of a VARY (never 0: xorshift) */
static uint32_t db_start(db_cfg_t *g, uint32_t fresh, uint32_t entropy)
{
    uint32_t r;
    if (!fresh) {
        r = g->seed ^ entropy;
        return r ? r : 1u;
    }
    g->seed = tb3_new_seed(entropy);
    r = g->seed;
    g->tom = (uint8_t)((tb3_u(&r) * 3u) >> 24);
    return r;
}

#if DBRAIN_ON
#include "felucca_dbrain.h"
/* the model (tools/gen_dbrain.py has its format). The text's symbols: DB_SYM's order there */
enum { DB_S_DASH = 4, DB_S_START = 5 };

/* node n of table t: its child on symbol s, 0 none (a root is nobody's child) */
static uint32_t db_child(uint32_t t, uint32_t n, uint32_t s)
{
    uint32_t mask = DB_NODE[n] & 63u, c, m;
    if (!((mask >> s) & 1u))
        return 0;
    c = DB_CK[n >> 3];
    for (m = n & ~7u; m < n; m++)
        c += (uint32_t)__builtin_popcount(DB_NODE[m] & 63u);
    return t + 1u + c + (uint32_t)__builtin_popcount(mask & ((1u << s) - 1u));
}
/* the distribution of text[0..n) in table t, 0: not a context (an interior node is none) */
static uint32_t db_walk(uint32_t t, const uint8_t *text, uint32_t n)
{
    uint32_t at = DB_ROOT[t], i;
    for (i = 0; i < n; i++) {
        at = db_child(t, at, text[i]);
        if (!at)
            return 0;
    }
    return DB_NODE[at] >> 6;
}
/* the longest end of text[0..len) of at most ctx characters that is a context (the empty one always is) */
static uint32_t db_find(uint32_t t, const uint8_t *text, uint32_t len, uint32_t ctx)
{
    uint32_t i = len > ctx ? len - ctx : 0u, d;
    for (; i < len; i++) {
        d = db_walk(t, text + i, len - i);
        if (d)
            return d;
    }
    return DB_NODE[DB_ROOT[t]] >> 6;
}
/* a step (0 rest, 1 low, 2 high, 3 flam) from distribution d: its weights x 100 / TEMP, then 2^-w (as AICD's, tb3po.c) */
static uint32_t db_draw(uint32_t d, uint32_t temp, uint32_t *r)
{
    const uint8_t *w = DB_DIST[d - 1u];
    uint32_t s[4], i, sum = 0, x;
    for (i = 0; i < 4u; i++) {
        uint32_t v = w[i] == 255u ? 16u * 17u : (uint32_t)w[i] * 100u / temp;
        s[i] = v >= 16u * 17u ? 0u : (uint32_t)AICD_EXP[v & 15u] >> (v >> 4);
        sum += s[i];
    }
    x = (uint32_t)(((uint64_t)tb3_u(r) * sum) >> 24);
    for (i = 0; i < 3u && x >= s[i]; i++)
        x -= s[i];
    return i;
}
/* out[0..n): the steps kept[0..nkeep), then the model's, on table t */
static void db_gen(const db_cfg_t *g, uint32_t t, const uint8_t *kept, uint32_t nkeep, uint8_t *out, uint32_t n,
                   uint32_t *r)
{
    uint8_t text[DB_NMAX + DB_NMAX / 4u + 2u];
    uint32_t len = 0, i, ctx = (uint32_t)tb3_clampi(g->ctx, 1, 32), temp = (uint32_t)tb3_clampi(g->temp, 10, 200);
    text[len++] = DB_S_START;
    for (i = 0; i < n; i++) {
        if ((len - 1u) % 5u == 4u)
            text[len++] = DB_S_DASH;
        out[i] = i < nkeep ? kept[i] : (uint8_t)db_draw(db_find(t, text, len, ctx), temp, r);
        text[len++] = out[i];
    }
}
/* DENS: keeps ceil(hits x dens / 100) of out[from..n), the weakest go first: off the beat, then low, then the stream's.
 * At 100 % nothing is drawn: the steps are the model's */
static void db_thin(uint8_t *out, uint32_t from, uint32_t n, uint32_t dens, uint32_t *r)
{
    static const uint8_t PLACE[DB_BEATS] = {4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0};
    uint16_t key[DB_NMAX];
    uint32_t i, c = 0, keep;
    if (dens >= 100u)
        return;
    for (i = from; i < n; i++)
        if (out[i]) {
            key[i] = (uint16_t)((uint32_t)PLACE[i & (DB_BEATS - 1u)] << 9 | (out[i] >= 2u ? 256u : 0u) |
                                ((tb3_u(r) >> 16) & 255u));
            c++;
        }
    keep = (c * dens + 99u) / 100u;
    for (; c > keep; c--) {
        uint32_t lo = n;
        for (i = from; i < n; i++)
            if (out[i] && (lo == n || key[i] < key[lo]))
                lo = i;
        out[lo] = 0;
    }
}
/* lane l's table on track t */
static uint32_t db_table(const db_cfg_t *g, const track_t *t, uint32_t l)
{
    static const uint8_t INST[NLANE] = {1, 2, 7, 11, 10, DB_I_TOM, 6, DB_I_CB};   /* KICK SNARE CLAP HATC HATO TOM RIM BELL */
    uint32_t i = INST[l & (NLANE - 1u)];
    if (l == DV_TOM)
        i += g->tom % 3u;
    else if (l == DV_BELL && (drum_swaps(t) & 4u))
        i = DB_I_CY;
    return (uint32_t)tb3_clampi(g->style, 0, DB_NSTYLE - 1) * DB_NINST + i;
}
/* the pattern of track t's settings: new, or (vary) going on from its steps' first half. Reads the steps, writes none */
static void db_line(const track_t *t, uint32_t vary, uint32_t *r, db_pat_t *p)
{
    const db_cfg_t *g = &db_cfg[trk_index(t)];
    uint32_t n = db_len(g, t), from = vary ? n / 2u : 0u, mask = DB_LANE_MASK[g->lanes % DB_NLANES], l, i;
    uint8_t kept[DB_NMAX], out[DB_NMAX];
    memset(p, 0, sizeof *p);
    p->n = (uint8_t)n;
    p->from = (uint8_t)from;
    for (l = 0; l < NLANE; l++) {
        uint32_t b = 1u << l;
        if (!(mask & b))
            continue;
        for (i = 0; i < from; i++)                     /* its first half as DrumBrain's Continue reads a clip */
            kept[i] = (uint8_t)(!(step_lanes(&t->step[i]) & b) ? 0u : (step_accents(&t->step[i]) & b) ? 2u : 1u);
        db_gen(g, db_table(g, t, l), kept, from, out, n, r);
        db_thin(out, from, n, g->dens, r);
        for (i = 0; i < n; i++) {
            if (out[i])
                p->hit[i] |= (uint8_t)b;
            if (out[i] >= 2u)
                p->acc[i] |= (uint8_t)b;
        }
    }
    db_gen(g, (uint32_t)tb3_clampi(g->style, 0, DB_NSTYLE - 1) * DB_NINST + DB_I_AC, kept, 0, out, n, r);   /* (from ^) */
    for (i = 0; i < n; i++)
        p->ac[i] = out[i] != 0u;
}
#else
static void db_line(const track_t *t, uint32_t vary, uint32_t *r, db_pat_t *p)
{
    (void)t; (void)vary; (void)r;
    memset(p, 0, sizeof *p);
}
#endif

/* ------------------------------------------------------------ project --- */
/* a DRUM track's FM6 area in a project (project.c: its FM6 patch is the init one then), only once its settings are not
 * the power-on ones: "DBRN", version 1, STYLE, TEMP (two bytes), DENS CONTEXT LANES BARS, the TOM source, the seed in
 * five 7-bit bytes; zeros; every byte 0..127 (proj_pack) */
#define DB_PK_VER 1u
static int db_touched(uint32_t k)
{
    const db_cfg_t *g = &db_cfg[k % NTRK];
    db_cfg_t d;
    db_defaults(&d);
    return g->style != d.style || g->temp != d.temp || g->dens != d.dens || g->ctx != d.ctx || g->lanes != d.lanes ||
           g->bars != d.bars || g->tom != d.tom || g->seed != d.seed;
}
static void db_pack(uint32_t k, uint8_t *pk)
{
    const db_cfg_t *g = &db_cfg[k % NTRK];
    uint32_t i;
    memset(pk, 0, FM6_PACKED);
    pk[0] = 'D'; pk[1] = 'B'; pk[2] = 'R'; pk[3] = 'N';
    pk[4] = DB_PK_VER;
    pk[5] = g->style;
    pk[6] = g->temp & 127u;
    pk[7] = (uint8_t)(g->temp >> 7);
    pk[8] = g->dens; pk[9] = g->ctx; pk[10] = g->lanes; pk[11] = g->bars; pk[12] = g->tom;
    for (i = 0; i < 5u; i++)
        pk[13u + i] = (uint8_t)((g->seed >> (7u * i)) & 127u);
}
/* track k's settings from a project's FM6 area: 1 = it holds them (each value into its range), else the defaults, 0 */
static int db_unpack(uint32_t k, const uint8_t *pk)
{
    db_cfg_t *g = &db_cfg[k % NTRK];
    uint32_t i, seed = 0;
    db_stale[k % NTRK] = 0;
    if (pk[0] != 'D' || pk[1] != 'B' || pk[2] != 'R' || pk[3] != 'N' || pk[4] != DB_PK_VER) {
        db_defaults(g);
        return 0;
    }
    g->style = (uint8_t)tb3_clampi(pk[5], 0, DB_NSTYLE - 1);
    g->temp = (uint8_t)tb3_clampi(pk[6] | pk[7] << 7, 10, 200);
    g->dens = (uint8_t)tb3_clampi(pk[8], 10, 100);
    g->ctx = (uint8_t)tb3_clampi(pk[9], 1, 32);
    g->lanes = (uint8_t)tb3_clampi(pk[10], 0, DB_NLANES - 1);
    g->bars = (uint8_t)tb3_clampi(pk[11], 1, 2);
    g->tom = (uint8_t)tb3_clampi(pk[12], 0, 2);
    for (i = 0; i < 5u; i++)
        seed |= (uint32_t)(pk[13u + i] & 127u) << (7u * i);
    g->seed = seed;
    return 1;
}
