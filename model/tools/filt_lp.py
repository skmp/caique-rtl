#!/usr/bin/env python3
"""filt_lp.py -- how far can EXACT (unrounded) SVF arithmetic explain a filt_cyc capture?

The slot filter's step responses (tests/filt_cyc/hw/fc_*: rest -> 256 samples of DC A -> zeros) are compared with
the exact-arithmetic SVF (band += f (x - low) - f q band; low += f band; y = floor(low), in 1/8 sample units,
y = -MIXS/2).  The unknown start state (low0, band0) enters the exact outputs affinely, so the set of start states
that reproduce the first n outputs is a convex polygon; it is clipped sample by sample (exact rationals).  The first
sample where the polygon becomes empty is where exact arithmetic stops being able to explain the console; with a
slack t (y - t <= low < y + 1 + t) the report also gives the smallest t that keeps every sample feasible, i.e. how
large the accumulated rounding error has to be.

    tools/filt_lp.py [stream index 0..23 ...]
"""
import sys
from fractions import Fraction as Fr
import numpy as np
sys.path.insert(0, __import__("os").path.dirname(__file__))
import cap

Q128 = [192, 176, 160, 144, 128, 120, 112, 104, 96, 88, 80, 72, 64, 60, 56, 52,
        48, 44, 40, 36, 32, 30, 28, 26, 24, 22, 20, 18, 16, 15, 14, 13]
CF = [(0x1FFE, 4, 1000), (0x1FFE, 4, -1000), (0x1FFE, 4, 7), (0x1FFE, 0, 1000),
      (0x1FF0, 4, 1000), (0x1FF0, 4, -1000), (0x1FF0, 0, 1000), (0x1FFC, 4, 1000),
      (0x1E00, 4, 1000), (0x1E00, 4, -1000), (0x1E00, 0, 1000), (0x1E00, 0, -1000),
      (0x1C00, 4, 1000), (0x1C00, 4, -1000), (0x1C00, 0, 1000), (0x1C00, 0, -1000),
      (0x1A00, 4, 1000), (0x1A00, 4, -1000), (0x1800, 4, 1000), (0x1800, 4, -1000),
      (0x1F55, 4, 1000), (0x1F55, 4, -1000), (0x1D55, 4, 1000), (0x1D55, 4, -1000)]
ON = [139] * 4 + [138] * 16 + [200] * 4   # first output change after key-on (batch 0: 139; its rest states are cycles)

def coef(F, Q):
    e = F >> 9
    f = Fr(256 + ((F & 0x1FF) >> 1), 2 ** (24 - e))
    return f, f * Fr(Q128[Q], 128)

def stream(i):
    """(F, Q, A, y[]) of filt_cyc stream i (y = -MIXS/2, 1/8 sample units)"""
    F, Q, A = CF[i]
    d = cap.load(f"tests/filt_cyc/hw/fc_{i // 4}")["data"][:, i % 4]
    return F, Q, A, (-(d.astype(np.int64)) // 2).tolist()

def clip(poly, a, b, c):
    """keep a*l + b*m + c >= 0 (poly: list of (l, m) Fractions, convex)"""
    out = []
    n = len(poly)
    for k in range(n):
        p, q = poly[k], poly[(k + 1) % n]
        vp = a * p[0] + b * p[1] + c
        vq = a * q[0] + b * q[1] + c
        if vp >= 0:
            out.append(p)
        if (vp >= 0) != (vq >= 0):
            t = vp / (vp - vq)
            out.append((p[0] + t * (q[0] - p[0]), p[1] + t * (q[1] - p[1])))
    return out

def feasible(F, Q, A, y, n0, n1, t, span=400):
    """clip start states over samples n0..n1-1; returns (last sample index reached, polygon)"""
    f, fq = coef(F, Q)
    X = 8 * A
    y0 = y[n0 - 1]
    poly = [(Fr(y0 - t), Fr(-span)), (Fr(y0 + 1 + t), Fr(-span)), (Fr(y0 + 1 + t), Fr(span)), (Fr(y0 - t), Fr(span))]
    # affine state: low = (l0, lL, lB) meaning l0 + lL*L0 + lB*B0
    low = (Fr(0), Fr(1), Fr(0))
    band = (Fr(0), Fr(0), Fr(1))
    for n in range(n0, n1):
        x = X if n < n0 + 256 else 0
        band = tuple(band[j] + (f * x if j == 0 else 0) - f * low[j] - fq * band[j] for j in range(3))
        low = tuple(low[j] + f * band[j] for j in range(3))
        # y - t <= low < y + 1 + t  (the strict bound is treated as <=; it only matters on exact ties)
        poly = clip(poly, low[1], low[2], low[0] - (y[n] - t))
        if not poly:
            return n, None
        poly = clip(poly, -low[1], -low[2], (y[n] + 1 + t) - low[0])
        if not poly:
            return n, None
    return n1, poly

def main():
    idx = [int(a) for a in sys.argv[1:]] or list(range(20))
    for i in idx:
        F, Q, A, y = stream(i)
        n0 = ON[i]
        n1 = min(len(y), n0 + 256 + 200)
        reach, _ = feasible(F, Q, A, y, n0, n1, 0)
        # smallest slack (in 1/16 steps) that makes the whole window feasible
        slack = None
        for t16 in range(0, 16 * 64):
            r, _ = feasible(F, Q, A, y, n0, n1, Fr(t16, 16))
            if r >= n1:
                slack = Fr(t16, 16)
                break
        print(f"stream {i:2d} F {F:04x} Q {Q} A {A:5d}: exact arithmetic explains {reach - n0} samples; "
              f"slack for all {n1 - n0}: {slack}")

if __name__ == "__main__":
    main()
