# case_aeg_koff -- console case `aeg_koff` + fitter `koff_fit` (S3 on the AEG, key-off in an attack, key-on during release)

Written 2026-09-23 (swarm agent, label case_aeg_koff).  No existing file was edited; nothing was run on the console.

## Files created

| Path | What |
|---|---|
| `cases/aeg_koff.c` | console/model case: runs koff_d2, koff_d2b, koff_att (16 cycles each), kon_rel (16 cycles); output `aeg_koff.txt`, `<run>.hdr/.bin` |
| `work/koff/koff_fit.cpp` | standalone integer AEG replay + hypothesis fitter (does not link the model); `-synth` self-test mode |
| `work/verify/s5/case_aeg_koff_model_{koff_d2,koff_d2b,koff_att,kon_rel}.txt` | fitter output on the model's own captures |
| `work/verify/s5/case_aeg_koff_synth_koff_att_{H0,H2,H3}.txt`, `..._koff_d2b_{H0,H3}.txt`, `..._kon_rel_{L1,L2,C1}.txt` | fitter self-tests: streams regenerated under an alternative rule |
| `tests/aeg_koff/model/` | the model run (captures + `aeg_koff.txt`) |
| `build/host/aeg_koff`, `build/hw/aeg_koff.elf`, `build/work/koff_fit` | binaries |

## Build / run commands (from `caique-rtl/model`)

```sh
./run_model.sh aeg_koff                                   # model run -> tests/aeg_koff/model/
bash -c 'source /opt/toolchains/dc/kos/environ.sh && make -C hw aeg_koff'    # console build only (build/hw/aeg_koff.elf), compiles clean
g++ -O2 -std=c++17 -o build/work/koff_fit work/koff/koff_fit.cpp
# fitter: <capture prefix> <c0 hex> <K> <kind> [aeg_koff.txt] [-v] [-synth H0|H2|H3|L1|L2|C1]
#   c0 0 + the case's text file: c0 and the stream table are taken from the file (otherwise built-in table = the case)
for r in koff_d2 koff_d2b koff_att kon_rel; do
  ./build/work/koff_fit tests/aeg_koff/model/$r 0 6491 $r tests/aeg_koff/model/aeg_koff.txt > work/verify/s5/case_aeg_koff_model_$r.txt
done
# console (after the orchestrator has run it):
for r in koff_d2 koff_d2b koff_att kon_rel; do ./build/work/koff_fit tests/aeg_koff/hw/$r 0 6491 $r tests/aeg_koff/hw/aeg_koff.txt; done
```
K is irrelevant for this case (all rates are R 48/52/56/62/63 = constant increment rows; the clock is the MDEC_CT
parity): the output with K 12345 is byte-identical to K 6491 (checked on koff_d2b and kon_rel).  The console's boot K
therefore does not need refitting first.

## Design -- and why it deviates from the task text

**The key-off sample cannot be found from the AEG level alone.**  A key-off from decay 2 into a release raises `a`
either way, so "S3 with the key-off on an even sample n" and "any rule with the key-off on the odd sample n+1" give
identical level sequences (the koff_same ambiguity of NOTES).  With the runs exactly as specified, H0 would have come
out FULL on the model's own captures with the key-off shifted by one sample, and the validation criterion
("H0 failing on at least some") could not be met by any fitter.  Marks are 30..100 samples off, so they cannot pin it.

**Witness slot.**  KYONEX applies the KYONB of every slot at once, so the write that keys the test slots OFF also keys
a WITNESS slot ON (slot 3 -> MIXS 3): AR 31 with KRS 1 (k = 1, s = 2 -> R 63 at pitch 1.0), whose level jumps to full
(a = 0, 520176) on the key-on sample (S2 + S5, both established for fresh key-ons).  The witness onset = the test
slots' key-off sample E_B.  The witness then decays to "off" on its own (D1R 31 to DL 31, D2R 31: ~256 samples), so
every witness key-on is a fresh one, never a key-on during release (which is one of the questions under test).
Pitfall found on the way: KRS 0 / FNS 0x200 also gives R 63 but FNS 0x200 = pitch 1.5, whose loop-end interpolation
halves one output sample per loop (eg_lock odd_* runs) -- that broke the witness stream's replay until KRS 1 / FNS 0.
The test slots' fresh key-on (496 = a 0x280 on the key-on sample, S2) pins the key-on sample E_A.  In kon_rel the
witness is keyed on by the write that keys the test slots on DURING their release, pinning that sample E_C.

Consequences: 3 test streams per run instead of 4.  Configs kept / dropped:
- koff_d2: (D2R,RR) = (26,24) (24,28) (28,0) [+2 vs +1, +1 vs +4, +4 vs hold]; dropped (0,26) -> moved to koff_d2b.
  Slot 2 (RR 0) never releases: its key-ons from cycle 1 on are key-ons during a held release (Q3, reported per cycle as
  "key-on L0/L1/L2").
- koff_d2b: (30,24) (26,30) (0,26) [+8 vs +1, +2 vs +8, 0 vs +2 = the rate-0 case]; dropped (24,30) and (30,26)
  (redundant with koff_d2's (24,28) and koff_d2b's (30,24)).
- koff_att: attack R 48/52/56 (inc 1/2/4) with RR 28/24/26 (inc 4/1/2) instead of RR 24/24/24/28: with R 48 + RR 24 the
  attack and release increments are both 1 and H0 = H1 there.  All three slots now separate H0/H1/H2/H3.
  On-time 200 + rand%300 us (cap_poll's ring read adds ~0.7 ms when a 64-sample block is due, so on-times are
  9..50 samples; the R 56 slot has finished its attack in ~3 of 16 cycles -> those are extra rate-0 cases).
- kon_rel: const AR 24 (R 48), ramp AR 24, ramp AR 31 (R 62), all RR 24; dropped the const AR 31 stream; 16 cycles
  (64 marks = CAP_MAXEV, marks 1/3/5/7 before the four writes).  The witness is keyed on at write C only.

Assumption the pin rests on: one KYONEX write applies the key-on and the key-off of different slots on the same
sample.  Support: eg_lock keys (identical onsets on 4 slots, 32 cycles), feg_track (one key-off sample shared by 3
slots in different segments, which is how S3 was found).  A mixed on/off write has not been measured before; if the
console applied key-offs one sample later than key-ons, the console would print exactly the S3 signature below
(indistinguishable) -- that is the residual caveat, stated once here.

## Model results (the fitter's summary tables on tests/aeg_koff/model)

Run lines from `tests/aeg_koff/model/aeg_koff.txt`:
```
koff_d2  : cap_start ... c0 ecde   43520 samples, errors 0, 64 marks
koff_d2b : cap_start ... c0 2d33   42048 samples, errors 0, 64 marks
koff_att : cap_start ... c0 73ea   40960 samples, errors 0, 64 marks
kon_rel  : cap_start ... c0 bea5   53760 samples, errors 0, 64 marks
```
Summary (reproduced / informative stream-cycles; informative = key event on a clock sample and the hypotheses'
predictions not all identical):
```
koff_d2  (16 cycles, E_B even 13 / odd 3):  H0 0/39   H1 39/39  H2 39/39  H3 0/39     no-fit 0
koff_d2b (16 cycles, E_B even 5 / odd 11):  H0 0/15   H1 15/15  H2 15/15  H3 5/15     no-fit 0   (H3's 5 = the rate-0 slot, where H1 = H3)
koff_att (16 cycles, E_B even 8 / odd 8):   H0 0/24   H1 24/24  H2 3/24   H3 3/24     no-fit 0   (the 3 = R 56 slot already past its attack)
kon_rel  (16 cycles, E_C even 13 / odd 3):  L0 48/48  L1 9/48   L2 39/48  | CA: C0 6/6  C1 0/6   no-fit 0
```
So on the model's own captures: H1 (S2 + S3 linear, also in the attack) and L0 / C0 are FULL on every informative
stream-cycle; H0 fails everywhere it differs, H3 fails except where inc_prev = 0, H2 (= H1 for decays) fails on every
attack key-off; L1 fails on every even E_C, L2 on every odd E_C, C1 on every odd E_C.  The whole cycle (key-on
segment, key-off, release to off, and in kon_rel the second attack and release) is reproduced sample by sample.

Every model timing landed where intended: koff_d2 key-offs at a 0x076..0x23c in decay 2 (< 0x240, level decoding
unambiguous), koff_d2b at a 0x040 (rate-0 slot) / 0x05x..0x1e0, koff_att at a 0x016..0x1cd inside the attack (except
the ~3 R 56 cycles), kon_rel second key-ons at release a 0x07d..0x11f.  No cycle needed a timing change.

Representative lines (model):
```
koff_d2 cycle 4: key-on E_A 11076 (even) key-off E_B 11280 (even, on 204 samples)
    s0: d2 at E_B a 0x0e8, inc_prev 2 inc_rel 1:  H0 fail@E_B+0  H1 FULL  H2 FULL  H3 fail@E_B+0   key-off samples reproducing: H0{+1,+2} H1{+0,+1} H2{+0,+1} H3{+1}
        observed: a 0e8 -> 0ea on E_B (+2), then 0eb on E_B+2
koff_att cycle 0: key-on E_A 450 (even) key-off E_B 470 (even, on 20 samples)
    s0: att at E_B a 0x162, inc_prev 1 inc_rel 4:  H0 fail@E_B+0  H1 FULL  H2 fail@E_B+0  H3 fail@E_B+0   key-off samples reproducing: H0{} H1{+0} H2{} H3{}
        observed: a 162 -> 163 on E_B (+1), then 167 on E_B+2         <- the attack went UP by inc_att (linear S3), then release +4
kon_rel cycle 0: E_A 449 (even); second key-on E_C 922 (odd, release a 0x0a9)
    s1 (ramp R48): L0 FULL  L1 FULL  L2 fail@E_C+0  | CA (under L0): C0 FULL  C1 fail@E_C+0
kon_rel cycle 1: E_C 4097 (even): L0 FULL  L1 fail@E_C+0  L2 FULL  | CA: uninformative (even E_C)
```

## Self-tests (`-synth`: streams regenerated from the pinned events under another rule, then fitted)

```
koff_att  -synth H0: H0 24/24  H1 0/24  H2 0/24  H3 0/24
koff_att  -synth H2: H0 0/24   H1 3/24  H2 24/24 H3 3/24
koff_att  -synth H3: H0 0/24   H1 3/24  H2 3/24  H3 24/24
koff_d2b  -synth H0: H0 15/15  H1 0/15  H2 0/15  H3 0/15
koff_d2b  -synth H3: H0 0/15   H1 5/15  H2 5/15  H3 15/15
kon_rel   -synth L1: L0 9/48   L1 48/48 L2 0/48   | C0 6/6 C1 0/6
kon_rel   -synth L2: L0 39/48  L1 0/48  L2 48/48  | C0 6/6 C1 0/6
kon_rel   -synth C1: L0 48/48  L1 9/48  L2 39/48  | C0 0/6 C1 6/6
```
Each alternative is picked out uniquely (the L verdict uses the best CA rule per L and vice versa, so Q3's two
sub-questions do not confound each other).

## Expected console signatures (per informative cycle, key-off E_B on an even sample)

Q1 (decay 2 -> release; `observed: a X -> Y on E_B, then Z on E_B+2`; `key-off samples reproducing` = offsets from E_B):
- S3 / H1 (the model): Y = X + inc_d2, Z = Y + inc_rel; sets H1{+0,+1} H0{+1,+2} H3{+1}; summary H1 = N/N, H0 0/N.
  Rate-0 slot (koff_d2b s2): Y = X (no step), Z = X + 2; H1 = H3 FULL, H0 fails.
- H0 (release increment already on the key-off clock): Y = X + inc_rel; sets H0{-1,+0} H1{-2,-1} H3{-1}; summary H0 = N/N.
- H3 (no step on the key-off sample): Y = X, Z = X + inc_rel; sets H3{+0}, all others {} (nothing else fits at any offset).
- Something else: "stream-cycles no H fits" > 0 and the `observed` / `levels` dump lines show the step; the free
  search sets show which offset each rule would need.
Q2 (attack -> release, koff_att):
- H1 linear: a goes UP by inc_att on E_B (against the attack's direction), then release; H1{+0} and all other sets empty.
- H2 attack formula: a goes DOWN by the attack step on E_B; H2{+0,+1} H0{+1,+2} H1{+1} H3{+1} (this one is ALSO what
  "key-off effect delayed to E_B+1" would produce -- see the assumption above).
- H0: Y = X + inc_rel; H3: Y = X.
Q3 (kon_rel, second key-on E_C pinned by the fresh witness):
- L0 (the model): 496 on E_C for the const stream whatever the parity; odd E_C: L2 fail@E_C+0 (L2 would still show the
  release level on E_C and 496 on E_C+1); even E_C: L1 fail@E_C+0 (L1 would show the first attack level 816 (R 48) on
  E_C instead of 496).  L0 = 48/48, L1 = (odd stream-cycles)/48, L2 = (even stream-cycles)/48.
- L1: no 496 on even E_C; L2: 496 first appears on the even sample after an odd E_C ("second key-on samples on odd
  MDEC_CT: 0" would also be a red flag).
- CA (ramp streams, odd E_C only): C0 -> the ramp level on E_C is level(0x280, s[0]) = -256; C1 -> level(0x280,
  s[old CA + 1]) (about -224..-176 for the release lengths used) and the whole later attack is one ramp sample behind.
- The koff_d2 slot 2 (RR 0, held release) prints "key-on L0/L1/L2" per cycle: the same question on a held level.
The parity counts printed at the end of each run ("witness key-off samples (E_B) on even/odd", "second key-on samples
(E_C) on even/odd") should both be non-zero over 16 cycles; all-even would mean the witness onset is itself clock-bound.

## Open / notes for the orchestrator

- Console run: `./run_hw.sh aeg_koff` (about 5 s of capture: 4 runs of ~1 s plus RAM writes), then the four fitter
  commands above with `tests/aeg_koff/hw/`.  `errors` must be 0 in every run line (cap.h header check).
- If a cycle prints `NOT FOUND`, the fitter dumps the first level change in the mark window for diagnosis (e.g. a
  witness that does not reach 520176 on its key-on sample).
- The fitter is a standalone re-derivation (NOTES formulas only): the increment table with rows 5/9/13 =
  {b,2b,b,b,b,2b,b,b}, the R < 48 offset -1, the D1 -> D2 equality on a[9:5] checked after the D1 step, the R 63
  key-on (a = 0, attack left on the next clock), the level law and the ramp level law with CA at pitch 1.0.
- Another agent's `work/verify/s5/aeg_koff_predict.*` (a sequence predictor for the task's original 4-slot design) is
  unrelated to these files.
