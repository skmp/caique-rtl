# analysis_S3alt -- alternative readings of claim S3 (2026-09-23)

Claim S3: a key-off sample that is an envelope clock steps toward the release target with the increment of the
segment the envelope was in (the rate lookup lags the state change by one clock).  Task: re-derive the evidence from
the tracked FEG values with a standalone simulator, enumerate mechanisms that could produce the same data, test each
against all 21 streams (plus the 3 feg_odd streams), predict the orchestrator's AEG experiment under every survivor,
and answer the odd-key-off ("koff_same") question.

Everything here is under `caique-rtl/model` ($M).  Nothing existing was edited; the console was not touched.

## Files

| File | What |
|---|---|
| `work/verify/s5/feg_law.h` | standalone FEG law (NOTES "Filter envelope (FEG)" / `feg_clock`) with the 11 key-off mechanisms below as switchable variants |
| `work/verify/s5/s3alt.cpp` | `dump`: every stream's key-off window (koff-40..koff+40) with MDEC_CT parity, counter, hw u, model u, state, rate segment and increment per clock; `fit`: every mechanism x stream, key-off samples that reproduce the whole stream, per-batch intersection |
| `work/verify/s5/s3alt_dump.txt`, `s3alt_fit.txt` | their outputs (24 streams: fk_0..3 x 3, ft_0..2 x 3, feg_odd x 3) |
| `work/verify/s5/aeg_koff_predict.cpp`, `.txt` | AEG level sequences around the key-off for the orchestrator's designs under S3 / S3' / noS3 / noStep, even and odd K, attack durations |
| `work/verify/s5/feg_koffdir_proposed.c` | PROPOSED console case (not copied into `cases/`, see "Rules"): decides the two readings the existing data cannot separate |
| `work/verify/s5/koffdir_check.cpp` | analysis of that case's captures: u recovery (feg_track algorithm) + fit of S3 / oldDir / noS3 / noStep / KYONB(vi) |
| `work/verify/s5/feg_koffdir_model/`, `koffdir_check_model.txt` | the case run through the model and its analysis (validates case + tool end to end) |

Build / run (from $M):
```
g++ -O2 -std=c++17 -o build/work/s3alt work/verify/s5/s3alt.cpp
./build/work/s3alt dump > work/verify/s5/s3alt_dump.txt          # 4 s
./build/work/s3alt fit -w > work/verify/s5/s3alt_fit.txt          # 4 s
g++ -O2 -std=c++17 -o build/work/aeg_koff_predict work/verify/s5/aeg_koff_predict.cpp && ./build/work/aeg_koff_predict
g++ -O2 -std=c++17 -o build/work/koffdir_check work/verify/s5/koffdir_check.cpp
# proposed case, model:
gcc -O2 -std=gnu11 -DCASE=\"feg_koffdir\" -I cases -c -o build/work/feg_koffdir.case.o work/verify/s5/feg_koffdir_proposed.c
g++ -o build/work/feg_koffdir build/work/feg_koffdir.case.o build/host/io_model.o build/host/aica_model.o
./build/work/feg_koffdir work/verify/s5/feg_koffdir_model && ./build/work/koffdir_check work/verify/s5/feg_koffdir_model
# proposed case, console build check (no upload), from $M/hw:
source /opt/toolchains/dc/kos/environ.sh; kos-cc -DMODEL_ROOT=\"$M\" -DCASE=\"feg_koffdir\" -I../cases -o ../build/work/feg_koffdir.elf ../work/verify/s5/feg_koffdir_proposed.c io_kos.c
```
Frame: capture sample i has MDEC_CT = (c0 - n_first - i) & 0xFFFF, clock on even MDEC_CT, eg_cnt = 6491 - MDEC_CT/2, u = v >> 1
(a +1 step of v is visible in u only every other clock).  The "key-off sample" K is the sample on which the state
changes (S2: the first slot pass after the KYONEX write).  K is not known from the capture; it is searched in the
mark window (fk: mark 3 - 64 .. mark 4 + 400; ft: mark 2 - 256 .. mark 2 + 400), and the three slots of a batch must
share one K because one KYONEX write keys them all off.

## 1. The data at the key-off (s3alt_dump.txt; standalone law reproduces all 24 streams FULL under S3)

| batch | K (S3) | parity | what each slot does on the key-off clock under S3 (old segment -> release) |
|---|---|---|---|
| fk_1 | 4454 | even | s0/s1/s2 (R 48/52/56/48 through KRS 15/2/5): decay 2 holding at 0x19FE (u cff, +4 would flip C vs FLV3 0x1A00) -> **+4 to 0x1A02 (u d01)** toward FLV4 0x1C00, then +1 per clock (d01 d02 d02 d03 ...) |
| fk_3 | 4564 | even | same programs on rotated slots: identical (**+4 then +1**) |
| ft_1 | 6660 | even | s2 (KRS 5: R 49/57/63/61): decay 2 +8 holding at 0x19FC (u cfe) -> **+8 to 0x1A04 (u d02)**, then +8 per clock; s0 (R 32 attack) and s1 (R 39 attack, u d64, still climbing) have increment 0 at cnt 5042 -> no step; their release (R 44 / R 51) steps from 6662 |
| ft_2 | 803 or 804 | even/odd | s0 attack R 48 +1 -> +1 (invisible in u), then release +4 (R 56); s1 attack R 36 tick +1 (c14 -> c15) at 803, then +1/clock; s2 decay 2 rate 0 -> nothing, release -2 from 805.  **Consistent with S3 but not decisive**: K = 804 (odd) also fits every slot (the +1 steps at 803 are then ordinary attack steps) |
| fk_0 | 6771 | odd | decay 2 +8 / +2 / +1 holds; nothing on 6771, release from 6772 |
| fk_2 | 4565 | odd | as fk_1 but odd: hold, then **+1 (0x19FF) at 4566** -- no +4 anywhere |
| ft_0 | 5469 | odd | s0 decay 2 +8 holding at 0x1AFA (u d7d) vs FLV3 0x1B00; release DOWN toward 0x1900: -2/-4 (R 54) from 5470; no +8 |
| feg_odd | 6772 | odd | decay 2 +4/+8 holds (R 57/59), release from 6773 with the release rows (R 45/47) |

So the claim's weight sits on fk_1, fk_3 (a +4 out of a hold: no decay-2 step can produce it) and ft_1 (no single K
serves the three slots without the rule); ft_2 only agrees.  Odd key-offs (fk_0, fk_2, ft_0, feg_odd) never show the
old increment.

## 2. Mechanisms and verdicts (s3alt_fit.txt, "per batch" table)

Per batch: key-off samples shared by the three slots (FAIL = a slot fits no K in the window; "no shared K" = each slot
fits but never the same sample).

| mechanism | fk_0 | fk_1 | fk_2 | fk_3 | ft_0 | ft_1 | ft_2 | feg_odd | verdict |
|---|---|---|---|---|---|---|---|---|---|
| **S3** (old increment, release direction, only when K is a clock) | 6771 | 4454 | 4565 | 4564 | 5469 | 6660 | 803,804 | 6772 | fits all |
| **oldDir** (= (iv) restricted to clocks: old increment AND old direction, hold check vs FLV4) | 6771 | 4454 | 4565 | 4564 | 5469 | 6660 | 803,804 | 6772 | fits all -- UNTESTED difference, see (iv) below and section 5 |
| **KYONB(vi)** (target/direction follow the KYONB clear at T, state/rate follow KYONEX at K, no lag) | K 6771/72 | K 4455/56 | 4565/66 | 4565/66 | 5469/70 | 6661/62 | 804/805 | 6772/73 | fits all (T <= K <= T+2 per slot, T in slot order) -- observationally equivalent, see (vi) below and section 5 |
| noS3 (release increment on the key-off clock; eg_phase koff_same 1) | 6771,6772 | FAIL | 4565,4566 | FAIL | 5469,5470 | no shared K | 804,805 | 6772,6773 | REFUTED (fk_1, fk_3: hw d01 vs d00 at +4316/+4426; ft_1 split) |
| noStep (nothing on the key-off clock, like the key-on sample) | 6770,6771 | FAIL | 4564,4565 | FAIL | 5468,5469 | no shared K | 804 | 6771,6772 | REFUTED (same) |
| (iii) lagClk: rate of the state at the previous clock, at EVERY clock | FAIL | FAIL | FAIL | FAIL | FAIL | FAIL | FAIL | FAIL | REFUTED before the key-off: at segment transitions |
| lagClkKO: old increment on the first clock AFTER the key-off, any parity | no shared K | 4453,4454 | FAIL | 4563,4564 | FAIL | 6659,6660 | 802,803 | FAIL | REFUTED by the odd batches (fk_2: +4429 hw d00 model d01; ft_0; feg_odd; fk_0 split) |
| (iv) pend: old increment + old direction applied at the first clock after the key-off, any parity | no shared K | 4453,4454 | FAIL | 4563,4564 | FAIL | 6659,6660 | 802,803 | FAIL | REFUTED (same batches; ft_0_s1 additionally by direction) |
| S3_2clk: old increment on K and on the next clock | no shared K | FAIL | FAIL | FAIL | FAIL | no shared K | 800,801 | FAIL | REFUTED (fk_1: +4318 hw d01 model d03) |
| (v) v_noS3: the state change lands only on even samples, release increment there | 6772 | FAIL | 4566 | FAIL | 5470 | no shared K | 805 | 6773 | REFUTED |
| (v) v_noStep | 6770 | FAIL | 4564 | FAIL | 5468 | no shared K | no shared K | 6771 | REFUTED |

Details per item of the task:

(i) rate mux lagging one SAMPLE.  Observationally identical to S3: the clock only exists on every other sample, so
"rate from the state one sample ago" and "rate from the state one clock ago at the key-off" give the same increment on
K (even: old; odd: nothing on K, release at K+1).  A lag of 2 samples is also identical; 3+ samples (S3_2clk) is
refuted by fk_1/fk_3 (+4 then +1, not +4 +4).  A concrete hardware reading: the key event is latched after the rate
lookup of the same slot pass (or the rate register is updated once per sample from the state).  (i) also has to put
the internal attack->decay 1 / decay 1->decay 2 transitions at the flipping clock, otherwise it becomes (iii).

(ii) key-off applied on the write sample when the write lands before the slot pass.  This is S2 restated at the
sub-sample level; the capture cannot see the write, only the first pass after it, so (ii) is a relabelling of K with
no observable of its own.  All three slots still share K (one KYONEX).

(iii) one-clock increment pipeline at all transitions.  Refuted by the transitions themselves, independently of the
key-off: ft_0_s0 attack R 56 (+4) overshoots FLV1 0x1C05 to 0x1C08 (u e04) and the very next clock steps -2 (u e03,
the decay 1 R 52 increment), not -4 (u e02) -- fails at +518; fk_1 decay 1 R 52 (-2) ends at 0x17FE (u bff) and the
next clock steps +4 (u c01, decay 2 R 56), not +2 (u c00) -- fails at +3076 in all fk_1/fk_2/fk_3 streams; ft_0_s1
+1108, ft_1_s2 +1642, ft_2_s2 +259, feg_odd_s0/s2 +1642, s1 +1172, fk_0_s0 +1640 likewise.  The AEG agrees: eg_lock
odd_dec (R 63 attack, decay 1 at rows 5/9/13/1) reproduces FULL with the decay-1 increment on the first decay-1 clock.
So the new segment's rate applies on the very next clock at every internal transition; the lag exists only across a
key event.

(iv) "compute at clock N, apply at N+1" for the whole EG.  To explain fk_1 the compare has to run at apply time
against the then-current target (the +4 was rejected by the decay-2 hold, then accepted against FLV4).  Taken
literally it also applies the pending old step at the first clock after an ODD key-off: refuted by fk_2 (hold at
0x19FE, then +1 to 0x19FF at 4566 -- a pending +4 would give u d01, hw d00), ft_0 (s0 +8 hold, release goes the other
way: hw d7c, model d81), feg_odd and fk_0.  Also the key-on phase: a pipelined step would make the first envelope step
follow the key-on by 2 clocks when the key-on sample is odd (eg_keys: 1 sample).  The only survivor of the (iv) family
is its restriction to clock samples, "oldDir": old increment AND old direction on an even K, hold check against FLV4.
It is indistinguishable from S3 on every existing stream because wherever the old increment was nonzero on an even K
the old segment and the release moved the same way (fk_1/fk_3/ft_1 s2: both up; ft_1 s0/s1 and ft_2 s2 had increment
0; ft_0 -- the only capture with opposite directions -- keyed off on an odd sample).

(v) key-off applied one sample later when the write sample is even.  Then the state change always lands on an even
sample and the +4 out of the hold has no source: FAIL on fk_1/fk_3 for both sub-variants.  REFUTED.

(vi) found while enumerating: KYONB-driven target.  If the FEG's target/direction mux followed the KYONB register
bit (cleared by the per-slot writes a few us BEFORE the KYONEX write) while the state/rate followed KYONEX, a clock
pass falling between a slot's KYONB clear and the KYONEX would step with the decay-2 rate toward FLV4 -- exactly the
"+4 out of the hold" -- with no rate lag at all.  It fits every batch (table: K one or two samples after the S3 K on
the even batches, T = the S3 K), and it is observationally equivalent to S3 on the existing captures because the
capture cannot see where the writes landed.  Two things speak against it: (1) statistics -- on the even batches the
tuples need the clock pass strictly between the LAST slot's KYONB clear and the KYONEX effect (fk_1: T0..T2 <= 4454 <
K in {4455,4456}), a window of one G2 read (2.4 us) plus one posted write, about 3 us of the 45.4 us clock period
(~7 % per batch); three of the eight batches (fk_1, fk_3, ft_1) did it, none showed the mixed outcome (slot 0 stepping,
slot 2 not, a ~13 % window) -- P(>= 3 of 8) ~ 1.4 % under (vi) against ~86 % for ">= 3 even of 8" under S3, roughly
60 : 1 for S3 with the caveat that the G2 write latency is not measured; (2) it predicts that clearing KYONB alone
moves a held FEG toward FLV4 at the decay-2 rate -- cheap to test, see section 5.

Also checked: an equivalent "rate register latched at the end of each clock" (S3 phrased as RTL) = S3; a lag of the
counter index together with the rate (increment value pipelined) = a shift of K absorbed by the fitted constant;
"the old segment's rule as well as its increment" differs from S3 only when the release target is within one step
(untestable here, irrelevant to the four events).

## 3. Predictions for the orchestrator's AEG experiment (aeg_koff_predict.txt)

Level law: MIXS = 16 * floor(32767 * (127 - (a & 63)) >> (7 + (a >> 6))).  Mechanisms on the AEG: S3 (the model:
`a += inc(old rate)`, release formula), S3' (one more OLD-segment step incl. the attack formula), noS3 (release
increment on K), noStep (nothing on K).  Every mechanism does nothing on an odd K and steps with the release increment
on K+1.

Decay 2 -> release designs (+2/+1/+4/0 with +1/+4/hold/+2; +8/+2/+1/+8 vs +1/+8/+8/+2), even K:
```
d2 +2 -> rel +1:  S3  a: ...0c4 0c4 *0c6 0c6 0c7 0c7 0c8 ...   noS3: *0c5 0c5 0c6 ...   noStep: *0c4 0c4 0c5 ...
d2 +1 -> rel +4:  S3  ...0a2 0a2 *0a3 0a3 0a7 0a7 0ab           noS3: *0a6 0a6 0aa       noStep: *0a2 0a2 0a6
d2 +4 -> rel 0 :  S3  ...108 108 *10c 10c 10c (holds)           noS3/noStep: *108 108 108 (holds)
d2  0 -> rel +2:  S3  ...080 080 *080 080 082 082 084           noS3: *082 082 084       noStep: *080 080 082
d2 +8 -> rel +1:  S3  ...190 190 *198 198 199 199 19a           noS3: *191 191 192       noStep: *190 190 191
```
**But for every one of these designs S3(even K) == noS3(odd K+1) == noStep(odd K+1) over the whole run** (the tool
checks it): under S3 the key-off clock does exactly what one more decay-2 clock would do, and an odd sample is a
no-op, so the sequence "+d ... +d, +r, +r" is read as K = last +d sample (S3) or K = one later (noS3/noStep).  K is not
observable from the AEG capture (cap_mark is 30-100 samples off; the counter is not readable), so **the decay-2 runs
cannot decide S3 on the AEG by themselves**.  Same for d2 rate 0 -> release (the eg_keys situation) and for the
"parity known from MDEC_CT" idea: the parity of K is only inferred from the first release step, which is the very
thing in question.  The only AEG-internal handle is a direction reversal:

Attack -> release designs (attack R 48/52/56 = inc 1/2/4, release +1 (R 48) / +4 (R 56)), even K (a at K-2 in the
attack, then):
```
att R48 -> rel +4:  S3 *+1 then +4/clock (a 162 -> *163 167 16b ...)   noS3 *+4 (162 -> *166 16a ...)   S3' one more attack step (-> *14b 14f ...)   noStep (-> *162 166 ...)
att R52 -> rel +1:  S3 *+2 (0bd -> *0bf 0c0 0c1 ...)                   noS3 *+1 (-> *0be 0bf ...)       S3' (-> *0a5 0a6 ...)                      noStep (-> *0bd 0be ...)
att R56 -> rel +1:  S3 *+4 (02e -> *032 033 034 ...)                   noS3 *+1 (-> *02f 030 ...)       S3' (-> *022 023 ...)                      noStep (-> *02e 02f ...)
att R56 -> rel +4:  S3 == noS3 (+4 either way)                          -- not discriminating
att R48 -> rel +1:  S3 == noS3 (+1 either way)                          -- not discriminating
```
Observable without knowing K: the first UP-step after the attack's down-steps.  Under S3 it equals the attack
increment in the even cycles (about half of 16) and the release increment in the odd ones; under noS3, S3' and noStep
it is always the release increment (S3' and noStep are noS3 with K relabelled).  So: **any cycle whose first up-step is
+4 (att R 56 / rel R 48), +2 (att R 52 / rel R 48) or +1 followed by +4s (att R 48 / rel R 56) confirms S3 on the AEG;
16 cycles without one refute it (P 2^-16 under S3).**  Requirements: the attack must still be running at the key-off
-- from a = 0x280 the attack reaches 0 after 66 clocks = 3.0 ms (R 48), 37 clocks = 1.7 ms (R 52), 19 clocks = 0.9 ms
(R 56) -- so on-times must stay below that (eg_lock keys' 700-2500 us suits R 48 only; use 200-800 us for R 52/56),
and the attack and release rates must differ.  This run also separates S3 from (vi): under a KYONB-driven target the
AEG (which has no target) would still be in the attack on the pass before KYONEX and never show an up-step of the
attack increment.

To make the decay-2 runs decisive, pin K independently: add an FEG slot with the fk_1 program (FLV 1800 1C00 1800
1A00 1C00, FAR 24 FD1R 26 FD2R 28 FRR 24, KRS 15, RR 0, VOFF 1, LPOFF 0, Q 4, random input) plus its unfiltered
reference on two of the four buses, keyed by the same KYONEX; its +4-out-of-the-hold marks K (under FEG-S3) and the
AEG slots are then read at that K: +d (S3) or +r (noS3) or nothing (noStep).  Cheaper: put one attack->release slot
in every run; in its even cycles it pins K (given S3 on the AEG attack) and the decay-2 slots at that K test the same
mux for decay 2.

## 4. Odd key-off samples (the koff_same ambiguity)

No surviving mechanism predicts a different observable for an odd K: S3, oldDir, (i), (ii) and (vi) all give
"nothing on K, release increment on the next clock".  The mechanisms that did predict something else for odd K --
lagClkKO, (iv) pend, S3_2clk: the old increment on the first clock after K -- are refuted by fk_2 (hold 0x19FE, +1 at
4566), ft_0 (s0 +8 hold, release the other way: -2 at 5470), feg_odd and fk_0.  For the FEG the question is closed by
the data.  For the AEG the ambiguity is the decay-2 equivalence of section 3 (eg_phase's koff_same 0 and 1 both fit
every AEG capture with K shifted by one) and is not resolvable from AEG-only captures; the attack->release run resolves
"S3 or not" and an FEG pin resolves K.

## 5. What the existing data leaves open, and the proposed case

Two readings survive alongside S3:
- **oldDir**: on an even K the step is the old segment's full vector (old increment, old direction), only checked
  against FLV4.  Same as S3 whenever the old segment and the release move the same way.
- **KYONB(vi)**: the target follows the KYONB register, not the state.

`work/verify/s5/feg_koffdir_proposed.c` decides both (feg_krs harness: random input, Q 4, VOFF 1, RR 0, stream 3
reference; KRS 15):
- slot 0: FLV 1C00 1800 1C00 1A02 1C00, rates 28/26/28/24 -- decay 2 goes DOWN and holds at 0x1A04 (u d02), release UP
  +1.  Even K: S3 -> 0x1A08 (u d04); oldDir -> 0x1A00 (u d00); odd K: 0x1A05 (u d02) on the next clock.
- slot 1: the fk_1 program (S3 witness, both up): even K +4 -> u d01; odd -> u cff.
- slot 2: FLV 1800 1C00 1800 1A00 1800, rates 28/26/28/24 -- decay 2 UP holds at 0x19FE, release DOWN +1.  Even K: S3
  -> 0x19FA (u cfd); oldDir -> 0x1A02 (u d01); odd: 0x19FD (u cfe).
- batches 0..5: normal key-off after 100 ms + 977 us * b (pseudo-random parity; P(no even) = 1/64).
- batches 6, 7: KYONB cleared on slots 0..2 with NO KYONEX, 100 ms later KYONEX alone.  KYONB-driven target: the held
  values move toward FLV4 at +4/clock right after the clears (slot 0 -> 0x1BFC, slot 1 -> 0x1BFE, slot 2 -> 0x1802
  within 6 ms); state-driven: they hold until the KYONEX.
Analysis: `build/work/koffdir_check <dir> [-K kc]` recovers u (feg_track algorithm) and fits the five readings with
the per-batch shared-K rule; it prints "MOVED: KYONB-driven target" / "held" for batches 6-7 and the u window per
batch.  Validation on the model (`koffdir_check_model.txt`): 8/8 batches FULL under S3 (K 4546e, 4609e, 4630o, 4674e,
4737o, 4760e, 9216e, 9281e); on the 6 even batches oldDir / noS3 / noStep are refuted by slots 0 and 2 (e.g. kd_0:
hw d04 model d03 at +4411; hw cfd model cfe); KYONB(vi) fits the plain batches with K one later (as expected) and is
refuted by the KYONB-only batches (u held for the 100 ms).  Console build: `build/work/feg_koffdir.elf` compiles with
kos-cc (not uploaded).  K = 6491 holds only if the console did not reboot since eg_lock; otherwise refit with
`eg_phase` on att_slow and pass `-K`.

## 6. Summary

- S3 re-derived independently: the standalone law reproduces all 24 FEG streams sample by sample with one shared
  key-off sample per batch (fk_0 6771, fk_1 4454, fk_2 4565, fk_3 4564, ft_0 5469, ft_1 6660, ft_2 803/804, feg_odd
  6772), matching eg_model.  Of the four cited events fk_1, fk_3 and ft_1 carry the claim; ft_2 is consistent only.
- Refuted alternatives: release increment or no step on the key-off clock (noS3, noStep), a one-clock lag at every
  transition (iii -- the transitions themselves refute it), a pending-step pipeline (iv) and any "first clock after the
  key-off" rule (the odd batches refute them), a two-clock lag, and (v).
- Not separable from S3 by the existing data: (i) and (ii) (rewordings), the old-direction variant of the same lag,
  and a KYONB-driven target (disfavoured ~60:1 by the write-window statistics).  The proposed feg_koffdir case decides
  the last two in one console run.
- The AEG decay-2 experiment as designed is not decisive (S3-even-K is identical to noS3-odd-K+1); the attack ->
  release runs are, provided attack and release rates differ and the key-off lands inside the attack (on-time < 3.0 /
  1.7 / 0.9 ms for R 48 / 52 / 56): an up-step equal to the attack increment in about half the cycles confirms S3 on
  the AEG, none in 16 refutes it.
- Odd key-off samples: every surviving reading agrees (nothing on K, release increment on the next clock).
