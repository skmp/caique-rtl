# docs_s6a -- session-6 documentation pass (2026-09-23 night)

Edited: `NOTES.md`, `HANDOVER.md`, `README.md`, `tests/SUMMARY.txt`.  Nothing else touched (no src/, cases/, tools/,
console, git).  Sources quoted: `work/verify/s6/case_feg_koffpass.md`, `work/koffpass/check_hw.txt` (TOTAL / per-run
blocks) and `check_model.txt` / `check_model_after.txt`, `tests/ca_stop/hw/ca_stop.txt` + `tests/ca_stop/model/ca_stop.txt`,
`tests/mixs_rd/hw/mixs_rd.txt` + `tests/mixs_rd/model/mixs_rd.txt`, `work/latch/check_hw.txt` SUMMARY lines,
`work/verify/s6/case_eg_latch.md`, `git diff HEAD -- src/ tests/probe tests/sgc_keys` (the implemented rules: `slot_stop()`
= `enabled = false; AEG.off = true; CA = 0`, `feg_prev_passed` / next-segment seg-dir in `feg_clock`, `read()` of 0x4500..
= `MIXS_bank[samples & 1]`; `off_in` / `slot_off` gone; the DL compare no longer gated by `!c.AEG.off`).

## NOTES.md

- Access / register map: MIXS bullet gained the CPU readback rule (current-parity bank, boundary behaviour, lo-before-hi
  sub-sample caveat, `read()`), claim U3; the 0x2814 CA line now says CA reads 0 from the 0x3C0 stop (ca_stop), with the
  session-5 K4 reading kept as history.
- Amplitude envelope: header lists ca_stop; the "Slot stop and off" bullet is rewritten as "Slot stop = off" -- 0x3C0
  clock + 1 sample: fetch stop, monitor 0x1FFF, CA 0; value still steps to 0x3FF, no mute, FEG runs on, DL compare on the
  stop clock (state 2); the session-5 statement and the K4 reading quoted as history; the ca_stop design and the S1 / S2 /
  S3 poll times (5873 / 5905 / 5937; 5859 / 5891 / 5936; 5864 / 5895 / 5927 us) with the old-CA-then-0 sequence; the
  "CA between stop and off" question marked moot; model sentence updated (`stop_in` only, K4 log line 6231 -> 5868 us).
- Loops and key events: the "Decay 2 reaching the top" sentence now puts the monitor / CA at 0x3C0 + 1 (history kept).
- FEG: header lists feg_koffpass; the sample-exact key-off sentence names the next-segment case; the Key-off clock bullet
  gained the passed-flag paragraph (design, 10/10 + 13/13, the three refuted readings 0/23, the other phases FULL, the
  generalised rule, model fields, gates 89/89 12/12 9/9 5 x 4/4 and nextSeg 11/11 + 8/8, report paths), claim U1.
- Envelope clock: one session-6 sentence appended to the key-off bullet (the generalised rule); every latch sentence left
  byte-identical.
- Session-5 section: FEG bullet notes the T4 INCONCLUSIVE closed; Slot stop / off bullet notes the "off past 0x3FF" half
  superseded and the latch statement under re-measurement; Model bullet notes `off_in` removed and `feg_prev_passed`.
- New short section "Session 6 (...)" before MIXS retention: U1-U3 one line each, eg_latch not final, model gates and the
  two whole-program diffs (probe MIXS0 lines, sgc_keys K4).
- MIXS retention: header lists mixs_rd / U3; new bullet with every run's readback pattern (R1-R7, the 30 ms reads), the
  reading, the lo-before-hi caveat, the eg_lock remark re-read as a straddling pair, the cross-session retention (R2),
  the model change and the one tests/probe nibble.
- Open items rewritten "(after session 6)": K at boot; the latch (eg_latch live results, tail_c's delayed RR: rate 0 ->
  nonzero arming vs a redundant key-off on a released slot, eg_latch2 pending; latch sentences stand); sub-sample MIXS
  order; filter; cap_start head estimate; a "closed in session 6" line.
- Test-writing notes: caveat that a bus nobody points at carries values across programs (mixs_rd R2).

## HANDOVER.md

- Title / intro: "+ session 6 addendum"; session 5 = commit 5207a9f, session 6 uncommitted (file list).
- What changed: FEG key-off / Slot stop / MIXS rows carry a "Session 6:" clause and claims U1 / U2 / U3; three new rows
  (Session 6 cases / tools / model); model-diff line points at 5207a9f and `git diff HEAD -- src/`.
- Setup: "47 case binaries" -> 52 (no 0x400 in the sanity lines; the T8 control `=0x400` stays as the recorded control).
- T4 INCONCLUSIVE -> closed by U1; T8 gained a session-6 paragraph (off superseded, latch under re-measurement).
- H list: the three items marked DONE / measured.
- New "Session 6 addendum" with U1 (koffpass_check command, check_hw.txt expected header / histogram / TOTAL lines, an
  example cycle, model before / after, controls, refutation, independent check), U2 (ca_stop lines to read per run,
  refutation, model output, what is superseded), U3 (mixs_rd lines per run, reading, refutation, model, caveat) and the
  "T8 latch statement -- under re-measurement" paragraph with the check_hw.txt SUMMARY numbers.
- Key paths: model functions, evidence dirs, session-6 tools and reports.

## README.md

Intro sentence for session 6; cases 47 -> 52 (eg_latch2 in progress); Session-6 cases paragraph; koffpass_check and
latch_check in the standalone-fitter list.

## tests/SUMMARY.txt

sgc_keys and slot_tail lines corrected (stop = off; latch under re-measurement); Session 6 header and the lines
feg_koffpass, ca_stop, mixs_rd (OK) and eg_latch (HW: all rewritten registers live; follow-up pending).

## Not touched on purpose

Every "one sample late" / `egreg` / `eg_latch` sentence (NOTES Envelope clock, Amplitude envelope model notes, session-5
section, HANDOVER T8 body, SUMMARY slot_tail's "RR rewrite reaches the EG one sample late") -- the orchestrator rewrites
them after eg_latch2.  Numbers are only those in the sources above; the model's whole-program d histograms of
feg_koffpass are not quoted (only the console's).
