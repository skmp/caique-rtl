#!/usr/bin/env python3
"""aeg_cmp.py -- compare sgc_aeg captures, console vs model, independent of the envelope clock phase at key-on and of
the key-off moment: per stream, (1) the exact path of distinct attenuation levels, (2) per monotonic segment
(attack / decay+release), the histogram of tick spacings excluding the segment's first spacing.
    aeg_cmp.py [run...]   (default: every run of tests/sgc_aeg)"""
import sys, os, glob
from collections import Counter
sys.path.insert(0, os.path.dirname(__file__))
import cap, aeg

D = os.path.join(os.path.dirname(__file__), "..", "tests", "sgc_aeg")

def profile(x):
    on = cap.onset(x)
    rs = aeg.runs(x[on:])
    path = [aeg.a_range(v) for (_, _, v) in rs]
    segs, cur, prev_dir = [], [], 0
    for i in range(1, len(rs)):
        d = 1 if rs[i][2] < rs[i - 1][2] else -1  # level down = attenuation up
        if d != prev_dir and cur:
            segs.append(cur); cur = []
        prev_dir = d
        cur.append(rs[i - 1][1])
    if cur: segs.append(cur)
    hist = [Counter(sg[1:]) for sg in segs]
    return path, hist

def main(runs):
    bad = 0
    for r in runs:
        a, b = cap.load(f"{D}/hw/{r}"), cap.load(f"{D}/model/{r}")
        for k in range(a["ns"]):
            pa, ha = profile(a["data"][:, k])
            pb, hb = profile(b["data"][:, k])
            ok_path = pa == pb
            # the hold before release is a spacing of the decay segment set by the key-off moment: drop the largest
            def clean(hs):
                out = []
                for h in hs:
                    h = Counter(h)
                    if h:
                        m = max(h)
                        if h[m] == 1 and len(h) > 1: del h[m]
                    out.append(dict(sorted(h.items())))
                return out
            ca, cb = clean(ha), clean(hb)
            ok_gap = ca == cb
            st = "ok" if ok_path and ok_gap else "DIFF"
            if st != "ok": bad += 1
            print(f"{r:10s} s{k}: path {'=' if ok_path else '!='} ({len(pa)} vs {len(pb)} levels), tick spacing "
                  f"{'=' if ok_gap else '!='}  {st}")
            if not ok_path:
                for i, (x, y) in enumerate(zip(pa, pb)):
                    if x != y:
                        print(f"      first path diff at level #{i}: hw {pa[max(0,i-2):i+3]} model {pb[max(0,i-2):i+3]}")
                        break
            if not ok_gap:
                print(f"      hw {ca}\n      model {cb}")
    return bad

if __name__ == "__main__":
    runs = sys.argv[1:] or sorted(os.path.basename(p)[:-4] for p in glob.glob(f"{D}/hw/*.hdr"))
    n = main(runs)
    print(f"{n} streams differ")
    sys.exit(1 if n else 0)
