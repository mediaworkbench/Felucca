/* SPDX-License-Identifier: GPL-3.0-only
 * Part of Felucca: free software under the GNU General Public License, version 3 only (see LICENSE) */
/* ARP on a 303 track: TB-3PO's pages (tb3po.c), as X0X has them. GENERATE: DENS ACCENT SLIDE OCTS; SCALE: ROOT SCALE
 * OCTAVE MUTATE (every 1..16 passes of the line while playing, OFF); 1.6.2 (FELUCCA_AICD): MODEL: MODE (TB-3PO, AICD).
 * In AICD GENERATE is DENS ACCENT SLIDE TEMP (as TB-3PO's, 0..100 %, on Aicd's model; and its temperature) and SCALE
 * offers CHRO first (no bias), each a setting of its own. ARP taps through them as between ARP and ARP 2 (ui.c
 * page_visible: these on a 303 track, the arp's on any other). The knobs set up the next line only: the line
 * plays as it is, hand edits too, until OCT+ (a new line, a new seed) or OCT- (a MUTATE, ~25 % of the steps); the
 * footer's NEW LINE gets a star while they no longer match it (X0X's). Each is an undo (SAVE held; several in a row
 * one, as sound loads while browsing); not in a song (its steps are a slot's). Included by ui.c */
static int32_t accel(uint32_t role, int32_t s, int32_t range);   /* (ui_input.c) */

enum { TB3_DENS, TB3_ACCT, TB3_SLID, TB3_OCTS, TB3_ROOT, TB3_SCL, TB3_BASE, TB3_MUT,
       TB3_MODE, TB3_TEMP, TB3_ADENS, TB3_AACC, TB3_ASLD, TB3_ASCL, TB3_NK };
static const char *const N_TB3_MUT[] = {"OFF", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13",
                                        "14", "15", "16"};
static const char *const N_TB3_MODE[] = {"TB-3PO", "AICD"};
static const param_desc_t TB3_DESC[TB3_NK] = {   /* X0X's ranges and defaults (tb3po.c tb3_defaults); then AICD's */
    {"DENS", F_INT, 0, 100, 70, 0, "%"},
    {"ACCENT", F_INT, 0, 100, 40, 0, "%"},
    {"SLIDE", F_INT, 0, 100, 25, 0, "%"},
    {"OCTS", F_INT, 1, 3, 2, 0, 0},
    {"ROOT", F_NOTE, 0, 11, 9, 0, 0},
    {"SCALE", F_ENUM, 0, TB3_NSCALES - 1, 0, TB3_SCALE_NAMES, 0},
    {"OCTAVE", F_INT, 0, 4, 1, 0, 0},
    {"MUTATE", F_ENUM, 0, 16, 0, N_TB3_MUT, 0},
    {"MODE", F_ENUM, 0, AICD_ON, 0, N_TB3_MODE, 0},
    {"TEMP", F_INT, 10, 200, 100, 0, "%"},
    {"DENS", F_INT, 0, 100, AICD_DENS, 0, "%"},
    {"ACCENT", F_INT, 0, 100, AICD_ACC, 0, "%"},
    {"SLIDE", F_INT, 0, 100, AICD_SLD, 0, "%"},
    {"SCALE", F_ENUM, 0, TB3_NSCALES, 0, TB3_ASCALE_NAMES, 0},
};
_Static_assert(NELEM(N_TB3_MUT) == 17u, "MUTATE: OFF, 1..16");
static int16_t tb3_show[TB3_NK];  /* the values page_desc hands the cards (a pointer each): the selected track's */

static int tb3_graph(uint32_t g) { return g == GR_TB3GEN || g == GR_TB3SCL || g == GR_TB3MOD; }
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
    default: g->a_scale = x; break;
    }
}

/* the setting KNOB slot edits on page pg, in track tr's MODE (TB3_NK: none) */
static uint32_t tb3_key(const page_t *pg, uint32_t slot, uint32_t tr)
{
    static const uint8_t AICD_K[8] = {TB3_ADENS, TB3_AACC, TB3_ASLD, TB3_TEMP, TB3_ROOT, TB3_ASCL, TB3_BASE, TB3_MUT};
    uint32_t k = (pg->graph == GR_TB3SCL ? 4u : 0u) + (slot & 3u);
    if (pg->graph == GR_TB3MOD)
        return slot == 0u ? TB3_MODE : TB3_NK;
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
    if (n == v)
        return;
    tb3_set(&tb3_cfg[tr], k, n);
    if (k != TB3_MUT)                                  /* the line stays: the settings are the next OCT+ / OCT-'s */
        tb3_rt[tr].stale = 1;
}

/* OCT+ (oct bit 1): a new line, a new seed; OCT- (bit 0): a MUTATE (X0X: its stream from the seed ^ the time). The
 * line is worked out with the audio ISR on (AICD's: under a ms); IRQs off only to read and to write the steps (the
 * sequencer reads them, its auto MUTATE writes them: one in between, the line again from its steps) */
static void tb3_oct(uint32_t oct)
{
    static const char HEX[] = "0123456789ABCDEF";
    track_t *t = TSEL;
    uint32_t k = trk_index(t), r, r0, fresh = (oct & 2u) != 0u, i, n;
    tb3_step_t line[NSTEP], old[NSTEP];
    char b[16];
    if (chain_busy()) {
        ui_message("STOP TO EDIT");
        return;
    }
    load_begin(t, UNDO_PAT);
    if (fresh)
        tb3_cfg[k].seed = tb3_new_seed(fm1_ms ^ (ui.frame << 7));
    r0 = tb3_cfg[k].seed ^ fm1_ms;
    for (;;) {
        fm1_irq_off();
        tb3_read(t->step, old);
        n = tb3_auto_n;
        fm1_irq_on();
        r = r0;
        tb3_line(t, old, line, (int)fresh, &r);
        fm1_irq_off();
        if (n == tb3_auto_n)
            break;
        fm1_irq_on();
    }
    tb3_write(t->step, line, fresh ? 0 : old);
    fm1_irq_on();
    load_end(t);
    tb3_rt[k].stale = 0;
    sync_reload = 1;                                   /* (the editor reads the steps again) */
    ui.force = 1;
    if (!fresh) {
        ui_message("MUTATED");
        return;
    }
    for (i = 0; i < 4u; i++)                           /* "NEW LINE B5CD": the seed's low 16 bits, as X0X shows it */
        b[i] = HEX[(tb3_cfg[k].seed >> (12u - 4u * i)) & 15u];
    b[4] = 0;
    ui_say("NEW LINE ", b);
}
