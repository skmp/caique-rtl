#!/usr/bin/env python3
"""filt_imp.py -- first-sample impulse responses from tests/filt_imp: for each slot (cutoff) and impulse amplitude A,
y0 (the first output, 1/8 sample units, sign restored: y0 = -MIXS/2), y1, and whether the state was at rest.
    filt_imp.py [hw|model] -> work/filt/imp_<F>.txt  (A y0 y1 y2 rest)"""
import sys, os
sys.path.insert(0, os.path.dirname(__file__))
import numpy as np, cap

D = os.path.join(os.path.dirname(__file__), "..", "tests", "filt_imp")
FV = [0x1C00, 0x1D55, 0x1E80, 0x1F55]

def extract(plat="hw"):
    out = {F: [] for F in FV}
    for r in range(4):
        amps = np.fromfile(f"{D}/{plat}/amps_{r}.bin", dtype="<i2").astype(int)
        c = cap.load(f"{D}/{plat}/fm_{r}")
        k2 = dict(c["events"])[2]
        for k, F in enumerate(FV):
            y = c["data"][:, k].astype(int)
            # marker impulse (A=16384): first sample after key-on deviating by > 1000
            p = k2 + int(np.argmax(np.abs(y[k2:]) > 1000))
            for i, A in enumerate(amps):
                q = p + 128 * i
                if q + 3 >= len(y): break
                rest = all(v == 0 for v in y[q - 8:q])
                out[F].append((int(A), -y[q] // 2, -y[q + 1] // 2, -y[q + 2] // 2, rest))
    return out

if __name__ == "__main__":
    plat = sys.argv[1] if len(sys.argv) > 1 else "hw"
    res = extract(plat)
    os.makedirs("work/filt", exist_ok=True)
    for F, rows in res.items():
        with open(f"work/filt/imp_{plat}_{F:04x}.txt", "w") as f:
            for row in sorted(set(rows)):
                f.write("%d %d %d %d %d\n" % (row[0], row[1], row[2], row[3], row[4]))
        nr = sum(1 for r in rows if r[4])
        print(f"F {F:04x}: {len(rows)} impulses, {nr} from rest")
