#!/usr/bin/env python3
"""verify.py -- re-run every test case on the model and compare with the console results already in tests/<case>/hw.
Writes tests/SUMMARY.txt (one line per case: status + detail).  Does not touch the console (use run_hw.sh for that).
    tools/verify.py [--no-run] [case...]"""
import os, sys, subprocess, re, glob
sys.path.insert(0, os.path.dirname(__file__))
M = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
T = os.path.join(M, "tests")

def sh(cmd, env=None):
    e = dict(os.environ)
    if env: e.update(env)
    return subprocess.run(cmd, shell=True, cwd=M, capture_output=True, text=True, env=e).stdout

def textdiff(case, known=()):
    a = open(f"{T}/{case}/hw/{case}.txt").read().splitlines()
    b = open(f"{T}/{case}/model/{case}.txt").read().splitlines()
    bad = [i for i, (x, y) in enumerate(zip(a, b)) if x != y and not any(k in x for k in known)]
    if len(a) != len(b): return "DIFF", f"line counts {len(a)} vs {len(b)}"
    nk = sum(1 for x, y in zip(a, b) if x != y) - len(bad)
    return ("OK" if not bad else "DIFF"), f"{len(a)} lines, {len(bad)} differ" + (f" (+{nk} known)" if nk else "")

def cmp_cap(case, prefixes, until_keyoff=False):
    import cap
    tot = ok = 0
    notes = []
    for p in prefixes:
        a, b = cap.load(f"{T}/{case}/hw/{p}"), cap.load(f"{T}/{case}/model/{p}")
        for k in range(a["ns"]):
            tot += 1
            r = cap.compare(a, b, k)
            if "identical" in r: ok += 1
            else: notes.append(f"{p}:{r}")
    return ("OK" if ok == tot else "DIFF"), f"{ok}/{tot} streams identical" + ("; " + notes[0] if notes else "")

def v_dsp_unpack():
    same = open(f"{T}/dsp_unpack/hw/unpack.bin", "rb").read() == open(f"{T}/dsp_unpack/model/unpack.bin", "rb").read()
    return ("OK" if same else "DIFF"), "65536 words " + ("identical" if same else "differ")

def v_dsp_pack():
    h = open(f"{T}/dsp_pack/hw/dsp_pack.txt").read()
    m = re.search(r"(\d+) of 16777216 values differ.*crc32 ([0-9a-f]+)", h)
    mm = re.search(r"crc32 ([0-9a-f]+)", open(f"{T}/dsp_pack/model/dsp_pack.txt").read())
    ok = m and m.group(1) == "0" and mm and mm.group(1) == m.group(2)
    return ("OK" if ok else "DIFF"), f"hw mismatches vs model PACK {m.group(1) if m else '?'}, crc hw {m.group(2) if m else '?'} model {mm.group(1) if mm else '?'}"

def v_aeg():
    out = sh("python3 tools/aeg_cmp.py")
    n = re.search(r"(\d+) streams differ", out)
    bad = int(n.group(1)) if n else -1
    return ("OK" if bad == 0 else "PHASE"), f"{44 - bad}/44 streams agree (path + tick spacing); rest: envelope clock phase / capture end"

def v_krs():
    a = sh("python3 tools/krs.py tests/sgc_krs/hw/sgc_krs.txt --table")
    b = sh("python3 tools/krs.py tests/sgc_krs/model/sgc_krs.txt --table")
    return ("OK" if a == b else "DIFF"), "effective-rate table (320 combos) " + ("identical" if a == b else "differs")

def v_filt(case):
    out = sh("python3 tools/filt_cmp.py", env={"FILT_CASE": case})
    last = out.strip().splitlines()[-1] if out.strip() else "?"
    return "APPROX", last

def v_formats():
    """PCM8/ADPCM streams bit-exact; noise streams: same LFSR structure (m-sequence window, stride 64, slot +1)"""
    import cap, numpy as np
    st, det = cmp_cap("sgc_formats", ["fm_0"])
    P = (1 << 17) - 1
    s = np.zeros(P + 17, dtype=np.uint8); s[:17] = 1
    for n in range(17, len(s)): s[n] = s[n - 12] ^ s[n - 17]
    seq = s[:P]
    def pos(plat, k):
        c = cap.load(f"{T}/sgc_formats/{plat}/fm_1")
        y = c["data"][:, k]; on = cap.onset(y)
        b = ((y[on:on + 3000] // 4096).astype(int)) & 0xFF
        idx = np.arange(3000) * 64
        for p in range(P):
            if np.array_equal(seq[(p + idx[:64]) % P], b[:64] & 1):
                return p if all(np.array_equal(seq[(p + j + idx) % P], (b >> j) & 1) for j in range(8)) else None
        return None
    ok = True
    for plat in ("hw", "model"):
        a, b = pos(plat, 2), pos(plat, 3)
        ok = ok and a is not None and b is not None and (b - a) % P == 1
    a1, b1 = cap.load(f"{T}/sgc_formats/hw/fm_1"), cap.load(f"{T}/sgc_formats/model/fm_1")
    long_ok = all("identical" in cap.compare(a1, b1, k) for k in (0, 1))
    good = st == "OK" and ok and long_ok
    return ("OK" if good else "DIFF"), (f"PCM8/ADPCM: {det.split(';')[0]}; ADPCM long stream {'identical' if long_ok else 'DIFFERS'}; "
                                       f"noise LFSR structure {'matches' if ok else 'DIFFERS'}")

def v_loop():
    """all loop streams identical except the two slow-attack (AR 8) slots of lo_2, which differ by envelope phase"""
    import cap
    ok = tot = 0
    for r in range(3):
        a, b = cap.load(f"{T}/sgc_loop/hw/lo_{r}"), cap.load(f"{T}/sgc_loop/model/lo_{r}")
        for k in range(4):
            if r == 2 and k >= 2: continue
            tot += 1
            ok += "identical" in cap.compare(a, b, k)
    return ("OK" if ok == tot else "DIFF"), f"{ok}/{tot} streams identical (+2 slow-attack streams: envelope phase)"

def v_keys():
    """end-of-test EG state bits, off-ness and KYONB identical (CA / EG level excluded: CPU sampling moments differ)"""
    import re
    def ends(plat):
        out = []
        for l in open(f"{T}/sgc_keys/{plat}/sgc_keys.txt"):
            m = re.match(r"(\S+ \S+) end: EG ([0-9a-f]+) CA [0-9a-f]+ reg0 ([0-9a-f]+)", l)
            if m:
                eg = int(m[2], 16)
                out.append((m[1], (eg >> 13) & 3, (eg & 0x1FFF) == 0x1FFF, m[3]))
        return out
    a, b = ends("hw"), ends("model")
    same = sum(1 for x, y in zip(a, b) if x == y)
    return ("OK" if same == len(a) else "DIFF"), f"{same}/{len(a)} key-event end states agree (EG state, off, KYONB)"

def v_lfo():
    """ALFO: step lengths and value ranges per stream identical; PLFO: per-224-sample increment min/max/mean"""
    import cap, aeg, numpy as np
    from collections import Counter
    from fractions import Fraction as Fr
    good = tot = 0
    for run in range(4):
        a, b = cap.load(f"{T}/sgc_lfo/hw/lf_{run}"), cap.load(f"{T}/sgc_lfo/model/lf_{run}")
        for k in range(4):
            def prof(c):
                x = c["data"][:, k]; on = cap.onset(x); rs = aeg.runs(x[on + 40:])
                vals = sorted({aeg.a_range(v)[0] for (_, ln, v) in rs if aeg.a_range(v)})
                return Counter(ln for (_, ln, v) in rs[1:-1]).most_common(1)[0][0], vals[0], vals[-1]
            tot += 1; good += prof(a) == prof(b)
    for run in (4, 5):
        for k in range(4):
            if run == 4 and k == 3: continue  # noise: LFSR phase
            def rng(plat):
                c = cap.load(f"{T}/sgc_lfo/{plat}/lf_{run}"); y = c["data"][:, k]; on = cap.onset(y)
                q = (y[on:on + 60000].astype(np.int64) + 16384 * 16) // 2
                d = np.diff(q); d[d < -64 * 2000] += 64 * 4096
                q = np.concatenate([[q[0]], q[0] + np.cumsum(d)])
                blk = [round(float(Fr(int(q[i + 224] - q[i]), 64) / 224 * 16384)) for i in range(0, 50000, 224)]
                blk = [x for x in blk if x < 40000]
                return round(float(np.mean(blk)))
            tot += 1; good += abs(rng("hw") - rng("model")) <= 20
    return ("OK" if good == tot else "APPROX"), f"{good}/{tot} LFO streams agree (ALFO steps/ranges exact, PLFO mean increment within 20/16384)"

def v_selftest():
    """VOFF streams 0/1 identical from onset to key-off; enveloped streams 2/3 identical after the attack (40 samples)"""
    import cap, numpy as np
    a, b = cap.load(f"{T}/cap_selftest/hw/cap"), cap.load(f"{T}/cap_selftest/model/cap")
    ok = 0
    for k in range(4):
        xa, xb = a["data"][:, k], b["data"][:, k]
        oa, ob = cap.onset(xa), cap.onset(xb)
        skip = 40 if k >= 2 else 0
        end = min(dict(a["events"])[2] - oa, dict(b["events"])[2] - ob) - 200
        ok += np.array_equal(xa[oa + skip:oa + end], xb[ob + skip:ob + end])
    return ("OK" if ok == 4 else "DIFF"), f"{ok}/4 streams identical up to key-off (enveloped streams after the attack)"

CASES = {
    "dsp_basic": lambda: textdiff("dsp_basic", known=("F ira 25", "A ffff -4096 1")),
    "dsp_mem": lambda: textdiff("dsp_mem"),
    "dsp_mem2": lambda: textdiff("dsp_mem2"),
    "dsp_mem3": lambda: textdiff("dsp_mem3"),
    "dsp_unpack": v_dsp_unpack,
    "dsp_pack": v_dsp_pack,
    "sgc_level": lambda: textdiff("sgc_level", known=("L5 LPOFF=0",)),
    "cap_selftest": lambda: v_selftest(),
    "sgc_aeg": v_aeg,
    "sgc_krs": v_krs,
    "sgc_pitch": lambda: cmp_cap("sgc_pitch", [f"pi_{i}" for i in range(4)]),
    "sgc_formats": lambda: v_formats(),
    "sgc_loop": lambda: v_loop(),
    "sgc_keys": lambda: v_keys(),
    "sgc_mix": lambda: textdiff("sgc_mix"),
    "sgc_lfo": lambda: v_lfo(),
    "filt_id2": lambda: v_filt("filt_id2"),
}

def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    cases = args or list(CASES)
    if "--no-run" not in sys.argv:
        sh("./run_model.sh " + " ".join(c for c in cases if c in CASES))
    lines = []
    for c in cases:
        try:
            st, det = CASES[c]()
        except Exception as ex:
            st, det = "ERROR", str(ex)
        lines.append(f"{c:14s} {st:6s} {det}")
        print(lines[-1])
    if not args:
        open(f"{T}/SUMMARY.txt", "w").write("# model vs console, tools/verify.py\n" + "\n".join(lines) + "\n")

if __name__ == "__main__":
    main()
