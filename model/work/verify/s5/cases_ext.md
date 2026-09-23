# cases_ext -- two console cases for the remaining key-off gaps: AEG decay 1 -> release (`aeg_koff koff_d1`) and FEG attack -> release with opposite directions (`feg_koffatt`)

Written 2026-09-23 (swarm agent, label cases_ext).  Both cases run on the model and compile for the console (KOS);
nothing was run on the console.  Model state at the time of the runs: `src/aica_model.cpp` of 18:06:22 (another
agent's F1/F2/F4/F5 changes landed while this task ran: `feg_prev_dir` = "one more step of the old segment, old
direction" for the FEG, "no step from an attack" for the AEG, decay 1 -> release assumed like decay 2, the 0x3C0 fetch
stop).  All model results below were (re)produced against that source.

## Files

| Path | Change |
|---|---|
| `cases/aeg_koff.c` | + run `koff_d1` (header comment + runs table entry; seed 781); the other four runs are untouched |
| `work/koff/koff_fit.cpp` | + kind `koff_d1` (built-in table, the text-file table works as before); c0 parse fixed (see below); usage text |
| `cases/feg_koffatt.c` | NEW case: FEG key-off on a clock from an attack, key-off sample pinned by an AEG witness (8 batches `ka_0..ka_7`) |
| `work/koffatt/koffatt_check.cpp` | NEW checker: FEG tracker without a reference stream, witness pin, four readings of the key-off clock, u window, summary |
| `work/koffatt/feg_law.h` | copy of `work/verify/s5/feg_law.h` (the standalone FEG law with the mechanisms; keep the two in step) |
| `work/koff/koff_fit_model_{koff_d2,koff_d2b,koff_d1,koff_att,kon_rel}.txt` | fitter output on the model captures (c0 from the text file) |
| `work/koffatt/koffatt_check_model.txt` | checker output on the model captures |
| `tests/aeg_koff/model/`, `tests/feg_koffatt/model/` | model runs (`aeg_koff.txt` now has 5 runs; `feg_koffatt.txt`, `ka_<b>.hdr/.bin`, `input.bin`) |
| binaries | `build/host/aeg_koff`, `build/hw/aeg_koff.elf`, `build/host/feg_koffatt`, `build/hw/feg_koffatt.elf`, `build/work/koff_fit`, `build/work/koffatt_check` |

Not touched: `src/`, `tools/`, other cases, `work/verify/s5/koffdir_check.cpp` (but see "Leftovers": it has the same
c0 bug and lost the hw kd_7 batch).

## Commands for the orchestrator (from `caique-rtl/model`)

```sh
# console (about 6 s of capture for aeg_koff: 5 runs of ~1 s; ~1 s for feg_koffatt: 8 batches of ~120 ms)
./run_hw.sh aeg_koff feg_koffatt

# builds (already done in this tree; repeat after a checkout)
g++ -O2 -std=c++17 -o build/work/koff_fit work/koff/koff_fit.cpp
g++ -O2 -std=c++17 -o build/work/koffatt_check work/koffatt/koffatt_check.cpp

# (A) AEG: all five runs, c0 and the stream table from the case's text output (pass 0 as c0)
for r in koff_d2 koff_d2b koff_d1 koff_att kon_rel; do
  ./build/work/koff_fit tests/aeg_koff/hw/$r 0 6491 $r tests/aeg_koff/hw/aeg_koff.txt > work/koff/koff_fit_hw_$r.txt
done
grep -H "^SUMMARY\|c0 " work/koff/koff_fit_hw_*.txt      # every header line must show the run's own c0 (never 0000)

# (B) FEG attack -> release
./build/work/koffatt_check tests/feg_koffatt/hw > work/koffatt/koffatt_check_hw.txt; tail -14 work/koffatt/koffatt_check_hw.txt
```
K: neither case depends on it.  aeg_koff uses only R >= 48 rows (constant increments per clock; the fitter's output is
byte-identical for any K, checked in case_aeg_koff.md), feg_koffatt uses R 52 / 56 only.  `-K` exists on the checker
for completeness (`koffatt_check <dir> -K 6491`).  The old console captures in `tests/aeg_koff/hw` are overwritten by
the re-run (intended).

## (A) `koff_d1` -- decay 1 -> release on a clock

Run added to `cases/aeg_koff.c` (same harness: constant 0x7FFF, TL 0, VOFF 0, LPOFF 1, IMXL 15, ISEL k -> MIXS k,
witness slot 3 = AR 31 KRS 1 -> R 63, keyed ON by the KYONEX that keys the test slots OFF; 16 cycles; marks 1/2
around the key-on write, 3/4 around the key-off write):

| stream | AR | D1R | DL | D2R | RR | decay 1 / release per clock |
|---|---|---|---|---|---|---|
| 0 | 31 | 26 | 31 | 0 | 24 | +2 / +1 |
| 1 | 31 | 24 | 31 | 0 | 28 | +1 / +4 |
| 2 | 31 | 28 | 31 | 0 | 26 | +4 / +2 |
| 3 (witness) | 31 | 31 | 31 | 31 | 31 | KRS 1: R 63 |

Key-on 1000 + rand%5000 us, key-off wait 55000 + rand%3000 us (LCG seed 781).  The attack (R 62 from a = 0x280) is
over within ~10 clocks, then decay 1 runs from a = 0 toward DL 31 (a[9:5] == 31, i.e. 0x3E0, never reached before the
key-off: the +4 slot needs 248 clocks = 11.2 ms; the 0x3C0 fetch stop 240 clocks = 10.9 ms; the longest key-on is
6 ms + cap_poll's <= 0.7 ms).  Model: the key-offs landed at a = 0x014..0x1e4 in decay 1 (all `d1 at E_B` lines),
12 even / 4 odd.

Fitter: `koff_fit ... koff_d1 ...`, hypotheses as for decay 2: H0 release increment on the key-off clock, H1 old
(decay 1) increment (= H2 for a decay), H3 no step.  Model results (all five runs, c0 from the file):

```
koff_d2  : c0 ecde  H0 0/39  H1 39/39  H2 39/39  H3 0/39   no-fit 0
koff_d2b : c0 2d33  H0 0/15  H1 15/15  H2 15/15  H3 5/15   no-fit 0   (H3's 5 = the rate-0 slot, H1 = H3 there)
koff_d1  : c0 73ea  H0 0/36  H1 36/36  H2 36/36  H3 0/36   no-fit 0   <- the new run: H1 FULL on the model, as required
koff_att : c0 b465  H0 0/12  H1 0/12   H2 0/12   H3 12/12  no-fit 0   (the model now implements F2 "no step from an attack")
kon_rel  : c0 feea  L0 48/48 L1 9/48  L2 39/48  | C0 6/6  C1 0/6      no-fit 0
```
The five c0 values equal the `, c0 XXXX` fields of `tests/aeg_koff/model/aeg_koff.txt` run by run.  Representative koff_d1 line:
```
cycle  0: key-on E_A 450 (even) key-off E_B 650 (even, on 200 samples)
    s0: d1 at E_B a 0x0b4, inc_prev 2 inc_rel 1:  H0 fail@E_B+0  H1 FULL  H2 FULL  H3 fail@E_B+0   key-off samples reproducing: H0{+1,+2} H1{+0,+1} H2{+0,+1} H3{+1}
        observed: a 0b4 -> 0b6 on E_B (+2), then 0b7 on E_B+2
```

### The c0 parse bug (fixed)

The cause was not the run-name prefix (`koff_d2` / `koff_d2b`): `parse_txt` located the field with `strstr(line, "c0 ")`,
which also matches inside the ring ADDRESS of the cap_start line.  The hw koff_d2b line is
`cap_start: counter word fd58 at a9c0 (n 679), next ..., c0 ac67, head n 682`: `"c0 "` first matches `a9c0 (`, the `%x`
fails on `(`, and no c0 was ever taken for that run (the model's koff_d2b line has `at 2b00`, which is why the bug did
not show on the model).  Fix: the field is matched as `", c0 "` (with the comma), and a run's scan now ends at its
`"<run>: N samples"` line, so a later run's cap_start line can never be taken either.  Verified: with the old hw text,
`koff_fit tests/aeg_koff/hw/koff_d2b 0 6491 koff_d2b tests/aeg_koff/hw/aeg_koff.txt` now prints `c0 ac67`; the other
three hw runs print 7d4b / e935 / 2b5e = the file.

### Expected console signatures (koff_d1, informative = key-off on an even sample, 3 streams x ~12 cycles)

- Decay 1 behaves like decay 2 (F1, the model): `observed: a X -> X+inc_d1 on E_B, then +inc_rel on E_B+2`; sets
  H1{+0,+1} H0{+1,+2} H3{+1}; SUMMARY `H1 N/N, H0 0/N, H3 0/N` (no rate-0 slot in this run, so H3 must be 0).
- Decay 1 behaves like the attack (F2): `a X -> X on E_B`, then `X + inc_rel` on E_B+2; H3{+0}, all other sets empty;
  SUMMARY `H3 N/N, H1 0/N`.
- Release increment on the key-off clock: `X -> X+inc_rel`; H0{-1,+0}... (see case_aeg_koff.md for the full table).
- Anything else: "stream-cycles no H fits" > 0 with the level dump lines.  The runs koff_d2 / koff_d2b / koff_att /
  kon_rel must reproduce today's F1 / F2 / F3 tallies (51/51 H1, 12/12 H3, 48/48 L0) on the fresh captures.

## (B) `feg_koffatt` -- FEG attack -> release on a clock, opposite directions

Modelled on `cases/feg_koffdir.c` (S3alt.md section 5) with the unfiltered reference replaced by an AEG witness:

- streams 0..2 = FEG slots: VOFF 1, LPOFF 0, Q 4, KRS 15, OCT 0, FNS 0, AR 31, D1R 0, RR 0 (the AEG sits at a = 0 and
  the released slot keeps playing), full-scale random input (LCG seed 4242, 8192 words at 0x20000, looped, pitch 1.0;
  `input.bin`).
- stream 3 = witness: constant 0x7FFF (24 words at 0x10000, loop [0,32)), TL 0, VOFF 0, LPOFF 1, AR 31 D1R 31 DL 31
  D2R 31 RR 31, KRS 1 (R 63): keyed ON by the KYONEX that keys the FEG slots OFF -> level 520176 on the key-off sample E,
  decays to off by itself (D1R 31 to DL 31, D2R 31), keyed off explicitly (marks 5/6) before the next batch's aica_quiet.
- programs (rates FAR / FD1R / FD2R / FRR = 26 / 31 / 31 / 28 everywhere: attack R 52 = 2 per clock, 512 clocks = 23 ms
  to FLV1; release R 56 = 4 per clock):

| slot | FLV0 -> FLV1 (attack) | FLV2/3 | FLV4 (release) | on an even E (v = the value before E) |
|---|---|---|---|---|
| 0 | 1C00 -> 1800, DOWN -2 | 1800 | 1C00, UP +4 | oldStep v-2 then +4; noStep v then +4; S3 (old inc toward FLV4) v+2 then +4; relInc v+4 then +4 |
| 1 | 1800 -> 1C00, UP +2 | 1C00 | 1800, DOWN -4 | the mirror: v+2 / v / v-2 / v-4, then -4 per clock |
| 2 | 1800 -> 1C00, UP +2 | 1C00 | 1C00, UP +4 | same direction: oldStep = S3 = v+2, noStep v, relInc v+4, then +4 |

- 8 batches: aica_quiet; configure; cap_start; 3 ms; key-on slots 0..2 (mark 1); 10000 + 977*b us; key-off (KYONB 0 on
  0..2, KYONB 1 on 3, one KYONEX; marks 3/4); 100 ms; witness off (marks 5/6); 2 ms; cap_stop; `ka_<b>`.  Every key-off
  lands inside the attacks (10..17 ms of 23 ms): model E at 444..896 samples after the key-on, v = 0x1980..0x1B00.

**Deviation from the task text: slot 2 uses FAR 26 (+2), not FAR 24 (+1).**  The tracker recovers u = v >> 1 (v bit 0
is unused by the filter).  With a +1 old step followed by +4 release steps, "old step" (v+1+4n) and "no step" (v+4n)
have identical u whenever v is even at E -- and v is even on every other clock of a +1 attack, so half of the even
batches would have been blind on slot 2, including at the hold (0x1BFD vs 0x1BFC -> both u DFE).  With +2 the four
readings differ by one u each on every even batch (see the predictions rows below).  Slot 2 keeps its role as the
same-direction witness (oldStep = S3 there); slots 0 / 1 separate oldStep from S3.

### Checker `work/koffatt/koffatt_check.cpp`

`koffatt_check <dir> [-K kc]`; reads `feg_koffatt.txt` (c0 per batch from `, c0 XXXX`; the FEG programs and Q from the
`ka_<b> stream k: slot k role feg ...` lines, fallback = the case's table), `ka_<b>.hdr/.bin`, `input.bin` (checked
against the regenerated LCG signal).  Per batch:
1. FEG onset (= key-on sample): the first sample in the mark-1 window (+-400) where a FEG stream CHANGES and from which
   the tracker follows stream 0 for 300 samples.  Not "first non-zero sample": a stopped VOFF 1 slot keeps a filter rest
   value on its bus (tests/slot_tail; the model does it too), so from the second batch on the FEG streams read e.g.
   `2 -28 2` before the key-on.  The tracker starts from L = -(last value)/2, B in [-256, 256].
2. Tracker = koffdir_check / feg_track algorithm, input x(i) = 8 * sig[(i - onset) mod 8192] (pitch 1.0, CA restarts at
   the key-on; MIXS of an unfiltered VOFF 1 slot is 16 * sample and the tracker's x is that >> 1).
3. E = the witness onset (first sample at 520176 after the key-on, previous sample != 520176); parity from MDEC_CT.
4. Four readings simulated with the standalone law (`feg_law.h`: oldStep = M_PEND_EVEN, noStep = M_NOSTEP, S3 = M_S3,
   relInc = M_NOS3), key-off on E: matched samples per stream (FULL / fail@E+n with hw u vs predicted u), the free
   key-off-sample search in [E-3, E+3] per reading and the shared set over the streams, the u window E-6..E+8 (observed,
   then each reading's u at E / E+2 / E+4), and a summary table.  A stream the tracker loses before E + 200 gets no
   verdict (UNTRACKED) instead of a vacuous FULL.

### Model results (`work/koffatt/koffatt_check_model.txt`; model = "old step, old direction", `feg_prev_dir`)

```
  ka_0  E    578  EVEN  oldStep 3/3  noStep 0/3  S3 1/3  relInc 0/3
  ka_1  E    641  odd   oldStep 3/3  noStep 3/3  S3 3/3  relInc 3/3
  ka_2  E    662  odd   oldStep 3/3  noStep 3/3  S3 3/3  relInc 3/3
  ka_3  E    706  odd   oldStep 3/3  noStep 3/3  S3 3/3  relInc 3/3
  ka_4  E    770  EVEN  oldStep 3/3  noStep 0/3  S3 1/3  relInc 0/3
  ka_5  E    791  odd   oldStep 3/3  noStep 3/3  S3 3/3  relInc 3/3
  ka_6  E    834  EVEN  oldStep 3/3  noStep 0/3  S3 1/3  relInc 0/3
  ka_7  E    897  EVEN  oldStep 3/3  noStep 0/3  S3 1/3  relInc 0/3
  even key-off samples: 4, odd: 4
  batches with an EVEN E fitted on all 3 streams:  oldStep 4/4  noStep 0/4  S3(oldInc->rel) 0/4  relInc 0/4
  per stream (even E, FULL / batches):  s0: oldStep 4/4 noStep 0/4 S3 0/4 relInc 0/4   s1: same   s2: oldStep 4/4 noStep 0/4 S3 4/4 relInc 0/4
```
Every stream of every batch is tracked to the end of the capture (5050..5370 samples, 74..841 ambiguous samples on
the slow-moving slot 1, max set 155).  The S3 `1/3` on the even batches is slot 2 alone (oldStep = S3 there), as
designed; the odd batches are indifferent (all four readings coincide, the free search shows every offset in
[E-3, E+3] reproducing).  The window of ka_4 (E 770 even; v before E: slot 0 0x1984 = u cc2+1.., slots 1/2 0x1A7C):
```
     i (rel. E)  MDEC_CT     s0     s1     s2
        768 ( -2) 3a26 EVEN    cc3    d3d    d3d     observed
        769 ( -1) 3a25 odd     cc3    d3d    d3d
        770 ( +0) 3a24 EVEN *  cc2    d3e    d3e     observed   <- one more attack step on the key-off clock (old direction)
        771 ( +1) 3a23 odd     cc2    d3e    d3e
        772 ( +2) 3a22 EVEN    cc4    d3c    d40     observed   <- release +4 / -4 / +4
        774 ( +4) 3a20 EVEN    cc6    d3a    d42
     oldStep                 E:cc2  E:d3e  E:d3e   E+2: cc4 d3c d40   E+4: cc6 d3a d42   (FULL on every tracked stream)
     noStep                  E:cc3  E:d3d  E:d3d   E+2: cc5 d3b d3f   E+4: cc7 d39 d41   (refuted)
     S3(oldInc->rel)         E:cc4  E:d3c  E:d3e   E+2: cc6 d3a d40   E+4: cc8 d38 d42   (partial)
     relInc                  E:cc5  E:d3b  E:d3f   E+2: cc7 d39 d41   E+4: cc9 d37 d43   (refuted)
```
The four predictions differ pairwise by at least one u on slots 0 / 1 at E itself, so each reading is picked out
uniquely; the checker's verdict on the model is exactly the rule the model has (`feg_clock`: rate from `feg_prev`,
direction from `feg_prev_dir`, hold check against the release target).  Had the run used the pre-F4 source ("old inc
toward the release target"), the S3 row would be FULL x3 on the even batches and oldStep 1/3.

### Expected console signatures (feg_koffatt, ~4 even batches of 8)

- FEG attack steps like decay 2 (F4 generalised): SUMMARY `oldStep 4/4, noStep 0/4, S3 0/4 (1/3 per batch = s2),
  relInc 0/4`; window: slot 0 goes DOWN one more -2 on E although its release goes up, slot 1 UP one more +2 although its
  release goes down, slot 2 +2; then +-4 per clock from E+2.
- FEG attack takes no step (the AEG's F2 behaviour): `noStep 4/4`, oldStep / S3 / relInc 0/4; window: u unchanged on E
  on all three slots, +-4 from E+2.
- Old increment toward the release target: `S3 4/4`, oldStep 1/3 per batch (s2 only).
- Release increment already on E: `relInc 4/4`.
- Anything else (e.g. an attack-style step without the hold check, or a step of a different size): all four rows
  "refuted"/"partial" on some even batch; the window rows show the observed u and each reading's prediction, and the
  free-search sets show which key-off offset each reading would need.  An odd-only parity draw (P = 1/256 for 8
  batches) would make every batch indifferent: re-run.
- Sanity: `tracked N/N` on every stream (a `TRACKER LOST` or `UNTRACKED` line means a wrong onset / a different filter
  rest state / a wrong FLV0; the rest values before the onset are printed), witness levels `0 520176 520176 ...` at E.

## Leftovers / notes

- `work/verify/s5/koffdir_check.cpp` (not mine to edit) has the same `strstr(line, "c0 ")` bug: the hw feg_koffdir.txt
  batch kd_7 has `at 0bc0 (n 512)`, so `koffdir_check_hw.txt` reports "7 batches" and never analysed kd_7 (its c0 is
  0dc0).  F4 stands on kd_0/1/5/6 regardless; kd_7 would be one more even batch once the field is matched as `", c0 "`.
- Model run timing: `tests/aeg_koff/model` was first produced against the pre-18:06 source (koff_att then H1 12/12) and
  re-run against the current one (koff_att H3 12/12); koff_d1 is H1 36/36 on both (the model treats decay 1 like
  decay 2 in either version).  `tests/feg_koffatt/model` was produced against the current source only.
- The witness pin rests on one KYONEX applying a key-on and a key-off on the same sample; today's aeg_koff console
  results (F1 51/51 with the pinned key-off) support it.
- Filter rest values between batches (F6's "rest value of 8") are visible in the model captures (`2 -28 2`, `-4 0 -4`,
  ...): a checker that assumes silence before a key-on on a VOFF 1 filtered slot is wrong from the second batch on.
