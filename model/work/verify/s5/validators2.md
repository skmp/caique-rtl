# validators2 -- the session-5 validators consolidated (2026-09-23, evening)

Task label `validators2`.  Everything below runs from `caique-rtl/model` ($M).  Model state: `src/aica_model.cpp` md5
`bda39fb83324...`, `aica_model.h` `4d6e14933e79...` (the final session-5 model, after the MIXS every-slot-writes change).
Not touched: `src/`, `cases/`, `NOTES.md`, `HANDOVER.md`, `README.md`, `tests/SUMMARY.txt`, the console.  No git commit.

## 1. Files

### Moved into tools/ (single file, no model link; built by the generic rule of tools/Makefile)

| From | To | Edits on the way |
|---|---|---|
| `work/koff/koff_fit.cpp` | `tools/koff_fit.cpp` | include `filt_capture.h`; Build line |
| `work/kprobe/kfit.cpp` | `tools/kfit.cpp` | include; Build line |
| `work/verify/s5/koffdir_check.cpp` | `tools/koffdir_check.cpp` | include; header (the case is `cases/feg_koffdir.c` now, feg_law.h is in tools/); Build line |
| `work/verify/s5/feg_law.h` | `tools/feg_law.h` | header: shared by koffdir_check, koffatt_check and the scratch s3alt.cpp |
| `work/koffatt/koffatt_check.cpp` | `tools/koffatt_check.cpp` | include; header; Build line; **`-u <prefix>` option added** (the koffdir_check format: `<prefix><b>_<k>.u`, int32 per sample from the onset, -1 unknown) |
| `work/koffatt/feg_law.h` | deleted | was a copy of work/verify/s5/feg_law.h; koffatt_check includes tools/feg_law.h |
| `work/mixsw/mixsw_check.cpp` | `tools/mixsw_check.cpp` | build / run lines only (system headers only) |

The other scratch stays where it is (`work/koff/*.txt`, `work/kprobe/*.txt`, `work/koffatt/*.txt`, `work/mixsw/*.txt`,
`work/verify/s5/s3alt.cpp`, `s1_indep.cpp`, `aeg_koff_predict.cpp`, `work/tail/`, `work/minus8/`, `work/model5/`).
`work/verify/s5/s3alt.cpp` includes `"feg_law.h"` from its own directory, which is gone: build it with `-I tools`
(`g++ -O2 -std=c++17 -I tools -o build/work/s3alt work/verify/s5/s3alt.cpp`, checked to compile).

### Edited

- `tools/eg_model.cpp`: group 3 = `ka_<b>_s<k>` (tests/feg_koffatt, 24 runs), see section 2; `Run::witness`, `Run::onset_by_change`,
  RAM kind 4, the `key_write` lambda (KYONB per slot at the key-on / key-off), the witness MIXS compare, the GROUPS line.
- `tools/validate_s5.sh`: eg_replay runs + `koff_d1`; the mixs_write gate (section 3); builds mixsw_check too.
- `tools/Makefile`: a comment naming the fitters (no rule needed: the generic `$(BUILD)/%: %.cpp $(HDRS)` builds them).

### Created

- `work/eg/ka_0_0.u .. ka_7_2.u` (24 files, 20180..21460 bytes): `build/tools/koffatt_check tests/feg_koffatt/hw -u work/eg/ka_`.
- `work/verify/expected/`: `eg_model_all.txt` (89 runs; the 33-run session-4 file kept as `eg_model_all_s4.txt`),
  `eg_replay_{koff_d2,koff_d2b,koff_d1,koff_att,kon_rel}.txt`, `tail_cmp_hw.txt`, `kfit_hw.txt`, `koff_fit_hw_<run>.txt` (5),
  `koffdir_check_hw.txt`, `koffatt_check_hw.txt` (both with `-u`, so the `-> work/eg/...` suffixes are in them),
  `mixsw_check_hw.txt`, `mixsw_check_model.txt`, `validate_s5.txt` (the PASS transcript), `feg_validate.txt` (updated: one
  line changed by the session-5 FEG key-off rule, old file kept as `feg_validate_s4.txt`), `filt_validate_model.txt`,
  `filt_overflow_model_s1w24{,_heldout}.txt` (re-generated, byte-identical to the previous ones).
- `work/model_outputs_2026-09-23c.sha256` (480 files; section 5).
- this report.

## 2. Tool list, build and run (all from $M; `make -C tools -j8` builds everything into build/tools/)

| Tool | Links the model | Run | Console result (tests/<case>/hw) |
|---|---|---|---|
| `build/tools/eg_model [-v] [name...]` | yes | `build/tools/eg_model` (34 s) | `GROUPS session4=33/33 eg_kprobe=8/8 feg_koffdir=24/24 feg_koffatt=24/24`, `TOTAL full=89/89` |
| `build/tools/eg_replay -case tests/aeg_koff/hw/aeg_koff.txt <run>` | yes | 5 runs, < 1 s each | koff_d2 174080/174080, koff_d2b 168704, koff_d1 174592, koff_att 163328, kon_rel 215808; each 4/4 streams FULL, 16/16 clean cycles |
| `tools/tail_cmp_all.sh tests/slot_tail/hw 6491` (build/tools/tail_cmp) | yes | 30 s | 12/12 streams FULL; tail_c retained buses hw 0 2 2 = model 0 2 2 |
| `build/tools/koff_fit <prefix> 0 6491 <run> tests/aeg_koff/hw/aeg_koff.txt` | no | 5 runs | koff_d2 H1 30/30, koff_d2b H1 18/18 (H3 6/18 = the rate-0 slot), koff_d1 H1 18/18, koff_att H3 27/27 (H1/H2 3/27 = attack already over), kon_rel L0 48/48 C0 18/18; no-fit 0 everywhere |
| `build/tools/kfit -v -expect 6491 -case tests/eg_kprobe/hw` | no | 18 s | K = 6491 on all 8 probes, bit 13 = 0, dK +0, `expect K = 6491: all probes match`, exit 0 |
| `build/tools/koffdir_check tests/feg_koffdir/hw -u work/eg/kd_` | no | 9 s, rewrites work/eg/kd_*.u (unchanged) | 8 batches; S3 / noS3 / noStep refuted on the even batches, oldDir fits all (17 "shared K: NONE" lines) |
| `build/tools/koffatt_check tests/feg_koffatt/hw -u work/eg/ka_` | no | < 1 s, writes work/eg/ka_*.u | 6 even / 2 odd; `oldStep 6/6 noStep 0/6 S3 0/6 relInc 0/6` |
| `build/tools/mixsw_check tests/mixs_write/{hw,model}` | no | < 1 s | `H_G 21 agree 0 disagree consistent` on both; the 21 probe/bus verdict rows identical |
| `tools/validate_s5.sh [K]` | -- | ~2 min | PASS (section 3) |
| `build/tools/feg_validate`, `filt_validate_model`, `filt_overflow_model 1 24 [x]` | yes | 4 s / 16 s / 2 s | 9/9 (77862); 265/265 (4286180); 15/15 (150558) and 12/12 (45384) |

Byte-for-byte reproduction of the earlier outputs by the moved tools (section 4 of the task): koff_fit x 5 =
`work/koff/koff_fit_hw_<run>.txt`, kfit = `work/kprobe/kfit_hw_f7.txt`, koffdir_check (with -u) =
`work/verify/s5/koffdir_check_hw2.txt` and the 24 `kd_*.u` unchanged (sha256 before / after), koffatt_check (without -u)
= `work/koffatt/koffatt_check_hw.txt`, mixsw_check = `work/mixsw/check_hw.txt` and (model) `work/mixsw/check_model_final.txt`:
all `cmp` identical, no header line differs (none of them prints its own path).

### eg_model group 3 (feg_koffatt)

- c0 per batch from the `, c0 XXXX` of the cap_start lines of `tests/feg_koffatt/hw/feg_koffatt.txt` in batch order.
- Streams: slot k (k = 0..2) = the FEG program of cases/feg_koffatt.c (VOFF 1, LPOFF 0, Q 4, KRS 15, OCT 0, FNS 0, AR 31, D1R 0,
  RR 0, SA 0x20000 / LEA 8192, FLV / rates from the table); slot 3 = the witness (constant 0x7FFF at 0x10000, loop [0,32), TL 0,
  VOFF 0, LPOFF 1, AR 31 D1R 31 DL 31 D2R 31 RR 31, KRS 1 -> R 63).  One run per (batch, k) as for kd_, 24 runs.
- RAM kind 4 = the 8192-word random signal at 0x20000 AND 48 samples of 0x7FFF at 0x10000.
- Key-on write: KYONB 1 on slot k, KYONB 0 on the witness, KYONEX.  Key-off write (searched in [mark 3 - 64, mark 4 + 400]):
  KYONB 0 on slot k, KYONB 1 on the witness, KYONEX (`Run::witness`, the `key_write` lambda).
- Onset (`Run::onset_by_change`): the first sample in [mark 1 - 400, mark 1 + 400) where one of streams 0..2 changes -- the
  koffatt_check rule (the buses carry the filters' rest values from the previous batch, e.g. `2 -26 2`, so "first non-zero"
  fails from batch 1 on).  On the 8 console batches it is 138 / 139 like koffatt_check's.
- Compare: FEG.v >> 1 of slot k against `work/eg/ka_<b>_<k>.u` (u -1 = ambiguous, skipped) AND MIXS 3 against the capture on
  every sample from the onset.  Because the witness jumps to 520176 on its key-on sample, only the true key-off sample
  survives the search: every ka run reports exactly ONE key-off sample -- ka_0 587 (even), ka_1 651 (odd), ka_2 668 (even),
  ka_3 715 (even), ka_4 779 (even), ka_5 798 (even), ka_6 733 (odd), ka_7 908 (even) = koffatt_check's E per batch.
- The 65 earlier runs are untouched: the first 65 lines of the new `eg_model_all.txt` are identical to
  `work/model5/eg_model_final.txt` (the pre-edit tool on the current model).  Against the session-4 expected file one line
  differs, `ft_0_s1 ... key-off samples 5469(odd)` (was `5468(even) 5469(odd)`): the F4 rule (old increment AND old
  direction on a key-off clock) no longer fits the even candidate for that stream -- a model change, already present in
  `work/verify/s5/validators_eg_model_current.txt` and `work/model5/eg_model_final.txt`.  `feg_validate.txt` moved the same
  way (`ft_0 s1: key-off sample +5331 MDEC_CT cc7b odd`, was `+5330 ... even`), 9/9 either way.

## 3. validate_s5.sh

Changes: `koff_d1` added to the eg_replay runs; the mixs_write gate: `./run_model.sh mixs_write` (the model's own run,
outputs identical to the ones before -- the sha256 of every tests/mixs_write/model file is unchanged, so the run is
deterministic), then `build/tools/mixsw_check` on `tests/mixs_write/model` and `tests/mixs_write/hw`; PASS iff both print
`H_G ... consistent` and their verdict rows (`P<n> <bus> <LOST|KEPT|OTHER>`, the first three fields of the rules table,
21 rows) are identical (`diff`); a differing row set or a refuted H_G is printed.  Exit 0 iff every gate passes.
`make -C tools eg_model eg_replay tail_cmp mixsw_check` first.  Transcript (`work/verify/expected/validate_s5.txt`):

```
model: bda39fb83324 4d6e14933e79  K 6491
eg_model  GROUPS session4=33/33 eg_kprobe=8/8 feg_koffdir=24/24 feg_koffatt=24/24
eg_model  TOTAL full=89/89
eg_replay TOTAL koff_d2: streams FULL 4/4, samples matched 174080/174080, clean cycles 16/16
eg_replay TOTAL koff_d2b: streams FULL 4/4, samples matched 168704/168704, clean cycles 16/16
eg_replay TOTAL koff_d1: streams FULL 4/4, samples matched 174592/174592, clean cycles 16/16
eg_replay TOTAL koff_att: streams FULL 4/4, samples matched 163328/163328, clean cycles 16/16
eg_replay TOTAL kon_rel: streams FULL 4/4, samples matched 215808/215808, clean cycles 16/16
tail_cmp  RESULT tests/slot_tail/hw/tail_a: 4/4 streams FULL, events ko 4650(even)
tail_cmp  RESULT tests/slot_tail/hw/tail_b: 4/4 streams FULL, events ko 4648(even)
tail_cmp  RESULT tests/slot_tail/hw/tail_c: 4/4 streams FULL, events ko 6875(odd) w14 11300(even) w00 11300(even) w2 12198(even)
tail_cmp  TOTAL tests/slot_tail/hw: streams FULL 12/12
mixs_write hw:    H_G  every slot writes its ISEL bus every sample (IMXL is a gain, 0 -> 0); a bus retains iff nobody points at it
H_G  21 agree   0 disagree  consistent
mixs_write model: H_G  every slot writes its ISEL bus every sample (IMXL is a gain, 0 -> 0); a bus retains iff nobody points at it
H_G  21 agree   0 disagree  consistent
mixs_write TOTAL: 21 probe/bus verdict rows, hw and model identical, H_G consistent on both
validate_s5: PASS (every stream FULL, mixs_write verdicts identical)
```

## 4. Every gate, run once more at the end (the current tree, outputs in work/verify/expected/)

| Gate | Result |
|---|---|
| `build/tools/eg_model` | TOTAL full=89/89 (33/33, 8/8, 24/24, 24/24) |
| `tools/validate_s5.sh` | PASS, exit 0 (above) |
| `build/tools/feg_validate` | TOTAL full=9/9 samples=77862/77862 |
| `build/tools/filt_validate_model` | TOTAL qbias=255 full=265/265 consecutive samples=4286180/4286180 (identical to the expected file) |
| `build/tools/filt_overflow_model 1 24` | TOTAL full=15/15 matched_full_samples=150558 (identical) |
| `build/tools/filt_overflow_model 1 24 x` | TOTAL full=12/12 matched_full_samples=45384 (identical) |
| `build/tools/kfit -v -expect 6491 -case tests/eg_kprobe/hw` | K 6491 x 8, `expect K = 6491: all probes match`, exit 0 |
| `tools/tail_cmp_all.sh tests/slot_tail/hw 6491` | 12/12 |
| eg_replay x 5 | 4/4 streams, 16/16 clean cycles each (numbers above) |

The eg_replay expected files differ from `work/verify/s5/validators_eg_replay_*.txt` only because those were made on the
FIRST console run (c0 7d4b ..., now `tests/aeg_koff/hw_run1`); the current `tests/aeg_koff/hw` is the second run (koff_d2
c0 a78b, n_first 677) -- the tool is unchanged and every verdict is the same.

## 5. Model-output baseline `work/model_outputs_2026-09-23c.sha256`

`(cd tests && find */model -type f ! -name console.log ! -name status | LC_ALL=C sort | xargs sha256sum)`, 480 files
(the 47 cases' outputs as left by the orchestrator's `./run_model.sh` on every case, 19:13-19:14, all `status` 0; the
mixs_write outputs re-made by validate_s5.sh hash the same).  Against `model_outputs_2026-09-23b.sha256` (390 files):
**362 unchanged, 28 changed, 90 new, 0 removed.**

New (the six session-5 cases): aeg_koff 11 files, eg_kprobe 17, feg_koffatt 18, feg_koffdir 18, mixs_write 17, slot_tail 9.

Changed, attributed (26 of them are the list of `work/verify/s5/model_fixes.md` section 2.5, and their hashes equal
`work/model5/model_outputs_after.sha256`, i.e. the MIXS change after it did not touch them):

| Files | Session-5 rule | Check |
|---|---|---|
| sgc_aeg/model/{att_06_01,att_22_16,att_31_28,dec2,rel_a,rel_b}_eg.txt, sgc_aeg.txt | R6: fetch stop at 0x3C0 and "off" one sample after the overflow clock (EG monitor logs of releases to off) | model_fixes 2.5; hw-vs-model line count unchanged (33) |
| sgc_krs/model/sgc_krs.txt | R5: the R 63 attack leaves the attack on the key-on clock, so a slow decay 1 first steps at +2 (`34:516080 ...` -> `2:516080 34:511984 ...`) | `git diff HEAD`; hw-vs-model differing lines 704 -> 680 (closer to the console: 24 fewer) |
| eg_lock/model/odd_dec.bin | R5 (same, the model run's even onset) | model_fixes 2.5 |
| eg_lock/model/mixs.bin | R6/R7: the stopped slot's tail (stop 8 clocks earlier, FEG running on) and the bus retention | model_fixes 2.5 |
| sgc_lfo2/model/l2_1.bin | R6: a release to off (165 samples from 18105) | model_fixes 2.5 |
| cap_selftest/model/{cap.bin, cap_selftest.txt} | R6: the slots stop 15 samples earlier (`88456 samples checked to 89352` -> `88441 ... 89337`) | `git diff HEAD`; hw-vs-model 8 lines as before |
| feg_krs/model/{feg_krs.txt, fk_1.bin, fk_1.hdr, fk_2.bin, fk_2.hdr, fk_3.bin}, feg_track/model/{feg_track.txt, ft_0.bin, ft_1.bin, ft_1.hdr, ft_2.bin, ft_2.hdr} | R11 harness artefact: the capture START moved by one sample (`head n 678 -> 677`, `624 -> 623`): cap_start's sync scan cost depends on the ring words the previous run left, which changed with R6/R7; the envelopes are unchanged (eg_model / feg_validate FULL) | `git diff HEAD` on the .txt; hw-vs-model line counts unchanged (10 / 8) |
| feg_probe/model/feg_probe.txt | R6 (c''): the FEG keeps stepping after off -- ONE monitor reading `0 e200` -> `0 e1fc`, see section 6 | `git diff HEAD` = that one line; hw-vs-model 14356 -> 14358 lines |
| sgc_level/model/sgc_level.txt (changed AFTER model5) | R7: every slot writes its bus, IMXL 0 sends 0: `L2 ... IMXL 0..15: 4064 4063 ...` -> `0 4063 ...` | **now equals the console** (`tests/sgc_level/hw`: `0 4063 ...`); hw-vs-model 4 -> 2 differing lines: only L5 is left (known, inherited filter state) |
| probe/model/probe.txt (changed AFTER model5) | R7: `MIXS0.l 4500 ... w55555555->r00000005 waaaaaaaa->r00000005` -> `r00000000 r00000000` (bus 0 rewritten by the 64 IMXL-0 slots) | pre-existing difference in a new form, see section 6; hw-vs-model 44 lines before and after |

The DSP cases: dsp_basic / dsp_temp / dsp_mem / dsp_mem2 / dsp_mem3 model outputs are byte-identical to git HEAD
(`git diff HEAD -- tests/<case>/model/` empty), so their console-vs-model differences are the pre-existing ones: dsp_basic 3
lines (`A ffff -4096 1` T= stale slot, `F MIXS5 right after write`, `F ira 25` -- the excluded sub-sample MIXS write),
dsp_temp 64 lines (microsecond timestamps and the TEMP-ring writer position of a whole-program run), dsp_mem* 0 lines.
sgc_keys.txt, eg_lock.txt, sgc_aeg.txt, cap_selftest.txt: unchanged line counts (63 / 36 / 33 / 8) -- monitor timing /
key-on phase of whole-program runs, as before.

## 6. Console-vs-model differences: what is new

**One new difference** (expected: none): `tests/feg_probe` "extremes" probe, first monitor reading: console `0 e200`,
model now `0 e1fc`, model at HEAD `0 e200`.  Mechanism, from the case (`cases/feg_probe.c`) and the model:

- The preceding "fast" probe leaves slot 0's FEG in release, held at FLV4 = 0x200 from above (dir -1, FRR 28 = R 56, +4 per
  clock, coming down from the decay-2 value and holding at 0x200 exactly).  The
  console's own log agrees: `... 6647 6200, 6895 e200, ...` to the probe end.
- `run()` starts with `aica_quiet()`: KYONB 0 + RR 31 + KYONEX (the AEG runs to off within ~6 ms), 20 ms, then
  `ch_zero_regs` writes reg 0x00..0x7C = 0 in address order: FLV4 (0x3C) is zeroed two writes BEFORE the FEG rates (0x44).
- Model since session 5: the FEG keeps stepping after off (R6 c'', forced by tail_c) and reads its rate register one sample
  late (R8 e, measured for RR only).  On the sample of the 0x3C write the release target becomes 0 while the rate is still
  R 56: C = (0x200 >= 0) is true, nv = 0x1fc >= 0 keeps C, so a clock in that window steps -4 -> 0x1fc; the host harness
  put a clock there.  Rate 0 then holds 0x1fc; the extremes config (FLV4 0x1fff, FRR 20) and the key-on follow, and the
  first EGMON poll reads the pre-key-on state e1fc.
- Console: e200, no step.  With the model's one-sample rate latch the window is up to a whole sample (~50 % chance of a
  clock); with a live FEG rate it is the two G2 writes (~5 us, ~11 %).  One reading of "no step" therefore does not decide
  between chance and a real difference (the FEG rate registers not latched / the FEG frozen once the AEG is off in this
  situation / the release target latched).  It is the open item "whether the FEG rate registers share the one-sample
  latch" (R11) plus "does the release keep its hold when FLV4 is rewritten under it".  Cheap console check: a slot
  released to off with its FEG held at FLV4; rewrite FLV4 alone (FRR kept) and poll the monitor -- a step toward the new
  target confirms the model (and the e200 was chance), a hold refutes it.  The other three probe boundaries are
  consistent on both platforms (slow -> fast: `e1ff` = the slow release's hold short of 0x200 from below, no step;
  extremes -> rate0: `fffe` on both, the R 40 tick did not fall in the window on either).
- Not changed here (src/ is the model agent's); the datum is one monitor reading in a whole-program run.

**Pre-existing differences whose form changed** (not new): `tests/probe` `MIXS0.l 4500`: console reads its own write's
low nibble back at once (`wffffffff->r0000000f`, `w55555555->r00000005`, `waaaaaaaa->r0000000a`) and MIXS0.h as written;
HEAD's model read `r00000000 / r00000005 / r00000005` (two of four values wrong), the current model `r00000000` for all
(every one of the 64 IMXL-0 slots now rewrites bus 0, so the CPU value is gone by the read).  Same open item as dsp_basic
"F ira 25": the sub-sample order of a CPU MIXS write against the SGC write (R11).  Everything else in probe.txt
(interrupt registers 2888/289c/28a0/28b8, microsecond timings, CA values, the LP bit after key-off) is unchanged since HEAD.

**Pre-existing difference closed**: `tests/sgc_level` L2 IMXL 0 (model 4064 = a retained bus, console 0): the R7 rule
makes the model read 0 too.  sgc_level now differs from the console in L5 only.

## 7. Notes for the handover

- `work/verify/expected/` now holds every session-5 gate output; `tools/validate_s5.sh` is the one command (PASS / FAIL,
  exit code).  A rebooted console changes K: eg_model's run table carries K per run (lo_2 165), validate_s5's argument
  goes to eg_replay / tail_cmp only.
- The `-u` outputs (kd_, ka_) are console-derived and model-independent; regenerate them only after a new console run of
  feg_koffdir / feg_koffatt.
- `work/verify/s5/s3alt.cpp` needs `-I tools` for feg_law.h now.
- The Makefile's generic rule compiles the fitters with `-fopenmp` like every other tool; none of them uses it.
