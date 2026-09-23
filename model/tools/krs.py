#!/usr/bin/env python3
"""krs.py -- effective envelope rate R per (KRS, OCT, FNS) from tests/sgc_krs/<platform>/sgc_krs.txt.
Each line lists the level changes of one slot (constant 0x7FFF, D1R 14, DL 31).  The decay-1 ticks (attenuation
increases) give R: for R >= 48 the increment cycle over 2-sample ticks, for R < 48 the spacing cycle.
    krs.py tests/sgc_krs/hw/sgc_krs.txt [--table]"""
import sys, os, re
sys.path.insert(0, os.path.dirname(__file__))
import aeg

ROWS = [[0,1,0,1,0,1,0,1],[0,1,0,1,1,1,0,1],[0,1,1,1,0,1,1,1],[0,1,1,1,1,1,1,1],[1,1,1,1,1,1,1,1],[1,1,1,2,1,1,1,2],
        [1,2,1,2,1,2,1,2],[1,2,2,2,1,2,2,2],[2,2,2,2,2,2,2,2],[2,2,2,4,2,2,2,4],[2,4,2,4,2,4,2,4],[2,4,4,4,2,4,4,4],
        [4,4,4,4,4,4,4,4],[4,4,4,8,4,4,4,8],[4,8,4,8,4,8,4,8],[4,8,8,8,4,8,8,8],[8,8,8,8,8,8,8,8]]

def expected(R):
    """one cycle of (spacing in samples, increment) for rate R"""
    if R >= 48:
        row = ROWS[16 if R >= 60 else 4 + R - 48]
        return [(2, i) for i in row]
    shift = 11 - (R >> 2)
    row = ROWS[R & 3]
    out, gap = [], 0
    for pos in range(16):
        gap += 2 << shift
        if row[pos & 7]:
            out.append((gap, 1)); gap = 0
    return out[:len(out) // 2] if out[:len(out) // 2] == out[len(out) // 2:] else out

def is_rotation(meas, cyc):
    n = len(cyc)
    if len(meas) < n: return False
    for r in range(n):
        if all(meas[i] == cyc[(i + r) % n] for i in range(len(meas))):
            return True
    return False

def infer(ticks):
    cands = [R for R in range(1, 64) if is_rotation(ticks, expected(R))]
    return cands

def parse(path):
    out = []
    for line in open(path):
        m = re.match(r"KRS (\d+) OCT (\d+) FNS ([0-9a-f]+) err (\d+):(.*)", line)
        if not m: continue
        krs, oct_, fns = int(m[1]), int(m[2]), int(m[3], 16)
        ch = [tuple(int(v) for v in t.split(":")) for t in m[5].split()]
        # attenuation path; decay ticks = increases after the minimum
        a = [aeg.a_range(v) for (_, v) in ch]
        ticks, last_t, started = [], None, False
        for i in range(1, len(ch)):
            if a[i] is None or a[i - 1] is None or a[i][0] != a[i][1] or a[i - 1][0] != a[i - 1][1]: continue
            if a[i][0] > a[i - 1][0]:
                if last_t is not None and started:
                    ticks.append((ch[i][0] - last_t, a[i][0] - a[i - 1][0]))
                started = True
                last_t = ch[i][0]
        out.append((krs, oct_, fns, ticks[1:25]))
    return out

if __name__ == "__main__":
    res = parse(sys.argv[1])
    table = {}
    for (krs, oct_, fns, ticks) in res:
        c = infer(ticks)
        table[(krs, oct_, fns)] = c
        if "--table" not in sys.argv:
            print(f"KRS {krs:2d} OCT {oct_:2d} FNS {fns:03x}: R {c}  ticks {ticks[:8]}")
    if "--table" in sys.argv:
        octs = sorted({k[1] for k in table}, key=lambda o: o - 16 if o & 8 else o)
        for fns in sorted({k[2] for k in table}):
            print(f"FNS {fns:03x}: R by KRS (rows) x OCT (cols {[o - 16 if o & 8 else o for o in octs]})")
            for krs in sorted({k[0] for k in table}):
                row = []
                for o in octs:
                    c = table.get((krs, o, fns))
                    row.append(("%2d" % c[0]) if c and len(c) == 1 else (" ?" if not c else "*"))
                print(f"  KRS {krs:2d}: " + " ".join(row))
