#!/usr/bin/env python3
"""filt_cmp.py -- console vs model filter responses for a filt_id-style case (default filt_id2): per (F, Q), aligned
on each platform's impulse, the fraction of samples that differ and the max difference (MIXS units; 2 = 1/8 sample).
    FILT_CASE=filt_id2 filt_cmp.py"""
import sys, os
sys.path.insert(0, os.path.dirname(__file__))
import numpy as np, filt

def main():
    tabh, x = filt.load("hw")
    tabm, _ = filt.load("model")
    alh, alm = {}, {}
    tot_bad = tot = 0
    worst = 0
    for (F, Q) in sorted(tabh):
        fh, fm = tabh[(F, Q)][0], tabm[(F, Q)][0]
        if fh not in alh: alh[fh] = filt.capture_align("hw", tabh, fh)
        if fm not in alm: alm[fm] = filt.capture_align("model", tabm, fm)
        yh, _ = filt.segment("hw", tabh, F, Q)
        ym, _ = filt.segment("model", tabm, F, Q)
        a, b = yh[alh[fh] - 4:], ym[alm[fm] - 4:]
        L = min(len(a), len(b), len(x) + 8)
        d = np.abs(a[:L].astype(int) - b[:L].astype(int))
        nb = int(np.count_nonzero(d))
        tot_bad += nb; tot += L
        worst = max(worst, int(d.max()))
        print(f"F {F:04x} Q {Q:2d}: {nb:5d}/{L} differ, max |diff| {int(d.max()):6d} (MIXS), mean {d.mean():.3f}")
    print(f"total: {tot_bad}/{tot} samples differ ({100.0 * tot_bad / tot:.1f}%), worst {worst}")

if __name__ == "__main__":
    main()
