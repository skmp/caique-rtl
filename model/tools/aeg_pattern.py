#!/usr/bin/env python3
"""aeg_pattern.py -- per stream of an sgc_aeg capture: the key-on start level, and the attack tick pattern as a list
of (samples since previous tick, shift s) for ticks where s is unambiguous, or decay/release (dt, delta)."""
import sys, os
sys.path.insert(0, os.path.dirname(__file__))
import numpy as np, cap, aeg

def transitions(x):
    rs = aeg.runs(x)
    out = []
    for i in range(1, len(rs)):
        a0, a1 = aeg.a_range(rs[i - 1][2]), aeg.a_range(rs[i][2])
        out.append((rs[i][0], rs[i - 1][1], a0, a1))
    return out

if __name__ == "__main__":
    c = cap.load(sys.argv[1])
    mode = sys.argv[2] if len(sys.argv) > 2 else "att"
    nmax = int(sys.argv[3]) if len(sys.argv) > 3 else 24
    for k in range(c["ns"]):
        x = c["data"][:, k]
        on = cap.onset(x)
        tr = transitions(x[on:])
        pat = []
        last_t = 0
        for (t, ln, a0, a1) in tr:
            if not (a0 and a1): continue
            if mode == "att":
                if a0[0] != a0[1] or a1[0] != a1[1] or a1[0] >= a0[0] or a0[0] < 48: continue
                ss = [s for s in range(0, 12) if a0[0] - (a0[0] >> s) - 1 == a1[0]]
                pat.append((t - last_t, ss[0] if len(ss) == 1 else ss))
            else:
                if a1[0] <= a0[0]: continue
                pat.append((t - last_t, (a1[0] - a0[0]) if a0[0] == a0[1] and a1[0] == a1[1] else (a0, a1)))
            last_t = t
            if len(pat) >= nmax: break
        print(f"stream {k}: first level a={aeg.a_range(int(x[on]))} ; ticks (dt, {'s' if mode == 'att' else 'da'}): {pat}")
