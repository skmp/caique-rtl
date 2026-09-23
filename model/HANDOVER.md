# caique AICA model — session 5 handover (2026-09-23, night)

**Scope: verify the session-5 results.**  Session 4 is commit `ba720b9` (its handover: `git show ba720b9:model/HANDOVER.md`);
session 5 is uncommitted (`git diff HEAD` plus the untracked files).  Session 5 ran on the **same console boot as
session 4 (K 6491)**, added six console cases (every capture with 0 counter errors), pinned the key-off sample with a
witness slot, measured the key-off clock on every envelope segment, the slot stop / off sequence, the K counter's
bit 13 and its non-resetters, and the MIXS writer rule, and folded eight model changes in.  Findings are in
[NOTES.md](NOTES.md) (the new section "Key-off clock, slot stop and MIXS writers" and the edited Access / Slot levels /
Amplitude envelope / Loops / FEG / Envelope clock / MIXS retention / Open items bullets); the reports with every number
are `work/verify/s5/*.md` (case_aeg_koff, case_eg_kprobe, case_slot_tail, case_mixs_write, cases_ext, analysis_minus8,
S1, S3alt, model_fixes, validators, docs).  **The filter arithmetic, DSP, levels and pitch are out of scope**
(unchanged; their validators still pass, see T10).

## What changed

| Area | Change | Claims |
|---|---|---|
| Envelope counter | K = 6491 exactly (bit 13 = 0) from an R 3 decay row; RBP/RBL, timers, MVOL, ARM7 release, DSP program load, 64-slot sweep do not change it | T1, T2 |
| AEG key-off clock | decay 1 / decay 2: one more step with the old increment, then the release; attack: NO step; odd samples: nothing | T3, T5 |
| FEG key-off clock | one more step of the old segment with its increment AND direction (attack included), hold check vs FLV4; KYONB alone holds | T4, T5 |
| Key-on in release | a = 0x280 on the key-on sample, no step, CA restarts on that sample (supersedes "on the next clock") | T6 |
| R 63 attack | leaves the attack on the first clock at or after the key-on sample (the key-on sample itself when it is a clock) | T7 |
| Slot stop / off | fetch stops at a = 0x3C0, off past 0x3FF, each one sample after its clock; NO mute; the FEG runs on; the EG reads its rate registers one sample late | T8 |
| MIXS | every slot writes its ISEL bus every sample, IMXL a pure gain; a bus retains only when no slot points at it; the eg_lock -8 = filter deadband rest at 0x1BFF Q 4 | T9 |
| Cases | `aeg_koff` (5 runs x 16 cycles), `eg_kprobe` (8 probes), `slot_tail` (3 runs), `feg_koffdir` (8 batches), `feg_koffatt` (8 batches), `mixs_write` (8 probes); console captures under `tests/<case>/hw/` (aeg_koff's first run in `tests/aeg_koff/hw_run1/`) | — |
| Tools | standalone fitters `kfit`, `koff_fit`, `koffdir_check` + `feg_law.h`, `koffatt_check`, `mixsw_check`; model replays `eg_replay`, `tail_cmp` + `tail_cmp_all.sh`; `eg_model` extended to 89 runs (GROUPS line: session4 33, eg_kprobe 8, feg_koffdir 24, feg_koffatt 24); `validate_s5.sh` one-shot gate | — |
| Model | `stop_in` / `off_in`, no mute, FEG gate removed, `egreg` + `eg_latch`, `feg_prev_dir`, attack key-off rule, R 63 on the key-on clock, `sent[]` for every slot; macros `CAIQUE_STOP_A` / `CAIQUE_STOP_LAG` / `CAIQUE_MUTE` for the controls only | T8, T9, T10 |
| Scratch | `work/koff/`, `work/kprobe/`, `work/tail/`, `work/koffatt/`, `work/mixsw/`, `work/minus8/`, `work/model5/` (controls, baselines), `work/eg/kd_*.u`, `ka_*.u` (tracked FEG u of feg_koffdir / feg_koffatt) | — |

Model diff: `git diff HEAD -- src/ tools/`.  New expected outputs (`work/verify/expected/`): `eg_model_all.txt` (updated
to 89 runs; the session-4 version kept as `eg_model_all_s4.txt`, likewise `feg_validate_s4.txt`), `eg_replay_<run>.txt` (koff_d2,
koff_d2b, koff_d1, koff_att, kon_rel), `tail_cmp_hw.txt`, `validate_s5.txt` (the gate's PASS output), `kfit_hw.txt`,
`koff_fit_hw_<run>.txt`, `koffdir_check_hw.txt`, `koffatt_check_hw.txt`, `mixsw_check_hw.txt`, `mixsw_check_model.txt`.
Model-output hashes after session 5: `work/model_outputs_2026-09-23c.sha256` (changed against `...23b`: the cases with a
release to off or VOFF captures, itemised in `work/verify/s5/model_fixes.md` 2.5).

**Session-4 claims S1-S9 in one paragraph.**  S1 (clock on even MDEC_CT, `eg_cnt = K - MDEC_CT/2`, one K per boot),
S2 (key events on the next sample, no step on a key-on sample), S4 (rows 5/9/13, the AEG's R < 48 offset), S6 (decay 1
→ 2 on `a[9:5] == DL`), S8 (filter form / width) and S9 (regression) stand unchanged; S1 was re-derived independently
(T2) and K is now exact (T1).  **S3 was re-worded**: the key-off clock takes one more step of the OLD segment (increment
and direction), not "the old increment toward the release target" -- the two coincide on every session-4 event; on the
AEG the decays take the old step and the exponential attack takes none (T3, T4).  **S5 was refined**: the R 63 attack
leaves the attack on the key-on sample when that is a clock, not always on the next clock (T7).  **S7 was superseded**:
the retention condition is "no slot points at the bus", not "no slot with IMXL != 0" (T9); the two-bank DSP read stands.

## Rules for swarm agents

Unchanged from sessions 3-4: main tree read-only except your report; work in a private copy (`build/swarm/$ID`, rsync
without `build`); console only for the agent assigned group H, through `./run_hw.sh` in the copy (L2 isolation);
everything you write must be trackable by git (merge cases / tools / captures / results into `cases/`, `tools/`,
`tests/<case>/`, `work/`); C++ integer math only, nothing in `/tmp`; report at `model/work/verify/<ID>.md` with
CONFIRMED / REFUTED / INCONCLUSIVE per claim; independent re-derivations from the NOTES formulas are worth more than
re-running our tools.

## Setup (inside the private copy)

```sh
make -C tools -j8        # every tools/*.cpp -> build/tools/ (eg_model, eg_replay, tail_cmp, feg_validate, filt_*_model link src/aica_model.cpp)
make -C host -j8         # 47 case binaries -> build/host/
mkdir -p build/work      # scratch programs (work/**/*.cpp) build here, e.g.:
g++ -O2 -std=c++17 -fopenmp -o build/work/s1_indep work/verify/s5/s1_indep.cpp
build/tools/eg_model | tail -n 1            # sanity: TOTAL full=89/89  (about half a minute)
tools/validate_s5.sh                        # sanity: last line "validate_s5: PASS (every stream FULL, mixs_write verdicts identical)"  (~2 min)
```

`validate_s5.sh [K]` runs eg_model (89 runs), eg_replay on the five aeg_koff runs, tail_cmp_all on tests/slot_tail/hw and
the mixs_write gate (`./run_model.sh mixs_write`, then `mixsw_check` on tests/mixs_write/hw and /model: H_G consistent on
both and identical verdict rows); its output is `expected/validate_s5.txt`.  It runs from
caique-rtl/model; K (default 6491) goes to eg_replay / tail_cmp only (eg_model's runs carry their own boot constant,
sgc_loop lo_2 has 165).  The ring position of a capture: `cap_start: ... c0 XXXX` in the case's text output (match
`, c0 ` with the comma -- a bare `c0 ` also matches inside the ring address), `n_first` = word 3 of the `.hdr`; the
MDEC_CT of capture sample i is `(c0 - n_first - i) & 0xFFFF`.  Every fitter below is standalone (NOTES formulas, no
model linked) unless it says "through the model".

## Claims

### T1 — K = 6491 exactly (bit 13 = 0) and nothing a program does resets it (tests/eg_kprobe)

Eight probes (`p0..p7`, 67392 samples each, 0 errors), each an R 63 key-on of four constant-input slots with decay 1
at R 3 / 13 / 29 / 45 for 1.5 s; R 3's row 3 is indexed by eg_cnt bits 13:11 and skips one tick per 32768 samples, so
the 16 ticks (2 skips) pin K mod 16384.  Before p2..p7 one candidate resetter each: RBP/RBL rewrite, TIMA/B/C writes,
MVOL writes, ARM7 released 5 ms (vectors `b .`), 128-step NOP DSP program, every register of 64 slots written 0xFFFF
with KYONEX then the quiet sequence.
- `build/tools/kfit -v -expect 6491 -case tests/eg_kprobe/hw` → `expected/kfit_hw.txt`: every probe `stream 0 ... 1 K
  fit ... K mod 16384 = 6491`, streams 1..3 `8 / 128 / 2048 K fit` (K mod 2048 = 347, mod 128 = 91, mod 8 = 3), the
  table `K 6491 mod8k 6491 bit13 0 dK +0` for p0..p7, drift within ±90 samples, `expect K = 6491: all probes match`.
- `build/tools/eg_model kp_p0 kp_p1 kp_p2 kp_p3 kp_p4 kp_p5 kp_p6 kp_p7` (through the model; in `expected/eg_model_all.txt`): 8/8 FULL.
- Controls: `kfit -oldr63 ...` (the session-4 R 63 rule) → p5 / p6 / p7 stream 3 `NO K fits: best 2/65746 ... first
  mismatch i 228 (+2) a 000 state 1 level 520176 hw 516080` (K still 6491 from streams 0..2: T7); `kfit -noslow`
  (rows indexed without the -1 offset) gives 6490 on every probe -- a relabelling, not a discriminator (all four rates
  are R < 48; the offset is pinned by the joint AEG + FEG fit, S4).
- Independent: derive MDEC_CT per sample from the header and the `probe pN: c0` line, invert the level law on stream 0
  and check that the ticks of the R 3 decay (+1 per 2048 clocks) skip exactly where `((cnt-1) >> 11) & 7 == 0`.
- INCONCLUSIVE / open: what sets the offset between the two counters (everything tested leaves it; power-on / BIOS
  boot; needs a controlled reboot, group H).  The key-offs of this case come from segments whose increment is 0 at
  that counter and say nothing about T3.

### T2 — S1 re-derived independently (work/verify/s5/S1.md)

- `g++ -O2 -std=c++17 -fopenmp -o build/work/s1_indep work/verify/s5/s1_indep.cpp && build/work/s1_indep` →
  `work/verify/s5/s1_indep_output.txt` (own CAP1 parser, own MDEC_CT derivation, own level-law inversion, own
  simulation; tests/eg_lock/hw att_slow + att_mid, tests/aeg_dl0/hw dl0): 690 envelope step samples over the 12
  streams, **690 on even MDEC_CT, 0 odd**; brute force over all 16384 K per stream intersected over the three runs
  (unrelated ring positions c0 44c9 / 3f0e / 87bb) = exactly {6491, 14683}; att_slow stream 3 (R 2) spacings
  4096 / 4096 / 8192 at the fitted K with 0 unexplained steps.
- Controls: a clock on odd MDEC_CT reproduces nothing (best = the sample before the first hardware step); K ± 1 fails
  at the first tick of every R < 48 stream; no -1 offset relabels to 6490 / 14682 (vacuous on AEG-only data); the
  YM2612 rows are vacuous here (no rows 5/9/13 in these runs -- S4's odd-rate runs are their evidence).
- `build/tools/eg_phase work/eg/eg_lock_runs.txt` and `build/tools/eg_phase -v` still reproduce
  `expected/eg_phase_eg_lock.txt` and `expected/eg_phase_dl0.txt` byte for byte.

### T3 — AEG key-off on a clock: decays take one more old step, the attack takes none (tests/aeg_koff)

Five runs of 16 random-timed cycles (koff_d2, koff_d2b, koff_d1, koff_att, kon_rel; 43520 / 42176 / 43648 / 40832 /
53952 samples, 0 errors, 64 marks each).  Three test slots (constant 0x7FFF, VOFF 0, LPOFF 1, IMXL 15) plus a
**witness** slot 3 (AR 31, KRS 1 → R 63) keyed ON by the KYONEX that keys the test slots OFF: its 520176 pins the
key-off sample E_B exactly (an AEG decay-2 run alone cannot: "old step on an even E_B" prints the same levels as
"release increment / no step on the odd E_B + 1", S3alt.md section 3).  Hypotheses per informative stream-cycle
(E_B on a clock, predictions differ): H0 release increment on the key-off clock, H1 old increment (linear), H2 one
more step by the old segment's own formula (= H1 for a decay), H3 no step.
- `for r in koff_d2 koff_d2b koff_d1 koff_att kon_rel; do build/tools/koff_fit tests/aeg_koff/hw/$r 0 6491 $r
  tests/aeg_koff/hw/aeg_koff.txt; done` → `expected/koff_fit_hw_<run>.txt`.  SUMMARY lines: koff_d2 `H0 0/30 H1 30/30
  H2 30/30 H3 0/30`; koff_d2b `H0 0/18 H1 18/18 H2 18/18 H3 6/18` (the 6 = the D2R 0 slot, where H1 = H3); koff_d1
  `H0 0/18 H1 18/18 H2 18/18 H3 0/18`; koff_att `H0 0/27 H1 3/27 H2 3/27 H3 27/27` (the 3 = cycles whose R 56 attack
  had already ended); `stream-cycles no H fits: 0` everywhere; witness key-off parity 10/6, 6/10, 6/10, 9/7 (even/odd).
  The header line of each output must show the run's own c0 (a78b / df43 / 1c86 / 546a / 96c8), never 0000.
- Through the model: `build/tools/eg_replay -case tests/aeg_koff/hw/aeg_koff.txt <run>` → `expected/eg_replay_<run>.txt`:
  `TOTAL <run>: streams FULL 4/4, samples matched N/N, clean cycles 16/16` with N = 174080 / 168704 / 174592 / 163328 /
  215808 (every sample of every stream from sample 0, the witness included).
- Controls: the session-4 model (`git show ba720b9:model/src/aica_model.cpp`) through eg_replay gives koff_att 0/4
  streams (it added the attack increment on the key-off clock) and fails the witness at every even E_B + 2 in every run
  (T7) -- `work/verify/s5/validators_eg_replay_before_*.txt`; `koff_fit ... -synth H0|H2|H3` regenerates the streams
  under an alternative and the fitter picks it out uniquely (case_aeg_koff.md "Self-tests").  The first console run
  (`tests/aeg_koff/hw_run1/`, 4 runs) gave the same verdicts.
- Independent: a decay-2 → release cycle with an even E_B must read `a X -> X + inc_d2 on E_B, then + inc_rel on E_B + 2`
  (koff_fit prints these `observed:` lines); an attack → release cycle `X -> X on E_B, then + inc_rel`.
- Caveat (stated once): the pin assumes one KYONEX applies the key-ons and key-offs of different slots on the same
  sample (supported by eg_lock keys and feg_track; a console that keyed off one sample later than it keys on would
  print exactly the T3 signature).

### T4 — FEG key-off clock: one more step of the old segment, increment AND direction, the attack included (tests/feg_koffdir, feg_koffatt)

- `build/tools/koffdir_check tests/feg_koffdir/hw` → `expected/koffdir_check_hw.txt` (feg_krs harness; slot 0: decay 2
  DOWN holding at 0x1A04, release UP; slot 1: the fk_1 S3 witness, both up; slot 2: decay 2 UP holding at 0x19FE,
  release DOWN; 8 batches, 0 errors): even key-offs kd_0 (4565), kd_1 (4628), kd_5 (4776), kd_6 (9247): `oldDir`
  shared K on all three slots, `S3` (old increment toward FLV4) and `noS3` `shared K: NONE (refuted on this batch)`
  (slot 0 hw u d00 vs model d03, slot 2 hw d01 vs cfe); odd key-offs kd_2 / kd_3 / kd_4 / kd_7 (4647 / 4693 / 4756 /
  9203): every reading agrees; batches 6 / 7 (KYONB cleared, no KYONEX for 100 ms): `held: target follows the state`
  on all three streams.  `-u work/eg/kd_` rewrites the u files eg_model reads (only after a new console run).
- `build/tools/koffatt_check tests/feg_koffatt/hw` → `expected/koffatt_check_hw.txt` (attack R 52 DOWN with release
  R 56 UP, the mirror, and a same-direction slot; witness-pinned key-off; 8 batches, 0 errors): `even key-off samples:
  6, odd: 2`; `batches with an EVEN E fitted on all 3 streams: oldStep 6/6 noStep 0/6 S3(oldInc->rel) 0/6 relInc 0/6`
  (per stream: s0 / s1 oldStep 6/6 and 0/6 for the rest; s2 releases in the attack's direction, so oldStep = S3 there).
- Through the model: `build/tools/eg_model` groups `feg_koffdir=24/24` (u files `work/eg/kd_<b>_<k>.u`) and
  `feg_koffatt=24/24` (u files `work/eg/ka_<b>_<k>.u` from `koffatt_check ... -u work/eg/ka_`, the witness MIXS compared too); the session-4
  model gave 16/24, failing slots 0 and 2 of every even batch at the key-off clock (`validators_eg_model_before.txt`).
- Controls: the readings differ pairwise by at least one u at E on slots 0 / 1 (the `window` blocks print observed u
  and each reading's prediction at E / E+2 / E+4); the model built from the pre-change source ("old increment toward
  the release target") makes the S3 row FULL and oldStep 1/3 (cases_ext.md).
- INCONCLUSIVE: the key-off clock when the old segment's `passed` flag is set (an attack / decay 1 that crossed its
  target on the previous clock) -- the model takes the old increment and direction against FLV4, unmeasured.

### T5 — odd key-off samples: nothing on the sample, release from the next clock (work/verify/s5/S3alt.md)

- `g++ -O2 -std=c++17 -o build/work/s3alt work/verify/s5/s3alt.cpp && build/work/s3alt fit -w` →
  `work/verify/s5/s3alt_fit.txt` (standalone FEG law with 11 switchable key-off mechanisms, all 24 session-4 FEG
  streams, one shared key-off sample per batch): S3 and oldDir fit all 8 batches (fk_0 6771, fk_1 4454, fk_2 4565,
  fk_3 4564, ft_0 5469, ft_1 6660, ft_2 803/804, feg_odd 6772); refuted with the batches named: release increment or
  no step on the key-off clock (fk_1, fk_3, ft_1), a one-clock rate lag at all transitions (the internal transitions:
  ft_0 s0 +518, fk_1 +3076), a pending old step on the first clock after the key-off whatever its parity (fk_2, ft_0,
  feg_odd, fk_0), a two-clock lag (fk_1 +4318), the state change landing only on even samples.
- The KYONB-driven target (statistically disfavoured there) is refuted by feg_koffdir batches 6 / 7 (T4).
- Every surviving reading agrees on odd samples, so T5 is CONFIRMED by elimination; there is no direct discriminator
  (mark INCONCLUSIVE only if you find a mechanism that fits every batch of T3 / T4 and predicts otherwise).

### T6 — key-on during a release that has not reached off: load on the sample, no step, CA restarts on it (tests/aeg_koff kon_rel)

- `koff_fit ... kon_rel ...` (T3) → SUMMARY `L0 48/48 L1 27/48 L2 21/48 | CA restart (ramp streams, odd E_C): C0 18/18
  C1 0/18 | stream-cycles no L fits: 0`; E_C (the witness-pinned second key-on) 7 even / 9 odd.  L1 = a step on the
  key-on sample (fits only odd E_C), L2 = the load on the next clock (fits only even E_C), C1 = CA restarting one sample
  later (the ramp slots' first attack sample).
- Through the model: `eg_replay ... kon_rel` 4/4, 215808/215808 (T3).
- Controls: `koff_fit ... -synth L1|L2|C1` picks each out (48/48 for the synthesised rule, the others as the parity split).
- The old monitor-based "EG loads 0x280 on the next envelope clock" (NOTES Loops, session 1) is superseded.

### T7 — the R 63 attack leaves the attack on the first clock at or after the key-on sample (tests/eg_kprobe, aeg_koff)

- eg_kprobe p5 / p6 / p7 (even onsets): stream 3 (R 45 decay 1, +1 at that counter) reads a = 1 at onset + 2; `kfit`
  32/32 streams with the rule, 29/32 with the session-4 rule (`-oldr63`); `eg_model` kp 8/8 vs 5/8 with the session-4
  model; the even witness key-ons of aeg_koff (KRS 1; 31 across the 4 runs of the first console run, validators.md) fail at E_B + 2 under the session-4 model and pass
  under the rule (T3's eg_replay).  eg_lock odd_dec is consistent with both (still FULL).
- Model: `aeg_clock`, the `keyed` branch moves ATTACK → DECAY1 when a == 0 without a step.

### T8 — slot stop at 0x3C0, off past 0x3FF, each one sample after its clock; no mute; the FEG runs on; rate registers one sample late (tests/slot_tail)

Three runs (tail_a / tail_b: 13568 samples, tail_c: 14528; 0 errors), random full-scale input, VOFF 1 or 0, filter on
or off, releases at R 62 / 48 / 63 / 49, tail_c re-enacting the eg_lock quiet sequence (RR + SA rewrite, IMXL 0) with a
known second signal at RAM 0.
- `tools/tail_cmp_all.sh tests/slot_tail/hw 6491` → `expected/tail_cmp_hw.txt` (through the model, stage-wise event
  search): `RESULT tail_a: 4/4 streams FULL, events ko 4650(even)`, `tail_b: 4/4 ... ko 4648(even)`, `tail_c: 4/4 ...
  ko 6875(odd) w14 11300(even) w00 11300(even) w2 12198(even)`, `TOTAL ...: streams FULL 12/12`; tail_c's retained bus
  values `hw 0 2 2 | model 0 2 2` (the console readbacks in slot_tail.txt: "after the IMXL 0 write: 6 2 2", "at the
  end: 0 2 2").  Key console numbers reproduced: fetch stop visible at 4891 (120th +8 clock 4890) and 6571 (960th +1
  clock) in tail_a; tail_b stream 0 (VOFF 0) `-16 / 0` half-waves through 5000 with the stop at 4889 and off at 4905;
  tail_c stops at 7115 (R 63), 8411 (R 49 row 5, 768 clocks) and 11541 (120 clocks after the RR rewrite's clock 11302,
  the rewrite itself on 11300 where the fetch already read the new SA).
- Controls (`g++ -O2 -std=c++17 -D<macro> -o build/work/tail_cmp_x tools/tail_cmp.cpp src/aica_model.cpp`, then the
  three runs; `work/model5/control_*.txt`, taken before the T9 fix so tail_c stream 0 also fails at 12198 in each):
  `-DCAIQUE_STOP_A=0x3BF` 3/4 4/4 2/4 (tail_a s1 6569: stop one clock early), `=0x3C1` 4/4 3/4 1/4, `=0x400` (the old
  rule) 3/4 2/4 1/4, `-DCAIQUE_STOP_LAG=0` 1/4 3/4 1/4, `=2` 1/4 3/4 1/4, `-DCAIQUE_MUTE=1` (mute from off) 4/4 3/4
  3/4 (tail_b s0 4923: hw -16 model 0), `=2` (mute from the stop) 4/4 3/4 3/4 (tail_b s0 4889).  FEG frozen at the stop:
  tail_c s1 fails 7118; frozen at off: 7136.  Live rate registers: tail_c s0 fails 11539.  The session-4 model: 4/12.
- The -8 of eg_lock mixs (`work/verify/s5/analysis_minus8.md`, tools in `work/minus8/`): `build/work/minus8_rest` lists
  the zero-input fixed points per cutoff (25 at 0x1BFF Q 4 incl. (L 4, B -4) → -8; none with L 4 at 0x1C00);
  `build/work/minus8_replay -sweep 32768` lands on -8 in 4.2-4.3 % of the quiet-sequence phases.
- INCONCLUSIVE / open: the off lag itself is visible only through the monitor and the CA reset (kept symmetrical with
  the measured fetch-stop lag); whether CA holds or advances between the stop and off; whether DL / KRS (same register
  as RR) and the FEG rates share the one-sample latch; the sub-sample alternative to the latch (the write landing between
  slot 0's EG update and its fetch) is indistinguishable at the model's resolution.

### T9 — every slot writes its ISEL bus every sample; IMXL is a gain; a bus retains only when no slot points at it (tests/mixs_write)

Eight probes (P0 P1 P1b P2 P3 P5 P7 P8; 2368-2816 samples, 0 errors), each: aica_quiet (every slot ISEL 0 IMXL 0 ...),
a few slots configured, a distinctive CPU value written to up to three captured buses; a bus that is rewritten by the
SGC loses the value within 1-2 samples (LOST), a bus nobody writes shows it on alternate samples to the end (KEPT).
- `build/tools/mixsw_check tests/mixs_write/hw` → `expected/mixsw_check_hw.txt`: 21 rows, 18 LOST, 3 KEPT (P0:1,
  P1b:0, P7:0 = the "nobody points at it" controls), no OTHER; rule table `H_G 21 agree 0 disagree consistent`;
  `H_M` (the session-4 model rule) `4 agree 17 disagree`, `H_B` (slot 0 always writes) 9/12, `H_V` (VOFF 0) 17/4,
  `H_F` (LPOFF 0) 17/4, `H_O` (off) 18/3, `H_S` (SA 0) 17/4, `H_0` (bus 0 special) 5/16 -- each REFUTED with its rows
  named.  Decisive rows: P2:2 / P8:4 (one IMXL-0 zeroed slot other than slot 0 on a bus: LOST), P3:2/3/4 (VOFF / LPOFF
  variants all LOST), P5:2/3/4 (playing IMXL-0 slots LOST, bus afterwards 0), P2:3 (IMXL 15 off slot: LOST, the
  session-4 "silent sender rewrites 0").
- `build/tools/mixsw_check tests/mixs_write/model` → `expected/mixsw_check_model.txt`: the identical verdict table (the
  production model now has H_G); the pre-fix model gave `H_M 21 agree` (`work/mixsw/check_model.txt`).
- Consequences reproduced: eg_lock mixs bus 0 read 0 / bus 2 retained; tail_c retained values 0 2 2 (T8).
- Not modelled: the sub-sample order of a CPU MIXS write against the SGC write (dsp_basic "F ira 25" stays excluded).

### T10 — regression (all after the last model change)

- `build/tools/eg_model | tail -1` → `TOTAL full=89/89` (`GROUPS session4=33/33 eg_kprobe=8/8 feg_koffdir=24/24 feg_koffatt=24/24`;
  `expected/eg_model_all.txt`); `build/tools/feg_validate | tail -1` → 9/9, 77862/77862; `build/tools/filt_validate_model
  | tail -1` → 265/265, 4286180/4286180; `build/tools/filt_overflow_model 1 24 | tail -1` → 15/15 and `... 1 24 x` →
  12/12 held out (`expected/filt_*`).  `tools/validate_s5.sh` → PASS (`expected/validate_s5.txt`).
- Whole-program model runs: `./run_model.sh` on all 47 cases exit 0; hashes `work/model_outputs_2026-09-23c.sha256`.
  Against `...23b` the changed outputs are the cases with a release to off or VOFF captures (model_fixes.md 2.5:
  sgc_aeg EG logs -- off one sample after the overflow clock, the stop 8 clocks earlier; sgc_krs -- R 63 key-ons on a
  clock; eg_lock odd_dec / mixs; sgc_lfo2 l2_1; cap_selftest stops 15 samples earlier; feg_krs / feg_track / feg_probe --
  the capture START moved by one sample, a harness artefact: cap_start's sync scan costs one G2 read per zero ring word
  and up to five per non-zero one, and the silent slots' filter rests left in the ring changed; the envelopes are
  unchanged).  sgc_keys is byte-identical to session 4 (K4's CA reset logged with EG 5fff, which is why CA resets at
  off, not at the stop).
- Console-vs-model text diffs: dsp_basic 3 known lines (unchanged since session 4), sgc_level L5 (known);
  sgc_krs.txt / sgc_keys.txt / probe.txt differ in monitor timing / key-on phase as before (whole-program runs key on at
  the model's own phase).

### H — console (ONE agent, private copy)

Conclusions must reproduce, not bytes: K is a property of the boot (fit it first with `kfit -case` on an eg_kprobe run
or `eg_phase` on an eg_lock att_slow capture, then pass `-K` / the K argument to eg_replay / tail_cmp / koffatt_check /
koffdir_check; eg_model's run table carries its own K).  Worth doing, from NOTES "Open items":
- **What sets K at boot**: power-cycle / reset the console and run `./run_hw.sh eg_kprobe` first thing; compare K with
  6491 and with MDEC_CT at the head (kfit prints both); a second reboot tells whether it is constant or random.
- **FEG key-off clock with the old segment's `passed` flag set**: an attack or decay 1 that crossed its target on the
  previous clock, keyed off on the following clock (witness-pinned as in feg_koffatt).
- **CA between the fetch stop and off**: a slow release (R 48: 64 clocks between 0x3C0 and 0x400) with the CA monitor
  polled through it.
- **DL / KRS and the FEG rate registers through the one-sample latch**: rewrite them on a running slot WITHOUT a
  KYONEX on a clock sample and see whether the first step at the new rate is on that clock or the next (only RR is
  measured, tail_c).
- **The sub-sample order of a CPU MIXS write against the SGC write** (dsp_basic F ira 25).
- Not a console item: the cap_start head-estimate shift of whole-program model runs (harness; see T10).

## Reference: key paths

- Model: `src/aica_model.cpp` -- `step()` (key events, clock, every slot's bus write, stop/off commit, `eg_latch`),
  `aeg_clock` (key-off rules, R 63 transition, stop/off arming), `feg_clock` (`feg_prev`, `feg_prev_dir`), `slot_stop`
  / `slot_off`, `slot_output` (no mute), `eg_increment`, `key_on`, `key_off`, `lpf_step`; macros at the top.
- Evidence: `tests/aeg_koff/hw/` (+ `hw_run1/`), `tests/eg_kprobe/hw/`, `tests/slot_tail/hw/`, `tests/feg_koffdir/hw/`,
  `tests/feg_koffatt/hw/`, `tests/mixs_write/hw/` (session 5), `tests/eg_lock/hw/`, `tests/feg_krs/hw/` (session 4),
  plus the earlier `tests/<case>/hw/`.
- Validators: `tools/eg_model.cpp` (everything envelope, 89 runs), `tools/eg_replay.cpp` (aeg_koff cycles),
  `tools/tail_cmp.cpp` + `tail_cmp_all.sh` (slot_tail), `tools/validate_s5.sh` (the gate), `tools/feg_validate.cpp`,
  `tools/filt_validate.cpp` (`filt_validate_model`), `tools/filt_overflow.cpp`.
- Standalone fitters: `tools/kfit.cpp`, `tools/koff_fit.cpp`, `tools/koffdir_check.cpp` + `tools/feg_law.h`,
  `tools/koffatt_check.cpp`, `tools/mixsw_check.cpp`; `work/verify/s5/s1_indep.cpp`, `s3alt.cpp`.
- Reports: `work/verify/s5/*.md`; session 4: `work/verify/s4.md`, `work/verify/s4/`.
- Previous handovers: `git show ba720b9:model/HANDOVER.md` (session 4), `git show fa451ca:model/HANDOVER.md` (session 3),
  `git show a07a62d:model/HANDOVER.md` (breakthrough).
