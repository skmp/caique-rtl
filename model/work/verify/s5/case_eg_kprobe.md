# case_eg_kprobe — K probes: pin K bit 13 (G1) and find what offsets the envelope counter against MDEC_CT (G2)

Status: case written, builds for the console (KOS) and the model; model run done; the standalone fitter reproduces
K = 6491 uniquely (mod 16384) on all 8 model probes.  The console has NOT been run (orchestrator's job, commands below).

## Files created (all under caique-rtl/model; nothing existing was edited)

- `cases/eg_kprobe.c` — the console/model case (8 probes, one capture each).
- `work/kprobe/kfit.cpp` — standalone integer AEG-law K fitter (needed: see "Why not eg_phase").
- `work/kprobe/runs.txt` — eg_phase-format runs file for the MODEL captures (c0s from `tests/eg_kprobe/model/eg_kprobe.txt`).
- `work/kprobe/kfit_model.txt` — kfit output on the model captures (`-case` mode, `-expect 6491`: all match).
- `work/kprobe/kfit_model_noslow.txt` — control without the R < 48 counter offset (K shifts to 6490 everywhere).
- Generated: `tests/eg_kprobe/model/{eg_kprobe.txt,p0..p7.hdr/.bin,console.log,status}`; binaries
  `build/hw/eg_kprobe.elf`, `build/host/eg_kprobe`, `build/work/kfit`.

## The probe

One probe = one cap.h capture of 4 MIXS buses, 4 slots (0..3, ISEL k -> MIXS k), constant 0x7FFF PCM16 at 0x10000 looped
[0,32), TL 0, VOFF 0, LPOFF 1, IMXL 15 (the eg_lock.c AEG harness).  All slots AR 31, DL 31, D2R 0, RR 31, KRS 0, OCT 0,
FNS 0x200 (s = 1, attack R 63 = instant, a = 0 on the key-on sample), D1R = 1 / 6 / 14 / 22 on slots 0..3 -> decay 1 at
effective R = 3 / 13 / 29 / 45: ticks every 2048 / 128 / 8 / 1 clocks, rows 3 / 1 / 1 / 1, +1 per tick from a = 0.
R 3's row is indexed by ((cnt-1) >> 11) & 7 = eg_cnt bits 13:11, and row 3 = {0,1,1,1,1,1,1,1} skips one tick per 8
(one per 32768 samples), so a 1.5 s key-on phase (66k samples, 16 ticks, 2 skips) pins K modulo 16384 — bit 13 included.
Streams 1..3 are redundancy: they pin K mod 2048 / 128 / 8 and the fitter intersects the four sets.

Sequence per probe: `aica_quiet()`; fill RAM; configure; `cap_start`; record the head measurement (c0, n_head, t_head);
`cap_wait_us(5000)`; `cap_mark(1)`, KYONB x4 + one KYONEX, `cap_mark(2)`; `cap_wait_us(1500000)`; `cap_mark(3)`, key-off,
`cap_mark(4)`; `cap_wait_us(20000)`; `cap_stop`; `cap_save("p<i>")`; one OUT line
`probe pN: c0 XXXX head n N t_head_us T mdec_head M samples S errors E marks V action <text>`.
Model run: 67392 samples per probe (key-on at +222, key-off at ~66373).  Console time estimate: 8 x 1.55 s of capture +
8 x 1.08 MB saves + actions ≈ 15–17 s (eg_lock did 22 s of captures + 14 MB in 25 s).

One deviation from the letter of the task, forced by the data: the constant fill is 24 words (48 samples) instead of 16.
FNS 0x200 is pitch 1.5, and the sample before LEA interpolates against the RAM word beyond the loop (the "+21" artefact
of the eg_lock odd_* runs, NOTES "Loop-end interpolation"): with 0 beyond LEA the model capture halves one sample per
loop (260080 = level of 16383) and no K fits.  With the constant continued past LEA the interpolation is constant and the
level law holds on every sample.  The loop is still [0,32).

### Probe / action list (the action is performed BEFORE the probe, after the previous save)

| probe | action before it |
|---|---|
| p0 | none (baseline) |
| p1 | none (repeatability) |
| p2 | RBP/RBL 0x2804 written 0x0000 (RBL 0, RBP 0), 2 ms, then back to the capture ring (RBL 3 at 0x1E0000) |
| p3 | TIMA, TIMB, TIMC (0x2890/4/8) each written 0x0000, 0x0700, 0x00FF, 0x0000, 500 us apart; SCIPD/MCIPD logged after |
| p4 | 0x2800 written 0x000F, 2 ms, then 0x0000 (MVOL only; bits 15/9/8 stay 0) |
| p5 | wave RAM 0x00..0x3C = 0xEAFFFFFE (every ARM vector = `b .`), 0x2C00 bit 0 cleared for 5 ms, set again; 0x2C00 readback before / released / after logged, vectors read back |
| p6 | `prog_reset(); PN = 128; prog_load()` (128 NOP steps), 10 ms, `prog_reset(); prog_load()` |
| p7 | every channel register (+00..+7C) of all 64 slots written 0xFFFF (each +00 write is itself a KYONEX with KYONB), 5 ms, then the aica_quiet sequence (+00 = 0, +14 = 0x1F, KYONEX, 20 ms, `ch_zero_regs` x64) |

Notes on the actions: p6 writes the same MPRO contents as `dsp_clear_prog` (all-zero = NOP), so it tests "load and run a
full program" only in the sense of the write sequence; p2 and p6 are known non-resetters (K was constant across the 11
eg_lock cap_starts) and act as controls for p3/p4/p5/p7.  Side effect: p4 leaves MVOL = 0 (analog output muted; not
observable in any capture), which also mutes the VOFF noise burst p7's 0xFFFF sweep would otherwise send to the DAC.
The case logs 0x2C00 at start as well.

## Why not eg_phase (checked by reading tools/eg_phase.cpp)

- (a) R 63 attack: NOT handled — `sim()` starts every stream at `a = 0x280`; the level of the key-on sample is that of
  a = 0 (520176), so it fails at the onset.  Confirmed by `work/verify/expected/eg_phase_eg_lock.txt`: odd_dec (AR 31,
  KRS 0, FNS 0x200) is `best 0/68534` on all four streams.
- (b) decay 1 at R < 48 with the -1 offset: handled (`slow_off` in `eg_increment`, the "slow_off -1" branch).
- (c) DL 31: handled (`a >= DL << 5` is equivalent to the equality for an attack-entered decay 1; slots 2/3 do reach
  0x3E0 at ~1.15 s / ~74 ms and hold in decay 2 at level 0, which both compares give).
- (d) K over 16384 for R 3: handled (`period_of(3) = 1 << 14`).
- Also blocking: the constant-input assumption (the pitch-1.5 loop-end artefact above; fixed in the case, so a
  constant-input predictor works, but (a) still rules eg_phase out).

So `work/kprobe/kfit.cpp` was written: the same rule set re-implemented from NOTES (ring-locked clock, key events on the
next sample with no step, a = 0 for an R 63 attack, R < 48 offset -1, rows 5/9/13 = {b,2b,b,b,b,2b,b,b}, DL equality
after the decay-1 step, "off" past 0x3FF, level law), K searched over the full 2^14 per stream on the key-on phase (up to
mark 3 - 400), the per-stream K sets intersected, then the key-off sample searched in [mark3-400, mark4+400] for three
key-off-sample rules (mode 0 no step, mode 1 previous segment's increment = S3/the model, mode 2 release increment).
It also computes the MDEC_CT continuity between probes from the head measurements (predicted from the SH4 clock at
44100 Hz).  Build: `g++ -O2 -std=c++17 -o build/work/kfit work/kprobe/kfit.cpp`.  About 2.2 s per probe.

## Model validation (model eg_K = 6491)

`build/work/kfit -v -expect 6491 work/kprobe/runs.txt` and `build/work/kfit -case tests/eg_kprobe/model -expect 6491`
(full output: `work/kprobe/kfit_model.txt`).  Every probe:

- stream 0 (R 3, period 16384): exactly 1 K fits the 65748-sample key-on phase: **K = 6491**
- stream 1 (R 13, period 2048): 8 K fit = {347, 2395, ..., 14683} (K mod 2048 = 347)
- stream 2 (R 29, period 128): 128 K fit (K mod 128 = 91)
- stream 3 (R 45, period 8): 2048 K fit (K mod 8 = 3)
- intersection: **K = 6491 (mod 8192: 6491, bit 13 = 0)** — unique, on all 8 probes; `expect K = 6491: all probes match`.

```
  probe       K  mod8k bit13     dK  mdec_head drift       action
  p0       6491   6491     0     +0  eade      -           none (baseline)
  p1       6491   6491     0     +0  ce0a      +0          none (repeatability)
  p2       6491   6491     0     +0  b09e      +0          RBP/RBL 0x2804 written 0x0000 (RBL 0 RBP 0) then back to the capture ring
  p3       6491   6491     0     +0  92f2      +0          TIMA/TIMB/TIMC each written 0x0000, 0x0700, 0x00FF, 0x0000
  p4       6491   6491     0     +0  75b8      +0          0x2800 written 0x000F then 0x0000 (MVOL only)
  p5       6491   6491     0     +0  5811      +0          ARM7 released from reset for 5 ms (vectors 0x00..0x3C = b .), then held again
  p6       6491   6491     0     +0  393d      +0          128-step NOP DSP program loaded, run 10 ms, unloaded
  p7       6491   6491     0     +0  169e      +0          every channel register of 64 slots written 0xFFFF (with KYONEX), then quiet sequence and zeroed
```

Controls: `-noslow` (no R < 48 offset) gives K = 6490 on every probe (`work/kprobe/kfit_model_noslow.txt`) — all four
decay rates are R < 48, so the offset only relabels K here; the console K must be compared with eg_lock att_slow's 6491
under the same -1 convention.  The first kfit run (before the RAM-fill fix) failed on every probe at +21 with the halved
sample, which is the loop-end artefact and not a fitter error.

Key-off side result (model): streams 0/1 are still audible at the key-off (a ≈ 0x0E / 0x51), streams 2/3 are silent
(a ≥ 0x3E0 in decay 2).  Mode 0 and mode 1 fit with the same key-off samples (66373 even / 66374 odd) because the
previous segment's increment is 0 at that counter for R 3 / R 13; mode 2 fits with the sample shifted by one (66374 /
66375).  This is the koff_same ambiguity of the open items: with these rates the probe cannot decide S3 on the AEG (a
decay with a nonzero increment on every clock would be needed), so the K result does not depend on the key-off rule.

## Console: commands for the orchestrator

```sh
cd /home/skmp/projects/dreamster/caique-rtl/model
./run_hw.sh eg_kprobe                         # ~15-17 s; writes tests/eg_kprobe/hw/{eg_kprobe.txt,p0..p7.hdr/.bin}
g++ -O2 -std=c++17 -o build/work/kfit work/kprobe/kfit.cpp      # if not built
build/work/kfit -v -case tests/eg_kprobe/hw | tee work/kprobe/kfit_hw.txt
```

`-case` reads `tests/eg_kprobe/hw/eg_kprobe.txt` itself (the `pN stream k: ...` program lines, the `probe pN: c0 XXXX
head n N t_head_us T ... action ...` lines).  To produce an eg_phase-format runs file instead (the console c0s):

```sh
grep -o '^probe p[0-9]*: c0 [0-9a-f]* head n [0-9]* t_head_us [0-9]*' tests/eg_kprobe/hw/eg_kprobe.txt \
 | awk '{print "tests/eg_kprobe/hw/" substr($2,1,length($2)-1), $4, $7, $9; print "31 1 31 0 31 0 0 512"; print "31 6 31 0 31 0 0 512"; print "31 14 31 0 31 0 0 512"; print "31 22 31 0 31 0 0 512"}' \
 > work/kprobe/runs_hw.txt
build/work/kfit -v work/kprobe/runs_hw.txt
```

Checks on the console output before trusting a K: every `probe` line has `errors 0`; the `.hdr` error word is 0 (cap()
refuses a capture with errors); all four onsets equal; stream 0 reports "1 K fit", streams 1..3 "8 / 128 / 2048 K fit"
(the "expected N for period P" text; "<-- unexpected count" flags anything else); the intersection line "K = ...".

## How to read the result

- **G1 (K bit 13)**: the "K = ... (mod 8192: ..., bit 13 = ...)" line of p0.  If the console has not rebooted since
  eg_lock, K mod 8192 must be 6491 (att_slow, -1 convention) and bit 13 decides 6491 vs 14683.  If it has rebooted, K
  is a new constant; the probe still pins it completely, and `-expect K` can then be used on later runs.
- **Repeatability**: p1 must equal p0 (dK +0) and the drift column must be a few samples at most (SH4 vs AICA crystal
  drift over ~1.7 s plus head-measurement latency; the model gives +0).
- **G2**: for p2..p7, dK = K(pN) - K(p0) mod 16384 (printed signed).  dK != 0 => the action before that probe reset or
  offset one of the counters.  The drift column separates the two: MDEC_CT is predicted from the previous probe's head
  measurement and the SH4 time between the two heads; a drift of thousands of samples together with a dK means MDEC_CT
  itself jumped (and the EG counter kept running: K = eg_cnt + MDEC_CT/2, so dK ≈ +drift/2 mod 16384); a
  dK with a drift of only a few samples means the EG counter was reset (MDEC_CT ran on).  A reset to 0 of a 14-bit
  counter would put K at (MDEC_CT/2 at the reset instant) — the log's `mdec_head` and `t_head_us` give the time frame.
- If K changed at p5 (ARM release), the boot-time offset is set by the BIOS releasing the ARM7; at p3 by the timers;
  at p4 by the 0x2800 write; at p7 by some channel register.  If nothing changes, all seven candidates are excluded and
  the offset is set before any of these (power-on / master reset of the two counters at different instants, or by a
  register this case does not touch: 0x28xx interrupt / 0x2D00 / MEM8MB / DAC18B / MONO were deliberately left alone).

## Open points / caveats

- The DSP NOP program (p6) is register-identical to a cleared program; a program with real instructions would be a
  stronger version of that probe (not done: the task specified NOPs).
- The key-off of this probe cannot test S3 on the AEG (see above); a dedicated case with a decay at R ≥ 48 (increment
  on every clock) keyed off onto a clock sample would.
- kfit's key-off search assumes RR 31 (release R 63, +8 per clock) after the fitted K; it is reported per stream and
  does not affect the K fit (the key-on phase ends 400 samples before mark 3).
- The MDEC_CT drift check needs the `t_head_us` field, which only this case logs; the runs.txt route carries it as the
  optional 3rd/4th fields (n_head, t_head_us).
