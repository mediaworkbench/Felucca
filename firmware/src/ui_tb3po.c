/* SPDX-License-Identifier: GPL-3.0-only
 * Part of Felucca: free software under the GNU General Public License, version 3 only (see LICENSE) */
/* ARP on a 303 track: TB-3PO's pages (tb3po.c), as X0X has them. GENERATE: DENS ACCENT SLIDE OCTS; SCALE: ROOT SCALE
 * OCTAVE MUTATE (every 1..16 passes of the line while playing, OFF); 1.6.2 (FELUCCA_AICD): MODEL: MODE (TB-3PO, AICD).
 * In AICD GENERATE is DENS ACCENT SLIDE TEMP (as TB-3PO's, 0..100 %, on Aicd's model; and its temperature) and SCALE
 * offers CHRO first (no bias), each a setting of its own. ARP taps through them as between ARP and ARP 2 (ui.c
 * page_visible: these on a 303 track, the arp's on any other). 1.8.2: DENS ACCENT SLIDE OCTS ROOT SCALE OCTAVE act on
 * the line that plays, at its next bar (stopped: at once; in a song: after it), back to the same line when turned back
 * (tb3po.c tb3_live; no undo: the knob is one). TEMP and MODE set up the next line only, until OCT+ (a new line, a new
 * seed) or OCT- (a MUTATE, ~25 % of the steps); the footer's NEW LINE gets a star while they no longer match it.
 * OCT+ / OCT- are each an undo (SAVE held; the seed and NEW LINE's star with it: UNDO_TB3); the track's automation and
 * locks stay; not in a song (its steps are a slot's). SHAPE: MODE ACID's STYLE NOTES RHYTHM TIES, AICD's JOIN BEST; FORM:
 * FORM (ACID's) FLAIR TWIST T.BARS; PLAY: MORPH SLIP P.LEN M.TYPE (OCT+ a new line B, OCT- A and B swap); LINK: POCKET
 * ANSWER ROOT< KEYS (OCT+ the first DRUM track's pattern too, DrumBrain's). Included by ui.c */
static int32_t accel(uint32_t role, int32_t s, int32_t range);   /* (ui_input.c) */
static void tl_undo_take(track_t *t);                 /* (ui_tools.c) */

enum { TB3_DENS, TB3_ACCT, TB3_SLID, TB3_OCTS, TB3_ROOT, TB3_SCL, TB3_BASE, TB3_MUT,
       TB3_MODE, TB3_TEMP, TB3_ADENS, TB3_AACC, TB3_ASLD, TB3_ASCL, TB3_SEED, TB3_HIST, TB3_KEEP,
       TB3_STYLE, TB3_NOTES, TB3_RHYTHM, TB3_TIES, TB3_FORM, TB3_JOIN, TB3_BEST,
       TB3_FLAIR, TB3_TWIST, TB3_TBARS, TB3_MORPH, TB3_SLIP, TB3_PLEN, TB3_MTYPE, TB3_KEYS,
       TB3_POCKET, TB3_ANSWER, TB3_ROOTF, TB3_NK };
static const char *const N_TB3_MUT[] = {"OFF", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13",
                                        "14", "15", "16"};
static const char *const N_TB3_MODE[] = {"TB-3PO", "AICD", "ACID"};
static const char *const N_TB3_KEEP[] = {"OFF", "NOTES", "RHYTHM"};
static const char *const N_TB3_STYLE[ACID_NSTYLE] = {"AUTO", "ROLLING", "STAB", "HYPNOTIC", "TRANCE", "DUB"};
static const char *const N_TB3_NOTES[] = {"AUTO", "1", "2", "3", "4", "5", "6", "7"};
static const char *const N_TB3_RHYTHM[] = {"AUTO", "16THS", "OFFBEAT", "3-3-2", "SPARSE", "ROLL", "MODEL"};
static const char *const N_TB3_TIES[ACID_NTIES] = {"AUTO", "0%", "10%", "20%", "30%", "40%", "50%", "60%", "70%", "80%", "90%", "100%"};
static const char *const N_TB3_FORM[ACID_NFORM] = {"OFF", "AAAB", "AABA", "ABAC", "CALL"};
static const char *const N_TB3_JOIN[] = {"OFF", "ON"};
static const char *const N_TB3_MTYPE[] = {"DRIFT", "VARY", "TURN"};
static const char *const N_TB3_KEYS[] = {"PLAY", "ROOT", "POOL"};
static const char *const N_TB3_POCKET[] = {"OFF", "DODGE", "LOCK", "AFTER"};
static const char *const N_TB3_ANSWER[] = {"OFF", "REST", "3RD", "5TH", "OCT"};
static const char *const N_TB3_ROOTF[] = {"OFF", "T1", "T2", "T3", "T4"};
static int db_groove(track_t *t, uint32_t entropy);   /* (ui_dbrain.c) */
static const param_desc_t TB3_DESC[TB3_NK] = {   /* X0X's ranges and defaults (tb3po.c tb3_defaults); then AICD's */
    {"DENS", F_INT, 0, 100, 70, 0, "%"},
    {"ACCENT", F_INT, 0, 100, 40, 0, "%"},
    {"SLIDE", F_INT, 0, 100, 25, 0, "%"},
    {"OCTS", F_INT, 1, 3, 2, 0, 0},
    {"ROOT", F_NOTE, 0, 11, 9, 0, 0},
    {"SCALE", F_ENUM, 0, TB3_NSCALES - 1, 0, TB3_SCALE_NAMES, 0},
    {"OCTAVE", F_INT, 0, 4, 1, 0, 0},
    {"MUTATE", F_ENUM, 0, 16, 0, N_TB3_MUT, 0},
    {"MODE", F_ENUM, 0, 2, 0, N_TB3_MODE, 0},            /* (without AICD: TB-3PO, ACID, tb3_knob) */
    {"TEMP", F_INT, 10, 200, 100, 0, "%"},
    {"DENS", F_INT, 0, 100, AICD_DENS, 0, "%"},
    {"ACCENT", F_INT, 0, 100, AICD_ACC, 0, "%"},
    {"SLIDE", F_INT, 0, 100, AICD_SLD, 0, "%"},
    {"SCALE", F_ENUM, 0, TB3_NSCALES, 0, TB3_ASCALE_NAMES, 0},
    {"SEED", F_INT, 0, 9999, 0, 0, 0},                  /* (0: a line without a number, X0X's seed BEEF too) */
    {"HIST", F_INT, 0, TB3_NHIST - 1, 0, 0, 0},
    {"KEEP", F_ENUM, 0, 2, 0, N_TB3_KEEP, 0},
    {"STYLE", F_ENUM, 0, ACID_NSTYLE - 1, 0, N_TB3_STYLE, 0},
    {"NOTES", F_ENUM, 0, 7, 0, N_TB3_NOTES, 0},
    {"RHYTHM", F_ENUM, 0, ACID_NRHYTHM - 1, 0, N_TB3_RHYTHM, 0},
    {"TIES", F_ENUM, 0, ACID_NTIES - 1, 0, N_TB3_TIES, 0},
    {"FORM", F_ENUM, 0, ACID_NFORM - 1, 0, N_TB3_FORM, 0},
    {"JOIN", F_ENUM, 0, 1, 0, N_TB3_JOIN, 0},
    {"BEST", F_INT, 1, 8, 1, 0, 0},
    {"FLAIR", F_INT, 0, 100, 0, 0, "%"},
    {"TWIST", F_INT, 0, 100, 0, 0, "%"},
    {"T.BARS", F_INT, 1, 16, 4, 0, 0},
    {"MORPH", F_INT, 0, 100, 0, 0, "%"},
    {"SLIP", F_INT, 0, 15, 0, 0, 0},
    {"P.LEN", F_ENUM, 0, 16, 0, N_TB3_MUT, 0},
    {"M.TYPE", F_ENUM, 0, 2, 0, N_TB3_MTYPE, 0},
    {"KEYS", F_ENUM, 0, 2, 0, N_TB3_KEYS, 0},
    {"POCKET", F_ENUM, 0, 3, 0, N_TB3_POCKET, 0},
    {"ANSWER", F_ENUM, 0, 4, 0, N_TB3_ANSWER, 0},
    {"ROOT<", F_ENUM, 0, 4, 0, N_TB3_ROOTF, 0},
};
_Static_assert(NELEM(N_TB3_MUT) == 17u, "MUTATE: OFF, 1..16");
static int16_t tb3_show[TB3_NK];  /* the values page_desc hands the cards (a pointer each): the selected track's */

static int tb3_graph(uint32_t g) { return g >= GR_TB3GEN && g <= GR_TB3LNK; }   /* (params.c: in a row) */
static int tb3_on(void) { return !ui.home && !ui.menu && tb3_graph(cur_page()->graph); }   /* a TB-3PO page shown */

static int32_t tb3_get(const tb3_cfg_t *g, uint32_t k)
{
    switch (k) {
    case TB3_DENS: return g->density;
    case TB3_ACCT: return g->accent;
    case TB3_SLID: return g->slide;
    case TB3_OCTS: return g->oct_range;
    case TB3_ROOT: return g->root;
    case TB3_SCL: return g->scale;
    case TB3_BASE: return g->base_oct;
    case TB3_MUT: return g->mutate_bars;
    case TB3_MODE: return g->mode;
    case TB3_TEMP: return g->temp;
    case TB3_ADENS: return g->a_dens;
    case TB3_AACC: return g->a_acc;
    case TB3_ASLD: return g->a_sld;
    case TB3_SEED: return g->line;
    case TB3_HIST: return tb3_hist[(uint32_t)(g - tb3_cfg) % NTRK].at;
    case TB3_KEEP: return g->keep;
    case TB3_STYLE: return g->style;
    case TB3_NOTES: return g->notes;
    case TB3_RHYTHM: return g->rhythm;
    case TB3_TIES: return g->ties;
    case TB3_FORM: return g->form;
    case TB3_JOIN: return g->join;
    case TB3_BEST: return g->best ? g->best : 1;
    case TB3_FLAIR: return g->flair;
    case TB3_TWIST: return g->twist;
    case TB3_TBARS: return g->tbars ? g->tbars : 4;
    case TB3_MORPH: return g->morph;
    case TB3_SLIP: return g->slip;
    case TB3_PLEN: return g->plen;
    case TB3_MTYPE: return g->mtype;
    case TB3_KEYS: return g->keys;
    case TB3_POCKET: return g->pocket;
    case TB3_ANSWER: return g->answer;
    case TB3_ROOTF: return g->rootfol;
    default: return g->a_scale;
    }
}
static void tb3_set(tb3_cfg_t *g, uint32_t k, int32_t v)
{
    int32_t c = clamp(v, TB3_DESC[k % TB3_NK].min, TB3_DESC[k % TB3_NK].max);
    uint8_t x = (uint8_t)c;
    switch (k) {
    case TB3_DENS: g->density = x; break;
    case TB3_ACCT: g->accent = x; break;
    case TB3_SLID: g->slide = x; break;
    case TB3_OCTS: g->oct_range = x; break;
    case TB3_ROOT: g->root = x; break;
    case TB3_SCL: g->scale = x; break;
    case TB3_BASE: g->base_oct = x; break;
    case TB3_MUT: g->mutate_bars = x; break;
    case TB3_MODE: g->mode = x; break;
    case TB3_TEMP: g->temp = x; break;
    case TB3_ADENS: g->a_dens = x; break;
    case TB3_AACC: g->a_acc = x; break;
    case TB3_ASLD: g->a_sld = x; break;
    case TB3_SEED: g->line = (uint16_t)c; break;
    case TB3_HIST: break;
    case TB3_KEEP: g->keep = x; break;
    case TB3_STYLE: g->style = x; break;
    case TB3_NOTES: g->notes = x; break;
    case TB3_RHYTHM: g->rhythm = x; break;
    case TB3_TIES: g->ties = x; break;
    case TB3_FORM: g->form = x; break;
    case TB3_JOIN: g->join = x; break;
    case TB3_BEST: g->best = x; break;
    case TB3_FLAIR: g->flair = x; break;
    case TB3_TWIST: g->twist = x; break;
    case TB3_TBARS: g->tbars = x; break;
    case TB3_MORPH: g->morph = x; break;
    case TB3_SLIP: g->slip = x; break;
    case TB3_PLEN: g->plen = x; break;
    case TB3_MTYPE: g->mtype = x; break;
    case TB3_KEYS: g->keys = x; break;
    case TB3_POCKET: g->pocket = x; break;
    case TB3_ANSWER: g->answer = x; break;
    case TB3_ROOTF: g->rootfol = x; break;
    default: g->a_scale = x; break;
    }
}

/* the setting KNOB slot edits on page pg, in track tr's MODE (TB3_NK: none) */
static uint32_t tb3_key(const page_t *pg, uint32_t slot, uint32_t tr)
{
    static const uint8_t AICD_K[8] = {TB3_ADENS, TB3_AACC, TB3_ASLD, TB3_TEMP, TB3_ROOT, TB3_ASCL, TB3_BASE, TB3_MUT};
    uint32_t k = (pg->graph == GR_TB3SCL ? 4u : 0u) + (slot & 3u);
    if (pg->graph == GR_TB3MOD) {
        static const uint8_t MOD_K[4] = {TB3_MODE, TB3_SEED, TB3_HIST, TB3_KEEP};
        return MOD_K[slot & 3u];
    }
    if (pg->graph == GR_TB3SHP) {
        if (tb3_cfg[tr % NTRK].mode == TB3_ACID)
            return TB3_STYLE + (slot & 3u);
        return (slot & 3u) > 1u ? TB3_NK : TB3_JOIN + (slot & 3u);
    }
    if (pg->graph == GR_TB3FRM) {                      /* (FORM: MODE ACID's; the others any MODE's) */
        if (!(slot & 3u))
            return tb3_cfg[tr % NTRK].mode == TB3_ACID ? TB3_FORM : TB3_NK;
        return TB3_FLAIR + (slot & 3u) - 1u;
    }
    if (pg->graph == GR_TB3PLY)
        return TB3_MORPH + (slot & 3u);
    if (pg->graph == GR_TB3LNK) {                      /* tracks that listen */
        static const uint8_t LNK_K[4] = {TB3_POCKET, TB3_ANSWER, TB3_ROOTF, TB3_KEYS};
        return LNK_K[slot & 3u];
    }
    return tb3_cfg[tr % NTRK].mode == TB3_AICD ? AICD_K[k] : k;
}

/* params.c page_desc on a TB-3PO page: KNOB slot's setting, its value in tb3_show */
static const param_desc_t *tb3_desc(const page_t *pg, uint32_t slot, int16_t **vp)
{
    uint32_t k = tb3_key(pg, slot, song.sel);
    if (k >= TB3_NK) {
        *vp = 0;
        return 0;
    }
    tb3_show[k] = (int16_t)tb3_get(&tb3_cfg[song.sel % NTRK], k);
    *vp = &tb3_show[k];
    return &TB3_DESC[k];
}

/* the line into the track: while playing at the next bar (tb3_next, tb3_bar), else now, a step at a time with IRQs off
 * (TIMER5 serves USB audio), as DrumBrain's */
static void tb3_emit(track_t *t, const tb3_step_t *line, int fresh)
{
    uint32_t k = trk_index(t), i;
    tb3_step_t old[NSTEP];
    if (song.playing) {
        tb3_next_t *q = &tb3_next[k];
        fm1_irq_off();
        memcpy(q->line, line, sizeof q->line);
        q->fresh = (uint8_t)fresh;
        q->k = tb3_knobs(&tb3_cfg[k]);
        q->state = TB3Q_READY;
        fm1_irq_on();
        return;
    }
    tb3_read(t->step, old);
    for (i = 0; i < NSTEP; i++) {
        fm1_irq_off();
        if (fresh || line[i].note != old[i].note || line[i].flags != old[i].flags)
            tb3_put(&t->step[i], &line[i], fresh);
        if (fresh)
            tb3_flair(&tb3_cfg[k], &t->step[i], line, i, (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP));
        fm1_irq_on();
    }
    fm1_irq_off();
    tb3_anchor(k, t->step, fresh);
    if (tb3_rt[k].pending && !chain_busy())
        tb3_live(t);                                   /* (MORPH POCKET SLIP still to come: at once, as a knob's) */
    fm1_irq_on();
    sync_reload = 1;                                   /* (the editor reads the steps again) */
    ui.force = 1;
}

/* a new action on track t's line: one undo (a line already waiting for its bar, a SEED / HIST gesture going on: that
 * one's) and the line as it is into HIST's list. The automation and locks stay (not load_begin: it drops them) */
static int tb3_undo(track_t *t, int gesture)         /* 1: a copy taken, a new action */
{
    static uint32_t ms;
    uint32_t k = trk_index(t), same = undo.trk == k + 1u && (undo.what & UNDO_TB3) != 0u;
    if ((tb3_next[k].state == TB3Q_READY && same) || (gesture && same && fm1_ms - ms < 1500u)) {
        ms = fm1_ms;
        return 0;
    }
    ms = fm1_ms;
    tl_undo_take(t);
    undo.what |= UNDO_TB3;
    undo.tb3_seed = tb3_cfg[k].seed;
    undo.tb3_seed_b = tb3_cfg[k].seed_b;
    undo.tb3_line = tb3_cfg[k].line;
    undo.tb3_stale = tb3_rt[k].stale;
    return 1;
}
static void tb3_begin(track_t *t, int gesture)
{
    uint32_t k = trk_index(t);
    if (tb3_undo(t, gesture)) {
        tb3_hist_t *h = &tb3_hist[k];
        fm1_irq_off();
        tb3_read(t->step, h->line[h->head]);
        fm1_irq_on();
        h->seed[h->head] = tb3_cfg[k].seed;
        h->no[h->head] = tb3_cfg[k].line;
        h->head = (uint8_t)((h->head + 1u) % TB3_NHIST);
        h->n = (uint8_t)(h->n < TB3_NHIST ? h->n + 1u : TB3_NHIST);
        h->at = 0;
    }
}

/* track t's line anew (fresh) or a MUTATE of it (of the line already waiting, if one is), the stream on from *rng */
static void tb3_prep(track_t *t, int fresh, uint32_t *rng)
{
    uint32_t k = trk_index(t);
    tb3_step_t line[NSTEP], old[NSTEP];
    fm1_irq_off();
    if (fresh)
        tb3_rt[k].pending = 0;                         /* (the new line is drawn at the knobs) */
    else if (tb3_rt[k].pending)
        tb3_live(t);                                   /* (a MUTATE of the line the knobs say) */
    if (!fresh) {
        tb3_cfg_t tmp;
        tb3_fill_rests(tb3_gcfg(k, &tmp), t->step);
    }
    if (tb3_next[k].state == TB3Q_READY)
        memcpy(old, tb3_next[k].line, sizeof old);
    else
        tb3_read(t->step, old);
    fm1_irq_on();
    tb3_line(t, old, line, fresh, rng);                /* (IRQs on: AICD's takes under a ms) */
    tb3_emit(t, line, fresh);
}
static void tb3_make(track_t *t, int fresh)
{
    uint32_t k = trk_index(t), r = tb3_cfg[k].seed ^ fm1_ms;
    tb3_prep(t, fresh, &r);
    tb3_rt[k].stale = 0;
}

static uint32_t tb3_line_seed(uint32_t line) { return tb3_new_seed(line); }

/* the first DRUM track other than k (NTRK: none): POCKET's kick, LINK's groove */
static uint32_t tb3_drums(uint32_t k)
{
    uint32_t d;
    for (d = 0; d < NTRK && (d == k || !drum_track(&trk[d])); d++)
        ;
    return d;
}
/* LINK's OCT+ rolls the drums too: a DRUM track to roll, DrumBrain built (the footer says NEW GROOVE) */
static int tb3_groove_ok(uint32_t k) { return DBRAIN_ON && tb3_drums(k) < NTRK; }
/* POCKET's kick: the first DRUM track's kick lane under track k's steps (its LEN repeated); a change is news for the line */
static void tb3_kick_take(uint32_t k)
{
    uint32_t d = tb3_drums(k), len = (uint32_t)clamp(trk[k].p[P_SLEN], 1, NSTEP), dl, i;
    uint64_t m = 0;
    if (d < NTRK) {
        dl = (uint32_t)clamp(trk[d].p[P_SLEN], 1, NSTEP);
        for (i = 0; i < len; i++)
            if ((step_lanes(&trk[d].step[i % dl]) >> DV_KICK) & 1u)
                m |= 1ull << i;
    }
    if (m == tb3_rt[k].kick)
        return;
    fm1_irq_off();
    tb3_rt[k].kick = m;
    tb3_rt[k].pending |= (uint8_t)(tb3_cfg[k].pocket != 0u);
    fm1_irq_on();
}
/* line B (MORPH) from its seed, at the knobs now; the audio ISR reads it */
static void tb3_b_make(uint32_t k)
{
    tb3_cfg_t tmp, g;
    tb3_step_t b[NSTEP];
    g = *tb3_gcfg(k, &tmp);                            /* (ROOT< following: B in that key too) */
    if (!g.seed_b)
        tb3_cfg[k].seed_b = g.seed_b = tb3_h(g.seed, 0, 'B') | 1u;   /* (a first MORPH: B from A's seed) */
    g.seed = g.seed_b;
    g.best = 0;
    tb3_gen(&g, b, (uint32_t)clamp(trk[k].p[P_SLEN], 1, NSTEP));
    fm1_irq_off();
    memcpy(tb3_b[k], b, sizeof b);
    tb3_rt[k].kb = tb3_knobs(&tb3_cfg[k]);
    tb3_rt[k].bok = 1;
    fm1_irq_on();
}
/* PLAY's OCT+: a new line B; OCT-: A and B change places */
static void tb3_ab(track_t *t, uint32_t fresh)
{
    uint32_t k = trk_index(t), x;
    tb3_step_t line[NSTEP];
    if (fresh) {
        tb3_cfg[k].seed_b = tb3_new_seed(fm1_ms ^ (ui.frame << 7));
        tb3_b_make(k);
        tb3_rt[k].pending |= (uint8_t)(tb3_cfg[k].morph != 0u);
        ui_message("NEW B");
        return;
    }
    if (!tb3_rt[k].bok)
        tb3_b_make(k);
    tb3_begin(t, 0);
    memcpy(line, tb3_b[k], sizeof line);
    fm1_irq_off();
    if (tb3_rt[k].have)
        memcpy(tb3_b[k], tb3_rt[k].anchor, sizeof line);
    else
        tb3_read(t->step, tb3_b[k]);
    tb3_rt[k].kb = tb3_rt[k].k0;
    x = tb3_cfg[k].seed;
    tb3_cfg[k].seed = tb3_cfg[k].seed_b;
    tb3_cfg[k].seed_b = x;
    tb3_cfg[k].line = 0;                               /* (B has no number: the header shows its seed; HIST keeps A's) */
    tb3_rt[k].pending = 0;
    fm1_irq_on();
    tb3_emit(t, line, 1);
    ui_message("A <> B");
}

/* HIST: to entry `to` back (0: the line it left) */
static void tb3_hist_go(track_t *t, uint32_t to)
{
    uint32_t k = trk_index(t);
    tb3_hist_t *h = &tb3_hist[k];
    const tb3_step_t *l;
    if (!h->at) {
        fm1_irq_off();
        tb3_read(t->step, h->live);
        fm1_irq_on();
        h->live_seed = tb3_cfg[k].seed;
        h->live_no = tb3_cfg[k].line;
    }
    h->at = (uint8_t)to;
    if (to) {
        uint32_t e = (h->head + TB3_NHIST - to) % TB3_NHIST;
        l = h->line[e];
        tb3_cfg[k].seed = h->seed[e];
        tb3_cfg[k].line = h->no[e];
    } else {
        l = h->live;
        tb3_cfg[k].seed = h->live_seed;
        tb3_cfg[k].line = h->live_no;
    }
    fm1_irq_off();
    tb3_rt[k].pending = 0;
    fm1_irq_on();
    tb3_emit(t, l, 0);
}

static void tb3_knob(uint32_t slot, int32_t steps)   /* (ui_input.c edit_param) */
{
    const page_t *pg = cur_page();
    uint32_t tr = song.sel % NTRK, k = tb3_key(pg, slot, tr);
    const param_desc_t *d = &TB3_DESC[k % TB3_NK];
    int32_t v, n;
    if (k >= TB3_NK)
        return;
    v = tb3_get(&tb3_cfg[tr], k);
    n = param_turn(d, v, accel(EN_K1 + slot, steps, d->fmt == F_ENUM ? 0 : d->max - d->min));
    if (k == TB3_MODE && !AICD_ON && n == TB3_AICD)    /* (no AICD built: past it) */
        n = n > v ? TB3_ACID : TB3_TB3PO;
    if (n == v)
        return;
    if (k == TB3_SEED || k == TB3_HIST) {              /* a line by number, an earlier one (a song's steps are a slot's) */
        if (chain_busy()) {
            ui_message("STOP TO EDIT");
            return;
        }
        if (k == TB3_HIST) {
            tb3_hist_t *h = &tb3_hist[tr];
            n = n > h->n ? h->n : n;
            if (n != v) {
                (void)tb3_undo(&trk[tr], 1);
                tb3_hist_go(&trk[tr], (uint32_t)n);
            }
            return;
        }
        if (!n)                                        /* (back to 0: no line has that number) */
            return;
        tb3_begin(&trk[tr], 1);
        tb3_set(&tb3_cfg[tr], k, n);
        tb3_cfg[tr].seed = tb3_line_seed(tb3_cfg[tr].line);
        tb3_make(&trk[tr], 1);
        return;
    }
    if (k == TB3_KEEP || k == TB3_TWIST || k == TB3_TBARS || k == TB3_MTYPE || k == TB3_KEYS) {   /* (read when used) */
        tb3_set(&tb3_cfg[tr], k, n);
        return;
    }
    if (k == TB3_MORPH && (!tb3_rt[tr].bok || !tb3_cfg[tr].seed_b))
        tb3_b_make(tr);
    if (k == TB3_POCKET)
        tb3_kick_take(tr);
    if (k == TB3_ROOTF) {                              /* (where the source is now; playing, each bar again) */
        tb3_set(&tb3_cfg[tr], k, n);
        fm1_irq_off();
        tb3_rt[tr].fol = (uint8_t)tb3_follow(&trk[tr]);
        fm1_irq_on();
    }
    if (k == TB3_MODE && tb3_rt[tr].pending && !chain_busy()) {   /* (a knob turned this bar: on the line, in the old MODE) */
        fm1_irq_off();
        tb3_live(&trk[tr]);
        fm1_irq_on();
    }
    tb3_set(&tb3_cfg[tr], k, n);
    if (k == TB3_MUT)
        return;
    if (k == TB3_TEMP || k == TB3_MODE || (k >= TB3_STYLE && k <= TB3_FLAIR) || k == TB3_ANSWER) {   /* the next OCT+ /
                                                         * OCT-'s (MODE: the live knobs from the steps) */
        tb3_rt[tr].stale = 1;
        if (k == TB3_MODE) {
            fm1_irq_off();
            tb3_rebase(tr);
            fm1_irq_on();
        }
        return;
    }
    tb3_rt[tr].pending = 1;                            /* the line, at its next bar (tb3po.c tb3_bar) */
    if (!song.playing && !chain_busy()) {              /* stopped: at once */
        fm1_irq_off();
        tb3_live(&trk[tr]);
        fm1_irq_on();
        sync_reload = 1;
        ui.force = 1;
    }
}

/* OCT+ (oct bit 1): a new line, a new number; OCT- (bit 0): a MUTATE (X0X: its stream from the seed ^ the time). The
 * line is worked out with the audio ISR on (AICD's: under a ms); while playing it waits for the bar (tb3_emit) */
static void tb3_oct(uint32_t oct)
{
    track_t *t = TSEL;
    uint32_t k = trk_index(t), fresh = (oct & 2u) != 0u, groove = 0;
    if (chain_busy()) {
        ui_message("STOP TO EDIT");
        return;
    }
    if (cur_page()->graph == GR_TB3PLY) {
        tb3_ab(t, fresh);
        return;
    }
    if (fresh && cur_page()->graph == GR_TB3LNK && tb3_groove_ok(k))   /* LINK's OCT+: the drums first (their own pattern,
                                                                            * no undo of theirs) */
        groove = (uint32_t)db_groove(&trk[tb3_drums(k)], fm1_ms ^ (ui.frame << 7) ^ 0x6A09E667u);
    tb3_kick_take(k);
    tb3_begin(t, 0);
    if (fresh) {
        uint32_t x = (fm1_ms ^ (ui.frame << 7)) * 2654435761u;
        tb3_cfg[k].line = (uint16_t)(1u + (x >> 8) % 9999u);
        tb3_cfg[k].seed = tb3_line_seed(tb3_cfg[k].line);
    }
    tb3_make(t, (int)fresh);
    if (!fresh) {
        ui_message("MUTATED");
        return;
    }
    {
        char b[8];
        uint32_t v = tb3_cfg[k].line, d = v >= 1000u ? 4u : v >= 100u ? 3u : v >= 10u ? 2u : 1u, j;
        b[d] = 0;
        for (j = d; j--; v /= 10u)
            b[j] = (char)('0' + v % 10u);
        ui_say(groove ? "NEW GROOVE " : "NEW LINE ", b);
    }
}

/* the main loop: a line that was waiting when the transport stopped (or a song took over); a line the audio ISR changed
 * (auto MUTATE, a knob's bar, a waiting line) is news for the editor, at most once a second */
static void tb3_poll(void)
{
    static uint32_t seen, at, kat;
    uint32_t k;
    if (fm1_ms - kat >= 1000u) {                       /* POCKET: the drums as they are now (edited, recorded) */
        kat = fm1_ms;
        for (k = 0; k < NTRK; k++)
            if (tb3_cfg[k].pocket && trk[k].eng_req % NENGINES == ENGI_303)
                tb3_kick_take(k);
    }
    for (k = 0; k < NTRK; k++) {
        tb3_next_t *q = &tb3_next[k];
        if (!song.playing || !tb3_cfg[k].twist || trk[k].eng_req % NENGINES != ENGI_303)
            tb3_tw[k] = 0;                             /* (TWIST: no offset left once stopped or off; seq_enter sets it) */
        if (tb3_rt[k].want) {                          /* AICD with JOIN: its auto MUTATE, for the bar after (tb3_pass) */
            tb3_rt[k].want = 0;
            if (song.playing && !chain_busy() && q->state == TB3Q_NONE && trk[k].eng_req % NENGINES == ENGI_303)
                tb3_prep(&trk[k], 0, &tb3_rt[k].rng);
        }
        if (q->state == TB3Q_READY) {
            if (chain_busy() || trk[k].eng_req % NENGINES != ENGI_303)
                q->state = TB3Q_NONE;
            else if (!song.playing) {
                fm1_irq_off();
                tb3_commit(&trk[k]);
                fm1_irq_on();
            }
        }
        if (tb3_cfg[k].morph && tb3_cfg[k].seed_b && !tb3_rt[k].bok &&
            trk[k].eng_req % NENGINES == ENGI_303) {     /* (a project's MORPH: its line B again) */
            tb3_b_make(k);
            tb3_rt[k].pending = 1;
        }
        if (tb3_rt[k].pending && !song.playing && !chain_busy() && trk[k].eng_req % NENGINES == ENGI_303) {
            fm1_irq_off();                             /* (a key's ROOT, SLIP on a new line, stopped) */
            tb3_live(&trk[k]);
            fm1_irq_on();
            sync_reload = 1;
            ui.force = 1;
        }
        if (q->state == TB3Q_DONE) {
            q->state = TB3Q_NONE;
            ui.force = 1;
        }
    }
    if (tb3_auto_n != seen && fm1_ms - at >= 1000u) {
        seen = tb3_auto_n;
        at = fm1_ms;
        sync_reload = 1;
    }
}
