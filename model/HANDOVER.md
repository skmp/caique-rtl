# caique AICA model — session 5 handover (2026-09-23, night) + session 6 addendum

**Known gaps and caveats: [LIMITATIONS.md](LIMITATIONS.md).**  **Scope: verify the session-5 results, then the session-6 addendum (claims U1-U3 at the end).**  Session 4 is commit
`ba720b9` (its handover: `git show ba720b9:model/HANDOVER.md`); session 5 is commit `5207a9f`; session 6 is uncommitted
(`git diff HEAD` plus the untracked files: cases ca_stop / eg_latch / eg_latch2 / eg_latch3 / feg_koffpass / mixs_rd,
tools koffpass_check / latch_check, tests/<case>/, work/koffpass, work/latch, work/verify/s6).  Session 5 ran on the **same console boot as
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
| FEG key-off clock | one more step of the old segment with its increment AND direction (attack included), hold check vs FLV4; KYONB alone holds.  Session 6: with the old segment's `passed` flag set it is the NEXT segment's step | T4, T5, U1 |
| Key-on in release | a = 0x280 on the key-on sample, no step, CA restarts on that sample (supersedes "on the next clock") | T6 |
| R 63 attack | leaves the attack on the first clock at or after the key-on sample (the key-on sample itself when it is a clock) | T7 |
| Slot stop / off | fetch stops at a = 0x3C0, off past 0x3FF, each one sample after its clock; NO mute; the FEG runs on; the EG reads its rate registers one sample late.  Session 6: the stop IS off (monitor 0x1FFF + CA 0 at 0x3C0 + 1 sample, nothing at 0x400); the rate latch is REFUTED -- every register live, tail_c's RR = in-sample ordering | T8, U2, U4 |
| MIXS | every slot writes its ISEL bus every sample, IMXL a pure gain; a bus retains only when no slot points at it; the eg_lock -8 = filter deadband rest at 0x1BFF Q 4.  Session 6: the CPU reads the bank of the current sample parity | T9, U3 |
| Cases | `aeg_koff` (5 runs x 16 cycles), `eg_kprobe` (8 probes), `slot_tail` (3 runs), `feg_koffdir` (8 batches), `feg_koffatt` (8 batches), `mixs_write` (8 probes); console captures under `tests/<case>/hw/` (aeg_koff's first run in `tests/aeg_koff/hw_run1/`) | — |
| Tools | standalone fitters `kfit`, `koff_fit`, `koffdir_check` + `feg_law.h`, `koffatt_check`, `mixsw_check`; model replays `eg_replay`, `tail_cmp` + `tail_cmp_all.sh`; `eg_model` extended to 89 runs (GROUPS line: session4 33, eg_kprobe 8, feg_koffdir 24, feg_koffatt 24); `validate_s5.sh` one-shot gate | — |
| Model | `stop_in` / `off_in`, no mute, FEG gate removed, `egreg` + `eg_latch` (removed again in session 6, U4), `feg_prev_dir`, attack key-off rule, R 63 on the key-on clock, `sent[]` for every slot; macros `CAIQUE_STOP_A` / `CAIQUE_STOP_LAG` / `CAIQUE_MUTE` for the controls only | T8, T9, T10 |
| Scratch | `work/koff/`, `work/kprobe/`, `work/tail/`, `work/koffatt/`, `work/mixsw/`, `work/minus8/`, `work/model5/` (controls, baselines), `work/eg/kd_*.u`, `ka_*.u` (tracked FEG u of feg_koffdir / feg_koffatt) | — |
| Session 6 cases | `feg_koffpass` (2 runs x 64 witness-pinned cycles), `ca_stop` (3 monitor-polled runs), `mixs_rd` (7 readback runs), `eg_latch` / `eg_latch2` / `eg_latch3` (6 + 5 + 5 runs x 16 cycles x 3 witness-pinned rewrites: every envelope register live, CA never restarted by SA / LPCTL / LEA); captures under `tests/<case>/hw/`, model runs under `tests/<case>/model/` | U1-U4 |
| Session 6 tools | standalone checkers `koffpass_check` (feg_koffpass), `latch_check` (eg_latch, eg_latch2, eg_latch3); `tail_cmp` stage 2 searches d = w00 - w14 in {1, 0, -1}; no gate change (`validate_s5.sh` still PASS) | U1, U4 |
| Session 6 model | `Slot::feg_prev_passed` + `feg_clock` next-segment step on the key-off clock; `slot_stop()` = fetch stop + `AEG.off` + `CA = 0` in one (`off_in` / `slot_off` removed), the DL compare also on the stop clock; `read()` of MIXS returns `MIXS_bank[samples & 1]`; `Slot::egreg` / `eg_latch` removed -- r10 / r14 / r18 / r40 / r44 and the FLV targets read live | U1-U4 |

Model diff (session 5): `git show 5207a9f -- model/src model/tools`; session 6: `git diff HEAD -- src/`.  New expected outputs (`work/verify/expected/`): `eg_model_all.txt` (updated
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
make -C host -j8         # 53 case binaries -> build/host/ (47 + the session-6 six)
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
- INCONCLUSIVE in session 5, CLOSED in session 6 (U1): the key-off clock when the old segment's `passed` flag is set
  (an attack / decay 1 that crossed its target on the previous clock) takes the NEXT segment's step; the session-5
  model's "old increment and direction against FLV4" is refuted 0/23 (tests/feg_koffpass).

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

### T8 — slot stop at 0x3C0, off past 0x3FF, each one sample after its clock; no mute; the FEG runs on; ~~rate registers one sample late~~ (REFUTED, see U4) (tests/slot_tail)

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
- Session 6 (see the addendum): "off past 0x3FF" is SUPERSEDED -- the monitor's 0x1FFF and the CA reset both come with
  the 0x3C0 stop (U2, tests/ca_stop), so the two open CA / off items are moot; the "rate registers one sample late"
  clause is REFUTED (U4: tests/eg_latch / eg_latch2 / eg_latch3, every register live, 0 latched) -- tail_c's RR is the
  in-sample ordering (its write landed after sample 11300's envelope phase), so the "Live rate registers: tail_c s0
  fails 11539" control above only holds with the RR write forced onto the SA write's sample; `tail_cmp` now searches
  d = w00 - w14 in {1, 0, -1} and tail_c fits with `w00 11300 / w14 11301`, 12/12 unchanged (`expected/tail_cmp_hw.txt`
  regenerated by the bit check, `work/verify/s6/bitcheck.txt`).

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
- **FEG key-off clock with the old segment's `passed` flag set** -- DONE in session 6 (U1, tests/feg_koffpass).
- **CA between the fetch stop and off** -- DONE in session 6 (U2, tests/ca_stop: moot, CA reads 0 from the stop).
- **The sub-sample order of a CPU MIXS write against the SGC write** (dsp_basic F ira 25).
- Not a console item: the cap_start head-estimate shift of whole-program model runs (harness; see T10).

## Session 6 addendum (2026-09-23 night, same console boot, K 6491; uncommitted)

Six console cases (feg_koffpass, ca_stop, mixs_rd, eg_latch, eg_latch2, eg_latch3 -- every capture 0 counter errors, 64
marks), two standalone checkers (`tools/koffpass_check.cpp`, `tools/latch_check.cpp`; both build with `make -C tools`),
four model changes (`git diff HEAD -- src/`): `Slot::feg_prev_passed` and the next-segment key-off step in `feg_clock`;
`slot_stop()` = fetch stop + `AEG.off` + `CA = 0` in one, `off_in` / `slot_off()` removed, the decay 1 -> 2 compare also on
the stop clock; `read()` of 0x4500.. returns `MIXS_bank[samples & 1]`; `Slot::egreg` / `eg_latch()` removed (every
envelope register read live).  `tools/tail_cmp.cpp` stage 2 searches the RR write at d = w00 - w14 in {1, 0, -1}.
Reports: `work/verify/s6/case_feg_koffpass.md`, `case_eg_latch.md`, `docs_s6a.md`, `docs_s6b.md`; the full bit-check
transcript `work/verify/s6/bitcheck.txt`.  Final gates: `eg_model` 89/89, `eg_replay` 5 runs 4/4 (16/16 clean cycles),
`tail_cmp_all.sh` 12/12 (tail_c `w14 11301`), `feg_validate` 9/9, `validate_s5.sh` PASS, `koffpass_check
tests/feg_koffpass/model` nextSeg 11/11 + 8/8.  Whole-program model outputs that changed against session 5:
`tests/probe/model/probe.txt` (the MIXS0.l/.h readback lines, now the console's except one nibble) and
`tests/sgc_keys/model/sgc_keys.txt` (K4's "EG 5fff CA 0000" at 5868 us, was 6231).  T1-T7, T9, T10 stand; T8's "off past
0x3FF" is superseded by U2 and its "rate registers one sample late" is refuted by U4.  Setup as above; the checkers take
`-K` after a reboot.

### U1 — FEG key-off on the clock after the old segment passed its target: the NEXT segment's step (tests/feg_koffpass)

Two runs (kp_a 33152 samples, kp_b 33280; 0 errors; 64 cycles each) of the feg_koffatt harness: FEG slots 0..2 (random
input, Q 4, VOFF 1) with the AEG witness slot 3 (R 63) keyed ON by the KYONEX that keys them OFF.  All three slots cross a
target on the same clock N (kp_a N = 8, kp_b N = 12): s0 attack UP 0x1800 → FLV1 0x1810 at +2 (R 52), s1 attack DOWN
0x1C00 → FLV1 0x1BF2 at -2 (ends strictly below, 0x1BF0), s2 decay 1 UP 0x1808 → FLV2 0x1816 at +2 after a one-clock
R 60 attack (kp_b: 0x1818 / 0x1BEA / 0x181E); releases at R 48 (+1 / -1 / +1).  The key-on → key-off spacing sweeps
d = E - key-on over 15..20 (kp_a) / 22..29 (kp_b) samples so the even key-offs fall on N, N+1 and the following segment.
Readings at an N+1 key-off: A oldStep (one more step of the passed segment: the session-5 model), B nextSeg (the segment
advance, then the new segment's rate and direction: what clock N+1 does without a key-off), C noStep, D relStep (the
release increment on E); they differ pairwise by >= 1 u at E or E+2 (v offsets +2 / -4 / 0 / +1).
- `build/tools/koffpass_check tests/feg_koffpass/hw` → `work/koffpass/check_hw.txt`.  Check first: both runs `errors 0`,
  `64 witness onsets`, every key-on found under hyp 0, `untracked stream-cycles: 0 0 0`, d histograms kp_a `14:2 15:8 16:12
  17:11 18:12 19:11 20:8`, kp_b `22:1 23:9 24:10 25:14 26:10 27:12 28:7 29:1`.  Verdict: the TOTAL block `N+1  A oldStep
  0/23  B nextSeg 23/23  C noStep 0/23  D relStep 0/23 | s0: 0 23 0 0/23 s1: 0 23 0 0/23 s2: 0 23 0 0/23` (kp_a 10/10,
  kp_b 13/13), `N 18/18`, `pre 2/2`, `post 20/20`, `odd* 24/24`, `odd 41/41` FULL for every reading, `N+1 cycles ... where
  NO reading is FULL: 0; where MORE THAN ONE reading is FULL: 0`.  Every N+1 cycle block ends `A oldStep refuted  B nextSeg
  FULL  C noStep refuted  D relStep refuted`; e.g. kp_b cycle 5 (E 2736 even, d 25): s0 observed u at E `c0a`, A predicts
  `c0d`, B `c0a`, C `c0c`, D `c0c`; s1 observed `df6` vs `df3 / df6 / df4 / df3`.
- Through the model: `./run_model.sh feg_koffpass && build/tools/koffpass_check tests/feg_koffpass/model` →
  `work/koffpass/check_model_after.txt`: `N+1 A 0/19 B 19/19 C 0/19 D 0/19` (kp_a 11, kp_b 8; the model's d histogram
  differs from the console's, so the phase counts do), the other phases FULL.  The pre-change model (`git show
  5207a9f:model/src/aica_model.cpp`) gives `A 19/19 B 0/19` on its own captures (`work/koffpass/check_model.txt`): the
  checker picks each mechanism out in both directions.  `eg_model | tail -1`
  still `TOTAL full=89/89` (the session-4/5 events never had `passed` set at the key-off).
- Controls / what would refute: the N cycles must read the plain attack / decay-1 step (feg_koffatt's rule) and the post
  cycles decay 1's (s0 / s1: -4 / +4) and decay 2's (s2: -4) plain old step -- decay 1's key-off clock is new FEG
  evidence; odd* cycles (passed flag pending on an odd key-off sample) must show nothing at E+1 and the release from the
  next clock (all 24 do; a pending flag acting on an odd sample would fail every reading at E+1).  Any N+1 cycle where B
  fails while N / post / odd stay FULL, or where two readings fit, refutes the reading (none).  The witness-pin caveat of
  T3 applies unchanged.
- Independent: from the NOTES FEG law simulate the key-on → E trajectory with no key-off through clock E and compare the
  observed u at E (koffpass_check prints the `s<k> observed` rows E-4..E+10): kp_a s0 must read 0x180C >> 1 = `c06` at E.

### U2 — the slot stop at a = 0x3C0 IS "off": monitor 0x1FFF and CA 0 one sample after that clock; nothing at 0x400 (tests/ca_stop)

Slot 0 constant 0x7FFF, loop [0, 4096), AR 31, D1R 31 (+8) to DL 30 -- decay 1 lands exactly on a = 0x3C0 after 120
clocks -- then decay 2 at D2R 10 (R 20: +1 per 64 clocks, 0x3C0 → 0x400 would take 8192 samples = 186 ms), RR 31; the
EG and CA monitors polled back to back every ~31 us; every EG state change and every CA change while a >= 0x3B8 is
logged, then a 5 ms heartbeat.  S1 pitch 1.0 loop, S2 pitch 0.5 (OCT -1, CA advances every other sample), S3 one-shot
(LPCTL 0, LEA 4096).
- No tool: `./run_hw.sh ca_stop` → `tests/ca_stop/hw/ca_stop.txt`, read the lines.  S1: `t 5873 us: EG 23c0 (st 1 a 3c0)
  CA 0102`, `t 5905 us: EG 5fff (st 2 a 1fff) CA 0103`, `t 5937 us: EG 5fff (st 2 a 1fff) CA 0000`; S3: the same at 5864 /
  5895 / 5927 (CA 0102 / 0103 / 0000); S2: `5859 EG 23b8 CA 0081`, `5891 EG 5fff CA 0081`, `5936 EG 5fff CA 0000`.  No
  run has a line with `a 3c1`..`a 3ff`; every heartbeat afterwards and the `end:` line read `EG 5fff CA 0000`.  So the
  monitor flag (with state 2: the decay 1 -> 2 compare ran on the stop clock) appears one poll after the 0x3C0 reading and
  CA reads 0 one poll after that (the CA read of the poll that first saw 0x1FFF, ~2.4 us after its EG read, still had the
  old CA): flag and CA reset within ~1-2 samples of the 0x3C0 clock, the CA reset trailing the flag by between one G2 read
  and one poll (sub-poll, not modelled).
- What would refute: any `a 3c1..3ff` line (a decay-2 step visible after the stop -- the session-5 rule predicts 0x3C1..0x3FF
  for 186 ms with CA counting on), the 0x1FFF reading arriving two or more polls after the 0x3C0 one, or CA still nonzero
  two polls after the 0x1FFF.  The pitch-0.5 and one-shot runs control for a held vs wrapped CA (a held CA would differ
  from 0 in S3).
- Model: `./run_model.sh ca_stop` → `tests/ca_stop/model/ca_stop.txt`: S1 `5820 EG 23b8 CA 0101`, `5845 EG 5fff CA 0000`
  (the model's polls are ~20-25 us apart and it resets CA with the flag in the same sample -- the console's one-poll trail is
  below that).  tests/sgc_keys K4 (`EG 5fff CA 0000` at 5868 us, console 5916) and the sgc_aeg monitor logs reproduce;
  `tail_cmp_all.sh tests/slot_tail/hw 6491` still 12/12 (the captures only ever saw the fetch stop; the T8 controls
  `-DCAIQUE_STOP_A` / `-DCAIQUE_STOP_LAG` still apply to the stop, `CAIQUE_MUTE` to the output).
- Superseded: T8's "off past 0x3FF, the 128th +8 clock" and the sgc_keys K4 reading "CA reset at off, not at the stop"
  (K4's poll spacing could not separate the two; ca_stop's slow decay 2 does).  The AEG value still steps to 0x3FF
  (tail_b's -16 half-waves need the saturated level) but nothing else is observable there.

### U3 — the CPU reads the MIXS bank of the current sample parity, the one its own writes go to (tests/mixs_rd)

Every run: `aica_quiet` (64 slots ISEL 0 IMXL 0 = 64 writers of 0 on bus 0), the configuration, the write of hi 0x1234 /
lo 0x5 to bus B, then 96 back-to-back hi/lo read pairs ~5.5 us apart (4 pairs per sample) and one read 30 ms later.
- No tool: `./run_hw.sh mixs_rd` → `tests/mixs_rd/hw/mixs_rd.txt`.  R1 (bus 0): `1 1234/0` then `0000/0` x 95, after 30 ms
  `0000/0`.  R2 (bus 5, no writer): `before: 1234/5` (retained from dsp_basic, a program run hours earlier), `1234/5` x 96,
  after 30 ms `1234/5`.  R3 (bus 5, slot 5 playing 0x0100 x 16 at IMXL 15): `1234/5` x 3, then `0100/0` x 93.  R4 (bus 5,
  slot 5 pointing at it, IMXL 0, off): `1234/5` x 3, `1234/0`, then `0000/0`.  R5 (bus 0, lo written first): `1234/5` once,
  then `0000/0`.  R6 (bus 5, hi only): `1234/0` x 1, `0000/0` x 4, `1234/0` x 4, `0000/0` x 4 ... (period 8 pairs = 2
  samples), after 30 ms `1234/0`.  R7 (bus 5, lo only; hi 0x1234 left in one bank by R6): `1234/5 1234/5 1234/0 0000/0 0000/0
  0000/0 0000/5` then `1234/5` again (period 8), after 30 ms `0000/0`.
- Reading: a bus with writers shows the CPU value until the next sample boundary (<= 4 pairs), then the SGC value (0, or the
  sender's 0x0100); a bus nobody points at alternates every sample between the written bank and the untouched bank, so a
  single-half write is visible on alternate samples for ever.  The low nibble's bank switch is seen one pair (~5 us) before
  the high word's (R4 `1234/5 → 1234/0 → 0000/0`, R7's `1234/0` / `0000/5` pairs): sub-sample, not modelled.
- What would refute: R6 / R7 reading one constant value (a single bank on the CPU side), the written value not reading
  back at all in the first pairs (the session-5 model: the CPU read the DSP-side `MIXS[]` of the last step, so a write was
  invisible -- its tests/probe MIXS0 lines read 0 for every write pattern, the console 000f / 5555 / aaaa), or R3 showing
  0x1234 beyond ~1 sample.
- Model: `./run_model.sh mixs_rd` → `tests/mixs_rd/model/mixs_rd.txt`: the same patterns with the boundary phase differing
  (R3 `1234/5` x 3 then `1234/0`, `0100/0`; R4 `1234/5` x 2 then `0000/0`; R6 `1234/0` x 4 / `0000/0` x 4; R7 `1234/0` x 4,
  `1234/5`, `0000/5` x 4 ...); R2 `before: 0000/0` (no earlier program on the model).  tests/probe MIXS0.l/.h now match the
  console except `MIXS0.l w55555555->r00000000` (console `r00000005`): a model sample boundary fell between that write and
  its read.
- Caveat for test writers (NOTES "Test-writing notes"): a bus nobody points at carries values across programs and hours.

### U4 — every envelope register is read live (no one-sample latch); tail_c's delayed RR is in-sample ordering (tests/eg_latch, eg_latch2, eg_latch3)

Method (`work/verify/s6/case_eg_latch.md`): constant-0x7FFF test slots 0..2 (or the feg_koffatt random-input FEG harness),
three R 63 witnesses 3 / 4 / 5 on bus 3; each rewrite is one write pair -- the register and the witness's reg 0x00 with
KYONB | KYONEX -- with the write order alternating per cycle (order 0: register then KYONEX, order 1: KYONEX then
register); the witness onset E pins the sample the pair takes effect on.  A live register acts at clock E, a latched one at
clock E + 2, so only even E is informative; a sample boundary inside the pair (~10 %) shifts the register by one sample
against the key event ("straddles": EARLY = the register acted at clock E - 1, only possible for a live register in order
0; LATE = at E + 3, only for a latched one in order 1).  16 cycles x 3 events per run, every run `errors 0`, 64 marks.
- `build/tools/latch_check tests/eg_latch/hw` → `work/latch/check_hw.txt`.  SUMMARY lines (even E, order 0 | order 1):
  `dl ... order0 7: live 6 latched 0 neither 1 | order1 9: live 8 latched 0 neither 1` (`found 33 missed 15`: monitor
  trigger misses), `krs ... order0 11: live 11 latched 0 | order1 12: live 12 latched 0`, `ar ... order0 8: live 8
  latched 0 neither 2 | order1 10: live 10 latched 0`, `d2r ... 11: live 11 | 10: live 10`, `feg_rate ... 14: live 14 |
  15: live 15`, `flv ... order0 1: live 1 latched 0 neither 9 | order1 4: live 4 latched 0 neither 12` (pre-fail 7,
  identical 6: targets that landed at or below the held value); `LATE 0/0` on every line; EARLY `10/7 5/0 5/0 1/0 1/0
  3/0`.
- `build/tools/latch_check tests/eg_latch2/hw` → `work/latch/check2_hw.txt` (5 runs, 48 events each, `found 48 missed 0`):
  `rr0` (RR 0 → 30 on a held release) `order0 12: live 12 latched 0 | order1 14: live 14 latched 0`, `rr24` (RR 24 → 30)
  `10: live 10 | 12: live 12`, `rr0_koff` (RR 0 → 30 with the slot's own reg 0x00 KYONB 0 rewritten + KYONEX = tail_c's
  group without the SA change) `10: live 10 | 10: live 10`, `rekoff24` (RR 24 → 30 with a redundant key-off) `11: live 11
  | 11: live 11`, `d2r0` (D2R 0 → 28 on a held decay 2) `6: live 6 | 16: live 16`; `LATE 0/0` everywhere, EARLY 3 / 2 /
  2 / 2 / 2 in order 0.  So no register latch, no "rate 0 → nonzero arming", and a redundant key-off on a released slot
  changes nothing.
- `build/tools/latch_check tests/eg_latch3/hw` → `work/latch/check3_hw.txt` (5 runs, RR 0 → 30 plus a PROBE write in the
  same group): `none` (control) `order0 16: live 16 | order1 18: live 18`, `sa_hi` (reg 0x00 SA[22:16] 0x20000 → 0x10000)
  `14: live 14 | 15: live 15`, `sa_lo` (reg 0x04) `13: live 13 | 11: live 11`, `lpctl` (1 → 0) `14: live 12 neither 2 |
  11: live 11`, `lea` `14: live 14 | 11: live 11`; `latched 0`, `LATE 0/0` everywhere; `CA restarted 0/48` on every run --
  an SA / LPCTL / LEA rewrite neither restarts the stream nor blocks the envelope step.
- Through the model: `./run_model.sh eg_latch eg_latch2 eg_latch3` then the checker on `tests/<case>/model` →
  `work/verify/s6/case_eg_latch_model.txt` (the session-5 latched model: order 1 100 % latched, LATE in order 1 -- the
  mirror), `case_eg_latch2_model.txt`, `case_eg_latch3_model.txt`; the live control build `work/latch/aica_model_live.cpp`
  (`case_eg_latch_model_live.txt`, `case_eg_latch2_model_live.txt`) reproduces the console signature (order 0 100 % live,
  EARLY only in order 0, LATE 0); `work/latch/aica_model_noskip.cpp` / `case_eg_latch3_model_noskip.txt` is the eg_latch3
  control build.  The production model now reads every register live (`git diff HEAD -- src/`: `egreg` / `eg_latch` gone).
- Controls / what would refute: a latched register predicts the order-1 events at clock E + 2 (`latched`) and LATE
  straddles -- 0 observed across 359 informative even-E events (110 + 112 + 137); the straddle minority must appear as EARLY in order 0 only
  (it does: 10 / 5 / 5 / 1 / 1 / 3 in eg_latch, 1-3 per run in eg_latch2 / eg_latch3, plus the odd order-1 EARLY in
  sa_hi 1 and lpctl 2 -- a KYONEX landing one sample after its register in order 1).  A `latched` count > 0 on any run,
  or `LATE` > 0, refutes U4; `CA restarted` > 0 on eg_latch3 would mean SA / LPCTL / LEA writes restart the stream.
- What tail_c's delayed RR was (T8): an in-sample ordering effect.  Within a sample period the envelope update runs before
  the sample fetch / output; key events always take effect from the next sample; a register write landing after a
  sample's envelope phase is seen by that sample's fetch but only by the next sample's envelope clock (one landing
  before it acts in its own sample).  tail_c's group (RR 0 → 31, reg 0x00 := 0, KYONEX) landed between the two phases of
  11300: the SA switch shows at 11300, the first +8 release step at 11302.  The EARLY straddles are the same effect
  seen from the other side (the register before the envelope phase, the KYONEX a sample later).  The model steps whole
  samples and applies writes between steps, so it cannot place a write inside a sample (a known sub-sample limitation;
  RTL rule: EG update first, then fetch / output); `tools/tail_cmp` stage 2 lets the RR write land d = w00 - w14 in {1, 0,
  -1} samples after the SA write and tail_c fits with `w00 11300 / w14 11301`, `TOTAL ... streams FULL 12/12`.
- Independent: on any eg_latch run derive E from the bus-3 witness onset and check the test slot's first sample at the
  new rate against the level law: a live DL / KRS / AR / D2R must change the step at clock E, never at E + 2.


## Session 7 addendum (2026-09-23/24, REBOOTED console: K 0; uncommitted)

Scope: the per-sample schedule.  NOTES.md "Session 7" has the findings; every number below is reproduced by the listed
tool on the console captures under `tests/<case>/hw/`.

### V1 — K = 0 on the rebooted console (tests/eg_kprobe, boot 2)
`build/tools/kfit -case tests/eg_kprobe/hw -expect 0` → `expect K = 0: all probes match` (8/8, dK 0 on every action);
boot 1: `build/tools/kfit -case tests/eg_kprobe/hw_boot1 -expect 6491`.  `build/tools/eg_model | tail -1` → `TOTAL full=97/97`
(GROUPS eg_kprobe=16/16 = kp_ boot 1 with K 6491 + kp2_ boot 2 with K 0).  Control: eg_model kp2_ with K 6491 fails at +2.

### V2 — register writes act at the slot's frame, ~k/64 into the sample (tests/eg_sched2 f_ runs)
`build/tools/sched2_check tests/eg_sched2/hw` (expected/sched2_check_hw.txt): per "fetch slot k order o" the d-1 share
rises 0.00 (slot 0) → 0.06 (8) → 0.27/0.31 (16) → 0.42/0.27 (24) → 0.35/0.31 (32) → 0.44/0.50 (40) → 0.73/0.71 (48) →
0.69/0.83 (56), 48 events per row, 0 not found.  Refutation: a flat line (boundary-processed registers) or a line
falling with k.  The model (writes between steps) gives d+0 on every slot: `build/tools/sched2_check tests/eg_sched2/model`.

### V3 — KYONEX latched at the boundary, KYONB read per frame in the next sample, start the sample after (tests/kon_defer, eg_sched2)
`tests/kon_defer/hw/kon_defer.txt` (expected/kon_defer_hw.txt): slot 1 "KYONEX then KYONB 1 after d" 7 5 4 4 1 3 1 0 0 0 0 0
(d 0..33 by 3), slot 62: 7 8 8 8 8 8 8 8 8 5 5 2; the reverse rows are the complements.  sched2_check "key-on (slot w)
order 1": d+1 41-44 of 48 on every run.  eg_sched (`build/tools/sched_check tests/eg_sched/hw`): "wh-wl" d+0 for 380 of 381
events.  Model: kon_defer model = a 0..1-sample window for both slots (the per-frame extension is sub-sample);
`work/model5/order_probe` prints anchor M, SA effect M, key-on M+1.  Refutation: key-on on M (no latch) or slot-dependent
onsets.

### V4 — the envelope of slot k for sample n is computed at frame k of sample n-1 (tests/eg_sched2 e_ runs)
sched2_check "envelope slot k order o M even": slot 56 d+0 10/10, slot 0 d+2 13/13, middle slots split (numbers in NOTES);
"M odd": d+1 for every slot.  Refutation: slot 0 stepping on M, or slot 56 on M+2.

### V5 — DSP writes are posted with their own slot (tests/dsp_wslot)
`diff tests/dsp_wslot/hw/dsp_wslot.txt tests/dsp_wslot/model/dsp_wslot.txt` → identical, `0 failing checks`; W5 "writes
completed 15/15, reads completed 15/15".  Refutation: any lost write or read in W2/W5.

### V6 — regression after the two model changes (KYONEX deferral + key latency, fetch before output)
`tools/validate_s5.sh` → PASS (eg_model 97/97, eg_replay 5 × 4/4, tail_cmp 12/12 with tail_c w00 = w14 = 11301,
mixs_write identical); feg_validate 9/9; filt_validate_model 265/265; filt_overflow_model 15/15 and 12/12; every case
exits 0 (`work/verify/s6/run_model_all_s7.log`); baseline `work/model_outputs_2026-09-23e.sha256`; transcript
`work/verify/s6/bitcheck_s7.txt`.  The model-linked validators (eg_model, eg_replay, tail_cmp, feg_validate) now write a
key event one step before its effect sample.

Console cases added: eg_sched, eg_sched2, kon_defer, kon_probe, kon_probe2, kon_first, dsp_wslot (all under tests/, with
model runs).  Group H (still open): what offsets K during a boot; the phase of the CPU/DMA memory slot in the frame.

## Reference: key paths

- Model: `src/aica_model.cpp` -- `step()` (key events, clock, every slot's bus write, stop commit; no register latch),
  `aeg_clock` (key-off rules, R 63 transition, stop arming; registers live), `feg_clock` (`feg_prev`, `feg_prev_dir`, `feg_prev_passed`),
  `slot_stop` (fetch stop + off + CA 0, session 6), `slot_output` (no mute), `eg_increment`, `key_on`, `key_off`,
  `lpf_step`, `read()` (MIXS = current-parity bank); macros at the top.
- Evidence: `tests/aeg_koff/hw/` (+ `hw_run1/`), `tests/eg_kprobe/hw/`, `tests/slot_tail/hw/`, `tests/feg_koffdir/hw/`,
  `tests/feg_koffatt/hw/`, `tests/mixs_write/hw/` (session 5), `tests/feg_koffpass/hw/`, `tests/ca_stop/hw/`,
  `tests/mixs_rd/hw/`, `tests/eg_latch/hw/`, `tests/eg_latch2/hw/`, `tests/eg_latch3/hw/` (session 6), `tests/eg_lock/hw/`, `tests/feg_krs/hw/` (session 4),
  plus the earlier `tests/<case>/hw/`.
- Validators: `tools/eg_model.cpp` (everything envelope, 89 runs), `tools/eg_replay.cpp` (aeg_koff cycles),
  `tools/tail_cmp.cpp` + `tail_cmp_all.sh` (slot_tail), `tools/validate_s5.sh` (the gate), `tools/feg_validate.cpp`,
  `tools/filt_validate.cpp` (`filt_validate_model`), `tools/filt_overflow.cpp`.
- Standalone fitters: `tools/kfit.cpp`, `tools/koff_fit.cpp`, `tools/koffdir_check.cpp` + `tools/feg_law.h`,
  `tools/koffatt_check.cpp`, `tools/mixsw_check.cpp`; `work/verify/s5/s1_indep.cpp`, `s3alt.cpp`; session 6:
  `tools/koffpass_check.cpp` (feg_koffpass, outputs `work/koffpass/`), `tools/latch_check.cpp` (eg_latch / eg_latch2 /
  eg_latch3, outputs `work/latch/check_hw.txt`, `check2_hw.txt`, `check3_hw.txt`; control builds `work/latch/aica_model_live.cpp`,
  `aica_model_noskip.cpp`).
- Reports: `work/verify/s6/*.md` (session 6), `work/verify/s5/*.md`; session 4: `work/verify/s4.md`, `work/verify/s4/`.
- Previous handovers: `git show ba720b9:model/HANDOVER.md` (session 4), `git show fa451ca:model/HANDOVER.md` (session 3),
  `git show a07a62d:model/HANDOVER.md` (breakthrough).
