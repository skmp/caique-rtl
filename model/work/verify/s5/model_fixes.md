# model_fixes -- F2 / F4 / F5 / F6 / F7 folded into the production model (session 5, 2026-09-23)

Files edited: `src/aica_model.cpp`, `src/aica_model.h` only.  Scratch: `work/model5/` (tools, every validator output
quoted below).  Nothing else touched (tools/, cases/, NOTES.md, HANDOVER.md are other agents').  No console run.

## 1. What changed

| Item | Function / field | Rule now in the model | Evidence |
|---|---|---|---|
| (a) AEG key-off clock | `aeg_clock`: `if (c.keyed_off && c.aeg_prev == EG_ATTACK) return;` before the rate lookup; decay 1 / 2 keep `rs = aeg_prev` (one more step with the old segment's increment) | key-off sample that is a clock: from an ATTACK no step at all; from DECAY 2 one more decay-2 step, the release increments from the next clock.  Decay 1 -> release unmeasured, assumed = decay 2 (comment in the code) | tests/aeg_koff koff_att 12/12, koff_d2(b) 51/51 (F1/F2) |
| (b) FEG key-off clock | `Slot::feg_prev_dir` (new, stored in `key_off()` before `FEG.dir` is recomputed); `feg_clock`: `dir = keyed_off ? feg_prev_dir : FEG.dir`, rate from `feg_prev`, target FLV4 with the release hold rule (the `state >= EG_DECAY2` branch) | one more step of the OLD segment (increment AND direction), held against FLV4 | tests/feg_koffdir kd_0/1/5/6/7 (F4); "old increment toward the release target" refuted there |
| (c) slot stop / off | `Slot::stop_in`, `Slot::off_in` (new countdowns), `slot_stop()` / `slot_off()`, armed in `aeg_clock`, committed at the end of `step()` after the `slot_output` loop | a keeps stepping to 0x3FF; `a >= 0x3C0` on a clock arms the fetch stop (`enabled = false`: zero input, no `stream_step`, the filter runs on); `a > 0x3FF` arms "off" (`AEG.off`, monitor 0x1FFF, `CA = 0`); a is clamped at 0x3FF; both take effect on the NEXT sample (the clock sample still outputs the fetched sample).  State kept, KYONB untouched, key-on still ignored until a key-off after a decay-2 off | tests/slot_tail tail_a / tail_b / tail_c (F5) |
| (c') NO mute | `slot_output`: the `AEG.off -> V16 = 0` branch is gone; the VOFF 0 path applies `att_apply` with `a` saturated at 0x3FF forever | F6's "muted from the sample after 0x400" is refuted by the same capture (section 2.2): the VOFF 0 output keeps showing -16 / 0 with the sign of the filter tail for 100 samples past "off" | tail_b stream 0 |
| (c'') FEG runs on | `feg_clock`: the `!c.enabled` gate removed (only `keyed` skips) | the FEG keeps stepping after the fetch stop and after off | tail_c streams 1 / 2 (section 2.2) |
| (c''') CA | `slot_off()` resets CA, `slot_stop()` does not | the CA monitor reads 0 from "off", not from the fetch stop | tests/sgc_keys K4 console log (section 2.5) |
| (d) R 63 attack on a key-on clock | `aeg_clock`: the `keyed` branch now does `if (state == EG_ATTACK && a == 0 && !LPSLNK) set_aeg_state(EG_DECAY1)` before returning (no increment step); `key_on` unchanged (a = 0 for R >= 63) | the R 63 attack leaves the attack on the first clock at or after the key-on sample, including the key-on sample itself | tests/eg_kprobe p5/p6/p7 (F7) |
| (e) EG reads its rate registers one sample late (NEW, forced by tail_c stream 0) | `Slot::egreg {r10, r14, r18, r40, r44}` (new), `AicaModel::eg_latch(ch)` called for every slot at the end of `step()` and in `reset()`; `aeg_clock` / `feg_clock` read `egreg`; `eff_rate` is now `static eff_rate(rate, r14, r18)` and gets the latched r14/r18 from the clocks, the live ones from `key_on` (the R 63 check) | a register write on sample n reaches the envelope generators at n+1 (the fetch sees it at n, as before) | tail_c stream 0: RR 0 -> 31 rewritten on the clock sample 11300 together with SA; the SA switch is visible at 11300, the first +8 step only at 11302 (section 2.2) |
| controls | `CAIQUE_STOP_A` (0x3C0), `CAIQUE_STOP_LAG` (1), `CAIQUE_MUTE` (0) macros at the top of aica_model.cpp, overridable with -D | production values are the measured ones; the macros exist only for section 3 | -- |

Comments: every changed rule carries its tests/<case> reference; the old comments that stated the refuted behaviour
("output muted", "leaves the attack on the next clock", "steps toward the release target", "CA reads 0" at the stop)
were rewritten, not deleted.  `git diff HEAD -- src/` (+~130 / -~40 lines).

## 2. Acceptance

### 2.1 Validators (outputs in work/model5/*_final.txt; the pre-edit baseline in work/model5/baseline_*.txt)

| Validator | Before (baseline, session-4 model) | After |
|---|---|---|
| `build/tools/eg_model` | TOTAL full=33/33 | `GROUPS session4=33/33 eg_kprobe=8/8 feg_koffdir=24/24 TOTAL full=65/65` (the validator agent extended eg_model.cpp during this session with the console eg_kprobe and feg_koffdir captures; the 33 session-4 runs are unchanged and the 32 new ones -- F7 and F4 on the console -- pass too) |
| `build/tools/feg_validate` | 9/9, 77862/77862 | `TOTAL full=9/9 samples=77862/77862` |
| `build/tools/filt_validate_model` | 265/265 | `TOTAL qbias=255 full=265/265 consecutive samples=4286180/4286180` |
| `build/tools/filt_overflow_model 1 24` | 15/15 | `TOTAL full=15/15 matched_full_samples=150558` |
| `build/tools/filt_overflow_model 1 24 x` (held out) | 12/12 | `TOTAL full=12/12 matched_full_samples=45384` |

### 2.2 Slot tail (work/tail/tail_cmp.cpp unchanged; `work/tail/tail_cmp_all.sh tests/slot_tail/hw 6491` -> work/model5/tail_cmp_hw_final.txt)

| Run | Result | Events found by the tool |
|---|---|---|
| tail_a (c0 0b6e) | **4/4 streams FULL** | ko 4650 (even), tie 4651 (odd) -- the ko {4650, 4651} of F5 (the old model needed the wrong 0x400 stop and found 4636 with 398 ties) |
| tail_b (c0 c0ba) | **4/4 streams FULL** | ko 4648 (even), tie 4649 |
| tail_c (c0 74eb) | **3/4**: streams 1, 2, 3 FULL; stream 0 11972/14302 | ko 6875 (odd, 1 tie), RR/SA rewrite w14 = w00 = 11300 (1 tie: d 0 is now pinned), IMXL 0 write w2 unresolved (501 ties: the model's bus 0 does not change at the write) |

tail_c stream 0 is FULL from the onset through the key-off, the RR rewrite at 11300, the fetch stop at 11541 (= the
console's), the whole zero-input tail and its rest value 6 up to sample **12198**, where the console's bus 0 reads 0
from then on while the model retains 6.  Retained bus values at the end: **hw 0 2 2 | model 6 2 2**; before the IMXL 0
write both read 6 2 2 (hw bus 0 "value before the IMXL 0 write 6, last change at 11598").  Console readbacks in
tests/slot_tail/hw/slot_tail.txt: "after the IMXL 0 write: 6 2 2 158944", "at the end: 0 2 2 -413312".  So the
console's bus 0 went 6 -> 0 exactly at the IMXL 0 write (12198 = mark 7/8 12164 + 34, the usual head-estimate lag) and
buses 1 / 2 retained their 2s.  See section 5.

How the three console runs were reached (intermediate states, for the record):

| Model state | tail_a | tail_b | tail_c |
|---|---|---|---|
| session 4 (work/tail/tail_cmp_hw.txt) | 1/4 (stop at 0x400: s0/s2 fail 4891, s1 6571) | 2/4 | 1/4 |
| (a)-(d) with F6's mute at 0x400, FEG frozen at the stop, live registers | 4/4 | 3/4: s0 fails 4923 (hw -16 model 0: the mute) | 1/4: s0 fails 11539 (the RR write took effect on the clock of 11300), s1 7118 / s2 8416 (FEG frozen at the stop: the tails drift by a few units) |
| + no mute | 4/4 | 4/4 | s1/s2 unchanged |
| + FEG frozen at off instead (control) | -- | -- | s1 fails 7136 (+6 after off) |
| + FEG never frozen | -- | -- | s1, s2 FULL |
| + rate registers one sample late | -- | -- | s0 FULL to 12198 (final) |

Key numbers from the console reproduced sample by sample: tail_a fetch stop visible at 4891 (120th +8 clock 4890) and
6571 (960th +1 clock), tail_b -16 / 0 pattern through 5000 with the stop at 4889 and off at 4905 (both invisible in
VOFF 0 except through the fetch stop), tail_c stops at 7115 (R 63, 119 clocks after the first release clock 6876),
8411 (R 49 row 5, 768 clocks) and 11541 (RR rewrite: 120 clocks after 11302).

### 2.3 Controls (work/model5/control_<name>.txt; each is `tail_cmp` rebuilt with one -D and run on all three console runs)

| Control | tail_a | tail_b | tail_c | First break (stream: sample, hw/model) |
|---|---|---|---|---|
| `-DCAIQUE_STOP_A=0x3BF` | 3/4 | 4/4 | 2/4 | tail_a s1 6569 (hw 58704 / 70294: stop one clock early), tail_c s2 8409 |
| `-DCAIQUE_STOP_A=0x3C1` | 4/4 (ko re-fitted to 4648 to absorb the shift) | 3/4 | 1/4 | tail_b s0 4923 (-16 / 0), tail_c s1 7115, s2 8411 (the FEG-pinned ko cannot absorb it) |
| `-DCAIQUE_STOP_A=0x400` (the old rule) | 3/4 | 2/4 | 1/4 | tail_a s1 6571, tail_b s0 4894 (-16 / 0), s1 4891 (0 / -16), tail_c s0 11541, s1 7115, s2 8411 |
| `-DCAIQUE_STOP_LAG=0` (stop visible on the clock sample) | 1/4 | 3/4 | 1/4 | tail_a s0 4891 (-94576 / -80474), s2 4891 (0 / -401088), tail_b s0 4904, tail_c s0 11540, s1 7115, s2 8411 |
| `-DCAIQUE_STOP_LAG=2` | 1/4 | 3/4 | 1/4 | tail_a s0/s2 4891, s1 6571; tail_b s0 4923; tail_c s0 11541, s1 7115, s2 8411 |
| `-DCAIQUE_MUTE=1` (mute from off = F6 as stated) | 4/4 | 3/4 | 3/4 | tail_b s0 4923 (hw -16 model 0) |
| `-DCAIQUE_MUTE=2` (mute from the 0x3C0 stop) | 4/4 | 3/4 | 3/4 | tail_b s0 4889 (hw -16 model 0) |

(tail_c stream 0 fails at 12198 in every row, the bus-0 retention of section 5.)  The FEG gate variants are the
"frozen at the stop" (s1 fails 7118) / "frozen at off" (s1 fails 7136) rows of the table in 2.2.

### 2.4 K probe (`./run_model.sh eg_kprobe` exit 0; `build/work/kfit -case tests/eg_kprobe/model -expect 6491` -> work/model5/kfit_model_final.txt)

K = 6491 on all 8 model probes (stream 0 "1 K fit", streams 1..3 8 / 128 / 2048 K fit), `expect K = 6491: all probes
match`, dK +0 and drift +0 throughout.  kfit.cpp had already been given the F7 rule by the validator agent (its header
line: "R 63 attack leaves the attack on the first clock at or after the key-on sample (F7)"), so the production model and
kfit agree on the even-onset probes; the signature the task expected is visible with kfit's `-oldr63` control (the
session-4 rule, work/model5/kfit_model_oldr63.txt):

- model p6 (onset 222, MDEC_CT 385e even, row-1 increment 1 at onset + 2): `stream 3 ... NO K fits: best 2/65748 at K 0,
  first mismatch i 224 (+2) a 000 state 1 level 520176 hw 516080` -- verbatim the console's p5/p6/p7 lines in
  work/kprobe/kfit_hw.txt (`first mismatch i 228 (+2) a 000 state 1 level 520176 hw 516080`).
- model p5 (onset even, MDEC_CT 5732, row-1 increment 0 at onset + 2): fits both rules, like the console's p2.
- model p0-p4, p7 (odd onsets): fit both rules, like the console's odd-onset probes.

The model's capture itself: p6 stream 3 = 520176 at 222/223 and 516080 (a = 1) from 224 = onset + 2 (work/model5 dump);
under the old rule the first decay-1 step would have been at onset + 4.  The console's kp_p0..p7 replays through the
production model are the "eg_kprobe=8/8" of eg_model above.

### 2.5 Whole-program regression (work/model5/run_model_all.log, work/model5/model_outputs_after.sha256)

`./run_model.sh` on every case in cases/ (46: the 42 of session 4 + aeg_koff, eg_kprobe, slot_tail, feg_koffdir, and
feg_koffatt which another agent added during the session): **46/46 exit 0**.  Against work/model_outputs_2026-09-23b.sha256:
364/390 files unchanged, 26 changed (all in cases with a release to off or with VOFF captures).  To attribute them, the
six affected small cases were also rebuilt against the committed session-4 model (git HEAD ba720b9) in build/work/old/
and diffed file by file (they reproduce the reference):

| Changed outputs | Why |
|---|---|
| sgc_aeg/model/{att_06_01,att_22_16,att_31_28,dec2,rel_a,rel_b}_eg.txt, sgc_aeg.txt | EG monitor logs of releases to off: "off" (0x1FFF) now one sample after the overflow clock and the fetch stop 8 clocks earlier (rel_a: the model's last non-off reading is 0x63f8 at 5119, off at 5120; the console's log has the same shape) |
| (sgc_keys/model/sgc_keys.txt -- changed in an intermediate state only) | K4 (decay 2 to off) read `t 5868 us: EG 43c0 CA 0000` while `slot_stop()` still reset CA (CA reset at the fetch stop); the console logs the CA reset in the same poll as EG 5fff (5916 us), which is why the final model resets CA at off (item c'''); the final output is byte-identical to session 4 (`t 6231 us: EG 5fff CA 0000`) |
| sgc_krs/model/sgc_krs.txt | the R 63 attacks keyed on on a clock sample now leave the attack on that clock: with a slow decay 1 the first step moves to the tick at +2 that the attack no longer occupies (e.g. `KRS 0 OCT 1 FNS 000: 34:516080 ...` -> `2:516080 34:511984 ...`) |
| eg_lock/model/odd_dec.bin | same (d): the model run's odd_dec onset is sample 449, MDEC_CT 0x0c9e-based parity even, decay 1 (R 49) steps at 451 = onset + 2 instead of + 4 |
| eg_lock/model/mixs.bin | the stopped slot's tail: fetch stop 8 clocks earlier, FEG running on -> a different rest value |
| sgc_lfo2/model/l2_1.bin | a release to off in that run: 165 samples differ from sample 18105 (the level applied to real samples between 0x3C0 and the old 0x400 stop, and the tail) |
| cap_selftest/model/{cap.bin, cap_selftest.txt} | the slots stop 15 samples earlier ("88456 samples checked to 89352" -> "88441 ... 89337": 8 clocks earlier, one sample later) |
| feg_krs/model/{feg_krs.txt, fk_1.bin, fk_1.hdr, fk_2.bin, fk_2.hdr, fk_3.bin}, feg_track/model/{feg_track.txt, ft_0.bin, ft_1.bin, ft_1.hdr, ft_2.bin, ft_2.hdr}, feg_probe/model/feg_probe.txt | the capture START moved by one sample, not the envelopes: `cap_start: ... head n 678 -> 677` (and n_first 679 -> 678, marks 811 -> 810, 5222 -> 5221 in fk_1.hdr).  cap_start's sync scan costs one G2 read per zero ring word and up to five per non-zero one; the ring's data words before the key-on are the silent slots' filter rests inherited from the previous run, which changed (stop 8 clocks earlier, FEG running on), so the head estimate moved by one sample and the whole capture is shifted by one.  The FEG values are unchanged (eg_model / feg_validate FULL).  feg_probe.txt: one FEG monitor reading `e200 -> e1fc` (the FEG keeps moving after the slot is off, item c'') |

### 2.6 AEG key-off rules through the production model (work/model5/koff_replay.cpp -> build/work/koff_replay)

One cycle each, key-on E_A and key-off E_B from work/koff/koff_fit_hw_<run>.txt, c0 from tests/aeg_koff/hw/aeg_koff.txt,
model from reset (every slot off, as on the console at E_A), 2400 samples compared on all four streams
(work/model5/koff_replay_att_c4.txt, koff_replay_d2_c1.txt):

- koff_att cycle 4: E_A 10422 (even), E_B 10444 (even, attack R 48/52/56 -> release R 56/48/52): **4/4 streams FULL
  2400/2400**.  Across E_B: s0 a 0x14b -> 0x14b on E_B (no step) -> 0x14f at E_B+2 (+4 release); s1 0x0a5 -> 0x0a5 -> 0x0a6;
  s2 0x022 -> 0x022 -> 0x024; witness 520176 (a 0) on E_B, then its D1R 31 decay (0x008, 0x010, ...).
- koff_d2 cycle 1: E_A 3111 (odd), E_B 3282 (even, decay 2 +2/+1/+4 -> release +1/+4/hold): **4/4 FULL 2400/2400**.
  Across E_B: s0 0x0c8 -> 0x0ca on E_B (+2, decay 2's increment) -> 0x0cb (+1); s1 0x084 -> 0x085 (+1) -> 0x089 (+4);
  s2 0x150 -> 0x154 (+4) -> 0x154 (RR 0 holds); witness as above.

Both cycles include the whole release to "off" of the three test slots and the witness's decay, so the stop / off
timing of item (c) is exercised on VOFF 0 constant-input slots too.

## 3. Reproduction

```sh
cd caique-rtl/model
make -C tools eg_model feg_validate filt_validate_model filt_overflow_model
build/tools/eg_model | tail -1; build/tools/feg_validate | tail -1; build/tools/filt_validate_model | tail -1
build/tools/filt_overflow_model 1 24 | tail -1; build/tools/filt_overflow_model 1 24 x | tail -1
g++ -O2 -std=c++17 -o build/work/tail_cmp work/tail/tail_cmp.cpp src/aica_model.cpp
work/tail/tail_cmp_all.sh tests/slot_tail/hw 6491                                  # 11/12 streams FULL, s0 of tail_c to 12198
g++ -O2 -std=c++17 -DCAIQUE_STOP_A=0x3BF -o build/work/tail_cmp_stopa3bf work/tail/tail_cmp.cpp src/aica_model.cpp   # etc.
g++ -O2 -std=c++17 -o build/work/koff_replay work/model5/koff_replay.cpp src/aica_model.cpp
build/work/koff_replay tests/aeg_koff/hw/koff_att e935 koff_att 10422 10444 2400
build/work/koff_replay tests/aeg_koff/hw/koff_d2 7d4b koff_d2 3111 3282 2400
./run_model.sh eg_kprobe && build/work/kfit -case tests/eg_kprobe/model -expect 6491 && build/work/kfit -oldr63 -case tests/eg_kprobe/model
g++ -O2 -std=c++17 -o build/work/tail_dump work/model5/tail_dump.cpp     # print samples with MDEC_CT: tail_dump <prefix> <stream> <c0hex> <from> <to>
```

## 4. What the data forced beyond the task list

1. **No mute at "off" (F6 corrected).**  tail_b stream 0 (VOFF 0, filter on) reads -16 on 4884..4904, 0 on 4905..4922,
   -16 on 4923..4941, 0 on 4942..4960, -16 on 4961..4977, 0 from 4978: the sign of the ringing zero-input filter tail
   (period ~37 samples) through the level law at a saturated at 0x3FF (`floor(x * 64 / 2^22)`: -1 -> -16 for any negative
   x, 0 for any positive one).  The "0 from 4905" of F6 is the first positive half-wave, not a mute.  All earlier
   "silent when off" observations were positive constant inputs (0 at a = 0x3FF anyway) or fetch-stopped slots.  The
   `AEG.off` flag is now only the monitor's 0x1FFF, the end of the envelope stepping and the CA reset.
2. **The FEG keeps running after the fetch stop and after off** (tail_c streams 1 / 2 are FULL only that way).
3. **Rate registers reach the EG one sample late** (tail_c stream 0: the same write lands on the fetch at 11300 and on
   the EG at 11302).  Implemented as a per-slot latch of r10 / r14 / r18 / r40 / r44 refreshed at the end of every
   sample; DL and KRS (in r14) and the FEG rates are latched with it (unmeasured for them; the LPSLNK bit read by
   `stream_step` and the FLV targets stay live).  The sub-sample alternative -- the write landing between slot 0's EG
   update and its fetch within the same sample -- is indistinguishable at the model's resolution and would make this a
   1-in-N coincidence; the latch is the sample-level statement of the same thing.  A cheap console check: rewrite RR on
   a released slot WITHOUT a KYONEX and see whether the first step is at the write's clock or the next.
4. **CA is reset at off, not at the fetch stop** (tests/sgc_keys K4 on the console: the CA-reset log line carries EG
   0x5FFF; a reset at the 0x3C0 stop would have been logged ~360 us earlier with EG 0x43C0).  Whether CA advances or holds
   between the stop and off is not visible (no log line either way).

## 5. What did not fit

- **tail_c stream 0 after the IMXL 0 write: the console's bus 0 reads 0, the model retains 6.**  Buses 1 and 2 retain
  their 2s on both platforms.  The same asymmetry sits in tests/eg_lock mixs: bus 0 read 0 from the start while buses 1
  and 2 retained -8 -- and feg_odd's slots 0 and 2 had identical programs (rates 24/26/28/22, KRS 0) and the same input,
  so their filters had converged to the same state at the stop and the model predicts the same rest (-8) on bus 0 as on
  bus 2.  So twice: a bus loses its retained value when every slot pointing at it has IMXL 0, but only bus 0.  What
  distinguishes bus 0 in both captures: slot 0 points at it, and so do the 60 zeroed slots (ISEL 0, IMXL 0, VOFF 0,
  all registers 0 after aica_quiet), while buses 1 / 2 are pointed at by one IMXL-0 slot with non-zero registers (VOFF 1).
  Two readings fit both captures: (H_A) a slot with IMXL 0 still writes 0 into its bus when some register property
  holds (VOFF 0 / all-zero registers) -- the first writer of a sample stores, so bus 0 becomes 0 -- but not when VOFF 1;
  (H_B) slot 0 always writes (IMXL 0 -> 0).  Discriminating console run: (i) slot 0 with VOFF 1 sending its filter rest
  to bus 1, then IMXL 0 on slot 0 only, zeroed slots left at ISEL 0: H_B -> bus 1 becomes 0, H_A -> bus 1 retains;
  (ii) the zeroed slots re-pointed at ISEL 5 (reg 0x20 = 0x05 on slots 8..63) after slot 5 has sent something and
  stopped: H_A -> bus 5 reads 0.  Not modelled (the production model keeps "IMXL 0 = no write, the bus retains");
  it would have made tail_c 4/4 by construction, without evidence for which rule.
- **Decay 1 -> release on a key-off clock** is assumed to behave like decay 2 (one more decay-1 step).  If the new
  koff_d1 run of cases/aeg_koff.c (added by another agent) shows "no step" instead, change
  `c.aeg_prev == EG_ATTACK` to `c.aeg_prev != EG_DECAY2` in `aeg_clock`.
- **FEG key-off clock when the old segment had `passed` set** (attack / decay 1 that crossed its target on the previous
  clock): the model takes the old segment's increment and direction against FLV4; unmeasured.
- **The "off" lag** (one sample after the overflow clock) is only visible through the monitor and the CA reset; the
  captures cannot distinguish it from an immediate off, so it was kept symmetrical with the measured fetch-stop lag.
- **Key-on during release (F3)**: already the model's behaviour (a = 0x280 loaded on the key-on sample, no step, CA
  restarts on it); nothing changed, kon_rel is covered by the other agent's validator.
- The capture-start shift of the feg_krs / feg_track / feg_probe model outputs (section 2.5) is a property of the
  host harness's sync scan, not of the envelopes.

## 6. Files

- `src/aica_model.cpp`, `src/aica_model.h` -- the model (git diff HEAD -- src/).
- `work/model5/koff_replay.cpp` -- section 2.6 tool; `work/model5/tail_dump.cpp` -- sample / MDEC_CT dumper.
- `work/model5/baseline_*.txt` (validators before the edits), `*_final.txt` (after), `tail_cmp_hw_v1.txt` (intermediate
  state (a)-(d) with mute), `tail_a_v2.txt`, `tail_b_v2.txt`, `tail_c_g0.txt` / `tail_c_g1.txt` / `tail_c_g2.txt` (FEG
  gate variants), `tail_cmp_hw_final.txt`, `control_*.txt`, `kfit_model_final.txt`, `kfit_model_oldr63.txt`,
  `koff_replay_att_c4.txt`, `koff_replay_d2_c1.txt`, `run_model_all.log`, `model_outputs_after.sha256`,
  `run_model_eg_kprobe.log`, `all_cases.txt`.
- `build/work/old/` -- the committed session-4 model built with six cases for the attribution in 2.5 (binaries only).
