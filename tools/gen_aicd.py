#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Part of Felucca: free software under the GNU General Public License, version 3 only (see LICENSE)
"""TB-3PO's AICD mode (firmware/src/tb3po.c): Aicd's acid line model as a C header.

assets/aicd/lm.js is Aicd's model as it is (Michael Ganss, github.com/mganss/Aicd at 2afe043, MIT:
LICENSES/MIT-Aicd.txt): a character n-gram of order 18, maximum likelihood, trained on ABL3 patterns. A step is
the line "N!a$s#g=\\n" (N the note as a character, 82 = C; a accent, s slide, g gate) and "~\\n" ends a pattern, so
18 characters are two steps. Aicd samples a character at a time; this samples a line at a time, the same thing:
a node is a context that ends a line (all 18 characters of it), its edges the lines the model can write next
(each line's probability the product of its characters'), each into the node its last 18 characters are.
Nothing of the model is changed: no line merged, every edge kept (but any into a context the model has no
continuation for: none in this model), the probabilities only rounded (to a factor of 2^(1/3), as the ratio to
the node's likeliest line's: the C side's temperature and biases work in log2 too).

A node's first and last 9 characters are each a symbol: a step (AICD_CODE, sorted) or, from AICD_NSTEP on, 9
characters with a pattern end in them. The nodes are numbered in groups by their first symbol, in a group by their
last (so a step's edges all go into one group, its last symbol's: the 2nd-order chain). The header, every table
const (flash):
  AICD_CODE[s]   the steps: (note offset + 16) << 3 | gate << 2 | acc << 1 | sld, the offset from C (-15 .. 35)
  AICD_GRP[g]    group g's first node (AICD_NSYM + 1: the last the node count)
  AICD_LAST      each node's last symbol, AICD_SYMBITS bits (LSB first): its step, read at once for each edge
  AICD_CK[k]     the byte in AICD_REC node 8k's record starts at (a checkpoint: any node 7 records from one at most)
  AICD_REC       the nodes' records in their order, LSB first:
                   1 bit: a "~" edge (a pattern ends there)
                   the line edges' count n: 0 = 1, 10 = 2, 110 = 3, 111 + 6 bits = n
                   more than one edge: the likeliest one's index (edges in order: the "~" one, then the line edges),
                     bitlen(edges) bits
                   the "~" edge: its node (AICD_NODEBITS); its weight
                   each line edge: its node's index in its group (the first: bitlen(group) bits, then the gap to the
                     one before as z zeros, a one and z bits v: gap (1 << z | v)); its weight
                 a weight (5 bits: round(-3 log2(p / p of the likeliest)), 31 at most) only for an edge of a node with
                 more than one that is not the likeliest.
  AICD_START[k]  the nodes that end with a pattern end, where Aicd starts (one picked evenly)

  gen_aicd.py OUT.h [REF.txt]     the header; REF: every node's edges, exact (tests/tb3po_test.c)
"""
import json
import math
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LM = ROOT / "assets" / "aicd" / "lm.js"
ORDER = 18
STEP_RE = re.compile(r"^(.)!([01])\$([01])#([01])=\n$")
WBITS, WQ = 5, 3


def load(path):
    src = path.read_text(encoding="utf-8")
    return json.loads(src[src.index("{"):src.rindex("}") + 1])


def lines_from(lm, w):
    """{line: p}: what the model writes after context w up to its next newline"""
    out = {}
    stack = [(w, "", 1.0)]
    while stack:
        h, tok, p = stack.pop()
        d = lm.get(h[-ORDER:])
        if d is None:
            continue
        for c, q in d.items():
            t = tok + c
            if c == "\n":
                out[t] = out.get(t, 0.0) + p * q
            elif len(t) < 9:
                stack.append((h + c, t, p * q))
            else:
                raise SystemExit(f"gen_aicd: a line longer than a step after {w!r}")
    return out


def step_code(s):
    m = STEP_RE.match(s)
    if not m:
        return None
    o = ord(m.group(1)) - 82
    assert -16 < o < 48, s
    return ((o + 16) << 3) | (int(m.group(4)) << 2) | (int(m.group(2)) << 1) | int(m.group(3))


def bitlen(n):                       # bits for an index below n
    return max(0, (n - 1).bit_length())


def build(lm):
    """the nodes in order, their (group, index), each node's edges {node: p} (normalised), the symbols"""
    keys = [w for w in lm if w.endswith("\n")]
    keyset = set(keys)
    raw, dropped = {}, 0
    for w in keys:
        e = {}
        for tok, p in lines_from(lm, w).items():
            if tok != "~\n" and step_code(tok) is None:
                raise SystemExit(f"gen_aicd: not a step: {tok!r}")
            t = (w + tok)[-ORDER:]
            if t in keyset:
                e[t] = e.get(t, 0.0) + p
            else:
                dropped += 1
        s = sum(e.values())
        raw[w] = {t: p / s for t, p in e.items()} if s else {}
    syms = {w[:9] for w in keys} | {w[9:] for w in keys}
    steps = sorted((s for s in syms if step_code(s) is not None), key=step_code)
    sym = {s: i for i, s in enumerate(steps + sorted(s for s in syms if step_code(s) is None))}
    keys.sort(key=lambda w: (sym[w[:9]], sym[w[9:]]))
    nsym = len(sym)
    groups = [[] for _ in range(nsym)]
    for w in keys:
        groups[sym[w[:9]]].append(w)
    at = {w: (sym[w[:9]], j) for g in groups for j, w in enumerate(g)}
    return keys, groups, at, raw, steps, sym, dropped


class Bits:
    def __init__(self):
        self.v, self.n = 0, 0

    def put(self, x, w):
        assert w >= 0 and 0 <= x < (1 << w) or (w == 0 and x == 0), (x, w)
        self.v |= x << self.n
        self.n += w

    def ones(self, q):               # q ones, a zero
        self.put((1 << q) - 1, q + 1)

    def gap(self, x):                # x >= 1: z zeros, a one, x's z bits under its top one (LSB first)
        z = x.bit_length() - 1
        self.put(1 << z, z + 1)
        self.put(x & ((1 << z) - 1), z)

    def align(self):
        self.n = (self.n + 7) & ~7

    def bytes(self, pad=4):
        return self.v.to_bytes((self.n + 7) // 8 + pad, "little")


def wq(p, pmax):
    return min((1 << WBITS) - 1, max(0, round(-WQ * math.log2(p / pmax))))


def edges_of(w, raw, at, sym):
    """node w's edges in their stored order: the "~" one first, then the line edges by index"""
    g_last = sym[w[9:]]
    bnd = [(t, p) for t, p in raw[w].items() if at[t][0] != g_last]
    reg = sorted(((t, p) for t, p in raw[w].items() if at[t][0] == g_last), key=lambda tp: at[tp[0]][1])
    assert len(bnd) <= 1 and all(t[-2:] == "~\n" for t, _ in bnd), w
    return bnd, reg


def encode(keys, groups, at, raw, sym, grp):
    sb, nb = bitlen(len(sym)), bitlen(len(keys))
    rec, ck = Bits(), []
    for n, w in enumerate(keys):
        g, j = at[w]
        if n % 8 == 0:
            rec.align()
            ck.append(rec.n // 8)
        last = sym[w[9:]]
        bnd, reg = edges_of(w, raw, at, sym)
        allp = bnd + reg
        k = len(reg)
        rec.put(len(bnd), 1)
        if 1 <= k <= 3:
            rec.ones(k - 1)
        else:
            assert k < 64
            rec.put(7, 3)
            rec.put(k, 6)
        many = len(allp) > 1
        pmax = max((p for _, p in allp), default=1.0)
        m = max(range(len(allp)), key=lambda i: allp[i][1]) if allp else 0
        if many:
            rec.put(m, bitlen(len(allp)))
        if bnd:
            rec.put(grp[at[bnd[0][0]][0]] + at[bnd[0][0]][1], nb)
            if many and m != 0:
                rec.put(wq(bnd[0][1], pmax), WBITS)
        jw = bitlen(len(groups[last]))
        pj = -1
        for i, (t, p) in enumerate(reg):
            jt = at[t][1]
            if pj < 0:
                rec.put(jt, jw)
            else:
                rec.gap(jt - pj)
            pj = jt
            if many and i + len(bnd) != m:
                rec.put(wq(p, pmax), WBITS)
    return rec, ck


class Reader:                        # the C side's decoder (tb3po.c aicd_*), to check the stream
    def __init__(self, b, pos):
        self.b, self.pos = b, pos

    def get(self, w):
        v = (int.from_bytes(self.b[self.pos >> 3:(self.pos >> 3) + 4], "little") >> (self.pos & 7)) & ((1 << w) - 1)
        self.pos += w
        return v

    def ones(self, cap=1 << 30):     # ones up to a zero (or cap of them)
        q = 0
        while q < cap and self.get(1):
            q += 1
        return q

    def gap(self):
        z = 0
        while not self.get(1):
            z += 1
        return (1 << z) | self.get(z)


def decode(r, last, grp, nb):
    """node record (its last symbol last): [(node, weight)], the weight 0 for the likeliest (and an only edge)"""
    bnd = r.get(1)
    n = r.ones(3)
    n = n + 1 if n < 3 else r.get(6)
    tot = bnd + n
    m = r.get(bitlen(tot)) if tot > 1 else 0
    out = []
    if bnd:
        t = r.get(nb)
        out.append((t, r.get(WBITS) if tot > 1 and m != 0 else 0))
    jw, j = bitlen(grp[last + 1] - grp[last]), -1
    for i in range(n):
        j = r.get(jw) if j < 0 else j + r.gap()
        out.append((grp[last] + j, r.get(WBITS) if tot > 1 and i + bnd != m else 0))
    return out


def check(keys, at, raw, sym, grp, rec, ck, lastb):
    sb, nb, data = bitlen(len(sym)), bitlen(len(keys)), rec.bytes()
    node = {w: n for n, w in enumerate(keys)}
    for n0 in range(0, len(keys), 8):    # (each checkpoint's records, as the C side walks them)
        r = Reader(data, ck[n0 // 8] * 8)
        for n in range(n0, min(n0 + 8, len(keys))):
            w = keys[n]
            prev = Reader(lastb, n * sb).get(sb)
            got = decode(r, prev, grp, nb)
            bnd, reg = edges_of(w, raw, at, sym)
            allp = bnd + reg
            pmax = max((p for _, p in allp), default=1.0)
            m = max(range(len(allp)), key=lambda i: allp[i][1]) if allp else 0
            want = [(node[t], wq(p, pmax) if len(allp) > 1 and i != m else 0) for i, (t, p) in enumerate(allp)]
            if prev != sym[w[9:]] or got != want:
                raise SystemExit(f"gen_aicd: node {n} decodes as {prev} {got}, not {sym[w[9:]]} {want}")


def c_array(name, ctype, vals, per=16):
    rows = [", ".join(str(v) for v in vals[i:i + per]) for i in range(0, len(vals), per)]
    return f"static const {ctype} {name}[{len(vals)}] = {{\n    " + ",\n    ".join(rows) + ",\n};\n"


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    lm = load(LM)
    keys, groups, at, raw, steps, sym, dropped = build(lm)
    grp = [0]
    for g in groups:
        grp.append(grp[-1] + len(g))
    node = {w: n for n, w in enumerate(keys)}
    assert all(node[w] == grp[at[w][0]] + at[w][1] for w in keys)
    rec, ck = encode(keys, groups, at, raw, sym, grp)
    nsym, nstep, sb, nb = len(sym), len(steps), bitlen(len(sym)), bitlen(len(keys))
    last = Bits()
    for w in keys:
        last.put(sym[w[9:]], sb)
    lastb = last.bytes()
    check(keys, at, raw, sym, grp, rec, ck, lastb)
    starts = [node[w] for w in keys if w.endswith("~\n")]
    maxe = max(len(e) for e in raw.values())
    recb = rec.bytes()
    size = 2 * nstep + 2 * (nsym + 1) + len(lastb) + 2 * len(ck) + len(recb) + 2 * len(starts)
    assert ck[-1] < 65536 and len(keys) < 65536 and step_code(steps[-1]) < 512
    nedge = sum(len(e) for e in raw.values())
    h = [f"/* generated by tools/gen_aicd.py from assets/aicd/lm.js (Aicd by Michael Ganss, MIT: LICENSES/MIT-Aicd.txt): "
         f"do not edit.\n * {len(keys)} nodes, {nedge} edges ({dropped} into the model's end left out), {nstep} steps, "
         f"{size} bytes */\n",
         f"#define AICD_NNODE {len(keys)}u\n#define AICD_NSYM {nsym}u\n#define AICD_NSTEP {nstep}u\n"
         f"#define AICD_NSTART {len(starts)}u\n#define AICD_MAXE {maxe}u\n"
         f"#define AICD_SYMBITS {sb}u\n#define AICD_NODEBITS {nb}u\n#define AICD_WBITS {WBITS}u\n"
         f"#define AICD_WQ {WQ}u\n#define AICD_BYTES {size}u\n",
         c_array("AICD_CODE", "uint16_t", [step_code(s) for s in steps]),
         c_array("AICD_GRP", "uint16_t", grp),
         c_array("AICD_LAST", "uint8_t", list(lastb), 24),
         c_array("AICD_CK", "uint16_t", ck),
         c_array("AICD_REC", "uint8_t", list(recb), 24),
         c_array("AICD_START", "uint16_t", starts)]
    Path(sys.argv[1]).write_text("".join(h))
    if len(sys.argv) > 2:            # the test's reference: "node last n" then n x "node p" (p exact), stored order
        with open(sys.argv[2], "w") as f:
            for n, w in enumerate(keys):
                bnd, reg = edges_of(w, raw, at, sym)
                f.write(f"{n} {sym[w[9:]]} {len(bnd) + len(reg)}")
                for t, p in bnd + reg:
                    f.write(f" {node[t]} {p:.17g}")
                f.write("\n")
    print(f"gen_aicd: {len(keys)} nodes, {nedge} edges, {nstep} steps, {size} bytes")


if __name__ == "__main__":
    main()
