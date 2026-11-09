/* SPDX-License-Identifier: GPL-3.0-only
 * Part of Felucca: free software under the GNU General Public License, version 3 only (see LICENSE) */
/* ARP on a DRUM track (FELUCCA_DBRAIN): after ARP and ARP 2 the pages of DrumBrain (dbrain.c). BRAIN: STYLE TEMP DENS
 * CONTEXT; BRAIN 2: LANES BARS. They set up the next pattern only; the footer's NEW gets a star while they no longer
 * match it. OCT+ a new pattern (a new seed), OCT- a VARY (DrumBrain's Continue: the first half kept). Only the lanes of
 * LANES are written, so HATS re-rolls the hats and keeps the rest.
 *
 * As SEQ TOOLS' actions (ui_tools.c), not as TB-3PO: the undo copy keeps the track's automation and locks (load_begin
 * with UNDO_PAT would drop them, and here lanes and steps stay), one undo each (SAVE held; the seed, the TOM source and
 * NEW's star with it: UNDO_DB). Not in a song (its steps are a slot's). Included by ui.c */
static void tl_undo_take(track_t *t);                 /* (ui_tools.c) */
static uint64_t tl_used(const track_t *t, uint32_t len);
static void tl_unlock(track_t *t, uint64_t was, uint32_t len);

enum { DB_K_STYLE, DB_K_TEMP, DB_K_DENS, DB_K_CTX, DB_K_LANES, DB_K_BARS, DB_NK };
static const param_desc_t DB_DESC[DB_NK] = {
    {"STYLE", F_ENUM, 0, DB_NSTYLE - 1, DB_HOUSE, DB_STYLE_NAMES, 0},
    {"TEMP", F_INT, 10, 200, 100, 0, "%"},
    {"DENS", F_INT, 10, 100, 100, 0, "%"},
    {"CTX", F_INT, 1, 32, 32, 0, 0},                     /* (CONTEXT: too wide for a card) */
    {"LANES", F_ENUM, 0, DB_NLANES - 1, DB_L_BEAT, DB_LANES_NAMES, 0},
    {"BARS", F_INT, 1, 2, 2, 0, 0},
};
static int16_t db_show[DB_NK];  /* the values page_desc hands the cards (a pointer each): the selected track's */

static int db_graph(uint32_t g) { return g == GR_DB1 || g == GR_DB2; }
static int db_on(void) { return !ui.home && !ui.menu && db_graph(cur_page()->graph); }   /* a BRAIN page shown */

static int32_t db_get(const db_cfg_t *g, uint32_t k)
{
    switch (k) {
    case DB_K_STYLE: return g->style;
    case DB_K_TEMP: return g->temp;
    case DB_K_DENS: return g->dens;
    case DB_K_CTX: return g->ctx;
    case DB_K_LANES: return g->lanes;
    default: return g->bars;
    }
}
static void db_set(db_cfg_t *g, uint32_t k, int32_t v)
{
    uint8_t x = (uint8_t)clamp(v, DB_DESC[k % DB_NK].min, DB_DESC[k % DB_NK].max);
    switch (k) {
    case DB_K_STYLE: g->style = x; break;
    case DB_K_TEMP: g->temp = x; break;
    case DB_K_DENS: g->dens = x; break;
    case DB_K_CTX: g->ctx = x; break;
    case DB_K_LANES: g->lanes = x; break;
    default: g->bars = x; break;
    }
}
/* the setting KNOB slot edits on page pg (DB_NK: none) */
static uint32_t db_key(const page_t *pg, uint32_t slot)
{
    slot &= 3u;
    if (pg->graph == GR_DB1)
        return slot;
    return slot < 2u ? DB_K_LANES + slot : DB_NK;
}
/* params.c page_desc on a BRAIN page: KNOB slot's setting, its value in db_show */
static const param_desc_t *db_desc(const page_t *pg, uint32_t slot, int16_t **vp)
{
    uint32_t k = db_key(pg, slot);
    if (k >= DB_NK) {
        *vp = 0;
        return 0;
    }
    db_show[k] = (int16_t)db_get(&db_cfg[song.sel % NTRK], k);
    *vp = &db_show[k];
    return &DB_DESC[k];
}
static void db_knob(uint32_t slot, int32_t steps)    /* (ui_input.c edit_param) */
{
    uint32_t tr = song.sel % NTRK, k = db_key(cur_page(), slot);
    const param_desc_t *d;
    int32_t v, n;
    if (k >= DB_NK)
        return;
    d = &DB_DESC[k];
    v = db_get(&db_cfg[tr], k);
    n = param_turn(d, v, accel(EN_K1 + slot, steps, d->fmt == F_ENUM ? 0 : d->max - d->min));
    if (n == v)
        return;
    db_set(&db_cfg[tr], k, n);
    db_stale[tr] = 1;
}

/* the pattern into t's steps below LEN, N repeated; only the set's lanes, and from p->from on (VARY's first half stays).
 * Per step the lanes that end on first: a step that lost its last hit on the way would lose its CHANCE and RATCH
 * (step_clear). AC is the step's velocity, on a step of the set's hits alone and no notes (it is one for the whole step).
 * IRQs off for a step at a time: the sequencer never plays one half written, a hit recorded in the ISR is not lost (and
 * the off time stays that of one step, TIMER5 services USB audio) */
static void db_write(track_t *t, const db_pat_t *p, uint32_t mask)
{
    uint32_t len = (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP), i, l;
    for (i = p->from; i < len; i++) {
        uint32_t j = i % p->n;
        step_t *s = &t->step[i];
        uint32_t vel;
        fm1_irq_off();
        vel = j < p->from ? t->step[j].vel : p->ac[j] ? 112u : 88u;   /* (a repeat of the first half: as it is) */
        for (l = 0; l < NLANE; l++)
            if (((mask & p->hit[j]) >> l) & 1u) {
                grid_hit(t, i, l, 1);
                grid_acc(t, i, l, (p->acc[j] >> l) & 1u);
            }
        for (l = 0; l < NLANE; l++)
            if (((mask & ~(uint32_t)p->hit[j]) >> l) & 1u)
                grid_hit(t, i, l, 0);
        if (s->time == ST_NOTE && s->hit && !s->n && !(s->hit & ~mask))
            s->vel = (uint8_t)vel;
        fm1_irq_on();
    }
}

/* TB-3PO's LINK OCT+ (ui_tb3po.c tb3_oct): a new pattern on DRUM track t, its lanes as LANES says; no undo of its own
 * (the undo is the 303 track's, one track's); 0: no DrumBrain built */
static int db_groove(track_t *t, uint32_t entropy)
{
#if DBRAIN_ON
    uint32_t k = trk_index(t), len = (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP), r;
    uint64_t was;
    db_pat_t p;
    r = db_start(&db_cfg[k], 1, entropy);
    db_line(t, 0, &r, &p);
    was = tl_used(t, len);
    db_write(t, &p, DB_LANE_MASK[db_cfg[k].lanes % DB_NLANES]);
    tl_unlock(t, was, len);
    db_stale[k] = 0;
    return 1;
#else
    (void)t, (void)entropy;
    return 0;
#endif
}

/* OCT+ (oct bit 1): a new pattern, a new seed; OCT- (bit 0): a VARY */
static void db_oct(uint32_t oct)
{
    static const char HEX[] = "0123456789ABCDEF";
    track_t *t = TSEL;
    uint32_t k = trk_index(t), fresh = (oct & 2u) != 0u, len = (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP), r, i, seed0;
    uint8_t tom0;
    uint64_t was;
    db_pat_t p;
    char b[8];
    if (chain_busy()) {
        ui_message("STOP TO EDIT");
        return;
    }
    seed0 = db_cfg[k].seed;                            /* (the undo's) */
    tom0 = db_cfg[k].tom;
    r = db_start(&db_cfg[k], fresh, fm1_ms ^ (ui.frame << 7));
    db_line(t, !fresh, &r, &p);
    was = tl_used(t, len);
    tl_undo_take(t);
    undo.what |= UNDO_DB;
    undo.db_seed = seed0;
    undo.db_tom = tom0;
    undo.db_stale = db_stale[k];
    db_write(t, &p, DB_LANE_MASK[db_cfg[k].lanes % DB_NLANES]);
    tl_unlock(t, was, len);
    db_stale[k] = 0;
    sync_reload = 1;                                   /* (the editor reads the steps again) */
    ui.force = 1;
    if (!fresh) {
        ui_message("VARIED");
        return;
    }
    for (i = 0; i < 4u; i++)                           /* "NEW BEAT B5CD": the seed's low 16 bits */
        b[i] = HEX[(db_cfg[k].seed >> (12u - 4u * i)) & 15u];
    b[4] = 0;
    ui_say("NEW BEAT ", b);
}
