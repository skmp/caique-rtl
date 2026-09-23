# validators -- making the production model checkable against every session-5 console capture (2026-09-23)

Task label `validators`.  Everything below runs from `caique-rtl/model` ($M).  `src/` was NOT touched (another agent
is changing the model in parallel); the "before" numbers are from the **session-4 model = git HEAD** (`b1f60f6`,
`git show HEAD:model/src/*` compiled into `build/work/before/`), the "current" numbers from the working tree at the
time of the run (`src/aica_model.cpp` md5 `c9aeef1b...`, `aica_model.h` `b066ea6b...`, 18:12), which already carries
the other agent's F1/F2/F4/F5/F7 changes.  All console captures used are the ones under `tests/<case>/hw/` (K 6491).

## Files

| File | What |
|---|---|
| `tools/eg_replay.cpp` (new) | multi-cycle AEG replay through the production model with key events (KYONB masks + one KYONEX) at given samples; `-case` derives the tests/aeg_koff events from the capture, `-ev` is the generic mode |
| `tools/eg_model.cpp` (edited) | + 8 `kp_p0..kp_p7` AEG runs (tests/eg_kprobe) and 24 `kd_<b>_s<k>` FEG runs (tests/feg_koffdir, u files); `GROUPS` line; the first-mismatch context is now the BEST key-off candidate's state (it used to be the last candidate tried) |
| `tools/tail_cmp.cpp` (new, copy of `work/tail/tail_cmp.cpp` with the include paths of tools/) | slot_tail replay; `work/tail/tail_cmp.cpp` kept |
| `tools/tail_cmp_all.sh` (new) | the three slot_tail runs of a directory through `build/tools/tail_cmp` (c0 from the `, c0 XXXX` of the cap_start lines), prints `TOTAL <dir>: streams FULL n/12` |
| `tools/validate_s5.sh` (new) | one-shot gate: eg_model + eg_replay x 4 + tail_cmp_all, summary lines + PASS/FAIL, exit 0 iff everything FULL |
| `tools/Makefile` (edited) | rules for `eg_replay` and `tail_cmp` (link `../src/aica_model.cpp`, like `eg_model`) |
| `work/kprobe/kfit.cpp` (edited) | F7: the R 63 attack leaves the attack on the first clock at or after the key-on sample (`-oldr63` = the session-4 rule as a control) |
| `work/kprobe/kfit_hw_f7.txt`, `kfit_hw_oldr63.txt` (new) | kfit outputs on the console probes, new rule / control (the old `kfit_hw.txt` untouched) |
| `work/verify/s5/koffdir_check.cpp` (edited) | `-u <prefix>` writes the recovered u per stream (`work/eg/kd_<b>_<k>.u`, the fk_*.u format); c0 parse fixed to `, c0 ` (a bare `c0 ` matched the ring address `at 0bc0 (n 512)` of kd_7 and DROPPED that batch: the earlier `koffdir_check_hw.txt` says "7 batches") |
| `work/verify/s5/koffdir_check_hw2.txt` (new) | koffdir_check on all 8 console batches (kd_7 included) |
| `work/eg/kd_0_0.u .. kd_7_2.u` (new, 24 files) | tracked FEG u(n) of the feg_koffdir console captures, read by eg_model |
| `work/verify/s5/validators_*.txt` (new) | the outputs quoted below: `eg_model_{before,current}`, `eg_replay_{before,current}_<run>`, `tail_cmp_{before,current}` |

Nothing in `cases/`, `src/`, `work/koff/`, `tests/` was edited.  `feg_law.h` was not needed.

## Commands

```sh
cd caique-rtl/model
make -C tools eg_model eg_replay tail_cmp              # -> build/tools/ (or make -C tools for all 40)
build/tools/eg_model                                   # 65 runs, ~28 s; GROUPS session4=a/33 eg_kprobe=b/8 feg_koffdir=c/24; TOTAL full=n/65
build/tools/eg_model kp_p5 kd_0_s0                     # a selection
for r in koff_d2 koff_d2b koff_att kon_rel; do build/tools/eg_replay -v -case tests/aeg_koff/hw/aeg_koff.txt $r; done   # <1 s each
tools/tail_cmp_all.sh tests/slot_tail/hw 6491          # ~20 s; TOTAL tests/slot_tail/hw: streams FULL n/12
tools/validate_s5.sh [K]                               # all of the above, ~1.6 min, PASS/FAIL (K -> eg_replay/tail_cmp only; eg_model -K is a GLOBAL override that breaks lo_2, K 165)
# standalone (no model): the K fitter and the FEG u recovery
g++ -O2 -std=c++17 -o build/work/kfit work/kprobe/kfit.cpp
build/work/kfit -v -expect 6491 -case tests/eg_kprobe/hw            # F7 rule, 17 s
build/work/kfit -v -oldr63 -expect 6491 -case tests/eg_kprobe/hw    # session-4 rule (control)
g++ -O2 -std=c++17 -o build/work/koffdir_check work/verify/s5/koffdir_check.cpp
build/work/koffdir_check tests/feg_koffdir/hw -u work/eg/kd_        # 9 s; rewrites work/eg/kd_*.u (only needed after a new console run)
```
eg_replay generic mode (any capture, explicit events; masks are hex over SLOT numbers, a slot in neither mask keeps its KYONB):
`build/tools/eg_replay <prefix> <c0 hex> <K> -txt tests/aeg_koff/hw/aeg_koff.txt koff_d2 -ev 458:7:8 -ev 601:8:7` or
`-slot k:ISEL:AR:D1R:DL:D2R:RR:KRS:OCT:FNS:ramp` per stream.  Checked: with only cycle 0's two events every stream
matches up to the next E_A (3111) and the witness up to its next key-on (3282).

## (1) eg_replay -- tests/aeg_koff (4 runs, 16 cycles each, 4 streams)

How the events are pinned (koff_fit's rules): E_A = the first 496 (a 0x280) after a silent gap on stream 0 within
mark 1 +- 300 (`keyx(TEST)`: KYONB on slots 0..2, off slot 3); E_B (koff runs) = the witness onset (stream 3 jumps to
520176 and holds it) within mark 3 +- 300 (`keyx(WIT)`); kon_rel: E_C = the witness onset within mark 5 +- 300
(all on), E_B (all off) searched in [E_A + 100, E_C - 50] and E_D (all off) in mark 7 +- 300 for the best SUMMED
per-stream matched prefix from a snapshot (ties listed; the first taken -- an even ko and the odd ko + 1 tie whenever
the key-off is from a rate-0 segment, which every kon_rel key-off is: the R 48 attack from 0x280 reaches 0 in ~67
clocks, the cycle keys off after 66).  Model side: slots as the case, RAM constant + ramp, 16 warm-up samples,
MDEC_CT = (c0 - n_first) for sample 0, then per sample the KYONB writes and one KYONEX BEFORE the step() that
produces it; every sample of every stream compared from sample 0.  Self-check on the model's own captures
(`tests/aeg_koff/model`, 5 runs incl. the new koff_d1): 5/5 runs 4/4 streams FULL.

Before (session-4 model, `validators_eg_replay_before_<run>.txt`):

| run | c0 | streams FULL | samples matched | clean cycles | what fails |
|---|---|---|---|---|---|
| koff_d2 | 7d4b | 3/4 | 173050/174080 | 11/16 | witness (stream 3) at every EVEN E_B + 2: hw 487408 (a 8) model 520176 (a 0): 5 cycles |
| koff_d2b | ac67 | 3/4 | 166232/168704 | 4/16 | witness at even E_B + 2: 12 cycles |
| koff_att | e935 | 0/4 | 155616/163328 | 12/16 | streams 0..2 at every even E_B + 0 (the model adds the attack increment on the key-off clock -- H1; hw: no step, F2), e.g. 7920: hw 87024 model 86000 (a 0x0ab); witness at even E_B + 2; 4 even cycles (3, 4, 9, 14) |
| kon_rel | 2b5e | 3/4 | 213236/215296 | 6/16 | witness at every even E_C + 2 (10 cycles); streams 0..2 FULL (F3 = L0/C0 is what the model does; every E_B/E_D is from a rate-0 decay 1, so F1/F2 are not exercised here) |

The **witness failure is F7 on the AEG** (KRS 1 -> R 63 attack keyed on an even sample: decay 1's first +8 lands at
E_B + 2, one clock earlier than the session-4 rule) -- 31 even witness key-ons across the 4 runs.  The task's
expectation "koff_d2 / koff_d2b / kon_rel FULL already" holds for the TEST streams (0..2) only.

Current tree (F1/F2/F7 in the model): **4/4 runs, 4/4 streams FULL, 16/16 clean cycles each**
(`validators_eg_replay_current_<run>.txt`): koff_d2 174080/174080, koff_d2b 168704/168704, koff_att 163328/163328,
kon_rel 215296/215296.  Expected after = the same.

## (2) eg_model -- 65 runs

New runs: `kp_p0..kp_p7` (tests/eg_kprobe/hw, c0 from the `probe pN: c0` lines, RAM kind 3 = 48 x 0x7FFF, key-off
window mark 3 - 400 .. mark 4 + 400 because the marks drift up to ~100 samples over the 1.5 s key-on) and
`kd_<b>_s<k>` (tests/feg_koffdir/hw, c0 from the `, c0` of the cap_start lines in batch order, programs of
cases/feg_koffdir.c, reference slot 3, u files `work/eg/kd_<b>_<k>.u`; batches 6/7 search the key-off around marks
7/8 = the KYONEX 100 ms after the KYONB-only clears, which the model ignores as the console does).  Runs whose text
file is missing are skipped with a note.  The 33 session-4 runs are untouched (33/33 in both builds).

Before (`validators_eg_model_before.txt`, 23 s): `GROUPS session4=33/33 eg_kprobe=5/8 feg_koffdir=16/24`, **TOTAL full=54/65**
- kp_p5 / kp_p6 / kp_p7 (even onsets 13b8 / e510 / b324): `fail 2/67166; first mismatch at onset+2 (even): s3 hw 516080 model 520176` -- F7 (decay 1 R 45 steps +1 on the key-on clock's successor; the model's attack still occupies that clock).  kp_p2 (even onset 98c6) FULL: row-1 increment 0 at that clock.  The odd-onset probes p0/p1/p3/p4 FULL.  Key-off samples found: an (even, odd+1) pair per probe (rate-0 decay-1 key-offs).
- kd_0 / kd_1 / kd_5 / kd_6 (even key-off clocks 4565 / 4628 / 4776 / 9247) streams 0 and 2: `fail at the key-off clock: hw u d00 model v 1a06` (slot 0: hw 0x1A00 = old increment -4 in the OLD direction; the model went +4 toward FLV4, the best candidate is an odd key-off 3 samples earlier) and `hw u d01 model v 19fd` (slot 2: hw 0x1A02 = +4 old direction; model -4).  Stream 1 (both directions up) FULL on every batch = the S3 witness.  kd_2 / kd_3 / kd_4 / kd_7 (ODD key-offs 4647 / 4693 / 4756 / 9203) FULL.  Note: **kd_7's key-off is odd**, not even as the task text says -- kd_7 was missing from the earlier koffdir_check output (the c0 parse bug above); the even batches are kd_0, kd_1, kd_5, kd_6.

Current tree (F4 + F7 in the model, `validators_eg_model_current.txt`, 28 s): `GROUPS session4=33/33 eg_kprobe=8/8 feg_koffdir=24/24`, **TOTAL full=65/65**.  Expected after = 65/65.

## (3) tail_cmp -- tests/slot_tail/hw (3 runs x 4 streams)

`tools/tail_cmp.cpp` is the promoted copy (the analysis is work/tail's; the stage-0 inherited filter state, the
key-off / RR-rewrite / IMXL searches are unchanged).  `tools/tail_cmp_all.sh tests/slot_tail/hw 6491`.

Before (`validators_tail_cmp_before.txt`): **1/4 + 2/4 + 1/4 = 4/12 streams FULL** (the reference streams and tail_b's
unity-cutoff limit cycle).  tail_a s0/s2 fail at 4891 (the model's slot still plays until a > 0x3FF at 4892: hw
zero input from the 120th release clock, F5), s1 at 6571 (same, R 48); tail_b s0 at 4894 (hw -16 until 4904: the
VOFF 0 mute is at 0x400, F6) and s1 at 4891 (hw 0: fetch stop at 0x3C0); tail_c s0 11541 (RR-rewrite tail), s1 7115,
s2 8411 (fetch stops).  tail_a's key-off is found at 4636 with 398 ties (the model's 0x400 stop hides it; F5 says
4650/4651).

Current tree (`validators_tail_cmp_current.txt`): tail_a **4/4** (ko 4650 even), tail_b **4/4** (ko 4648), tail_c 1/4:
s0 fails at 12198 (+11972, even: hw 0 model 6 -- the retained-bus value after IMXL 0 / the "off" of the RR-rewritten
slot 0; the model's slot is `a 3ff rel OFF`), s1 at 7118 (hw -26918 model -26930, a 3d0), s2 at 8416 (hw 35590 model
35594, a 3c4) -- small filter-tail residuals 3 samples after the fetch stop; the other agent's F5/F6 work in
progress.  **9/12**.  Expected after = 12/12.

## (4) kfit (standalone, no model) -- F7

`work/kprobe/kfit.cpp` `sim()`: at a clock with the state ATT and R 63, the state becomes D1 without a step, on the
key-on sample too when it is a clock (`-oldr63`: only from the first clock after it, the session-4 rule).
`build/work/kfit -v -expect 6491 -case tests/eg_kprobe/hw` (`kfit_hw_f7.txt`): **all 4 streams of all 8 probes fit**,
`expect K = 6491: all probes match`, exit 0:

| probe | onset | K | mod 8192 | bit 13 | dK | drift | action |
|---|---|---|---|---|---|---|---|
| p0 | f387 odd | 6491 | 6491 | 0 | +0 | - | none |
| p1 | c7eb odd | 6491 | 6491 | 0 | +0 | +26 | none (repeatability) |
| p2 | 98c6 even | 6491 | 6491 | 0 | +0 | +23 | RBP/RBL rewrite |
| p3 | 6bfb odd | 6491 | 6491 | 0 | +0 | -86 | TIMA/B/C writes |
| p4 | 3fe7 odd | 6491 | 6491 | 0 | +0 | +26 | MVOL writes |
| p5 | 13b8 even | 6491 | 6491 | 0 | +0 | +25 | ARM7 released 5 ms |
| p6 | e510 even | 6491 | 6491 | 0 | +0 | +24 | 128-step NOP DSP program |
| p7 | b324 even | 6491 | 6491 | 0 | +0 | -88 | 64-slot register sweep + KYONEX |

Control `-oldr63` (`kfit_hw_oldr63.txt`): p5 / p6 / p7 stream 3 `NO K fits: best 2/65746 ... first mismatch i 228 (+2)
a 000 state 1 level 520176 hw 516080` (K still 6491 from streams 0..2); everything else identical.  The key-off
mode result is unchanged (mode 0 = mode 1, rate-0 segments).

## (5) Before / after summary

| gate | session-4 model (HEAD) | current tree (18:12) | expected after F1-F7 |
|---|---|---|---|
| `eg_model` (65 runs) | 54/65 (33/33, 5/8, 16/24) | 65/65 | 65/65 |
| `eg_replay` koff_d2 / koff_d2b / koff_att / kon_rel (streams) | 3/4, 3/4, 0/4, 3/4 | 4/4 x 4 | 4/4 x 4, 16/16 clean cycles each |
| `tail_cmp_all tests/slot_tail/hw` (streams) | 4/12 | 9/12 (tail_c s0/s1/s2) | 12/12 |
| `kfit -case tests/eg_kprobe/hw` (standalone) | 29/32 streams (old rule) | 32/32, K 6491 x 8 | -- |

`tools/validate_s5.sh` runs the three model gates and prints PASS only at 65/65 + 16/16 + 12/12.

## Notes for the orchestrator

- The witness slot (AR 31 KRS 1, R 63) is an F7 probe of its own: every even E_B / E_C shows decay 1 stepping at +2.
  Any model that passes eg_replay 4/4 has F7 for the KRS-1 path too.
- eg_model's u files for kd_ come from the console captures and do not depend on the model; regenerate them only
  after a new feg_koffdir console run (`koffdir_check ... -u work/eg/kd_`).  The c0 parse of koffdir_check was wrong
  for kd_7 (fixed); tools/eg_model and tools/eg_replay match `, c0 ` for the same reason.
- eg_replay's kon_rel E_B / E_D come out as (even, odd+1) ties because those key-offs are from a rate-0 decay 1; an
  informative AEG key-off from a running segment is what koff_d2 / koff_d2b (F1) and koff_att (F2) provide, where the
  witness pins the sample exactly.
- tail_cmp's stage-1 key-off for tail_a / tail_b is only pinned to an (even, odd+1) pair (D2R 0 slots); tail_c pins
  it exactly through the FEG.

## validate_s5.sh on the current tree (`validators_validate_s5_current.txt`)

```
model: 25af8e139ae1 b066ea6bd214  K 6491   (the tree moved on while this ran: aica_model.cpp md5 25af8e13... at 18:3x)
eg_model  GROUPS session4=33/33 eg_kprobe=8/8 feg_koffdir=24/24
eg_model  TOTAL full=65/65
eg_replay TOTAL koff_d2: streams FULL 4/4, samples matched 174080/174080, clean cycles 16/16
eg_replay TOTAL koff_d2b: streams FULL 4/4, samples matched 168704/168704, clean cycles 16/16
eg_replay TOTAL koff_att: streams FULL 4/4, samples matched 163328/163328, clean cycles 16/16
eg_replay TOTAL kon_rel: streams FULL 4/4, samples matched 215296/215296, clean cycles 16/16
tail_cmp  RESULT tests/slot_tail/hw/tail_a: 4/4 streams FULL, events ko 4650(even)
tail_cmp  RESULT tests/slot_tail/hw/tail_b: 4/4 streams FULL, events ko 4648(even)
tail_cmp  RESULT tests/slot_tail/hw/tail_c: 3/4 streams FULL, events ko 6875(odd) w14 11300(even) w00 11300(even) w2 12064(even)
tail_cmp  TOTAL tests/slot_tail/hw: streams FULL 11/12   (tail_c s0 still fails at 12198: hw 0 model 6 after the IMXL 0 write; s1/s2 now FULL)
validate_s5: FAIL (see above)
```
(the script's first draft passed `-K` to eg_model as well, which is a global override and made `lo_2` (K 165) fail:
removed -- eg_model's runs carry their own boot constant.)  After the model's tail_c fixes the last line must read
`validate_s5: PASS (every stream FULL)`.
