#!/usr/bin/env python3
"""filt_step.py -- export the filt_cyc step responses for the filter searches.

tests/filt_cyc/hw/fc_<b>.{hdr,bin} stream k = filt_cyc case b*4+k (F, Q, A: rest -> 256 samples of A -> zeros).
Writes work/filt/step.bin: per stream int32 F, Q, A, on, N, y[N] with y = -MIXS/2 (1/8 sample units; MIXS is always
even with VOFF = 1) and `on` = the first sample that can see the input (filt_lp.ON: the first output change after
key-on -- 139 for batch 0, 138 for batches 1..4, 200 for batch 5).  Also writes work/filt/decay.bin (the older
DC-to-zero windows: per stream F, Q, A, N=1500, y[on+150 .. on+1650)) for filt_search6..8.
"""
import os, struct, sys
import numpy as np
sys.path.insert(0, os.path.dirname(__file__))
import cap
from filt_lp import CF, ON

def main():
    os.makedirs("work/filt", exist_ok=True)
    with open("work/filt/step.bin", "wb") as fs, open("work/filt/decay.bin", "wb") as fd:
        for i, (F, Q, A) in enumerate(CF):
            d = cap.load(f"tests/filt_cyc/hw/fc_{i // 4}")["data"][:, i % 4].astype(np.int64)
            assert not np.any(d & 1)
            y = (-d // 2).astype("<i4")
            fs.write(struct.pack("<5i", F, Q, A, ON[i], len(y)) + y.tobytes())
            w = y[ON[i] + 150:ON[i] + 1650]
            fd.write(struct.pack("<4i", F, Q, A, len(w)) + w.tobytes())
    print(f"work/filt/step.bin, work/filt/decay.bin: {len(CF)} streams")

if __name__ == "__main__":
    main()
