#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Part of Felucca: free software under the GNU General Public License, version 3 only (see LICENSE)
"""The levels of the DRUM engine's kits of SLOOP: tools/drumkit_levels.json, the trim (dB) tools/gen_drumkits.py adds
to each sound's level, so that every kit is as loud as Felucca's own, lane by lane, and no hit goes into the engine's
soft knee. After SLOOP's tools/level_drumkits.py (which levelled its 32 kits in its own mixer; the trims of the kits
PICK leaves out are still SLOOP's: level a kit when it is picked).

  build/host/drum_level > build/host/drum_level.txt       (tests/drum_level.c says how to build it; its "K" lines:
  tools/level_drumkits.py build/host/drum_level.txt        kit sound loudness top target, tests/drum_loud.h)
  tools/gen_drumkits.py build/gen/felucca_drumkits.h      (or ./build.sh), and again until it reports no change: the
                                                           knee and a sound's drive are not linear

A sound moves by target - loudness, but its top (the level it holds for 1 ms) never over the ceiling (the "C" line: as
far into the engine's knee as the hardest of Felucca's kits goes): a very short sound stops there, under its target.
The trims are in quarter dB, the sound level's step (drum_synth.c dsnd_t.level)."""
import json
import sys
from pathlib import Path

LEVELS = Path(__file__).with_name("drumkit_levels.json")


def main(path):
    levels = json.loads(LEVELS.read_text())
    moved = short = n = 0
    worst = 0.0
    lines = [line.split() for line in Path(path).read_text().splitlines()]
    ceiling = [float(f[1]) for f in lines if f and f[0] == "C"][0]
    for f in lines:
        if not f or f[0] != "K":
            continue
        kit, sound, loud, top, target = f[1], f[2], float(f[3]), float(f[4]), float(f[5])
        adj = min(target - loud, ceiling - top)
        if adj < target - loud - 0.125:
            short += 1                                  # held by the knee: under its target
        old = levels[kit][sound]
        new = round(max(-31.75, min(31.75, old + adj)) * 4) / 4
        assert abs(new) < 31.75, (kit, sound, "the trim is at the end of the sound level's range")
        moved += new != old
        worst = max(worst, abs(adj))
        levels[kit][sound] = new
        n += 1
    LEVELS.write_text(json.dumps(levels, indent=1) + "\n")
    print(f"{n} sounds: {moved} moved (the largest step {worst:.2f} dB), {short} at the ceiling ({ceiling:+.2f} dB re the"
          f" knee) under their target -> {LEVELS}")


if __name__ == "__main__":
    main(sys.argv[1])
