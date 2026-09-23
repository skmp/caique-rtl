#!/usr/bin/env python3
"""aeg.py -- attenuation sequences from sgc_aeg captures (constant 0x7FFF sample, TL 0, IMXL 15).

MIXS = 16 * floor(32767 * (127 - (a & 63)) / 2^(7 + (a >> 6)))  (tests/sgc_level), so every MIXS value maps to a
range of attenuations a.  Prints, per stream, the runs of equal level after key-on: start sample, length, a range.
    aeg.py tests/sgc_aeg/hw/<run> [stream] [max_runs]
"""
import sys, os
sys.path.insert(0, os.path.dirname(__file__))
import numpy as np
import cap

def level(a, s=32767):
    return 16 * ((s * (127 - (a & 63))) >> (7 + (a >> 6)))

LV = np.array([level(a) for a in range(1024)])

def a_range(m):
    idx = np.nonzero(LV == m)[0]
    if len(idx):
        return int(idx[0]), int(idx[-1])
    return None

def runs(x):
    """(start, length, value) of runs of equal values"""
    out = []
    i = 0
    while i < len(x):
        j = i
        while j + 1 < len(x) and x[j + 1] == x[i]:
            j += 1
        out.append((i, j - i + 1, int(x[i])))
        i = j + 1
    return out

if __name__ == "__main__":
    c = cap.load(sys.argv[1])
    streams = [int(sys.argv[2])] if len(sys.argv) > 2 else range(c["ns"])
    maxr = int(sys.argv[3]) if len(sys.argv) > 3 else 40
    print("events", c["events"])
    for k in streams:
        x = c["data"][:, k]
        on = cap.onset(x)
        print(f"stream {k}: onset {on}")
        for (st, ln, v) in runs(x[on:])[:maxr]:
            print(f"  +{st:7d} x{ln:6d}  MIXS {v:7d}  a {a_range(v)}")
