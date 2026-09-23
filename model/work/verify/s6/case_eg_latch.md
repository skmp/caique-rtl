# case_eg_latch -- console case `eg_latch` + checker `latch_check`: which envelope registers reach the EG one sample late?

Written 2026-09-23 (session 6 agent).  No existing file was edited; nothing was run on the console.  Open item from
HANDOVER T8 / NOTES "Open items": tests/slot_tail tail_c showed RR rewritten on a clock sample acting on the NEXT clock
while the SA rewrite of the same write pair acted on that sample.  The model latches r10 (AR/D1R/D2R), r14
(RR/DL/KRS/LPSLNK), r18 (OCT/FNS), r40/r44 (FEG rates) one sample late (`Slot::egreg`, `eg_latch`) and reads the FLV
targets (0x2C..0x3C) live -- only RR was measured.  This case measures DL, KRS, AR, D2R, FD1R, FD2R and FLV3.

## Files created

| Path | What |
|---|---|
| `cases/eg_latch.c` | console/model case: 6 runs (dl, krs, ar, d2r, feg_rate, flv) x 16 cycles x 3 rewrite events; output `eg_latch.txt`, `<run>.hdr/.bin`, `input.bin` |
| `tools/latch_check.cpp` | standalone integer checker (AEG law as koff_fit, FEG law from `tools/feg_law.h`, u tracker from koffatt_check; no model link); built by the generic tools rule -> `build/tools/latch_check` |
| `tests/eg_latch/model/` | the model run (`./run_model.sh eg_latch`: 6 captures, 0 errors, 64 marks each) |
| `work/verify/s6/case_eg_latch_model.txt` | checker output on the model captures (K 6491) |
| `work/verify/s6/case_eg_latch_model_live.txt` | checker output on a control model whose EGs read the registers live (below) |
| `work/verify/s6/case_eg_latch_model_K6490.txt` | wrong-K control (only the ar run's R < 48 streams depend on K) |
| `work/latch/` | scratch: `aica_model_live.cpp` (patched copy of the model for the control), `model_live/` (its captures), checker outputs |
| `build/host/eg_latch`, `build/hw/eg_latch.elf` (KOS build, 0 warnings), `build/tools/latch_check`, `build/work/eg_latch_live` | binaries |

## Commands (from `caique-rtl/model`)

```sh
make -C tools latch_check                     # -> build/tools/latch_check
./run_model.sh eg_latch                       # model run -> tests/eg_latch/model/
build/tools/latch_check tests/eg_latch/model  # model verdicts (expected table below)
# console (orchestrator; about 3.5 s of capture + 6 x (aica_quiet + ring clear) ~ 6-8 s of console time, 2.4 MB of files):
./run_hw.sh eg_latch
build/tools/latch_check tests/eg_latch/hw [-K <boot K>] [-run <name>] [-v]
```
K matters only for the `ar` run's streams 1 and 2 (AR 20 / 18 = R 40 / 36, slow rows): with K 6490 the model's ar run
shows 32 pre-event mismatches, every other run is byte-identical (`case_eg_latch_model_K6490.txt`).  If the console has
been rebooted since session 5, fit K first (`kfit -case tests/eg_kprobe/hw` on a fresh eg_kprobe run) and pass `-K`.
The checker's console log must show every run with `errors 0`; per cycle it prints the key-on sample E_A, per event the
witness onset E with its parity, the four fits and the verdict; `SUMMARY` lines per run at the end.

## Method

**Pinning.**  Every register rewrite is one write pair: the register write and the witness slot's own reg 0x00 write with
KYONB | KYONEX (a single write: KYONEX applies every slot's KYONB) -- two G2 writes ~2.4 us apart, almost always inside
one 22.7 us sample.  The witness (AR 31 KRS 1 -> R 63, constant 0x7FFF, VOFF 0) jumps to 520176 (a = 0) on the sample E
the key event takes effect on, so E is the sample on which a fetch-timed register write from the same pair acts.  The
question per rewrite: does the EG show the new register at clock E ("live") or at clock E + 2 ("latched", the model's
egreg)?  Only an even E (a clock sample) separates the two; on an odd E both act at clock E + 1.

**Three witnesses.**  Slots 3, 4, 5 all send to bus 3; witness j serves event j of a cycle.  Each cycle's key-on KYONEX
(test slots' KYONB 1, witnesses' 0) keys all three witnesses off -- a slot that reached off in decay 2 cannot be keyed on
again without a key-off (tests/sgc_keys K4: "keyon-after-off" ignored).  Events are >= 2.6 ms apart, so the previous
witness (D1R 31 to DL 31: off within 5.8 ms) is at a >= 0x1C0, its residual level <= 4048, and the 20-bit bus sum never
wraps; the checker finds E as a jump >= 450000 on bus 3 (the model shows 520224 = 520176 + 48 where a residual overlapped).

**Write order alternates per cycle** (even cycles: register then KYONEX; odd cycles: KYONEX then register).  A sample
boundary inside the pair (~2.4 / 22.7 = 10 % of the bursts) shifts the register write by one sample against the key
event.  The checker therefore fits FOUR switch samples Vc (the EG sees the new image from clock Vc on): E-1, E, E+1, E+2.
- even E: {E-1, E} = the live pattern, {E+1, E+2} = the latched pattern.  A latched register with an order-0 straddle
  reads "live", a live register with an order-1 straddle reads "latched": the order with the 100 % consistent verdict is
  the truthful one, the other shows the ~10 % minority.
- odd E: {E, E+1} coincide (clock E+1).  A fit at E-1 alone ("EARLY": the write acted at clock E-1) can only come from a
  LIVE register whose write landed one sample before the key event (order 0); a fit at E+2 alone ("LATE": clock E+3)
  only from a LATCHED register whose write landed one sample after it (order 1).  The straddles thus give one-sided
  proof on the parity that is otherwise uninformative.

**Bursts land on both parities.**  Time-triggered events end their wait phase-locked to a ring block read, and the
monitor-triggered ones fire right after a clock step, so every trigger is followed by a random spin (0..45 us, or the
window-sized spin of the dl run); without it the first model run put 48/48 flv events on an odd E.

**Test slots keep KYONB = 1 through the events** (a KYONEX on a slot already keyed on does nothing: the model; sgc_keys
K2) and are keyed off + released to off (RR 31) before the next cycle; the rewritten registers are restored during the
release.  Every run starts with `aica_quiet`.  Marks: 1 at the key-on, 2 / 3 / 4 after event 0 / 1 / 2 (16 x 4 = 64).

### Runs (KRS 15, OCT 0, FNS 0 on the test slots; constant-0x7FFF level law for the AEG runs, random-input FEG harness of feg_koffatt for the FEG runs)

| run | test slots (0 / 1 / 2) | event 0 | event 1 | event 2 | reading difference |
|---|---|---|---|---|---|
| dl | AR 31, D1R 24 / 26 / 28 (+1/+2/+4), DL 31, D2R 0, RR 31 | slot 2: reg 0x14 DL 13 while a[9:5] == 13 (AEG monitor polled, spin 0..200 us), ~5.1 ms | slot 1: DL 17 at a[9:5] == 17 (spin 0..450 us), ~12.7 ms | slot 0: DL 13 (spin 0..1000 us), ~19.3 ms | decay 1 -> 2 compare true at clock E (hold from E) or E+2 (one more +inc, then hold) |
| krs | AR 31, D1R 24 (+1), DL 31, D2R 0 | slot 2: KRS 2 (R 52, +2) at 4-6 ms | slot 0: KRS 4 (R 56, +4) at 10-12 ms | slot 1: KRS 6 (R 60, +8) at 16-18 ms | +inc_new at E vs +1 at E then +inc_new |
| ar | AR 24 / 20 / 18 (R 48 / 40 / 36), D1R 24, DL 31 | slot 0: AR 28 (inc 4) at 0.6-1.5 ms | slot 1: AR 30 (inc 8) at 6-9 ms | slot 2: AR 31 (R 62, inc 8) at 13-17 ms | attack step size changes at E or E+2; D1R 24 keeps the one-clock offset visible |
| d2r | AR 31, D1R 31, DL 2 (decay 2 from 0x40), D2R 24 / 24 / 26 | slot 2: D2R 30 (+2 -> +8) at 4-6 ms | slot 0: D2R 28 (+1 -> +4) at 10-12 ms | slot 1: D2R 30 (+1 -> +8) at 16-18 ms | as krs |
| feg_rate | VOFF 1 LPOFF 0 Q 4; s0/s1 FLV 1A00 1A00 1A00 0800 1FF8 FAR 31 FD1R 31 FD2R 24 (decay 2 -1/clock); s2 FLV 1A00 1A00 0800 0800 1FF8 FD1R 24 (decay 1 -1/clock) | slot 0: reg 0x44 FD2R 28 (-4) at 4-6.5 ms | slot 1: FD2R 30 (-8) at 11-13.5 ms | slot 2: reg 0x40 FD1R 28 (-4) at 18-20.5 ms | v differs by 3 from E on (u = v >> 1 differs on every sample) |
| flv | FLV 1800 1800 1800 1C00 1FF8, FAR 31 FD1R 31 FRR 31, FD2R 30 / 26 / 28 (decay 2 UP +8/+2/+4) | slot 0: reg 0x38 FLV3 := v_mon + 5, v_mon >= 0x1880 + rnd % 0x300 | slot 2: FLV3 := v_mon + 3, v_mon >= 0x1A80 + rnd % 0x100 | slot 1: FLV3 := v_mon + 2, v_mon >= 0x1B00 + rnd % 0xC0 | target in (v_E, v_E+inc]: live -> hold at v_E from E; latched -> clock E steps past it, C stays true, runaway to 0x1FFF |

flv: v_mon is re-read after the spin so the target sits just above the value the EG holds; when a clock fell between
the read and E the target is at or below v_E and both readings run away (the checker prints "at/below v_E", counts the
event as identical/uninformative).  The AEG monitor (0x2810, MSLC) drives the dl triggers and the FEG monitor (AFSEL 1)
the flv triggers; a monitor lag of a sample only changes the hit rate, never the verdict (v is recovered exactly).

### Checker

`latch_check <dir>`: per cycle E_A = the 496 onset of stream 0 (AEG runs) or the first sample near the first stream-0
change from which the tracker follows 300 samples (FEG runs: the input is the LCG signal from the key-on sample, filter
state at rest, FLV0 known); per event E = the bus-3 jump in the window around the mark; the rewritten slot is simulated
from E_A under none / Vc = E-1 / E / E+1 / E+2 and compared up to E + 600 samples (or the key-off); a cycle-event is
"informative" when E is even and the live and latched predictions differ.  "old law fail@E+d" is the observed change
(the first sample the unchanged registers fail to predict: E+2 for live, E+4 for latched with a constant-row decay).
`-v` prints the window E-3..E+8 with captured a / u and the five predictions.

## Model verdicts (`tests/eg_latch/model`, K 6491)

```
dl        events 48 found 48 missed 0 pre-fail 0 odd-E plain 23 identical 0 | even E, order0 12: live 2 latched 10 neither 0 | order1 10: live 0 latched 10 neither 0 | odd E straddles: EARLY 0/0 LATE 0/3
krs       events 48 found 48 missed 0 pre-fail 0 odd-E plain 31 identical 0 | even E, order0  8: live 0 latched  8 neither 0 | order1  7: live 0 latched  7 neither 0 | odd E straddles: EARLY 0/0 LATE 0/2
ar        events 48 found 48 missed 0 pre-fail 0 odd-E plain 23 identical 0 | even E, order0 15: live 4 latched 11 neither 0 | order1  9: live 0 latched  9 neither 0 | odd E straddles: EARLY 0/0 LATE 0/1
d2r       events 48 found 48 missed 0 pre-fail 0 odd-E plain 28 identical 0 | even E, order0 14: live 1 latched 13 neither 0 | order1  5: live 0 latched  5 neither 0 | odd E straddles: EARLY 0/0 LATE 0/1
feg_rate  events 48 found 48 missed 0 pre-fail 0 odd-E plain 22 identical 0 | even E, order0 13: live 2 latched 11 neither 0 | order1 12: live 0 latched 12 neither 0 | odd E straddles: EARLY 0/0 LATE 0/1
flv       events 48 found 48 missed 0 pre-fail 0 odd-E plain 22 identical 0 | even E, order0 14: live 14 latched 0 neither 0 | order1  8: live 8 latched  0 neither 0 | odd E straddles: EARLY 4/0 LATE 0/0
```
Every latched register (dl, krs, ar, d2r, feg_rate): order 1 100 % latched, order 0 latched with the ~10 % straddle
minority reading "live" (9 of 62 informative order-0 events), LATE only in order 1, no EARLY.  The live FLV3 target:
100 % live in both orders (no order-1 straddle happened on an even E in this run), EARLY only in order 0, no LATE.
"neither" = 0, "pre-fail" = 0, every witness onset found, no missed trigger.

**Live control** (`case_eg_latch_model_live.txt`; `work/latch/aica_model_live.cpp` = the model with every `c.egreg.rXX`
read replaced by `chr(ch, 0xXX)`, linked into `build/work/eg_latch_live`, captures in `work/latch/model_live/`): the
mirror signature -- order 0 100 % live on all six runs (12/12, 8/8, 15/15, 14/14, 13/13, 13/13), order 1 live with the
straddle minority "latched" (2/10, 0/7, 1/9, 0/5, 1/12, 0/9), EARLY 3 + 2 + 4 in order 0, LATE 0.  So the checker
separates the two hypotheses in both directions with the expected straddle pattern.

## Expected console signatures and what each implies for the model

| run | register | if LATCHED (model as is) | if LIVE | model change if LIVE |
|---|---|---|---|---|
| dl | DL (r14) | order 1 all latched; order 0 mostly latched; LATE possible, EARLY 0 | order 0 all live; order 1 mostly live; EARLY possible, LATE 0 | `aeg_clock`: take the DL compare from `chr(ch, 0x14)` instead of `c.egreg.r14` (RR stays latched: tail_c) |
| krs | KRS (r14) | as above | as above | pass `chr(ch, 0x14)` (KRS field) and, by the same token, probably `chr(ch, 0x18)` to `eff_rate` in `aeg_clock` / `feg_clock`; keep the RR field latched |
| ar | AR (r10) | as above (streams 1, 2 need the right K) | as above | read `r10` live in `aeg_clock` (AR, D1R, D2R fields; a per-field split of r10 is unlikely -- if only AR is live, split it) |
| d2r | D2R (r10) | as above | as above | with ar: `r10` live |
| feg_rate | FD2R (r44), FD1R (r40) | as above | as above | `feg_clock`: `r40` / `r44` from `chr()` (remove them from `egreg`) |
| flv | FLV3 (0x38) | LATCHED would mean: order 1 all latched, order 0 mostly latched, LATE possible | model as is: order 0 all live, order 1 mostly live, EARLY possible | if latched: add the FLV targets to `egreg` (r2c..r3c) and read the target / direction from the latched copies in `feg_clock` (key_on / key_off keep the live FLV0 / FLV4 unless measured otherwise) |

Mixed outcomes are meaningful: a register that is live for one field of a word and latched for another (e.g. DL live,
RR latched in r14) would show as dl live while tail_c stays latched -- then the latch is per field (the EG state
machine reads the compare operand live but the rate lookup late), and `egreg` must be split by field.  If an AEG run
gives the live pattern but the FEG runs stay latched (or vice versa), the two generators have different register
paths.  Any "neither" or "pre-fail" on the console means the law or the harness assumptions failed for that event; look
at its `-v` window before trusting the run (the first console run of a new case may need one iteration, as aeg_koff did).

Interpretation rule for the orchestrator: a register is LIVE iff every informative order-0 event reads live (and no
LATE occurs); LATCHED iff every informative order-1 event reads latched (and no EARLY occurs).  With 16 cycles the
model gave 15-24 informative even-E events per run (>= 12 per question as required), plus 1-4 straddle proofs.

## Assumptions and pitfalls

- One KYONEX applies the key-on of the witness and (nothing) on the keyed-on test slots on the same sample -- the
  aeg_koff pin (T3 caveat) plus "KYONEX on a slot already keyed on does nothing".  A test slot restarting at a KYONEX
  would show a = 0x280 (level 496) at E and fail every reading ("neither" + old law fail@E+0): the checker would flag it.
- The witness's own reg 0x00 write carries both KYONB and KYONEX (ch_keyon does the same on the console); the test
  slots' KYONB is never touched between the cycle's key-on and its key-off.
- The event spacing (>= 2.6 ms) keeps the previous witness's residual below the wrap limit; cap_poll's 0.7 ms block
  reads only delay events, never advance them, and the monitor-triggered events are physics-timed.
- Monitor reads (MSLC + EGMON, ~5 us per poll) run without cap_poll for up to ~10 ms; the ring is 165 ms deep.
- The dl triggers depend on the AEG monitor showing a within a sample of the true value; a miss (a already past the DL
  window) is logged as "MISSED (no burst)" and skipped by the checker.
- Console time: 152640 captured samples (3.46 s) + 6 x (aica_quiet 25 ms + ring clear ~80 ms + sync) -- well under 25 s;
  the six .bin files total ~2.4 MB.

## Addendum -- `eg_latch2`: what delayed tail_c's RR rewrite (H_rate0 vs H_rekoff)

Console result of eg_latch (coordinator, `work/latch/check_hw.txt`): DL, KRS, AR, D2R, FD1R/FD2R and FLV3 are all LIVE
(order 0 and order 1 100 % live, EARLY straddles only, LATE 0), so the one-sample register latch the model took from
slot_tail tail_c is not a register latch.  Remaining candidates for tail_c (RR 0 -> 31 written in one group with reg
0x00 := 0 (KYONB 0) + KYONEX on clock 11300; the first +8 only at 11302): **H_rate0** -- a rate register going from 0
("no change") to nonzero needs one extra clock; **H_rekoff** -- the redundant key-off delivered by that KYONEX makes the
key-off clock use the previous sample's register copy.

### Files
| Path | What |
|---|---|
| `cases/eg_latch2.c` | 5 runs x 16 cycles x 3 events, same harness (witnesses, write pair, alternating order, random spins); output `eg_latch2.txt`, `<run>.hdr/.bin` |
| `tools/latch_check.cpp` (extended) | reads `eg_latch2.txt` when no `eg_latch.txt` is in the dir (`-txt` to force); `koff_before` runs: the unpinned key-off sample is searched around koff_us on stream 0 under the old law (T3 rule) and included in every simulation; "reg00 written" events are flagged |
| `tests/eg_latch2/model/`, `work/verify/s6/case_eg_latch2_model.txt`, `..._model_live.txt` | model run + checker output; the live-register control (`build/work/eg_latch2_live`, captures `work/latch/model2_live/`) |
| `build/hw/eg_latch2.elf` (0 warnings), `build/host/eg_latch2` | binaries |

### Runs (every test slot AR 31, D1R 24, DL 4 -> decay 2 holds at a = 0x80, level 130032, D2R 0; KRS 15)
| run | before the events | event j (slot j, +8 = RR 30 / +4 = D2R 28) | H_rate0 predicts | H_rekoff predicts | model (latch) |
|---|---|---|---|---|---|
| rr0 | key-off at 10 ms, RR 0: release holds at 0x80 | 12/16/20 ms (+rand 1.5): reg 0x14 RR 0 -> 30 + witness KYONEX, no reg 0x00 write | E+2 (LATCHED pattern) | E (LIVE) unless the KYONEX itself counts as a key-off (then E+2) | E+2 |
| rr24 | key-off at 10 ms, RR 24: release runs at +1 | RR 24 -> 30 (control, nonzero -> nonzero) | E | E (no reg 0x00 write) | E+2 |
| rr0_koff | as rr0 | RR 0 -> 30 with the slot's reg 0x00 rewritten (KYONB 0, SA/LPCTL kept) before the KYONEX -- the tail_c group | E+2 | E+2 | E+2 |
| rekoff24 | as rr24 | RR 24 -> 30 with the reg 0x00 write | E | E+2 | E+2 |
| d2r0 | keyed on, decay 2 holding at 0x80 (D2R 0) | 10/14/18 ms: reg 0x10 D2R 0 -> 28 (+4) + witness KYONEX, test slots' KYONB stays 1 (no key event) | E+2 | E | E+2 |

Decision table for the console: rr24 must read LIVE (eg_latch's result; else the harness differs from eg_latch).  Then
H_rate0 <=> rr0 and d2r0 read "latched" (E+2) while rekoff24 reads live; H_rekoff <=> rekoff24 and rr0_koff read
"latched" while rr0 and d2r0 read live.  Both true: all four late.  Neither: all live -- then tail_c's delay came from
something in that group not reproduced here (reg 0x00 := 0 also moved SA to 0 and cleared LPCTL; or the KYONEX
arriving on the clock sample itself).  In rr0 the witness KYONEX also delivers KYONB 0 to the released test slots
without a reg 0x00 write; if rr0 reads late but d2r0 live, the KYONEX (not the reg 0x00 write) is the key-off carrier.
Write orders: order 0 = reg 0x14, [reg 0x00], KYONEX; order 1 = [reg 0x00], KYONEX, reg 0x14; verdict rule as before
(LIVE iff order 0 100 % live and no LATE; late iff order 1 100 % latched and no EARLY).  Key-off timing margin: the
hold is reached 6.2 ms after the key-on (console timing ran ~40 % off in eg_latch's dl run), the key-off is at 10 ms.

### Commands
```sh
./run_hw.sh eg_latch2                          # ~2.6 s of capture + 5 x setup: ~5 s of console time, 1.8 MB of files
build/tools/latch_check tests/eg_latch2/hw     # (-K only if the boot changed; every rate here is a constant row)
```

### Model verdicts (`tests/eg_latch2/model`) -- the model latches every register: all five runs read E+2
```
rr0       events 48 found 48 pre-fail 0 odd-E plain 23 | even E, order0 11: live 2 latched  9 | order1 12: live 0 latched 12 | EARLY 0/0 LATE 0/2
rr24      events 48 found 48 pre-fail 0 odd-E plain 21 | even E, order0 13: live 1 latched 12 | order1 13: live 0 latched 13 | EARLY 0/0 LATE 0/1
rr0_koff  events 48 found 48 pre-fail 0 odd-E plain 15 | even E, order0 18: live 4 latched 14 | order1 13: live 0 latched 13 | EARLY 0/0 LATE 0/2
rekoff24  events 48 found 48 pre-fail 0 odd-E plain 23 | even E, order0 12: live 2 latched 10 | order1 12: live 0 latched 12 | EARLY 0/0 LATE 0/1
d2r0      events 48 found 48 pre-fail 0 odd-E plain 27 | even E, order0 11: live 2 latched  9 | order1  9: live 0 latched  9 | EARLY 0/0 LATE 0/1
```
The rr24 key-off search finds 2 candidate samples per cycle (the koff_fit +-1 ambiguity, identical sequences); with
RR 0 the key-off is invisible (hundreds of candidates, any one serves).  Live-register control
(`case_eg_latch2_model_live.txt`): order 0 100 % live on all five runs (11/11, 13/13, 18/18, 12/12, 11/11), order 1
live with the straddle minority (2/12, 1/13, 4/13, 1/12, 0/9), EARLY 2 + 1, LATE 0 -- the checker separates E from E+2
on every run.  (rr0_koff's order 1 has a third write in the group; its straddle share fluctuates, 4/13 here.)

### Model change per outcome
- H_rate0 confirmed (rr0, d2r0 late; rekoff24 live): drop `egreg`/`eg_latch`, read every rate register live, and add a
  per-slot "rate armed" rule: a segment whose effective increment source was rate 0 on the previous clock skips its
  first step when the rate becomes nonzero (equivalently: the increment used at a clock is computed from the rate the
  register held at the previous clock only when that rate was 0 -- pin the exact form with the -v windows: the model
  must reproduce tail_c 11302 and rr0/d2r0 alike).
- H_rekoff confirmed (rekoff24, rr0_koff late; rr0, d2r0 live): drop `egreg`, keep live registers, and in `step()` treat a
  KYONEX that delivers KYONB 0 to a slot already in release as a key event for the clock rule: on that clock the slot
  steps with the previous sample's register copy (one `prev_regs` per slot, refreshed every sample, used only on the
  `keyed_off` clock); check whether it also applies to a redundant key-ON (KYONB 1 on a keyed-on slot -- eg_latch's krs /
  d2r / ar events did exactly that and read live, so no: only the key-off path).
- Both live: tail_c's one-clock delay needs another candidate (SA 0 / LPCTL 0 in the same write, or the KYONEX landing on
  the clock sample); re-read `tests/slot_tail/hw` tail_c with tail_cmp's stage search before changing the model.

## Addendum 2 -- `eg_latch3`: is tail_c's late RR a fetch restart blocking the envelope step?

eg_latch2 console (coordinator): rr0 12/14 live, rr24 10/12, rr0_koff 10/10, rekoff24 11/11, d2r0 6/16 live, 0 latched,
EARLY only -- no register latch, no rate-0 arming, no redundant-key-off effect.  What tail_c's group changed and
rr0_koff did not: reg 0x00 := 0 moved SA[22:16] (0x20000 -> 0) and cleared LPCTL.  Hypothesis: a write that changes
the stream address / LPCTL / the start registers restarts the fetch path and, like a key-on sample, blocks the envelope
step on its effect sample.  The model now (coordinator's edit in progress) reads every register live and implements
exactly this as `Slot::eg_skip` for reg 0x00 writes that change bits 0x07FF (SA hi / PCMS / LPCTL / SSCTL).

### Files
| Path | What |
|---|---|
| `cases/eg_latch3.c` | 5 runs (none, sa_hi, sa_lo, lpctl, lea) x 16 cycles x 3 events; the same constant (4224 words) at 0x10000 and 0x20000, LEA 4096 so SA / LEA / LPCTL changes keep the level law valid and no loop end falls inside a 31 ms cycle; output `eg_latch3.txt`, `<run>.hdr/.bin` |
| `tools/latch_check.cpp` (extended) | reads eg_latch3.txt; parses the probe write and the three CA monitor reads per event; prints "CA a -> b -> c: RESTARTED / running" and a per-run "CA restarted n/48" |
| `tests/eg_latch3/model/`, `work/verify/s6/case_eg_latch3_model.txt` (current model, eg_skip), `case_eg_latch3_model_noskip.txt` (control: the same model with the eg_skip_pending line removed, `work/latch/aica_model_noskip.cpp`, `build/work/eg_latch3_noskip`, captures `work/latch/model3_noskip/`) | model runs + checker outputs |
| `build/hw/eg_latch3.elf` (0 warnings), `build/host/eg_latch3` | binaries |

### Design
eg_latch2 rr0 skeleton: AR 31 D1R 24 DL 4 -> decay 2 holds at a = 0x80 (level 130032), key-off at 10 ms with RR 0 (the
release holds), events at 12 / 16 / 20 ms (+ rand 1.5 ms, spin 0..45 us): the write group = reg 0x14 RR 0 -> 30 (+8),
the PROBE write, the witness's KYONB|KYONEX (order 0: RR, probe, KYONEX; order 1: probe, KYONEX, RR).  Probes:
`none` (control = rr0), `sa_hi` reg 0x00 SA[22:16] 1 -> 2 (KYONB 0, LPCTL/PCMS/SSCTL kept), `sa_lo` reg 0x04 0x0000 ->
0x0040 (32 samples into the block, LSA/LEA unchanged), `lpctl` reg 0x00 LPCTL 1 -> 0 only, `lea` reg 0x0C 4096 -> 4160.
The optional sa_hi + KYONB 1 variant is the known key-on-during-release case and is not run.  CA restart evidence: the CA
monitor (0x2814, MSLC = the slot) is read just before the group, right after it (~10 us, before the effect sample) and
250 us later (~11 samples after it): CA is 530..950 at the events and grows by ~11 when the fetch keeps running; a
restart gives CA < 40 in the third read (the checker's rule: ca2 < 40 with ca0 > 100).  The rewritten registers are
restored at 29 ms (every slot is off by then).  Live (no skip): first +8 at clock E; a blocked step: at E + 2 (the
checker's "latched" pattern).  Marks 1 / 2-4 as before; 16 x 4 = 64.

### Commands
```sh
./run_hw.sh eg_latch3                        # 5 x 22592 samples = 2.56 s of capture + 5 x setup: ~5 s of console time, 1.8 MB
build/tools/latch_check tests/eg_latch3/hw   # (every rate a constant row: K irrelevant)
```

### Model predictions
Current model (`egreg` removed, `eg_skip` on reg 0x00 changes; `case_eg_latch3_model.txt`), CA never restarted (the model
does not restart the fetch on any register write):
```
none    even E, order0 11: live 11 latched  0 | order1 10: live  8 latched 2 | EARLY 1/0 LATE 0/0 | CA restarted 0/48
sa_hi   even E, order0 14: live  3 latched 11 | order1 11: live  4 latched 7 | EARLY 3/0 LATE 0/0 | CA restarted 0/48
sa_lo   even E, order0 13: live 13 latched  0 | order1 12: live 11 latched 1 | EARLY 0/0 LATE 0/0 | CA restarted 0/48
lpctl   even E, order0 16: live  1 latched 15 | order1 12: live  0 latched 12 | EARLY 2/0 LATE 0/0 | CA restarted 0/48
lea     even E, order0 17: live 17 latched  0 | order1 13: live 12 latched 1 | EARLY 1/0 LATE 0/0 | CA restarted 0/48
```
sa_hi and lpctl read late (E + 2), none / sa_lo / lea live -- the eg_skip rule as coded (reg 0x00 bits 0x07FF only).  The
"live" minority of sa_hi/lpctl is the straddle between the probe write and the KYONEX (the skip then lands on the odd
sample E - 1 and does nothing); with a three-write group the straddle share is larger than in a two-write pair (up to
~20 %) and the order rule reads: the probe blocks the step iff the order-1 events are (almost) all late.
Live-register model with NO skip (`case_eg_latch3_model_noskip.txt`): live everywhere -- none 11/11 + 8/10, sa_hi 14/14 +
11/11, sa_lo 13/13 + 11/12, lpctl 16/16 + 9/12, lea 17/17 + 12/13 live; EARLY only, LATE 0; CA restarted 0/48 everywhere.

### Decision table for the console
- sa_hi and/or lpctl late, none / sa_lo / lea live: the reg 0x00 restart hypothesis as coded (keep `eg_skip`; if only one
  of the two is late, narrow the bit mask to SA[22:16] or LPCTL accordingly; check the CA column: a RESTARTED CA on the
  late runs ties the blocked step to the fetch restart, a running CA means the step is blocked without a CA restart).
- sa_lo or lea late as well: any start-register write (0x04 / 0x0C) restarts -- extend `eg_skip_pending` to those offsets
  (0x08 LSA presumably too).
- everything live: tail_c's delay is not a register-write effect at all; the remaining difference is that tail_c's KYONEX
  followed a write group that included reg 0x00 := 0 on THREE slots (2, 1, 0) plus RR on each -- 6 writes ~14 us before
  the KYONEX, which raises the straddle odds to ~60 %, i.e. tail_c's single observation may simply be an order-0
  straddle (register write one sample before the key event... which would make it EARLY, not late) -- re-fit tail_c with
  `tail_cmp` under a live model before adding any rule; also consider that the KYONEX itself landed on the clock sample.
- CA RESTARTED on some run while its step is live: the fetch restarts without blocking the envelope -- model the CA
  restart (`c.CA = 0`, `decode_initial`) on that register write, no eg_skip.
