/* SPDX-License-Identifier: GPL-3.0-only
 * Part of Felucca: free software under the GNU General Public License, version 3 only (see LICENSE) */
/* How loud a drum hit is, for the DRUM engine's kits of SLOOP (eng_drum.c DK_SYN ..): shared by tests/drum_level.c
 * (which measures, for tools/level_drumkits.py) and tests/dsyn_test.c (which holds every kit to it). Include after
 * hostsim.c.
 *
 * The hit: the part alone (track_render: the engine's output, before the mixer), a kit at its design (TUNE TONE DECY
 * SNAP 64, ACC 100, KICK PUNCH, DRV 0, the lanes' LEVELs 100 %; KIT STD as its preset has it, TONE 70), velocity 100,
 * DL_SEC seconds.
 * The measure (SLOOP's, tools/level_drumkits.py there): the loudest 100 ms, K-weighted (ITU-R BS.1770-4: the shelf
 * and the high-pass, made for FS), in dB re full scale - 0.691 (LUFS-like), and the peak.
 * The targets: a sound the grid's lanes play (KICK SNARE CLAP CHH OHH TOMLO RIM COWBELL) is as loud as that lane is on
 * Felucca's nine kits (STD 80 10 66 55 77 DEEP TIGHT PERC: their mean, in dB; drum_test.c holds each within 1.5 dB of
 * its lane's level); the other eight sit where SLOOP has them from those (its REL: KICK 2 as the kick, SNARE 2 1.5 dB
 * under the snare, HI TOM 0.5 and CONGA 2 under the tom, CLAVE 1 under the rim, CRASH 2 over and RIDE 1 under the
 * open hat, SHAKER 2 under the closed one). No kit is louder than another: SLOOP's softer JAZZ, VINTAGE and LATIN and
 * its kicks' 2.5 dB lift are not taken over.
 * The top: the level a hit holds for 1 ms in all (its DL_HOLD-th largest sample), in dB re the start of the engine's
 * knee at this velocity (DL_KNEE: soft_knee 24000 after x1.25, which ends 4.5 dB higher). Felucca's kits go into the
 * knee (the 66 snare 3.6 dB at the output, for a millisecond), and a sound of SLOOP's goes no further than the hardest
 * of them (the ceiling, dl_targets). Not the single highest sample: SLOOP's clicks and filters start with a spike of
 * a few samples, up to 9 dB over the rest of the sound (a closed hat), which the knee rounds off unheard; held to it,
 * those sounds would sit 5 .. 8 dB under their lane. What is left: a few of SLOOP's sounds are much shorter than
 * Felucca's (the 808's and 606's snares, a closed hat of 30 ms), so at the ceiling their loudest 100 ms still hold
 * less. Such a sound is as loud as it can be without more of the knee than Felucca's kits have (SLOOP's own levels
 * stop at -4 dBFS in the same way). So a sound is level when it is on its target, or at the ceiling under it. */
#include <math.h>

#define DL_SEC 2u
#define DL_N (FS * DL_SEC / CTL * CTL)
#define DL_VEL 100u
#define DL_NOWN 9u                                       /* Felucca's kits: STD and the eight model kits */
static const uint8_t DL_OWN[DL_NOWN] = {DK_STD, DK_80, DK_10, DK_66, DK_55, DK_77, DK_DEEP, DK_TIGHT, DK_PERC};
/* SLOOP's 16 sounds (tools/gen_drumkits.py LANES): the note that plays each as designed (drum_synth.c DS_MAP), but
 * TOMLO as the grid's TOM lane plays it (45: 3 semitones up) */
enum { DL_KICK, DL_SNARE, DL_CLAP, DL_CHH, DL_OHH, DL_TOMLO, DL_TOMHI, DL_CRASH, DL_RIDE, DL_SHAKER, DL_CONGA, DL_RIM,
       DL_COWBELL, DL_CLAVE, DL_KICK2, DL_SNARE2, DL_NSND };
static const char *const DL_NAME[DL_NSND] = {"KICK", "SNARE", "CLAP", "CHH", "OHH", "TOMLO", "TOMHI", "CRASH", "RIDE",
                                             "SHAKER", "CONGA", "RIM", "COWBELL", "CLAVE", "KICK2", "SNARE2"};
static const uint8_t DL_NOTE[DL_NSND] = {36, 38, 39, 42, 46, 45, 48, 49, 51, 70, 63, 37, 56, 75, 35, 40};
/* a sound's target: its lane's level on Felucca's kits (the grid's note of that lane), and dB from it */
static const struct { uint8_t lane; float rel; } DL_REL[DL_NSND] = {
    {DV_KICK, 0}, {DV_SNARE, 0}, {DV_CLAP, 0}, {DV_HATC, 0}, {DV_HATO, 0}, {DV_TOM, 0}, {DV_TOM, -0.5f}, {DV_HATO, 2},
    {DV_HATO, -1}, {DV_HATC, -2}, {DV_TOM, -2}, {DV_RIM, 0}, {DV_BELL, 0}, {DV_RIM, -1}, {DV_KICK, 0}, {DV_SNARE, -1.5f},
};
/* the knee's start at this velocity, at the part's output: 24000 x the voice's level (the velocity, VOICE_FS: dsp.c
 * voice_amp) x 2 (eng_drum.c drum_render) */
#define DL_KNEE (24000.0 * (DL_VEL * 258u) / 32768.0 * VOICE_FS / 32768.0 * 2.0)

static int32_t dl_buf[DL_N];

/* part 1 a DRUM track on kit (a stored KIT value) at its design, nothing sounding */
static void dl_kit(uint32_t kit)
{
    static const int16_t STD[8] = DRUM_KIT_E;
    uint32_t i;
    host_tracks_init();
    memset(trk[0].v, 0, sizeof trk[0].v);
    memset(eng_mem, 0, sizeof eng_mem);
    host_preset(&trk[0], ENGI_DRUM, 0);
    for (i = 0; i < 8u; i++)
        trk[0].p[P_E0 + i] = kit == DK_STD ? STD[i] : (int16_t)(i == 0 ? (int32_t)kit : i == 5u ? 100 : i < 5u ? 64 : 0);
}
/* one hit of note on kit into dl_buf, the part alone */
static void dl_hit(uint32_t kit, uint32_t note)
{
    uint32_t b;
    dl_kit(kit);
    trk_note_on(&trk[0], note, DL_VEL);
    for (b = 0; b < DL_N / CTL; b++)
        track_render(&trk[0], dl_buf + b * CTL, CTL);
}
/* a bar of 16 steps at 120 BPM on kit's 8 lanes into dl_buf (DL_N: the bar): the kick on 1 9 11, the snare on 5 13,
 * the clap with it on 13, closed hats on the odd steps, the open one on 16, a tom on 8, the rim on 12, the bell on 15 */
static void dl_beat(uint32_t kit)
{
    static const uint16_t LANES[16] = {0x09, 0, 0x08, 0, 0x0A, 0, 0x08, 0x20, 0x09, 0, 0x09, 0x40, 0x0E, 0, 0x88, 0x10};
    uint32_t b, l, step = DL_N / 16u / CTL;
    dl_kit(kit);
    for (b = 0; b < DL_N / CTL; b++) {
        if (b % step == 0u && b / step < 16u)
            for (l = 0; l < DV_NLANE; l++)
                if ((LANES[b / step] >> l) & 1u)
                    trk_note_on(&trk[0], DRUM_LANE_NOTE[l], DL_VEL);
        track_render(&trk[0], dl_buf + b * CTL, CTL);
    }
}

#define DL_HOLD (FS / 1000u)                             /* 1 ms of samples */
static double dl_top(void)                               /* dl_buf's level held for 1 ms, dB re DL_KNEE (above 0: into the knee) */
{
    int32_t top[DL_HOLD];                                /* the largest |samples|, the smallest of them first */
    uint32_t i, k;
    for (k = 0; k < DL_HOLD; k++)
        top[k] = 1;
    for (i = 0; i < DL_N; i++) {
        int32_t a = abs(dl_buf[i]);
        if (a <= top[0])
            continue;
        for (k = 0; k + 1u < DL_HOLD && top[k + 1u] < a; k++)
            top[k] = top[k + 1u];
        top[k] = a;
    }
    return 20.0 * log10(top[0] / DL_KNEE);
}

/* K-weighting for FS (BS.1770's two biquads, from their analogue prototypes) */
static void dl_kweight(double *b, double *a, int stage)
{
    const double PI = 3.14159265358979323846;
    if (stage == 0) {                                    /* the high shelf: +4 dB above ~1.7 kHz */
        double k = tan(PI * 1681.974450955533 / FS), q = 0.7071752369554196, vh = pow(10.0, 3.999843853973347 / 20.0),
               vb = pow(vh, 0.4996667741545416), a0 = 1.0 + k / q + k * k;
        b[0] = (vh + vb * k / q + k * k) / a0, b[1] = 2.0 * (k * k - vh) / a0, b[2] = (vh - vb * k / q + k * k) / a0;
        a[0] = 2.0 * (k * k - 1.0) / a0, a[1] = (1.0 - k / q + k * k) / a0;
    } else {                                             /* the high-pass at 38 Hz */
        double k = tan(PI * 38.13547087602444 / FS), q = 0.5003270373238773, a0 = 1.0 + k / q + k * k;
        b[0] = 1.0, b[1] = -2.0, b[2] = 1.0;
        a[0] = 2.0 * (k * k - 1.0) / a0, a[1] = (1.0 - k / q + k * k) / a0;
    }
}
static double dl_loud_win(uint32_t w)                    /* dl_buf's loudest w samples, K-weighted */
{
    static double sq[DL_N + 1];
    double b[2][3], a[2][2], z[2][2] = {{0, 0}, {0, 0}}, best = 0;
    uint32_t i, s;
    dl_kweight(b[0], a[0], 0);
    dl_kweight(b[1], a[1], 1);
    sq[0] = 0;
    for (i = 0; i < DL_N; i++) {
        double x = dl_buf[i] / 32768.0;
        for (s = 0; s < 2u; s++) {                       /* transposed direct form II */
            double y = b[s][0] * x + z[s][0];
            z[s][0] = b[s][1] * x - a[s][0] * y + z[s][1];
            z[s][1] = b[s][2] * x - a[s][1] * y;
            x = y;
        }
        sq[i + 1u] = sq[i] + x * x;
    }
    for (i = w; i <= DL_N; i++)
        best = sq[i] - sq[i - w] > best ? sq[i] - sq[i - w] : best;
    return 10.0 * log10(best / w + 1e-12) - 0.691;
}
static double dl_loud(void) { return dl_loud_win(FS / 10u); }   /* a hit: its loudest 100 ms */
static double dl_bar(void) { return dl_loud_win(DL_N); }        /* a bar (dl_beat): all of it */

/* the targets of SLOOP's 16 sounds (above); own[k][l], ownpk[k][l]: Felucca's kit k (DL_OWN) on lane l, its loudness
 * and top, for the table (0: not wanted). -> the ceiling: the highest top of Felucca's kits */
static double dl_targets(double tgt[DL_NSND], double own[DL_NOWN][DV_NLANE], double ownpk[DL_NOWN][DV_NLANE])
{
    double lane[DV_NLANE], ceil = -99;
    uint32_t k, l, s;
    for (l = 0; l < DV_NLANE; l++) {
        lane[l] = 0;
        for (k = 0; k < DL_NOWN; k++) {
            double v;
            dl_hit(DL_OWN[k], DRUM_LANE_NOTE[l]);
            v = dl_loud();
            lane[l] += v / DL_NOWN;
            if (own)
                own[k][l] = v;
            {
                double top = dl_top();
                ceil = top > ceil ? top : ceil;
                if (ownpk)
                    ownpk[k][l] = top;
            }
        }
    }
    for (s = 0; s < DL_NSND; s++)
        tgt[s] = lane[DL_REL[s].lane] + DL_REL[s].rel;
    return ceil;
}
