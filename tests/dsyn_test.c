/* SPDX-License-Identifier: GPL-3.0-only
 * Part of Felucca: free software under the GNU General Public License, version 3 only (see LICENSE) */
/* The DRUM engine's kits of SLOOP (KIT 808 .. JAZZ, eng_drum.c DK_SYN ..; drum_synth.c), on the real sources (with
 * the stubs of tests/ui_test.c). After SLOOP's tests/drumkit_test.c and dsyn_test.c.
 *   TABLE     KIT 12 .. are SLOOP's kits in tools/gen_drumkits.py PICK's order (stored values: the first 16 pinned
 *             here), the 12 before as they were; a preset each after PERC KIT (no FX, category DRUM)
 *   SOUNDS    every kit x its 16 sounds: heard, no DC, ends
 *   LEVELS    every sound on its target (tests/drum_loud.h: its lane's level on Felucca's own kits) or at the ceiling
 *             under it, none over the ceiling; a bar of the 8 lanes on every kit as loud as on Felucca's own
 *   KNOBS     at 64 the sound is SLOOP's (the kit's bytes, ds_on at velocity 110, DECAY 0); TUNE DECY TONE SNAP ACC
 *             each move what they say; every corner (the knobs at 0 and 127, DRV 127, velocity 127) stays under the
 *             knee's end and ends
 *   VELOCITY  once: without accent half the velocity is half the level; with it louder and brighter
 *   CHOKE     the closed and pedal hat choke the open one, the shaker (on the closed hat's lane) does not; across the
 *             two kinds of kit both ways
 *   LANES     the 16 sounds on their lanes, 8 lanes = 8 voices, the lanes' names; a lane plays what it was struck with
 *             while KIT moves to the other kind and back; the part's memory cleared under a ringing hit
 *   STORED    a project keeps the kit; firmware without these kits reads them as PERC; the KIT knob walks them
 * argv[1]: a directory for kits.wav (KIT STD, then each of SLOOP's: two bars of the same beat).
 * With -DFELUCCA_DKITS=0: none of it, the DRUM engine as it was (12 kits, 4 presets).
 * Built and run by tests/run_tests.sh. */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"
#include "drum_loud.h"

static uint32_t on_voices(const track_t *t)
{
    uint32_t i, n = 0;
    for (i = 0; i < NVOICE; i++)
        n += t->v[i].active;
    return n;
}
/* nb blocks of part 1 alone -> the largest |sample| */
static int32_t part_blocks(uint32_t nb)
{
    int32_t o[CTL], pk = 0;
    uint32_t b, i;
    for (b = 0; b < nb; b++) {
        track_render(&trk[0], o, CTL);
        for (i = 0; i < CTL; i++)
            pk = abs(o[i]) > pk ? abs(o[i]) : pk;
    }
    return pk;
}
/* .. until nothing sounds, sec seconds at most -> 1 when it ended; *pk the largest |sample| */
static int part_ends(uint32_t sec, int32_t *pk)
{
    uint32_t b;
    *pk = 0;
    for (b = 0; b < sec * FS / CTL; b++) {
        int32_t v = part_blocks(1);
        *pk = v > *pk ? v : *pk;
        if (!on_voices(&trk[0]))
            return 1;
    }
    return 0;
}

#if FELUCCA_DKITS
static const char *const PICK16[16] = {"808", "909", "606", "VINTAGE", "TRAP", "BOOMBAP", "LO-FI", "HOUSE", "TECHNO",
                                       "ELECTRO", "JUNGLE", "LATIN", "CHIP", "ARCADE", "INDUSTR", "JAZZ"};
/* the note that plays each of SLOOP's 16 sounds as designed (drum_synth.c DS_MAP: no semitones) and its lane */
static const uint8_t SND_NOTE[DS_LANES] = {36, 38, 39, 42, 46, 43, 48, 49, 51, 70, 63, 37, 56, 75, 35, 40};
static const uint8_t SND_LANE[DS_LANES] = {DV_KICK, DV_SNARE, DV_CLAP, DV_HATC, DV_HATO, DV_TOM, DV_TOM, DV_BELL, DV_BELL,
                                           DV_HATC, DV_TOM, DV_RIM, DV_BELL, DV_RIM, DV_KICK, DV_SNARE};

/* ------------------------------------------------------------- TABLE --- */
static int table(void)
{
    static const char *const OWN[DK_SYN] = {"STD", "66", "10", "77", "80", "10", "66", "55", "77", "DEEP", "TIGHT", "PERC"};
    static const char *const OWNP[4] = {"DRUM KIT", "DEEP KIT", "TIGHT KIT", "PERC KIT"};
    const param_desc_t *d = &ENG_DRUM.edit[0];
    int bad = 0, ok;
    uint32_t k, i;
    ok = DK_SYN == 12 && DK_COUNT == 12 + (int32_t)DS_NKITS && DS_NKITS >= 16u && d->max == DK_COUNT - 1 && d->min == 0 &&
         d->def == DK_STD && d->names == N_DRUM_KIT;
    for (k = 0; k < (uint32_t)DK_SYN; k++)
        ok &= str_eq(d->names[k], OWN[k]) && drum_kit_plays((int32_t)k) == (k >= 1u && k <= 3u ? DK_PLAYS[k] : k);
    for (k = 0; k < DS_NKITS; k++)
        ok &= str_eq(d->names[DK_SYN + k], DS_KITS[k].name) && drum_kit_plays(DK_SYN + (int32_t)k) == DK_SYN + k &&
              enum_orig(d, DK_SYN + (int32_t)k) == DK_SYN + (int32_t)k && (k >= 16u || str_eq(DS_KITS[k].name, PICK16[k]));
    bad += check("TABLE: KIT 0..11 as they were, 12 .. SLOOP's kits in PICK's order (808 .. JAZZ at 12..27), themselves", ok);

    ok = ENG_DRUM.npresets == 4u + DS_NKITS;
    for (k = 0; k < 4u && ok; k++)
        ok &= str_eq(ENG_DRUM.presets[k].name, OWNP[k]);
    for (k = 0; k < DS_NKITS && ok; k++) {
        const preset_t *p = &ENG_DRUM.presets[4u + k];
        char name[16];
        snprintf(name, sizeof name, "%s KIT", DS_KITS[k].name);
        ok &= str_eq(p->name, name) && p->e[0] == DK_SYN + (int32_t)k && p->e[5] == 100 && !p->e[6] && !p->e[7] && p->pat == 12 &&
              preset_cat(ENGI_DRUM, 4u + k) == CAT_DRUM;
        for (i = 1; i < 5u; i++)
            ok &= p->e[i] == 64;
        ui_power_on();
        host_preset(&trk[0], ENGI_DRUM, 4u + k);
        for (i = 0; i < 4u; i++)
            ok &= trk[0].p[P_DIST + i] == 0;
    }
    bad += check("  DRUM's presets: the four as before, then a kit each (NAME KIT, the knobs at 64, no FX, category DRUM)", ok);
    return bad;
}

/* ------------------------------------------------------------ SOUNDS --- */
static int sounds(void)
{
    int bad = 0, ok = 1;
    uint32_t k, s, quiet = 0, dc = 0, hang = 0;
    for (k = 0; k < DS_NKITS; k++)
        for (s = 0; s < DS_LANES; s++) {
            double mean = 0, top;
            int32_t pk;
            uint32_t i;
            dl_hit(DK_SYN + k, SND_NOTE[s]);
            top = dl_top();
            for (i = 0; i < DL_N; i++)
                mean += dl_buf[i];
            mean = fabs(mean / DL_N) / (DL_KNEE * pow(10.0, top / 20.0));
            if (top < -20.0)
                quiet++, printf("dsyn: %s %s: quiet, %.1f dB re the knee\n", DS_KITS[k].name, DL_NAME[s], top);
            if (mean > 0.005)
                dc++, printf("dsyn: %s %s: DC %.2f %% of its level\n", DS_KITS[k].name, DL_NAME[s], mean * 100);
            if (!part_ends(8, &pk))                     /* (after dl_hit's 2 s) */
                hang++, printf("dsyn: %s %s: still sounding after 10 s\n", DS_KITS[k].name, DL_NAME[s]);
        }
    ok = !quiet && !dc && !hang;
    bad += check("SOUNDS: every kit x its 16 sounds is heard, has no DC and ends", ok);
    return bad;
}

/* ------------------------------------------------------------ LEVELS --- */
/* (tests/drum_loud.h) on target: within DL_ON dB; at the ceiling: its top within DL_AT under it, then under its target
 * by DL_UNDER at most (the shortest sounds: the 808's, 606's and the rhythm boxes' snares); none over the ceiling. A
 * bar on a kit of SLOOP's: within DL_BAR of the mean of Felucca's own (which lie within 0.8 dB of it) */
#define DL_ON 1.0
#define DL_AT 0.5
#define DL_UNDER 3.5
#define DL_BAR 1.5
static int levels(void)
{
    static const char *const OWN[DL_NOWN] = {"STD", "80", "10", "66", "55", "77", "DEEP", "TIGHT", "PERC"};
    double tgt[DL_NSND], own[DL_NOWN][DV_NLANE], ceil = dl_targets(tgt, own, 0), worst = 0, bar = 0, bw = 0, ospread = 0;
    int bad = 0;
    uint32_t k, s, l, off = 0, at = 0, over = 0, boff = 0;
    for (l = 0; l < DV_NLANE; l++) {                    /* (for reading: how far Felucca's own kits lie apart) */
        double lo = 0, hi = -99;
        for (k = 0; k < DL_NOWN; k++)
            lo = own[k][l] < lo ? own[k][l] : lo, hi = own[k][l] > hi ? own[k][l] : hi;
        ospread = hi - lo > ospread ? hi - lo : ospread;
    }
    for (k = 0; k < DS_NKITS; k++)
        for (s = 0; s < DL_NSND; s++) {
            double lo, top, d;
            dl_hit(DK_SYN + k, DL_NOTE[s]);
            lo = dl_loud(), top = dl_top(), d = lo - tgt[s];
            if (top > ceil + 0.3)
                over++, printf("dsyn: %s %s: %.2f dB over the ceiling\n", DS_KITS[k].name, DL_NAME[s], top - ceil);
            if (fabs(d) <= DL_ON)
                continue;
            if (d < 0 && d >= -DL_UNDER && top >= ceil - DL_AT) {
                at++;
                worst = d < worst ? d : worst;
                continue;
            }
            off++;
            printf("dsyn: %s %s: %+.2f dB from its target (%.2f), its top %.2f (the ceiling %.2f): run tools/level_drumkits.py\n",
                   DS_KITS[k].name, DL_NAME[s], d, tgt[s], top, ceil);
        }
    printf("dsyn: levels: %u sounds, %u on their target (+-%.1f dB), %u at the ceiling (%+.2f dB re the knee) under it, %.1f dB"
           " at most; Felucca's own kits lie up to %.1f dB apart on a lane\n",
           DS_NKITS * DL_NSND, DS_NKITS * DL_NSND - at - off, DL_ON, at, ceil, -worst, ospread);
    bad += check("LEVELS: every sound of SLOOP's kits on its lane's level on Felucca's kits, or at the ceiling under it", !off && !over);

    for (k = 0; k < DL_NOWN; k++) {
        dl_beat(DL_OWN[k]);
        own[k][0] = dl_bar();
        bar += own[k][0] / DL_NOWN;
    }
    printf("dsyn: a bar of the 8 lanes, dB from the mean of Felucca's kits (%.1f):", bar);
    for (k = 0; k < DL_NOWN; k++)
        printf(" %s %+.1f", OWN[k], own[k][0] - bar);
    printf(" |");
    for (k = 0; k < DS_NKITS; k++) {
        double v;
        dl_beat(DK_SYN + k);
        v = dl_bar() - bar;
        printf(" %s %+.1f", DS_KITS[k].name, v);
        bw = fabs(v) > bw ? fabs(v) : bw;
        boff += fabs(v) > DL_BAR;
    }
    printf("\n");
    bad += check("  a bar of the 8 lanes on each of SLOOP's kits: as loud as on Felucca's (within 1.5 dB of their mean)", !boff);
    return bad;
}

/* ------------------------------------------------------------- KNOBS --- */
/* part 1 on SLOOP's kit k at its design, nothing sounding; then one hit */
static drum_lane_t *hit(uint32_t k, uint32_t note, uint32_t vel)
{
    trk_note_on(&trk[0], note, vel);
    return &drum_kit_of(&trk[0])[drum_lane(note)];
}
/* blocks until the part is silent (16 s at most) */
static uint32_t ring(void)
{
    uint32_t b;
    for (b = 0; b < 16u * FS / CTL && on_voices(&trk[0]); b++)
        part_blocks(1);
    return b;
}
static int knobs(void)
{
    int bad = 0, ok = 1;
    uint32_t k, s, c, over = 0, hang = 0;
    int16_t *p = trk[0].p;
    for (k = 0; k < DS_NKITS; k++)                      /* at 64: SLOOP's sound, as its ds_on at velocity 110, DECAY 0 */
        for (s = 0; s < DS_LANES; s++) {
            const drum_lane_t *L;
            dsv_t ref;
            dl_kit(DK_SYN + k);
            L = hit(k, SND_NOTE[s], DL_VEL);
            ds_on(&ref, &L->snd, DS_KITS[k].crush, 0, 110, 0, 4u << 8, 0, SND_NOTE[s] * 2654435761u ^ 1u * 0x9E3779B1u);
            ref.gain = 32767;
            ref.lg = (ref.lg * (16384 + 78 * 75)) >> 14;
            ok &= L->syn == 1 && L->ds.d == &L->snd && !memcmp(&L->snd, &DS_KITS[k].s[s], sizeof L->snd) &&
                  !memcmp(&L->ds, &ref, sizeof ref);
        }
    bad += check("KNOBS: at 64 a hit is SLOOP's sound (the kit's bytes; its ds_on at velocity 110, TUNE 0, DECAY 0)", ok);

    dl_kit(DK_SYN);                                     /* 808 */
    {
        const drum_lane_t *L;
        dsv_t ref;
        int32_t base, cut;
        uint32_t ka, r0, r1, r2;
        L = hit(0, 36, DL_VEL);
        base = L->ds.base16, ka = L->ds.ka;
        r1 = ring();
        p[P_E1] = 64 + 16;                              /* TUNE: 3 semitones a 16 */
        L = hit(0, 36, DL_VEL);
        ok = L->ds.base16 == base + 48;
        trk_all_off(&trk[0]), ring();
        L = hit(0, 42, DL_VEL);                         /* .. a closed hat (no tone): its filter too */
        cut = L->ds.cut;
        trk_all_off(&trk[0]), ring();
        p[P_E1] = 64;
        L = hit(0, 42, DL_VEL);
        ok &= cut == L->ds.cut + 48 * 1197 / 64;
        ring();
        p[P_E3] = 0;                                    /* DECY: SLOOP's DECAY -4 .. +3 */
        L = hit(0, 36, DL_VEL);
        ok &= L->ds.ka == ds_k32((uint32_t)(DS_KITS[0].s[0].decay - 26)) && L->ds.ka < ka;
        r0 = ring();
        p[P_E3] = 127;
        L = hit(0, 36, DL_VEL);
        ok &= L->ds.ka == ds_k32((uint32_t)(DS_KITS[0].s[0].decay + 21)) && L->ds.ka > ka;
        r2 = ring();
        ok &= r0 * 2u < r1 && r1 * 2u < r2;
        p[P_E3] = 64;
        L = hit(0, 38, DL_VEL);                         /* TONE: the cutoff, 84 / 256 of a step a unit */
        cut = L->ds.cut;
        ring();
        p[P_E2] = 127;
        L = hit(0, 38, DL_VEL);
        ok &= L->ds.cut == cut + 63 * 84;
        ring();
        p[P_E2] = 0;
        L = hit(0, 38, DL_VEL);
        ok &= L->ds.cut == cut - 64 * 84;
        ring();
        p[P_E2] = 64;
        p[P_E4] = 0;                                    /* SNAP: the click, the noise under a tone; noise alone: its high-pass */
        L = hit(0, 38, DL_VEL);
        ok &= !L->snd.click && !L->snd.nlev && L->snd.tlev == DS_KITS[0].s[1].tlev;
        ring();
        L = hit(0, 42, DL_VEL);
        ok &= L->snd.nlev == DS_KITS[0].s[3].nlev && L->snd.hpf == DS_KITS[0].s[3].hpf - 16;
        ring();
        p[P_E4] = 127;
        L = hit(0, 38, DL_VEL);
        ok &= L->snd.click == clamp(DS_KITS[0].s[1].click * 127 / 64, 0, 127) && L->snd.nlev == clamp(DS_KITS[0].s[1].nlev * 127 / 64, 0, 127);
        ring();
        p[P_E4] = 64;
        p[P_E5] = 0;                                    /* ACC 0: no lift, a ghost note's brightness */
        L = hit(0, 38, DL_VEL);
        ds_on(&ref, &L->snd, 0, 0, 110, 0, 4u << 8, 0, 1);
        ok &= L->ds.lg == ref.lg && L->ds.cut == cut - (110 - 42) * 40 && L->ds.gain == 32767;
        ring();
        p[P_E5] = 127;                                  /* ACC 127 at full velocity: +4 dB (the accent 126), as bright as it gets */
        L = hit(0, 38, 127);
        ok &= L->ds.lg == (ref.lg * (16384 + 126 * 75)) >> 14 && L->ds.cut == cut + (127 - 110) * 40 && L->ds.gain == 32767;   /* (127 x 127 x 4 >> 9) */
        ring();
        p[P_E5] = 100;
    }
    bad += check("  TUNE (3 st a 16; a hat's filter too), DECY (SLOOP's -4 .. +3), TONE (the cutoff), SNAP, ACC move it", ok);

    for (k = 0; k < DS_NKITS; k++)                      /* the corners */
        for (s = 0; s < DS_LANES; s++)
            for (c = 0; c < 2u; c++) {
                int32_t pk;
                uint32_t i;
                dl_kit(DK_SYN + k);
                for (i = 1; i < 5u; i++)
                    p[P_E0 + i] = c ? 127 : 0;
                p[P_E5] = 127;
                p[P_E7] = 127;
                hit(k, SND_NOTE[s], 127);
                if (!part_ends(16, &pk))
                    hang++, printf("dsyn: %s %s, the knobs at %u: still sounding after 16 s\n", DS_KITS[k].name, DL_NAME[s], c ? 127u : 0u);
                if (pk > 2 * (24000 + 16384) * VOICE_FS / 32768)   /* the knee's end at full velocity (drum_render) */
                    over++, printf("dsyn: %s %s, the knobs at %u: a peak of %d\n", DS_KITS[k].name, DL_NAME[s], c ? 127u : 0u, pk);
            }
    bad += check("  every corner (TUNE TONE DECY SNAP 0 / 127, ACC and DRV 127, velocity 127) under the knee's end, and ends", !over && !hang);
    return bad;
}

/* ---------------------------------------------------------- VELOCITY --- */
static double hit_sum(uint32_t note, uint32_t vel)
{
    double sum = 0;
    uint32_t b, i;
    trk_note_on(&trk[0], note, vel);
    for (b = 0; b < FS / CTL; b++) {
        int32_t o[CTL];
        track_render(&trk[0], o, CTL);
        for (i = 0; i < CTL; i++)
            sum += abs(o[i]);
    }
    return sum;
}
static int velocity(void)
{
    int bad = 0, ok;
    double a, b;
    dl_kit(DK_SYN);
    trk[0].p[P_E5] = 0;                                 /* no accent: the same sound, the voice's velocity alone */
    a = hit_sum(36, 100);
    dl_kit(DK_SYN);
    trk[0].p[P_E5] = 0;
    b = hit_sum(36, 50);
    ok = fabs(a / b - 2.0) < 0.02;
    printf("dsyn: velocity 100 / 50: ACC 0 x%.3f", a / b);
    dl_kit(DK_SYN);
    a = hit_sum(38, 100);
    dl_kit(DK_SYN);
    b = hit_sum(38, 50);
    printf(", ACC 100 x%.3f\n", a / b);
    ok &= a / b > 2.15 && a / b < 3.0;                   /* (ACC 100: the accent 78 against 39, x1.15, and a brighter filter) */
    bad += check("VELOCITY: once (ACC 0: half the velocity, half the level); ACC lifts and brightens the harder hit", ok);
    return bad;
}

/* ------------------------------------------------------------- CHOKE --- */
static int choke(void)
{
    static const uint8_t BY[3] = {42, 44, 70};          /* closed, pedal, shaker */
    int bad = 0, ok = 1;
    uint32_t i;
    for (i = 0; i < 3u; i++) {
        const drum_lane_t *K;
        dl_kit(DK_SYN);
        K = drum_kit_of(&trk[0]);
        trk_note_on(&trk[0], 46, 100);
        part_blocks(20);
        ok &= K[DV_HATO].syn && ds_alive(&K[DV_HATO].ds);
        trk_note_on(&trk[0], BY[i], 100);
        part_blocks(14);                                /* 10 ms */
        ok &= (i == 2u) == (ds_alive(&K[DV_HATO].ds) != 0) && K[DV_HATC].syn;
    }
    bad += check("CHOKE: the closed hat (42) and the pedal (44) end the open one in 10 ms, the shaker (70, their lane) does not", ok);
    {
        const drum_lane_t *K;
        dl_kit(DK_STD);                                 /* a model kit's open hat, a closed hat of SLOOP's */
        K = drum_kit_of(&trk[0]);
        trk_note_on(&trk[0], 46, 100);
        part_blocks(20);
        ok = !K[DV_HATO].syn && K[DV_HATO].v.live && !K[DV_HATO].v.choke;
        trk[0].p[P_E0] = DK_SYN;
        trk_note_on(&trk[0], 42, 100);
        ok &= K[DV_HATO].v.choke && !K[DV_HATO].syn && K[DV_HATC].syn;
        dl_kit(DK_SYN);                                 /* .. and the other way */
        K = drum_kit_of(&trk[0]);
        trk_note_on(&trk[0], 46, 100);
        part_blocks(20);
        trk[0].p[P_E0] = DK_80;
        trk_note_on(&trk[0], 42, 100);
        part_blocks(14);
        ok &= K[DV_HATO].syn && !ds_alive(&K[DV_HATO].ds) && !K[DV_HATC].syn && K[DV_HATC].role != DRUM_ROLE_SYN;
    }
    bad += check("  .. across the kinds of kit: SLOOP's closed hat chokes a model kit's open one, and the other way", ok);
    return bad;
}

/* ------------------------------------------------------------- LANES --- */
static int lanes(void)
{
    static const char *const N[NLANE] = {"KICK", "SNARE", "CLAP", "HATCL", "HATOP", "TOM", "RIM", "BELL"};
    static const uint8_t GRID[NLANE] = {0, 1, 2, 3, 4, 5, 11, 12};   /* the grid's lanes: SLOOP's KICK SNARE CLAP CHH OHH TOMLO RIM COWBELL */
    int bad = 0, ok = 1;
    uint32_t s, l, k;
    int32_t semi, pk;
    drum_lane_t *K;
    dl_kit(DK_SYN + 1u);
    K = drum_kit_of(&trk[0]);
    for (s = 0; s < DS_LANES; s++)
        ok &= drum_lane(SND_NOTE[s]) == SND_LANE[s] && ds_lane(SND_NOTE[s], &semi) == s && !semi;
    for (l = 0; l < NLANE; l++)
        ok &= ds_lane(DRUM_LANE_NOTE[l], &semi) == GRID[l] && semi == (l == DV_TOM ? 3 : 0) &&
              str_eq(drum_lane_name(&trk[0], l), N[l]);
    ok &= !drum_swaps(&trk[0]);
    bad += check("LANES: SLOOP's 16 sounds on the lanes of their kind, the grid's 8 its KICK .. COWBELL, the lanes' names", ok);

    for (k = 0; k < DS_NKITS; k++) {
        uint32_t live = 0;
        dl_kit(DK_SYN + k);
        K = drum_kit_of(&trk[0]);
        for (l = 0; l < NLANE; l++)
            trk_note_on(&trk[0], DRUM_LANE_NOTE[l], 110);
        ok &= part_blocks(2) > 1000;
        for (l = 0; l < NLANE; l++)
            live += K[l].syn && ds_alive(&K[l].ds) && K[l].owner;
        ok &= live == 8u && on_voices(&trk[0]) == 8u;
        ok &= part_ends(12, &pk) && !on_voices(&trk[0]);
    }
    bad += check("  the 8 lanes of every kit ring together (8 voices) and end", ok);

    dl_kit(DK_SYN);                                     /* SLOOP's kick rings, KIT moves to a model kit */
    K = drum_kit_of(&trk[0]);
    trk_note_on(&trk[0], 36, 100);
    part_blocks(4);
    trk[0].p[P_E0] = DK_80;
    ok = part_blocks(4) > 1000 && K[DV_KICK].syn && K[DV_KICK].role == DRUM_ROLE_SYN;
    trk_note_on(&trk[0], 36, 100);                      /* .. the next hit is the model kit's, from rest */
    ok &= !K[DV_KICK].syn && K[DV_KICK].role == DV_KTYPE(1u, DV_KICK);
    ok &= part_blocks(4) > 1000 && K[DV_KICK].v.live && on_voices(&trk[0]) == 1u;
    trk[0].p[P_E0] = DK_SYN + 3;                        /* .. and back: it rings on, the next hit is SLOOP's */
    ok &= part_blocks(4) > 1000 && !K[DV_KICK].syn && K[DV_KICK].v.live;
    trk_note_on(&trk[0], 36, 100);
    ok &= K[DV_KICK].syn && part_blocks(4) > 1000 && on_voices(&trk[0]) == 1u;
    ok &= part_ends(12, &pk);
    bad += check("  a lane plays what it was struck with while KIT moves to the other kind of kit, and back", ok);

    dl_kit(DK_SYN);                                     /* the part's memory cleared under a ringing hit */
    for (l = 0; l < NLANE; l++)
        trk_note_on(&trk[0], DRUM_LANE_NOTE[l], 100);
    part_blocks(4);
    memset(eng_mem, 0, sizeof eng_mem);
    part_blocks(2);
    ok = !on_voices(&trk[0]);
    trk_note_on(&trk[0], 36, 100);
    ok &= part_blocks(4) > 1000;
    bad += check("  the part's memory cleared under ringing hits: they end, the next hit plays", ok);
    return bad;
}

/* ------------------------------------------------------------ STORED --- */
static int stored(void)
{
    const param_desc_t *d = &ENG_DRUM.edit[0];
    param_desc_t old = *d;
    track_t *t;
    int bad = 0, ok;
    int32_t v, k;
    ui_power_on();
    t = &trk[0];
    host_preset(t, ENGI_DRUM, 4u + 5u);                 /* BOOMBAP KIT */
    t->engine = t->eng_req;
    song.playing = 0;
    ok = t->p[P_E0] == DK_SYN + 5 && project_save_as(2, "DKITS") == 0;
    t->p[P_E0] = DK_STD;
    project_load(2);
    ok &= t->p[P_E0] == DK_SYN + 5;
    old.max = DK_PERC;                                  /* (firmware without these kits, FELUCCA_DKITS=0) */
    for (k = DK_SYN; k < DK_COUNT; k++)
        ok &= param_fit(&old, k) == DK_PERC && param_fit(d, k) == k;
    ok &= param_fit(d, 999) == DK_COUNT - 1;
    bad += check("STORED: a project keeps the kit; firmware without these kits reads them as PERC", ok);

    v = DK_PERC;
    ok = 1;
    for (k = DK_SYN; k < DK_COUNT; k++) {               /* the knob: PERC, then each, and back */
        v = param_turn(d, v, 1);
        ok &= v == k;
    }
    ok &= param_turn(d, v, 1) == DK_COUNT - 1;
    for (k = DK_COUNT - 2; k >= DK_PERC; k--) {
        v = param_turn(d, v, -1);
        ok &= v == k;
    }
    bad += check("  the KIT knob walks PERC, 808 .. JAZZ and back", ok);
    return bad;
}

/* -------------------------------------------------------------- DEMO --- */
/* kits.wav: KIT STD, then each of SLOOP's: two bars of a beat on the 8 lanes, the mix as the device makes it */
static void demo(const char *dir)
{
    static const uint16_t LANES[16] = {0x09, 0, 0x08, 0, 0x0A, 0, 0x08, 0x20, 0x09, 0, 0x09, 0x40, 0x0E, 0, 0x88, 0x10};
    uint32_t step = FS / 8u / CTL, bars = 2u, kit, b, l, i, total = (DS_NKITS + 1u) * bars * 16u * step * CTL;
    char path[512];
    FILE *f;
    snprintf(path, sizeof path, "%s/kits.wav", dir);
    f = fopen(path, "wb");
    if (!f)
        return;
    wav_hdr(f, total);
    for (kit = 0; kit <= DS_NKITS; kit++) {
        dl_kit(kit ? DK_SYN + kit - 1u : DK_STD);
        for (b = 0; b < bars * 16u * step; b++) {
            int32_t o[2 * CTL];
            if (b % step == 0u)
                for (l = 0; l < DV_NLANE; l++)
                    if ((LANES[b / step % 16u] >> l) & 1u)
                        trk_note_on(&trk[0], DRUM_LANE_NOTE[l], b / step % 4u ? 90 : 115);
            mix_block(o, CTL);
            for (i = 0; i < CTL; i++)
                wav_put(f, o[2u * i], o[2u * i + 1u]);
        }
    }
    fclose(f);
    printf("dsyn: %s: KIT STD, then %u kits of SLOOP's, two bars each\n", path, DS_NKITS);
}

int main(int argc, char **argv)
{
    int bad = table() + sounds() + levels() + knobs() + velocity() + choke() + lanes() + stored();
    if (argc > 1)
        demo(argv[1]);
    printf("%s\n", bad ? "DRUM KITS (SLOOP) TEST FAILED" : "DRUM kits of SLOOP: tests passed");
    return bad != 0;
}
#else
/* FELUCCA_DKITS=0: the DRUM engine as it was */
int main(void)
{
    const param_desc_t *d = &ENG_DRUM.edit[0];
    int bad = 0, ok;
    int32_t pk;
    ok = DK_SYN == 12 && DK_COUNT == 12 && d->max == DK_PERC && NELEM(N_DRUM_KIT) == 12u && ENG_DRUM.npresets == 4u &&
         str_eq(ENG_DRUM.presets[3].name, "PERC KIT") && param_fit(d, 12) == DK_PERC && param_fit(d, 27) == DK_PERC &&
         preset_cat(ENGI_DRUM, 3) == CAT_DRUM;
    dl_kit(DK_PERC);
    trk[0].p[P_E0] = 20;                                /* (a value of a build with the kits, set past param_fit: PERC) */
    trk_note_on(&trk[0], 36, 100);
    ok &= part_blocks(4) > 1000 && drum_kit_of(&trk[0])[DV_KICK].role == DV_KTYPE(8u, DV_KICK) && part_ends(8, &pk);
    bad += check("FELUCCA_DKITS=0: 12 kits, 4 presets; a KIT of a build with SLOOP's kits is PERC", ok);
    printf("%s\n", bad ? "DRUM KITS (SLOOP) TEST FAILED" : "DRUM kits of SLOOP, a build without them: tests passed");
    return bad != 0;
}
#endif
