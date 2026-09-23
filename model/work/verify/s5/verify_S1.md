# verify_S1 — report (2026-09-23)

Task: independent re-derivation of claim S1 (HANDOVER.md: envelope clock on even MDEC_CT, eg_cnt = K - MDEC_CT/2
mod 2^14, one K per boot) from the capture files alone, without the project's envelope tools, then compare.

## Files created (all under caique-rtl/model)

- `work/verify/s5/s1_indep.cpp` — the tool (own CAP1 header parser, MDEC_CT per sample = (c0 - n_first - i) & 0xFFFF,
  level-law inversion, brute-force K fit over [0, 16384) per stream with OpenMP, controls, timing-law listing)
- `work/verify/s5/s1_indep_output.txt` — its full output (210 lines)
- `work/verify/s5/S1.md` — verdicts per sub-claim with the numbers
- `work/verify/s5/eg_phase_eg_lock_rerun.txt`, `eg_phase_eg_lock_rerun_nov.txt`, `eg_phase_dl0_rerun.txt` — the
  project tool's outputs used for the comparison
- binary: `build/work/s1_indep`

No existing file was edited; the console was not touched; nothing in /tmp.

## Commands (from caique-rtl/model)

    g++ -O2 -std=c++17 -fopenmp -o build/work/s1_indep work/verify/s5/s1_indep.cpp
    build/work/s1_indep > work/verify/s5/s1_indep_output.txt        # 1.3 s wall, 27 s CPU
    build/tools/eg_phase -v work/eg/eg_lock_runs.txt > work/verify/s5/eg_phase_eg_lock_rerun.txt
    build/tools/eg_phase    work/eg/eg_lock_runs.txt > work/verify/s5/eg_phase_eg_lock_rerun_nov.txt
    build/tools/eg_phase -v                          > work/verify/s5/eg_phase_dl0_rerun.txt
    diff work/verify/expected/eg_phase_eg_lock.txt work/verify/s5/eg_phase_eg_lock_rerun_nov.txt   # SAME
    diff work/verify/expected/eg_phase_dl0.txt     work/verify/s5/eg_phase_dl0_rerun.txt           # SAME

## Data and setup

| run | path | c0 | n_first | onset (MDEC_CT) | mark 3 | window (samples/stream) | streams |
|---|---|---|---|---|---|---|---|
| att_slow | tests/eg_lock/hw/att_slow | 44c9 | 591 | 458 (40b0 even) | 485572 | 484,914 | AR 6/4/2/1 -> R 12/8/4/2, D1R 0, RR 31 |
| att_mid | tests/eg_lock/hw/att_mid | 3f0e | 585 | 458 (3afb odd) | 88653 | 87,995 | AR 22/20/18/16 -> R 44/40/36/32 |
| dl0 | tests/aeg_dl0/hw/dl0 | 87bb | 652 | 461 (8362 even) | 18120 | 17,459 | AR 31/31/31/20, D1R 31/20/10/31, DL 0, D2R 0/0/0/10, RR 31 -> R 62/62/62/40, 62/40/20/62, -, 20 |

c0 checked against the `cap_start:` lines; 0 counter errors in every header; the four streams of a run share the onset;
every onset level is 496 = LV[0x280].  Window = onset .. mark 3 - 200 (decay/release never reached in att_*).

Level law facts found on the way: LV[0] 520176, LV[0x280] 496, LV[0x3FF] 0; the levels are pairwise distinct for
a <= 0x23F only (the task said ~0x300; at k = 9, M 126/125 collide).  Here that only affects the key-on level
(496 <-> a 0x27F..0x282, taken as 0x280) and the first inc-1 attack step (816 <-> 0x257..0x258); every later a is unique.

## Findings

### (a) Step-sample parity — CONFIRMED
690 step samples (level changes after the onset) over the 12 streams: **690 on even MDEC_CT, 0 on odd**.
Per stream: att_slow 66/66/66/66, att_mid 66/66/66/66, dl0 10/9/9/134, all even.  First steps: att_slow s0 i 1478
(3cb4), s1 966 (3eb4), s2 1990 (3ab4), s3 8134 (22b4); att_mid s0 459 (3afa), s1 461 (3af8), s2 473 (3aec), s3 481
(3ae4); dl0 all streams 463 (8360).  The "odd" claim is refuted by every stream.

Tick condition alone (no simulation; R < 48 attack streams; step needs (K - MDEC_CT/2 - 1) = 0 mod 2^(11 - R/4)):
each stream yields ONE residue over all its steps: att_slow 91 mod 256, 347 mod 512, 347 mod 1024, 347 mod 2048;
att_mid 1 mod 2, 3 mod 4, 3 mod 8; all satisfied by K = 6491 (and, with the offset dropped, by 6490).

### (b) K fit — CONFIRMED, K in {6491, 14683} only
Brute force over all 16384 K per stream (clock on even MDEC_CT, cnt = (K - MDEC_CT/2) & 0x3FFF, R < 48 rows on cnt-1
with row R&3 at ((cnt-1) >> shift) & 7, R >= 48 rows 4+(R-48) at cnt & 7 with rows 5/9/13 = {b,2b,b,b,b,2b,b,b},
attack a += (~a*inc)>>4 to 0 then decay 1, decay 1 -> 2 on a[9:5] == DL after the step, no step on the key-on sample):
every stream is reproduced in full by some K.

- att_slow: s0 K = 347 mod 512 (32 values), s1 347 mod 1024 (16), s2 347 mod 2048 (8), s3 **6491 mod 8192 (2)**;
  run: {6491, 14683}
- att_mid: s0 1 mod 2, s1 3 mod 4, s2 3 mod 8, s3 11 mod 16; run: 11 mod 16 (1024 values)
- dl0: s0 all 16384 (R 62 everywhere: cnt-insensitive), s1 12288 (the first decay-1 clock must NOT step at R 40),
  s2 16256 (R 20 must not step on that clock), s3 91 mod 128; run: 91 mod 128 (128 values)
- **all 12 streams: {6491, 14683}** — exactly the expected pair (rate 2 pins mod 8192).

The three runs sit at unrelated ring positions (c0 44c9 / 3f0e / 87bb, onsets even / odd / even), so one K fitting
all of them is the ring-lock evidence, not a per-run phase fit.

### (c) Controls
- **No -1 offset on R < 48 rows**: at K = 6491 fails at the first tick of every R < 48 stream (att_slow +1018 / +506 /
  +1530 / +7674 with the model stepping 2 samples early; att_mid +1 / +1 / +13 / +21; dl0 s3 +2; dl0 s0/s1/s2 pass).
  With K free it reproduces everything at **K = 6490 / 14682**: all rates here are R < 48 or row 16 (all 8s), so the
  control only relabels K by -1 — VACUOUS as a discriminator on these runs.  The offset is S4's claim (joint AEG+FEG
  fit), not decidable from S1's AEG-only data.
- **Clock on odd MDEC_CT** (K free): no K reproduces any stream; best match = the sample before the first hardware step
  (att_slow s3 7676/484914 at K 6233; att_mid s0 1/87995; dl0 s0 1/17459 with the model stepping on MDEC_CT 8361 odd).
  FAILS everywhere.
- **YM2612 rows 5/9/13**: no stream here uses rows 5/9/13 (R 45/49/53/57 absent) — VACUOUS; K set unchanged
  ({6491, 14683}).  This control is meaningful only on the odd-rate runs (S4).
- **K off by one**: K-1 (6490 / 14682): the model holds where the hardware steps — att_slow 1020 / 508 / 1532 / 7676,
  att_mid 1 / 3 / 1 / 23, dl0 s3 2, dl0 s1 20 (model a = 1 in decay 2, level 516080, hw 520176); K+1 (6492 / 14684):
  the model steps 2 samples early — att_slow 1018 / 506 / 1530 / 7674, att_mid 1 / 1 / 13 / 21, dl0 s3 2.  dl0 s0/s2
  (and s1 for K+1) pass because R 62 / R 20 are cnt-insensitive over this window.  FAILS on every rate that can see it.

### (d) Timing law, att_slow stream 3 (R 2) — CONFIRMED
First 12 step samples: i 8134, 12230, 16326, 24518, 28614, 32710, 40902, 44998, 49094, 57286, 61382, 65478
(MDEC_CT 22b4, 12b4, 02b4, e2b4, d2b4, c2b4, a2b4, 92b4, 82b4, 62b4, 52b4, 42b4 — all even); spacings 4096, 4096, 8192,
4096, 4096, 8192, 4096, 4096, 8192, 4096, 4096.  At K = 6491 every step has (cnt-1) & 0x7FF = 0 and row index
1,2,3,5,6,7,1,2,3,5,6,7 (row 2 = {0,1,1,1,0,1,1,1}: indices 0 and 4 are the zeros, hence the 8192 gaps; period 4
indices = 8192 clocks = why bit 13 of K is invisible).  Whole window: 66 steps, 0 steps without a predicted tick,
0 predicted ticks without a step during the attack; 22 predicted ticks after the attack reached a = 0 (i 360390,
+359932) fall in decay 1 with D1R 0.

### Comparison with eg_phase (run afterwards)
`expected/eg_phase_eg_lock.txt` reproduces byte for byte (the expected file was made without -v; with -v only the
"first mismatch" lines for the odd_* runs are added); `expected/eg_phase_dl0.txt` reproduces byte for byte with -v.
Every residue agrees with mine: att_slow s3 {6491, 14683} (slow_off -1) / {6490, 14682} (slow_off 0); s0..s2 sets =
347 mod 512/1024/2048; att_mid = 1 mod 2, 3 mod 4, 3 mod 8, 11 mod 16; dl0 s3 {91,219,347,475} mod 512 = 91 mod 128;
dl0 s1 12/16 residues, s2 508/512.

## Verdict on S1

**CONFIRMED**: clock on even MDEC_CT (690/690), a single counter constant across three runs at different ring
positions (K in {6491, 14683}, nothing else), the R 2 timing pattern 4096/4096/8192 at the fitted K.  Caveats: the
numeric K is bound to the -1 offset convention for R < 48 rows (offset 0 with K 6490 is the same law on these runs);
the YM-row control is vacuous here.  Nothing in the three runs contradicts any part of S1.  Details: `work/verify/s5/S1.md`.
