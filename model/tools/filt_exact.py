#!/usr/bin/env python3
"""filt_exact.py -- bit-exact search for the filter's fixed-point form on aligned (input, MIXS) pairs from filt_id.
Hypothesis family (Chamberlin SVF, integer state in units of 2^-FB samples):
    band += T(f_i * T2(x_i - low - T3(q_i * band)));  low += T(f_i * band);  MIXS = -(low >> (FB - 4))
with f_i = (512 + m) << e (scaled), products truncated (floor) at the stated positions."""
import sys, os
sys.path.insert(0, os.path.dirname(__file__))
import numpy as np, filt

def get(plat, F, Q):
    tab, x = filt.load(plat)
    fi = tab[(F, Q)][0]
    s = filt.capture_align(plat, tab, fi)
    y, k2 = filt.segment(plat, tab, F, Q)
    return x.astype(np.int64), y.astype(np.int64), s

def run(x, F, qnum, qden_bits, FB, out_shift_mode, low0=0, band0=0):
    e, m = F >> 9, F & 0x1FF
    # f = (512 + m) * 2^(e - 16) / 512 = (512+m) << e >> 25 ; keep as fraction fnum / 2^25
    fnum = (512 + m) << e
    low, band = low0, band0
    out = []
    for xn in x:
        xs = int(xn) << FB
        t = xs - low - ((qnum * band) >> qden_bits)
        band += (fnum * t) >> 25
        low += (fnum * band) >> 25
        o = low >> (FB - 4)
        out.append(-o if out_shift_mode == 0 else -((low + ((1 << (FB - 4)) - 1)) >> (FB - 4)))
    return np.array(out)

if __name__ == "__main__":
    plat = "hw"
    QT = {0: (3, 1), 4: (1, 0), 8: (3, 2), 16: (3, 3)}  # q as num / 2^bits
    for (F, Q) in [(0x1E00, 0), (0x1C00, 0), (0x1F00, 0), (0x1E00, 16), (0x1A00, 4), (0x1800, 8)]:
        x, y, s = get(plat, F, Q)
        L = min(len(x), len(y) - s - 3)
        best = []
        for d in (0, 1, 2):
            yy = y[s + d:s + d + L]
            pre = int(y[s + d - 20])
            for FB in (4, 8, 10, 12, 14, 16, 18, 20):
                for mode in (0, 1):
                    qn, qb = QT[Q]
                    low0 = -pre << (FB - 4)
                    sim = run(x[:L], F, qn, qb, FB, mode, low0=low0)
                    nd = int(np.count_nonzero(sim != yy))
                    best.append((nd, d, FB, mode))
        best.sort()
        print(f"F {F:04x} Q {Q:2d}: best (mismatches, delay, FB, outmode): {best[:4]}  of {L}")
