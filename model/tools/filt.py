#!/usr/bin/env python3
"""filt.py -- filter identification from tests/filt_id captures.

Structure found: Chamberlin state-variable low-pass, output negated at MIXS:
    band += f * (x - low - q * band);  low += f * band;  MIXS = -16 * low
f from FLV: e = FLV >> 9, m = FLV & 0x1FF, f = (1 + m / 512) * 2^(e - 16).  q per Q fitted here.
    filt.py [hw|model] fit        -- float fit of q for every (F, Q), residual stats
"""
import sys, os, re
sys.path.insert(0, os.path.dirname(__file__))
import numpy as np, cap

D = os.path.join(os.path.dirname(__file__), "..", "tests", os.environ.get("FILT_CASE", "filt_id"))
TXT = os.environ.get("FILT_CASE", "filt_id") + ".txt"

def f_of(F):
    e, m = F >> 9, F & 0x1FF
    return (1 + m / 512.0) * 2.0 ** (e - 16)

def load(plat="hw"):
    tab = {}
    for l in open(f"{D}/{plat}/{TXT}"):
        m = re.match(r"fi_(\d+) stream (\d+): F ([0-9a-f]+) Q (\d+)", l)
        if m: tab[(int(m[3], 16), int(m[4]))] = (int(m[1]), int(m[2]))
    x = np.fromfile(f"{D}/{plat}/input.bin", dtype="<i2").astype(np.float64)
    return tab, x

def segment(plat, tab, F, Q):
    f, k = tab[(F, Q)]
    c = cap.load(f"{D}/{plat}/fi_{f}")
    y = c["data"][:, k].astype(np.float64)
    ev = dict(c["events"])
    return y, ev[2]

def align(y, k2, x):
    """index in y of input sample 0: the impulse (input index 64) is the first big excursion after key-on"""
    base = y[k2 - 5]
    i = k2 + int(np.argmax(np.abs(y[k2:] - base) > 100))
    return i - 64 - 1  # response to x[n] shows at y[n + 1 + start]? refined by fit

def svf(x, f, q, low0=0.0, band0=0.0):
    low, band = low0, band0
    out = np.empty(len(x))
    for n in range(len(x)):
        band += f * (x[n] - low - q * band)
        low += f * band
        out[n] = low
    return out

def capture_align(plat, tab, fi):
    """input-index-0 position in capture fi, from its most responsive stream"""
    c = cap.load(f"{D}/{plat}/fi_{fi}")
    k2 = dict(c["events"])[2]
    best = None
    for k in range(c["ns"]):
        y = c["data"][:, k].astype(np.float64)
        dev = np.abs(y[k2:k2 + 400] - y[k2 - 5])
        if best is None or dev.max() > best[0]:
            best = (dev.max(), k2 + int(np.argmax(dev > 0.1 * dev.max())))
    return best[1] - 64 - 1

def fit(plat="hw", fmin=0x1400):
    tab, x = load(plat)
    al = {}
    for (F, Q) in sorted(tab):
        if F < fmin: continue
        fi = tab[(F, Q)][0]
        if fi not in al: al[fi] = capture_align(plat, tab, fi)
        y, k2 = segment(plat, tab, F, Q)
        s = al[fi]
        f = f_of(F)
        best = None
        L = min(len(x), len(y) - s - 3)
        for d in (-1, 0, 1, 2):
            yy = -y[s + d:s + d + L] / 16.0
            y0 = yy[:60].mean()
            for q in np.linspace(0.0, 2.0, 401):
                sim = svf(x[:1200], f, q, low0=y0)
                err = np.abs(sim - yy[:1200]).max()
                if best is None or err < best[0]: best = (err, q, d)
        err, q, d = best
        # full-length residual at the best q
        yy = -y[s + d:s + d + L] / 16.0
        sim = svf(x[:L], f, q, low0=yy[:60].mean())
        r = np.abs(sim - yy)
        print(f"F {F:04x} Q {Q:2d}: f {f:.6f} q {q:.3f} delay {d}  max|res| {r.max():8.3f}  mean {r.mean():.4f}")

if __name__ == "__main__":
    plat = sys.argv[1] if len(sys.argv) > 1 else "hw"
    fit(plat, int(sys.argv[2], 16) if len(sys.argv) > 2 else 0x1400)


def export(plat, out_bin, out_in):
    """aligned per-dataset MIXS (8-sample margin) for tools/filt_search*.cpp"""
    import struct
    tab, x = load(plat)
    al = {}
    with open(out_bin, "wb") as out:
        for (F, Q) in sorted(tab):
            fi = tab[(F, Q)][0]
            if fi not in al: al[fi] = capture_align(plat, tab, fi)
            y, k2 = segment(plat, tab, F, Q)
            s = al[fi] + 1 - 8
            L = min(len(x) + 8, len(y) - s - 3)
            out.write(struct.pack("<iiii", F, Q, L, int(y[s])))
            out.write(np.asarray(y[s:s + L], dtype="<i4").tobytes())
    np.asarray(x, dtype="<i2").tofile(out_in)
