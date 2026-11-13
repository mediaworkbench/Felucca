/* SPDX-License-Identifier: GPL-3.0-only
 * Part of Felucca: free software under the GNU General Public License, version 3 only (see LICENSE) */
/* Measures the DRUM engine's kits of SLOOP (eng_drum.c DK_SYN ..) for tools/level_drumkits.py, which sets their
 * levels (tools/drumkit_levels.json): every sound of every kit, one hit, against its target (tests/drum_loud.h: what,
 * how, and where the targets come from). After SLOOP's tests/drum_level.c, which measured in its own mixer.
 *
 *   cc -O2 -w <the hardware layer's -I, as tests/run_tests.sh> -Ibuild/gen -Ifirmware/src -o build/host/drum_level \
 *       tests/drum_level.c -lm
 *   build/host/drum_level > build/host/drum_level.txt && python3 tools/level_drumkits.py build/host/drum_level.txt
 *   (then python3 tools/gen_drumkits.py build/gen/felucca_drumkits.h, or ./build.sh, and once more: the knee and a
 *   sound's drive are not linear; tests/dsyn_test.c says when it is done)
 *
 * Output: "O kit lane loudness top" for Felucca's own kits (for reading), "T sound target", "C ceiling" (the top a
 * sound may reach) and for each of SLOOP's "K kit sound loudness top target" (dB; a top: the level held for 1 ms, re
 * the knee's start, drum_loud.h dl_top). */
#define main hostsim_main
#include "hostsim.c"
#undef main
#include "drum_loud.h"

int main(void)
{
    static const char *const OWN[DL_NOWN] = {"STD", "80", "10", "66", "55", "77", "DEEP", "TIGHT", "PERC"};
    static const char *const LANE[DV_NLANE] = {"KICK", "SNARE", "CLAP", "HATCL", "HATOP", "TOM", "RIM", "BELL"};
    double tgt[DL_NSND], own[DL_NOWN][DV_NLANE], ownpk[DL_NOWN][DV_NLANE], ceil;
    uint32_t k, l, s;
    ceil = dl_targets(tgt, own, ownpk);
    for (k = 0; k < DL_NOWN; k++)
        for (l = 0; l < DV_NLANE; l++)
            printf("O %s %s %.2f %.2f\n", OWN[k], LANE[l], own[k][l], ownpk[k][l]);
    for (s = 0; s < DL_NSND; s++)
        printf("T %s %.2f\n", DL_NAME[s], tgt[s]);
    printf("C %.2f\n", ceil);
#if FELUCCA_DKITS
    for (k = 0; k < DS_NKITS; k++)
        for (s = 0; s < DL_NSND; s++) {
            double lo;
            dl_hit(DK_SYN + k, DL_NOTE[s]);
            lo = dl_loud();
            printf("K %s %s %.2f %.2f %.2f\n", DS_KITS[k].name, DL_NAME[s], lo, dl_top(), tgt[s]);
        }
#endif
    return 0;
}
