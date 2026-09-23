# docs_s6b -- final session-6 documentation pass: the register latch (2026-09-23 night)

Edited: `NOTES.md`, `HANDOVER.md`, `README.md`, `tests/SUMMARY.txt` (nothing else; no src/, cases/, tools/, console, git).
Sources: `work/latch/check_hw.txt`, `check2_hw.txt`, `check3_hw.txt` (SUMMARY lines; every run `errors 0`, 64 marks in
`tests/eg_latch*/hw/*.txt`), `cases/eg_latch2.c` / `eg_latch3.c` headers (run definitions), `git diff HEAD -- src/`
(`Slot::egreg` / `eg_latch()` removed; `aeg_clock` / `feg_clock` / `eff_rate` read r10, r14, r18, r40, r44 live),
`tools/tail_cmp.cpp` stage 2 (d = w00 - w14 in {1, 0, -1}), and the coordinator's final gate numbers (eg_model 89/89,
eg_replay 5 x 4/4 with 16/16 clean cycles, tail_cmp 12/12 with tail_c w14 11301, feg_validate 9/9, validate_s5 PASS;
transcript `work/verify/s6/bitcheck.txt`, complete at the time of this pass: eg_model 89/89, eg_replay 5 x 4/4 16/16,
tail_cmp 12/12, mixs_write 21 rows identical, validate_s5 PASS, feg_validate 9/9 77862/77862, filt_validate_model 265/265,
filt_overflow 15/15 + 12/12; `expected/tail_cmp_hw.txt` regenerated: tail_c `best w14 11301 w00 11300 (d -1)`, `w14
11301(odd) w00 11300(even)`).

## NOTES.md

- Envelope clock: the "rate registers one sample late" bullet is replaced by two bullets -- (a) every envelope register
  is read live, with the session-5 statement and the tail_c observation kept as history, the method, and the eg_latch
  (DL 6+8, KRS 11+12, AR 8+10, D2R 11+10, FD1R/FD2R 14+15, FLV3 1+4; 0 latched, LATE 0), eg_latch2 (rr0 12+14, rr24
  10+12, rr0_koff 10+10, rekoff24 11+11, d2r0 6+16) and eg_latch3 (sa_hi 14+15, sa_lo 13+11, lpctl 12+11, lea 14+11,
  none 16+18; CA restarted 0/48) numbers, claims T8 (latch clause refuted) / U4; (b) the in-sample ordering rule
  (envelope update before the fetch; key events from the next sample; a write after the envelope phase acts on the next
  clock), tail_c's 11300 / 11302 explained, the EARLY-straddle statistic (krs 5/25, ar 5/28, d2r 1/27, feg_rate 1/19,
  eg_latch2/3 1-3 per run), the model's sub-sample limitation and tail_cmp's d search (w00 11300 / w14 11301), the RTL
  rule.
- Session-5 section: Slot stop / off bullet -- the latch clause marked REFUTED with the explanation; Model bullet --
  `egreg` noted as removed in session 6.
- Session 6 section: header and intro (six cases, U1-U4, all four open envelope items closed, bitcheck.txt); the "Not
  final" bullet replaced by U4; the model-gates line updated to the final numbers (tail_c w14 11301, validate_s5 PASS).
- Open items: the latch item removed; new item "sub-sample position of register writes vs the envelope phase (not
  modelled; RTL: EG update precedes the fetch within a sample)"; K at boot, MIXS sub-sample order, filter, cap_start
  head estimate kept; U4 added to the "closed in session 6" line.

## HANDOVER.md

- Intro file list: eg_latch2 / eg_latch3.  What changed: Slot stop row ("latch REFUTED", U4), Model row (`egreg` removed
  again), Session-6 cases / tools / model rows (eg_latch2/3 with their model runs, tail_cmp d search, `egreg` removal).
  Setup: case count 53.
- T8: heading strikes "rate registers one sample late" (-> U4); the session-6 bullet states the refutation, qualifies the
  session-5 control "Live rate registers: tail_c s0 fails 11539" (only with the RR write forced onto the SA sample), and
  gives tail_cmp's w00 11300 / w14 11301.
- H list: the latch item removed.
- Addendum intro: six cases, four model changes, tail_cmp change, final gates, reports incl. docs_s6b.md / bitcheck.txt.
- "T8 latch statement -- under re-measurement" replaced by "U4 -- every envelope register is read live ...": method,
  the three latch_check commands with the SUMMARY lines to expect per run, model / control builds, controls (a latched
  model predicts order-1 events at E+2: 0 observed; LATE 0; EARLY only where a live register allows it), what would
  refute, the in-sample ordering explanation of tail_c with the tail_cmp fit, an independent check.
- Key paths: `step()` without `eg_latch`, eg_latch2/3 evidence dirs, latch_check outputs and control builds.

## README.md

Intro: live envelope registers (359 informative witness-pinned rewrites, 0 latched); case count 53; Session-6 cases paragraph
(eg_latch all live, eg_latch2, eg_latch3); latch_check covers eg_latch2/3 and the CA-restart count; tail_cmp d search.

## tests/SUMMARY.txt

slot_tail line: the RR clause rewritten (write landed after sample 11300's envelope phase, w00 11300 / w14 11301);
eg_latch HW -> OK with the per-run numbers; new eg_latch2 OK and eg_latch3 OK lines.

## Left as is on purpose

The T8 body's session-5 wording and control list (history; the session-6 bullet below it carries the correction); the
FEG / Amplitude-envelope sections never mentioned the latch.  No number outside the sources above was used; the "359
informative even-E events" is the sum of the per-run order-0 + order-1 counts quoted (eg_latch 6+8+11+12+8+10+11+10+14+15+1+4
= 110, eg_latch2 12+14+10+12+10+10+11+11+6+16 = 112, eg_latch3 16+18+14+15+13+11+14+11+14+11 = 137; note lpctl's order-0
14 includes 2 "neither", ar's order-0 8 includes 2, dl's 7 / 9 one each -- the "0 latched" is exact, the "live" totals are
the counts minus those).
