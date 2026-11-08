#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The DCO engine's factory presets (firmware/src/eng_dco.c) from fm-106's Juno-106 patches.

  tools/fm106_presets.py FM106_SRC [OUT]   FM106_SRC: fm-106's firmware/src (github.com/PatttF/fm-106, db39e04;
                                           its patches are the same at b795c3d)
                                           OUT: default firmware/src/dco_presets.h

Reads fm106_presets.h (the patches, decoded by fm-106 from the Juno-106 factory SysEx as KR-106 ships them) and
fm106_tables.h (the D7811G envelope rates), and writes DCO_PRESETS[] and DCO_PX[] for the patches listed in PICK;
prints their PRESET_CAT string (category.c). The mapping, Juno -> Felucca:
  DCO    PULSE / SAW switches -> WAVE (E0: OFF PLS SAW P+S); SUB level -> SUB (E2: fm-106 plays the sub whenever its
         level is up, whatever its switch); NOISE -> NOIS (E3); RANGE 16' 8' 4' -> P_DRNG (0 1 2, the same order)
  PWM    MAN: the slider is the width (E1). LFO: the width swings 0 .. the slider, so E1 = half of it and the track's
         LFO -> SHP (P_LD_SHP) the other half
  VCF    FREQ, RES, KYBD -> CUT, RES, KEY (E4..E6), the same 0..127 and the same DAC code (eng_dco.c); ENV -> the
         track's ENV -> FLT (P_ED_FLT): the engine reads it twice as strongly, so Juno 127 = 63, its polarity the
         sign; LFO -> P_LD_FLT (Juno's full depth is a quarter of the cutoff's range: 127 -> 16)
  HPF    0..3 -> HPF (E7: BOOST FLAT 236 754)
  VCA    GATE -> RANGE + 3 (P_DRNG 3..5: eng_dco.c dco_amp, the VCA a gate, the ADSR on the filter only); ENV: 0..2
  ENV    A D S R -> ATK DEC SUS REL by time: the attack's length, the decay's and release's time constant (the
         D7811G ticks at 236.2 Hz; Felucca's times: tools/gen_tables.py TIME_MS_X10, ENV_EXP)
  LFO    RATE (0.123 Hz a step on fm-106) -> P_LRATE (0.05 * 800^(v/127) Hz); DELAY (11.8 ms a step + a 250 ms
         ramp) -> P_LFADE (its fade-in time); TRI -> P_LWAVE; DCO depth (fm-106: 104 cents at 127) -> P_LD_PIT
         (3/16 semitone a step)
  CHORUS on (I, II or both) -> the CHORUS send at CHO_SEND; the global CHORUS TYPE (BBD I / II) is the user's
Not carried over: VCA level (the engine's output is level-matched), the bender, the arpeggiator."""
import math
import re
import sys
from pathlib import Path

# (Juno patch, Felucca name, category letter (category.c), PAT(n) hint (engines.c PATTERNS))
PICK = [("B11", "STRINGS", "P", 5), ("A11", "BRASS", "L", 6), ("A48", "SYNTH BASS", "B", 2),
        ("A18", "PIANO", "K", 6), ("A27", "E PIANO", "K", 6), ("A15", "MOVING STR", "P", 5),
        ("A17", "CHOIR", "P", 5), ("A14", "FLUTES", "L", 4), ("A36", "STRINGS II", "P", 5),
        ("A44", "GUITAR", "U", 3), ("A32", "STEEL DRUM", "U", 3), ("A68", "SYNTH PAD", "P", 5),
        ("A72", "PLUCK SWP", "U", 3), ("A74", "SWEEP", "F", 5), ("A76", "DARK SYNTH", "L", 4),
        ("B15", "HARPSI", "K", 3), ("B23", "ORCHESTRA", "P", 5), ("B43", "DARK ENS", "P", 5),
        ("B48", "CHORUS BEL", "U", 7), ("B56", "ETHEREAL", "P", 5),
        # (after 1.8.1: 50 more, no FX; appended, never reordered)
        ("A12", "BRASS SWEL", "L", 5), ("A13", "TRUMPET", "L", 4), ("A34", "BRASS III", "L", 5),
        ("A35", "FANFARE", "L", 6), ("A43", "BRASS ENS", "L", 6), ("A51", "LEAD I", "L", 4),
        ("A52", "LEAD II", "L", 4), ("B37", "SHARP REED", "L", 4), ("B71", "PICCOLO", "L", 4),
        ("B62", "CLARINET", "L", 4), ("B53", "BASSOON", "L", 4), ("A42", "ENGLISH HN", "L", 4),
        ("B16", "RECORDER", "L", 4), ("A41", "BASS CLAR", "L", 4), ("B12", "VIOLIN", "L", 4),
        ("A16", "BRASS+STR", "P", 5), ("A38", "HIGH STR", "P", 5), ("B83", "CELLO", "P", 5),
        ("B81", "LOW STRING", "P", 5), ("B47", "PHASE ENS", "P", 5), ("B13", "CHORUS VIB", "P", 5),
        ("A21", "ORGAN I", "K", 6), ("A22", "ORGAN II", "K", 6), ("A23", "COMBO ORG", "K", 6),
        ("A24", "CALLIOPE", "K", 6), ("A63", "FRONTIER", "K", 6), ("B26", "ACCORDION", "K", 6),
        ("B25", "ORGAN BELL", "K", 6), ("B78", "BRASSY ORG", "K", 6), ("A28", "E PIANO II", "K", 6),
        ("A61", "PIANO II", "K", 6), ("B61", "EP TREMOLO", "K", 6), ("A26", "CELESTE", "K", 7),
        ("A62", "CLAV", "K", 6), ("B51", "CLAV II", "K", 6), ("A55", "BASS II", "B", 2),
        ("B38", "BASS PLUCK", "B", 2), ("B73", "MEOW BASS", "B", 2), ("A33", "XYLOPHONE", "U", 7),
        ("A37", "PIZZICATO", "U", 3), ("A45", "KOTO", "U", 3), ("A46", "DARK PLUCK", "U", 3),
        ("A75", "PLUCK BELL", "U", 7), ("B17", "PERC PLUCK", "U", 3), ("B22", "NYLON GTR", "U", 3),
        ("B33", "LUTE", "U", 3), ("B21", "SPACE BELL", "U", 7), ("B77", "PING BELL", "U", 7),
        ("A57", "THUD WAH", "U", 3), ("B46", "GLASSY WAH", "U", 3)]
CHO_SEND = 70           # the chorus send of a patch with its chorus on
REV_SEND = 24           # a little room (the Juno has none; Felucca's presets mostly have some)
REL_MAX = 118           # a voice ends within 12 s of its note-off (tests/regress.c FREE_CAP_S): REL at most ~5 s
TICK_MS = 1000.0 / 236.211
FS, CTL = 44100, 32


def table(src, name):
    m = re.search(r"static const \w+ " + name + r"\[\d+\] = \{(.*?)\};", src, re.S)
    return [int(v.strip(), 0) for v in m.group(1).replace("\n", " ").split(",") if v.strip()]


def felucca_time(ms):
    """the F_TIME value whose time is nearest ms (1 ms .. 10 s, exponential)"""
    return max(0, min(127, round(127 * math.log10(max(ms, 1.0)) / 4)))


def main(src_dir, out_path):
    d = Path(src_dir)
    presets = d.joinpath("fm106_presets.h").read_text()
    tabs = d.joinpath("fm106_tables.h").read_text()
    atk, decrel = table(tabs, "FM106_ENV_ATK"), table(tabs, "FM106_ENV_DEC_REL")
    juno = {m.group(1): [int(x) for x in m.group(3).split(",")]
            for m in re.finditer(r'\{"(\w+) ([^"]*)", \{([0-9, ]+)\}\}', presets)}

    def attack(v):          # linear to the top: 16383 / step ticks
        return felucca_time(math.ceil(16383 / max(1, atk[v])) * TICK_MS if atk[v] < 16383 else 1.0)

    def expo(v):            # per tick x mul / 65536: time constant -> Felucca's time (4.6 time constants)
        mul = decrel[v] / 65536.0
        tau_ms = TICK_MS / -math.log(mul) if 0 < mul < 1 else 1.0
        return felucca_time(4.6 * tau_ms)

    rows, px, cats = [], [], ""
    for code, name, cat, pat in PICK:
        p = juno[code]
        p += [0] * (66 - len(p))
        wave = (1 if p[23] else 0) + (2 if p[24] else 0)
        lfo_pwm = p[33] == 0
        pw = (p[6] + 1) // 2 if lfo_pwm else p[6]
        e = [wave, pw, p[7], p[8], p[10], p[11], p[14], min(p[9], 3)]
        env = [attack(p[16]), expo(p[17]), p[18], min(REL_MAX, expo(p[19]))]
        fenv = min(63, (p[12] + 1) // 2) * (-1 if p[34] else 1)
        hz = p[3] * 12000 * FS / 2 ** 32
        lrate = 0 if hz <= 0.05 else min(127, round(127 * math.log(hz / 0.05) / math.log(800)))
        lfade = felucca_time(p[4] * 520 * 1000 / FS + 250) if p[4] else 0
        ld_pit = round(104 * p[5] / 127 / 18.75)
        ld_flt = round(p[13] / 8)
        ld_shp = min(63, p[6] // 2) if lfo_pwm and p[23] else 0
        cho = CHO_SEND if p[27] or p[28] else 0
        rows.append(f'    {{"{name}", {{{", ".join(map(str, e))}}}, {{{", ".join(map(str, env))}}}, {fenv}, 0, '
                    f'FX(0, {cho}, 0, {REV_SEND}), PAT({pat})}},   /* {code}{" GATE" if p[35] else ""} */')
        px.append(f"    {{{lrate}, {lfade}, {ld_pit}, {ld_flt}, {ld_shp}, {min(p[29], 2) + (3 if p[35] else 0)}}},   "
                  f"/* {name} */")
        cats += cat
    L = ["/* SPDX-License-Identifier: GPL-3.0-only",
         " * generated by tools/fm106_presets.py from fm-106's patches (PatttF, GPL-3.0-only, github.com/PatttF/fm-106 at",
         " * db39e04, the same at b795c3d: the Juno-106 factory patches as Ultramaster KR-106 has them); the DCO engine's",
         " * (eng_dco.c). Stored preset numbers index this: append, never reorder */",
         "static const preset_t DCO_PRESETS[] = {",
         "    /* name, {WAVE, PW, SUB, NOIS, CUT, RES, KEY, HPF}, {A D S R}, ENV -> FLT, mono, sends, pattern */",
         *rows, "};",
         "/* the rest of each preset (eng_dco.c dco_track_loaded): LFO RATE, FADE, -> PIT, -> FLT, -> SHP; RANGE (+ 3: GATE) */",
         "static const int8_t DCO_PX[][6] = {", *px, "};", ""]
    Path(out_path).write_text("\n".join(L))
    print(f'{out_path}: {len(rows)} presets; category.c PRESET_CAT[14]: "{cats}"')


if __name__ == "__main__":
    if len(sys.argv) not in (2, 3):
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2] if len(sys.argv) == 3 else
         str(Path(__file__).resolve().parents[1] / "firmware" / "src" / "dco_presets.h"))
