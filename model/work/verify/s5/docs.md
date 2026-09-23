# docs -- documentation update for session 5 (2026-09-23)

Files edited: `NOTES.md`, `HANDOVER.md`, `README.md`, `tests/SUMMARY.txt` (nothing else; no src/, cases/, tools/; no
git commit; nothing run on the console).  Sources for every number: the task's R1-R11, `work/verify/s5/*.md`, the
tool outputs `work/koff/koff_fit_hw_*.txt`, `work/kprobe/kfit_hw_f7.txt`, `work/koffatt/koffatt_check_hw.txt`,
`work/verify/s5/koffdir_check_hw2.txt`, `work/mixsw/check_hw.txt` / `check_model_final.txt`, `work/model5/*_final.txt`
and `control_*.txt`, the console text logs `tests/<case>/hw/<case>.txt` (sample counts, errors 0, MIXS readbacks), and
the validators agent's final files that appeared while this was written: `work/verify/expected/{validate_s5,
eg_model_all, eg_replay_<run>, tail_cmp_hw, kfit_hw, koff_fit_hw_<run>, koffdir_check_hw, koffatt_check_hw,
mixsw_check_hw, mixsw_check_model}.txt`, `work/verify/s5/run_model_all_final.log` (47 x exit 0),
`work/model_outputs_2026-09-23c.sha256`.  Checks I ran myself against the current tree: `build/tools/eg_model` (at
that moment 65 runs: `GROUPS session4=33/33 eg_kprobe=8/8 feg_koffdir=24/24`, `TOTAL full=65/65`; the validators
agent then added the 24 feg_koffatt ka_ runs, `expected/eg_model_all.txt` and `expected/validate_s5.txt` say 89/89 and
the docs use that), `build/tools/tail_cmp tests/slot_tail/hw/tail_c 74eb 6491 c` → 4/4 FULL, w2 12198, retained
`hw 0 2 2 | model 0 2 2`, `build/tools/eg_replay -case ... koff_d2 / koff_d2b / koff_d1 / koff_att` → 4/4 with
174080 / 168704 / 174592 / 163328 (kon_rel's 215808 from `expected/eg_replay_kon_rel.txt` = 53952 x 4).

## NOTES.md

Edited in place (no appended contradictions):
- "Access / register map": the MIXS bullet now says every slot writes its ISEL bus (IMXL 0 writes 0), retention only
  when no slot points at the bus, tagged [claims D2, T9]; the monitor line notes CA reads 0 from "off", not from the
  fetch stop (sgc_keys K4).
- "Slot levels": "IMXL = 0 is off" → "IMXL = 0 sends 0 -- a pure gain: the slot still writes its bus every sample".
- "Amplitude envelope": header lists aeg_koff / slot_tail / eg_kprobe; the "off" bullet rewritten as **Slot stop and
  "off"** (fetch stop at 0x3C0 and off past 0x3FF each one sample after its clock, no mute with the -16 half-waves,
  FEG runs on, the controls that break streams, `stop_in` / `off_in`, CA between stop and off not visible) [T8]; a
  new **Key-off on a clock sample** bullet with the koff_d2 30/30, koff_d2b 18/18, koff_d1 18/18, koff_att 27/27
  counts and the refuted counts [T3]; the R 63 bullet rewritten (transition on the key-on clock, kprobe 32/32 vs
  29/32, the even witness key-ons 10 + 6 + 6 + 9 + 7, odd_dec consistent with both) [T7].
- "Loops and key events": key-on during release now "loaded on the key-on sample, no step, CA restarts on that
  sample" with the kon_rel 48/48, L1 27/48, L2 21/48, C0 18/18, C1 0/18 counts and the superseded monitor wording
  [T6]; the decay-2 "off" sentence now distinguishes the 0x3C0 fetch stop from off past 0x3FF.
- "Key rate scaling": the R 63 line cross-references the key-on-clock transition.
- "Filter envelope (FEG)": header lists feg_krs / feg_koffdir / feg_koffatt / slot_tail; the key-off sentence in the
  sample-exact list now says "one more step of the OLD segment -- its increment AND its direction, hold check against
  FLV4"; a new **Key-off clock** bullet with feg_koffdir (4 even batches kd_0/1/5/6, the refuted readings), feg_koffatt
  (6 even batches 6/6, alternatives 0/6), KYONB-without-KYONEX holds, FEG runs on after the stop, `feg_prev_dir` [T4];
  the batch-1 anomaly bullet's wording adjusted.
- "Slot filter": the LPOFF / stopped-slot bullet gains the slot_tail confirmation sentence.
- "Envelope clock, key timing and increment rows": header now "sessions 4-5" with eg_kprobe; K bullet says **K = 6491
  exactly** (bit 13 = 0, the R 3 row indexed by eg_cnt bits 13:11, 8 probes) and that the number is bound to the -1
  offset convention; the eg_model sentence adds 89/89 and the independent S1 re-derivation (690 steps, K in {6491,
  14683}); new bullets **What sets K is not any register a program touches** (the six actions, dK +0) [T1] and **The
  envelope generators read their rate registers one sample late** (`egreg` / `eg_latch`, only RR measured) [T8]; the
  key-off bullet re-worded ("takes one more step of the segment the envelope was in", the session-5 correction:
  increment AND direction, AEG decays vs attack, odd samples nothing) [T3, T4, T5]; the R 63 bullet corrected [T7].
- New section **Key-off clock, slot stop and MIXS writers (session 5, 2026-09-23)** between "Envelope clock" and
  "MIXS retention": why the AEG decay-2 runs cannot pin the key-off sample (S3-even-K == noS3-odd-K+1), the witness
  slot method and its assumption, the K probe design (R 3 row, 16 ticks / 2 skips, set intersection), the results
  R3-R7 with the counts, the refuted alternatives from S3alt.md (release increment / no step, one-clock lag at all
  transitions with the ft_0 / fk_1 witnesses, pending step on the first clock after, two-clock lag, even-only state
  change, KYONB-driven target), the -8 resolution (bus 2 retained because nothing pointed at it; L 4 B -4 rest state
  at 0x1BFF Q 4; 25 fixed points, none at 0x1C00; ~4 % of trajectories; a held sample / frozen filter would be of
  order 10^5), the model changes (R8), the validation status (R9, with eg_model 89/89 incl. feg_koffatt 24/24) and the
  final tool names (R10).
- "MIXS retention and CPU writes": first bullet rewritten to the H_G rule with the mixsw_check counts (H_G 21/21,
  session-4 rule 4/21, the conditioned variants 9 / 17 / 17 / 18 / 17 / 5 of 21), the eg_lock -8 explanation and the
  tail_c 0 2 2 reproduction; the two-bank bullet untouched.
- "Open items (after session 5)": rewritten from R11 (K at boot; FEG key-off with `passed` set; CA between stop and
  off; DL / KRS / FEG rates through the latch; CPU MIXS write order; sgc_level L5; the cap_start head-estimate shift
  as a harness artefact).
- Every earlier finding that still stands was kept; the only sentences removed are the ones the session refuted
  (the old "output is silent" at off, "EG loads 0x280 on the next envelope clock", "steps toward the release target",
  "leaves the attack on the next clock", "IMXL = 0 is off", "a bus that no slot sends to (IMXL 0 ...) keeps its value").

## HANDOVER.md

Rewritten as the session-5 handover in the session-4 form: scope; "What changed" table (counter, AEG / FEG key-off
clock, key-on in release, R 63, slot stop / off, MIXS, cases, tools, model, scratch) with claim references; model
diff and the list of expected files (R10 names, plus `validate_s5.txt`, `eg_model_all_s4.txt`, `feg_validate_s4.txt`
as found on disk) and the `...23c.sha256` baseline; the session-4 claims S1-S9 summarised in one paragraph (S3
re-worded, S5 refined, S7 superseded; `git show ba720b9:model/HANDOVER.md`); rules for swarm agents (unchanged); setup
with the two sanity lines (`eg_model | tail -1` → `TOTAL full=89/89`, `tools/validate_s5.sh` → `validate_s5: PASS
(every stream FULL, mixs_write verdicts identical)`) and what validate_s5.sh covers (89 eg_model runs, the five
eg_replay runs, tail_cmp_all, the mixs_write verdict comparison); claims T1 (K exact, non-resetters), T2 (S1
independent), T3 (AEG key-off), T4 (FEG key-off, eg_model groups feg_koffdir + feg_koffatt), T5 (odd samples, by
elimination), T6 (kon_rel), T7 (R 63), T8 (slot stop / off, no mute, FEG runs on, rate latch, the -8), T9 (MIXS
writers), T10 (regression), each with the check command, the expected file, the controls that must fail and
INCONCLUSIVE / open notes; group H console list from R11; reference paths (model functions, evidence directories,
validators, standalone fitters, reports, previous handovers).

## README.md

Status paragraph updated (89/89 envelope streams, 5 multi-cycle runs of 16 cycles, 12/12 tails, 21/21 MIXS probes);
layout: cases count 47, `tests/aeg_koff/hw_run1/`, `work/eg/*.u` and `work/verify/`; a short list of the session-5
cases; `tools/validate_s5.sh` in Running; comparison tools: eg_model 89/89 with the four run groups, new entries for
eg_replay / tail_cmp / tail_cmp_all.sh / validate_s5.sh and for the standalone fitters kfit / koff_fit /
koffdir_check + feg_law.h / koffatt_check / mixsw_check (final tools/ names, now present under tools/).

## tests/SUMMARY.txt

Added a session-5 block with lines for aeg_koff (5 runs, per-run sample counts, 16/16 clean cycles, the koff_fit
counts, hw_run1 identical), eg_kprobe (K 6491 on 8/8, the non-resetters, 8/8 via eg_model, R 63), slot_tail (12/12,
the stop / off rules, 0 2 2), feg_koffdir (24/24 via eg_model, 4 even batches, KYONB holds), feg_koffatt (24/24 via
eg_model, 6/6 even batches) and mixs_write (21/21, H_G).  Adjusted: sgc_keys (K4's CA reset at off), eg_lock (the
mixs -8 / bus-0 zero explained), feg_krs (the session-5 wording of the key-off step).

## Numbers taken on trust, or with a caveat

- "./run_model.sh on all 47 cases exit 0" is R9's statement, corroborated by `work/verify/s5/run_model_all_final.log`
  (47 "exit 0" lines); the console-vs-model text-diff status (dsp_basic 3 lines, sgc_level L5, sgc_krs / sgc_keys /
  probe monitor timing) is R9's, not re-diffed here.
- The eg_model sanity line is 89/89 on disk (the ka_ group was added after the task text's 65/65 was written); every
  65/65 in the docs was replaced by 89/89 with the fourth group named.  My own eg_model run (65/65) predates that build.
- The number of tools `make -C tools` builds is not stated (the session-4 "38 tools" was replaced by "every
  tools/*.cpp").
- The tail_cmp control table in HANDOVER T8 quotes `work/model5/control_*.txt`, which were run before the MIXS (T9)
  fix, so tail_c stream 0 fails at 12198 in every row there for the MIXS reason; stated in the text.
- The "31 even witness key-ons" of validators.md refer to the FIRST console run (now `tests/aeg_koff/hw_run1`); for
  the second run the koff_fit parity lines give 10 + 6 + 6 + 9 (E_B) + 7 (E_C); both are quoted with their provenance.
- The `koff_d2b` H3 6/18 (session-4 model) vs the task's "no step only where the decay-2 increment was 0": the 6 are
  the D2R 0 slot's even cycles (koff_fit_hw_koff_d2b.txt), consistent.
- eg_model timing "about half a minute" for 89 runs is an estimate from the 65-run time (~28 s, validators.md), not
  measured.
