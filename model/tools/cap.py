#!/usr/bin/env python3
"""cap.py -- load caique captures (cases/cap.h: <name>.hdr + <name>.bin) and compare console vs model.

    cap.py cmp tests/<case>/hw/<name> tests/<case>/model/<name> [--align onset|events]
"""
import sys
import numpy as np

def load(prefix):
    h = np.fromfile(prefix + ".hdr", dtype="<u4")
    assert h[0] == 0x31504143, "bad magic"
    ns, n, n_first, errors, first_err, nev = (int(x) for x in h[1:7])
    mixs = [int(x) for x in h[7:11]][:ns]
    ev = [(int(h[11 + 2 * i]), int(h[12 + 2 * i]) - n_first) for i in range(nev)]
    d = np.fromfile(prefix + ".bin", dtype="<i4").reshape(-1, ns)[:n]
    return dict(ns=ns, n=n, errors=errors, first_err=first_err, mixs=mixs, events=ev, data=d)

def onset(x):
    nz = np.nonzero(x)[0]
    return int(nz[0]) if len(nz) else None

def compare(a, b, stream, label=""):
    xa, xb = a["data"][:, stream], b["data"][:, stream]
    oa, ob = onset(xa), onset(xb)
    if oa is None or ob is None:
        return f"{label}stream {stream}: silent (hw onset {oa}, model onset {ob})"
    m = min(len(xa) - oa, len(xb) - ob)
    da, db = xa[oa:oa + m], xb[ob:ob + m]
    diff = np.nonzero(da != db)[0]
    if len(diff) == 0:
        return f"{label}stream {stream}: {m} samples from onset identical"
    i = int(diff[0])
    return (f"{label}stream {stream}: {len(diff)}/{m} samples differ; first at +{i}: hw {int(da[i])} model {int(db[i])}; "
            f"max |diff| {int(np.max(np.abs(da.astype(np.int64) - db)))}")

if __name__ == "__main__":
    if len(sys.argv) >= 4 and sys.argv[1] == "cmp":
        a, b = load(sys.argv[2]), load(sys.argv[3])
        print(f"hw: {a['n']} samples, counter errors {a['errors']}; model: {b['n']} samples, errors {b['errors']}")
        for k in range(a["ns"]):
            print(compare(a, b, k))
    else:
        print(__doc__)
