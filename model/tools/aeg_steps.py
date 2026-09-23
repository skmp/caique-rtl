#!/usr/bin/env python3
"""aeg_steps.py -- EG step events from an sgc_aeg capture: for each stream, the samples (after key-on onset) at which
the attenuation changes, with a before/after, and for the attack the shift s with a' = a - (a >> s) - 1.
    aeg_steps.py tests/sgc_aeg/hw/<run> [stream] [--from N] [--max M]"""
import sys, os
sys.path.insert(0, os.path.dirname(__file__))
import numpy as np, cap, aeg

def seq(x):
    """[(sample, a_lo, a_hi)] per run of equal level"""
    out = []
    for (st, ln, v) in aeg.runs(x):
        r = aeg.a_range(v)
        out.append((st, ln, r))
    return out

if __name__ == "__main__":
    c = cap.load(sys.argv[1])
    args = sys.argv[2:]
    k = int(args[0]) if args and not args[0].startswith("--") else None
    frm = int(args[args.index("--from") + 1]) if "--from" in args else 0
    mx = int(args[args.index("--max") + 1]) if "--max" in args else 60
    for s in ([k] if k is not None else range(c["ns"])):
        x = c["data"][:, s]
        on = cap.onset(x)
        sq = seq(x[on:])
        print(f"stream {s} onset {on}:")
        shown = 0
        for i in range(1, len(sq)):
            st, ln, r = sq[i]
            if st < frm: continue
            p = sq[i - 1]
            txt = ""
            if p[2] and r and p[2][0] == p[2][1] and r[0] == r[1]:
                a0, a1 = p[2][0], r[0]
                ss = [sh for sh in range(0, 12) if a0 - (a0 >> sh) - 1 == a1]
                txt = f"d={a1 - a0:+d}" + (f" s={ss}" if ss and a1 < a0 else "")
            print(f"  @{st:7d} (prev run x{p[1]}) a {p[2]} -> {r}  {txt}")
            shown += 1
            if shown >= mx: break
