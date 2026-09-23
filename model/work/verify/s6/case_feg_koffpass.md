# case feg_koffpass -- the FEG key-off clock with the old segment's `passed` flag set (session 6, 2026-09-23)

Open item (NOTES "Open items", HANDOVER T4 INCONCLUSIVE): an attack / decay 1 that crossed its target on clock N sets
`passed`; on clock N+1 the FEG normally switches to the next segment BEFORE stepping (its rate and direction, no idle
clock).  What does a key-off landing exactly on clock N+1 do?  Readings (v = the value at the crossing):

| reading | on the N+1 key-off clock | then |
|---|---|---|
| **A oldStep** | one more step of the segment that just PASSED: its increment, its direction, hold check vs FLV4 | release |
| **B nextSeg** | what a normal clock N+1 does: advance to the next segment and step with ITS rate toward ITS target | release |
| **C noStep** | nothing (the AEG attack's rule) | release from the next clock |
| **D relStep** | the release increment toward FLV4 already on E | release |

Elsewhere (E = N, E inside a segment, E from the following segment, odd E) all four readings are the established rule
(one more step of the old segment, feg_koffdir / feg_koffatt) and coincide.

## What the model does

`src/aica_model.cpp`: `key_off()` saves `feg_prev = FEG.state` (the passed segment) and `feg_prev_dir = FEG.dir` (its
direction), sets `FEG.state = RELEASE`, `FEG.dir` toward FLV4 and **clears `passed`**.  On the key-off sample
`feg_clock()` runs with `keyed_off`: the `passed && state < DECAY2` advance does not fire (passed is false, state is
RELEASE), the rate comes from `feg_prev`, the direction from `feg_prev_dir`, the target and hold rule are the release's.
So the model takes **reading A**: slot 0 (attack 0x1800 -> 0x1810 at +2, passed at 0x1810) steps to 0x1812 on E and
releases +1 from there.  Confirmed on the model captures below (A FULL on every N+1 cycle, B / C / D refuted on every one).

## Files

- `cases/feg_koffpass.c` -- two runs (kp_a, kp_b) of 64 key-on / key-off cycles, one capture each (4 buses; the model
  gives 33024 / 33088 samples, 0 errors, 64 marks).  Harness = feg_koffatt: FEG slots 0..2 (random 8192-word input at
  0x20000, Q 4, VOFF 1, LPOFF 0, KRS 15, AEG AR 31 / D1R 0 / RR 0), AEG witness slot 3 (constant 0x7FFF at 0x10000,
  TL 0, VOFF 0, LPOFF 1, AR/D1R/DL/D2R/RR 31, KRS 1 -> R 63) keyed ON by the KYONEX that keys the FEG slots OFF.
- `tools/koffpass_check.cpp` -- standalone checker (includes `tools/feg_law.h` for the increment tables / eff_rate and
  `tools/filt_capture.h`; built by the generic tools rule -> `build/tools/koffpass_check`).
- `work/koffpass/check_model.txt`, `check_model_q.txt` -- the checker on `tests/feg_koffpass/model` (full / `-q`).
- Console binary: `build/hw/feg_koffpass.elf` (compile check passed under the KOS environment).

## Design

All three slots cross on the same clock (N = 8 in kp_a, N = 12 in kp_b), so one key-off sample is "N+1" for all three:

| slot | attack | decay 1 | decay 2 | release | at the N+1 clock (kp_a): A / B / C / D |
|---|---|---|---|---|---|
| 0 | FLV0 1800 -> FLV1 1810, FAR 26 (R 52, +2), passes at 1810 on clock 8 | toward FLV2 1000, FD1R 28 (R 56, -4) | FD2R 28 | UP to FLV4 1C00, FRR 24 (R 48, +1) | 1812 / 180C / 1810 / 1811 = u C09 / C06 / C08 / C08, then E+2: C09 / C06 / C08 / C09 |
| 1 | 1C00 -> 1BF2 at -2 (down: ends strictly below, 1BF0 on clock 8) | UP toward 1FF0 at +4 | -- | DOWN to 1400 at -1 | 1BEE / 1BF4 / 1BF0 / 1BEF = u DF7 / DFA / DF8 / DF7, E+2: DF6 / DF9 / DF7 / DF7 |
| 2 | 1800 -> 1808 at +8 (FAR 30, R 60: passes on clock 1) | 1808 -> 1816 at +2 (FD1R 26, R 52), passes at 1816 on clock 8 | toward 1000 at -4 (R 56) | UP to 1C00 at +1 | 1818 / 1812 / 1816 / 1817 = u C0C / C09 / C0B / C0B, E+2: C0C / C0A / C0B / C0C |

Slot 2 tests the decay 1 -> decay 2 boundary (passed set in decay 1), slots 0 / 1 the attack -> decay 1 boundary in both
directions.  Every pair of readings differs in u at E or at E+2 (the release +1 / -1 shifts the trajectories by a constant
offset of +2 / -4 / 0 / +1 in v, so the u sequences stay distinct from E on).  Two deviations from the task text: slot 1's
FLV1 is 0x1BF2, not 0x1BF0, so that the downward attack (which ends strictly below its target) also passes on clock 8;
slot 2's attack is the one-clock 0x1800 -> 0x1808 at R 60 and its decay 1 0x1808 -> 0x1816, so that its decay-1 crossing
is on clock 8 too (the task's 0x1900 -> 0x1910 after a +8 attack over 0x100 would have crossed on clock 40, needing a
separate key-off window).  kp_b: FLV1 0x1818 / 0x1BEA, slot 2 FLV2 0x181E (crossing on clock 12).

Cycle: `cap_poll()` (reader caught up, so no ring read lands in the timed window), `keyx(0x7)` (FEG slots on: a fresh
key-on during their held release, FLV0 reloaded, CA restarted), `spin_us(on_us)` WITHOUT polling (key-on -> key-off
spacing = keyx (10 G2 accesses, 24 us on the model) + on_us, timer-exact), `keyx(0x8)` (FEG slots off + witness on),
mark 3, 8 ms (the witness reaches off by itself: 128 clocks), `keyx(0)` (witness off -> RELEASE, so its next key-on is
fresh; the released FEG slots are untouched), 3 ms.  No aica_quiet between cycles.  `on_us = on_base + 8 * (cyc % 16) +
2 * (cyc / 16)`, on_base 310 (kp_a) / 490 (kp_b): 128 us = 5.6 samples of spread, so d = E - key-on covers 15..20 (kp_a:
N at 15 / 16, N+1 at 17 / 18, N+2 at 19 / 20) and 22..28 (kp_b: N+1 at 25 / 26).  Half the key-offs land on odd samples
(indifferent); of the even ones about a third are N+1, a third N (re-tests the plain old step of the attack / decay 1), a
third from the next segment (old step of decay 1 / decay 2 -- decay 1 was not measured for the FEG before).  A console
systematic offset of up to +-1.5 samples against the model's timing still leaves the N+1 offsets inside the window.
Model d histogram: kp_a 15:9 16:12 17:11 18:11 19:11 20:10; kp_b 22:2 23:8 24:15 25:7 26:13 27:10 28:9.

## Checker (`build/tools/koffpass_check <dir> [-K kc] [-q]`)

1. Runs, programs, Q and c0 from `feg_koffpass.txt` (the `kp_x stream k: slot k role feg ...` lines, the `, c0 ` field
   of the following cap_start line); the input regenerated from the LCG and checked against `input.bin`.
2. Key-off samples E = the witness onsets (stream 3 jumps to 520176 = a 0; the first sample at that level).
3. Key-on sample per cycle: the filter is never cleared, so the tracker (koffatt_check's algorithm: state set of
   (L, B, u) through the bit-exact filter, u +- 4 per sample, x(n) = 8 sig[(n - on) mod 8192]) carries the filter state
   through the previous cycle's release and tests the key-on hypothesis on every candidate in [E-64, E-6]: hyp 0 "CA
   restarts at sig[0] and u = FLV0 >> 1 on that sample" (the model; every model cycle), hyp 1 "FLV0 one sample later",
   hyp 2 "CA restarts, FLV0 not reloaded" -- the first candidate tracking 100 samples on all three streams is the key-on
   (the checker flags hyp 1 / 2 if the console needs them).  Cycle 0 starts from the rest prior (L from the last output,
   B in [-256, 256]).  Then it tracks to the next cycle (the next E - 70).
4. Phase per stream from the FEG law simulated from the key-on (no step on the key-on sample, clock on even MDEC_CT,
   eg_cnt = K - MDEC_CT/2): the tested crossing = the last clock within the first 64 on which the normal law sets passed
   (slot 2's one-clock attack is skipped that way).  **N+1** = passed set at E; **N** = E is the crossing clock; **pre** =
   inside the segment before its crossing; **post** = keyed off from the following segment; **odd** / **odd\*** = E not a
   clock (odd\*: the passed flag was pending on the odd key-off sample -- the prediction is the release from the next
   clock; the model clears the flag).
5. Each reading simulated from the key-on to the end of the cycle and compared with the tracked u where unambiguous:
   FULL = every known sample to the cycle end; else the first mismatch (rel. E) with observed / predicted u, and whether
   it matched through E+8.  Per cycle it prints the key-on, E, MDEC_CT parities, d, phase, the state before E, the
   crossing clock, observed u at E-4..E+10 and every reading's prediction.  Summary per run and total: FULL per reading
   per phase (all-3-streams and per stream), the d histogram, N+1 cycles where no / more than one reading fits.
   `-q` prints only the N+1 cycles and anything anomalous (untracked stream, phases differing between streams, no reading
   fitting) plus the summary.  Runtime ~20 s per capture pair.

## Model results (`build/tools/koffpass_check tests/feg_koffpass/model`)

- 128 / 128 key-ons found (hyp 0 everywhere), no untracked stream-cycle, tracker set size <= 35.
- Phases: kp_a N+1 11, N 8, post 11, odd\* 12, odd 22; kp_b N+1 8, N 12, pre 2, post 9, odd\* 13, odd 20.
- **N+1: A oldStep 19/19 cycles FULL on all 3 streams; B nextSeg 0/19, C noStep 0/19, D relStep 0/19** (per stream
  19 / 0 / 0 / 0 out of 19 each); no N+1 cycle with no or more than one reading fitting.  B / C fail at E on every stream,
  D fails at E on s0 / s2 and at E+2 on s1 (its u coincides with A's at E there), as designed.
- N 20/20, pre 2/2, post 20/20, odd\* 25/25, odd 42/42 FULL for every reading (the established rule).
- Example (kp_a cycle 6, key-on 3207 odd, E 3224 even, d 17): s0 observed u E-4..E+10 `c07 c07 c08 c08 c09 c09 c09 c09
  c0a ? c0a c0a c0b c0b c0b`; A predicts `... c09 c09 c09 c09 c0a ...` (FULL), B `c06 ...`, C `c08 ...`, D `c08 c08 c09 ...`.

## Commands for the orchestrator

```sh
cd caique-rtl/model
./run_hw.sh feg_koffpass                              # ~2 s of console audio; two captures + input.bin + feg_koffpass.txt in tests/feg_koffpass/hw
build/tools/koffpass_check tests/feg_koffpass/hw      # K 6491 (same boot); -K <kc> after a reboot (kfit on an eg_kprobe run first)
build/tools/koffpass_check tests/feg_koffpass/hw -q   # the N+1 cycles and the summary only
```
Check first: both runs `errors 0`, `64 witness onsets`, all key-ons found under hyp 0, `untracked stream-cycles: 0 0 0`,
and the d histogram covering 17 / 18 (kp_a) and 25 / 26 (kp_b) with a few even N+1 cycles (>= 3 per run expected; if the
console's spacing is offset, shift `on_base` by the observed d shift x 22.7 us and rerun).

## Expected console signatures per reading (N+1 cycles, kp_a values; E even, v at the crossing)

| reading | s0 u at E, E+2, E+4 | s1 u at E, E+2, E+4 | s2 u at E, E+2, E+4 | TOTAL line |
|---|---|---|---|---|
| A oldStep (model) | C09 C09 C0A | DF7 DF6 DF6 | C0C C0C C0D | `N+1  A n/n  B 0/n  C 0/n  D 0/n` |
| B nextSeg | C06 C06 C07 | DFA DF9 DF9 | C09 C0A C0A | `A 0/n  B n/n  C 0/n  D 0/n` |
| C noStep | C08 C08 C09 | DF8 DF7 DF7 | C0B C0B C0C | `A 0/n  B 0/n  C n/n  D 0/n` |
| D relStep | C08 C09 C09 | DF7 DF7 DF6 | C0B C0C C0C | `A 0/n  B 0/n  C 0/n  D n/n` |

(u = v >> 1 with the +1 / -1 release continuing; the exact values depend on the crossing value -- kp_b's are 4 higher on
s0 / s2 and 4 lower on s1 -- but the offsets between readings are +2 / -4 / 0 / +1 in v everywhere.)  Anything else
(all four refuted on the N+1 cycles while N / post / odd cycles stay FULL) means a fifth mechanism: the printed observed u
against the four predictions pins the step taken.  The N cycles must read the old attack / decay-1 step (feg_koffatt's
rule), the post cycles the old decay-1 (s0 / s1: -4 / +4) and decay-2 (s2: -4) step -- decay 1's key-off clock is new
evidence for the FEG -- and odd\* cycles must show the plain release from the next clock (a pending passed flag doing
anything on an odd key-off would show there as A/B/C/D all failing at E+1).
