# analysis_minus8 -- the -8 a stopped slot left on MIXS2 (tests/eg_lock mixs), explained

Task: explain the -8 residual in the model's terms and predict what the console does after a slot stops.
No existing file was edited, no console access.  Everything is under `work/minus8/` (tools, logs) and this report.

## Verdict in one paragraph

The -8 is the slot filter's **zero-input rest state (low L = 4, band B = -4)** at the cutoff the FEG was holding
when the slot stopped (release from FLV3 0x1A00 toward FLV4 0x1C00 at R 45, held short at **0x1BFF**: k = 511,
s = 11, Q 4), sent through the VOFF path: `out = -2L = -8` in 1/16 units = MIXS -8 at IMXL 15.  (4,-4) is one of
the 25 zero-input fixed points of the NOTES "Slot filter" recurrence at that cutoff (verified by direct iteration),
the corner of the deadband.  The model already has exactly this mechanism (`slot_output`: a stopped slot feeds 0,
the filter keeps running, VOFF exposes `-2*low`), and lands on -8 in 4.2-4.3 % of the possible trajectories; the
model's own whole-program run landed on (0,0) -> 0 because the SH4 timing of the quiet writes, hence the signal
phase at which the input stopped, differs.  Nothing in the model needs changing for this datum; the value is
trajectory-dependent, not a missing path.  Two corroborations: -8 is **impossible** at 0x1C00 (L = 4 is not a rest
state there), so the console's FEG release did hold short of FLV4 through the stop, as the FEG rule says; and the
"held last sample" / "frozen filter" alternatives predict values of order 10^4..10^5, not a tiny even number.

## Files

- `work/minus8/tail.cpp` -- RLE printer for a sample range of a CAP1 stream with the MDEC_CT of each run
  (`tail <prefix> <stream> <c0hex> <from> <to>`, from < 0 counts from the end).
- `work/minus8/rest.cpp` -- zero-input fixed points / limit cycles of the filter recurrence per cutoff and Q, basins
  over [-64,64]^2, and landing statistics for decaying inputs.  Output: `work/minus8/rest.txt`.
- `work/minus8/replay.cpp` -- ring-locked replay of feg_odd (all four slots, key-on 139, key-off 6772, K 6491)
  through `AicaModel`, sample-by-sample check of MIXS2/MIXS3 against the capture, then N more samples, then the
  `aica_quiet()` writes and a per-sample listing of slot 2; `-sweep` runs the quiet sequence at every gap in a
  range and histograms the landing (OpenMP).  Outputs: `work/minus8/replay_gap4410{,_latched}.{log,txt}`,
  `work/minus8/sweep_exact.txt`, `work/minus8/sweep_latched.txt`.

Build and run (from `caique-rtl/model`):

```sh
g++ -O2 -std=c++17 -o build/work/minus8_tail work/minus8/tail.cpp
g++ -O2 -std=c++17 -o build/work/minus8_rest work/minus8/rest.cpp
g++ -O2 -std=c++17 -fopenmp -o build/work/minus8_replay work/minus8/replay.cpp src/aica_model.cpp
./build/work/minus8_tail tests/eg_lock/hw/feg_odd 2 0374 -300 0     # (1) end of feg_odd stream 2
./build/work/minus8_tail tests/eg_lock/hw/mixs 2 bd48 0 40           # (1) start of mixs stream 2
./build/work/minus8_replay -gap 4410 -o work/minus8/replay_gap4410.txt            # (2) exact aica_quiet
./build/work/minus8_replay -gap 4410 -latched -o work/minus8/replay_gap4410_latched.txt
./build/work/minus8_replay -sweep 32768            # 14 s on 24 cores; -latched for the other variant
./build/work/minus8_rest                           # (3) 1A00 1B00 1B80 1BC0 1BFE 1BFF 1C00, Q 4, 3 s
```

## (1) Data

- **feg_odd stream 2, last 300 samples (10964..11263):** every run has length 1 (no RLE compression at all): it
  is the live filtered full-scale random signal, |values| up to 252,282 (sample 11126), last sample 11263 =
  -108,498 at MDEC_CT 0xd560 (even).  Stream 3 (unfiltered reference) is full scale to the end (+-513,008).  So at
  the capture end the slot is still playing at full level: its AEG sits at a = 0 in release with RR 0 (never
  moves), its FEG at the release hold value.
- **mixs stream 2:** -8 from sample 0 (MDEC_CT 0xba89, odd) for 7193 samples, then 43981 (0xABCD) / -8
  alternating from sample 7193 (MDEC_CT 0x9e70, even) to the end: the CPU-written bank is the one the DSP reads on
  the even-MDEC_CT samples of this capture.
- **Ring distance between the two captures:** (0xd560 - 0xba89) & 0xFFFF = **6871 samples (155.8 ms)** between
  the last feg_odd sample and the first mixs sample.  cap_stop (5 ms), cap_save (180 KB over the network),
  aica_quiet (20 ms + 128 writes), ch_zero_regs (2048 writes), dsp_clear_prog (512), the mixs setup, cap_start
  (128 KB ring clear + 12 ms + sync) and the 10 ms pre-wait all fit in it; the quiet writes land somewhere in the
  middle (order 2000..5500 samples after the feg_odd end), not resolvable to the sample.  That is why the model
  replay must sweep the gap (below) instead of replaying one timing.

## (2) Model replay (`replay_gap4410.log`, `replay_gap4410_latched.log`)

Replay check: MIXS3 (reference slot) 0 mismatches over 11125 samples; MIXS2 140 mismatches, all in 139..284 =
the console's inherited filter state at key-on (the model starts from (0,0); NOTES "Slot filter" documents the same
first-300-sample transient); **exact from sample 285 to the capture end**, last sample -108,498 = console.  State
at the capture end: AEG a 0x000 release, enabled, CA 303, FEG v **0x1BFF** release (reached at capture sample
**8409**, 1637 samples after the key-off at 6772: 511 steps at R 45 = row 1, 5 steps per 8 clocks), low 54249,
band 61771.

The quiet sequence as the model sees it: reg 0x00 := 0 clears KYONB but also sets **SA := 0 and LPCTL := 0**
(one-shot), reg 0x14 := 0x1F sets RR 31 with KRS 0 (OCT 0, FNS 0x200 -> R 63, +8 per clock).  The KYONEX is
irrelevant to slot 2: it is already in release (`step()` ignores a key-off in release), so no `keyed_off` step.
The release runs from a = 0 at +8 per clock: **a > 0x3FF on the 128th clock after the write, 255 or 256 samples
later** (t 255 in both listings; the write sample had odd MDEC_CT).  The model reads SA live, so two variants:

- **exact aica_quiet** (SA -> 0: the slot plays RAM 0.., zeros in the model's RAM): the filter input becomes 0 on
  the sample after the write.  MIXS2 decays -78574, -103866, -116380 (peak), ..., -6 (t 26), ..., 306 (t 44), ...,
  -38 (t 58-59), ..., 0 (t 67), 2, 4, **6 from t 70 on** = (low -3, band -1), a fixed point.  The slot stops at
  t 255 (AEG off, CA reads 0) with **no visible change**: the output had settled 185 samples earlier.  Landing 6.
- **latched** (only KYONB cleared, SA/LPCTL kept): the live signal continues to t 254 (-3374, 19500); the first
  zero-input sample is t 255 (35452); the tail decays 45216, 49730, 50022 (t 258), ..., -6848 (t 271), ..., 944
  (t 285), ..., -126 (t 299), ..., 16 (t 312-315), 14, 12, 10, 8, 6, 4, 2, **0 from t 323** = (0,0).  Landing 0,
  68 samples after the stop.

What the model sends after "off" with VOFF 1 and the filter on -- the path in `slot_output()` for `enabled ==
false`: `s16 = 0` (the stopped sample supplies zero; the interpolator is not evaluated); `if (!LPOFF) s16 =
clamp(lpf_step(ch, 0) * 2)` runs every sample (the filter is never frozen by the stop, only by LPOFF, and its state
is never reset); `VOFF -> V16 = s16`; `d = send_level(V16, IMXL)` -> MIXS.  So it is **the filter of zero input,
evolving**, then resting at a fixed point; neither a frozen value nor 0.  (With VOFF 0 the `AEG.off` branch forces
V16 = 0, so the residual exists only through VOFF.)  Does the model ever produce -8?  **Yes**: the gap sweeps
(`sweep_exact.txt`, `sweep_latched.txt`; 32768 consecutive gaps = two full 16384-sample signal periods at pitch
1.5, both write parities):

| variant | -8 | -6 | -4 | -2 | 0 | 2 | 4 | 6 | 8 |
|---|---|---|---|---|---|---|---|---|---|
| exact (input 0 from the write) | **1384 (4.2 %)** | 2506 (7.6) | 4844 (14.8) | 1340 (4.1) | 4484 (13.7) | 10364 (31.6) | 1354 (4.1) | 5588 (17.1) | 904 (2.8) |
| latched (input 0 from the stop) | **1412 (4.3 %)** | 2632 (8.0) | 4748 (14.5) | 1344 (4.1) | 4532 (13.8) | 10232 (31.2) | 1332 (4.1) | 5588 (17.1) | 948 (2.9) |

Final (low, band) states, exact variant: (-4,-1):904 (-3,-1):5588 (-2,-3):1354 (-1,-3):6374 (-1,0):3990 (0,0):4484
(1,-2):1340 (2,-2):4844 (3,-4):2506 **(4,-4):1384** -- ten rest states, all of them fixed points of the k 511 /
s 11 map (next section), and -8 occurs only as (4,-4).  Exact variant: 31248 stops by the AEG, 1520 (4.6 %) by
the one-shot end (LPCTL 0 with CA within 384 of LEA 8192 at the write; 384/8192 = 4.7 %); the output always
settles ~74 samples after the write, i.e. ~181 samples before the stop.  Latched: the stop is at t 254/255 and the
output settles 66.5 samples after it on average, max 80; both parities give identical histograms because the stop
always falls on a clock sample and the two parities then see the same set of signal phases.  Nothing is
unsettled within 882 samples (20 ms) in either variant.

## (3) Filter theory: zero-input rest states (`rest.txt`)

Zero-input map, Q 4 (q128 = 128, so D = 2*ceil(B/2) = B + (B & 1)): a state is fixed iff `(k*H) >> s == 0` and
`ceildiv(k*B, s) == 0` with H = -L - D, i.e. `0 <= k*H < 2^s` and `-2^s < k*B <= 0`: a deadband of B in
(-2^s/k, 0] and, per B, L in [-D(B) - (2^s-1)/k, -D(B)].  Iterating every (L,B) in [-64,64]^2 (16641 states):

| v | k | s | k/2^s | fixed points (per B the fixed L range) | cycles | (4,-4) | box basin of L = 4 | random full-scale drive cut to 0: land at L = 4 |
|---|---|---|---|---|---|---|---|---|
| 0x1A00 | 256 | 11 | 0.125 | 64: B -7,-6: L -1..6; B -5,-4: L -3..4; B -3,-2: L -5..2; B -1,0: L -7..0 | none | fixed (out -8) | 1.3 % | 2.0 % (391/20000; (4,-4)..(4,-7)) |
| 0x1B00 | 384 | 11 | 0.1875 | 36: B -5,-4: L -1..4; B -3,-2: L -3..2; B -1,0: L -5..0 | none | fixed | 1.5 % | 1.4 % (289) |
| 0x1B80 | 448 | 11 | 0.21875 | 25: B -4: L 0..4; B -3,-2: L -2..2; B -1,0: L -4..0 | none | fixed | 4.3 % | 5.6 % (1125) |
| 0x1BC0 | 480 | 11 | 0.23438 | 25 (same ranges) | none | fixed | 5.1 % | 5.8 % (1154) |
| 0x1BFE / 0x1BFF | 511 | 11 | 0.24951 | 25 (same ranges) | none | **fixed** | 4.4 % | **4.9 % (974)** |
| 0x1C00 | 256 | 10 | 0.25 | 16: B -3,-2: L -1..2; B -1,0: L -3..0 | none | **not fixed** ((4,-4) -> (3,-4)) | **0** | **0** (L = 4 unreachable) |

No limit cycles of period > 1 exist at any of these cutoffs (the period-3 cycle of NOTES lives at unity cutoff
k 512).  So **L = 4 (out -8) is a rest state at every cutoff below 0x1C00 in the list, and at none of 0x1C00 or
above** (at 0x1C00 the deadband is L in [-3, 2]).  The -8 therefore also says the FEG was < 0x1C00 at the stop:
the release did hold short of FLV4 (0x1BFF), as measured on the FEG before.

Which rest state does a decaying signal land in, at 0x1BFF Q 4 (20000 random full-scale drives of random
length, then zero; settling 72 samples on average, max 85): (-1,-3) out 2: 18.5 %; (-3,-1) out 6: 17.0 %;
(-1,0) out 2: 14.2 %; (2,-2) out -4: 13.8 %; **(0,0) out 0: 13.0 %**; (3,-4) out -6: 8.0 %; **(4,-4) out -8:
4.9 %**; (-2,-3) out 4: 3.9 %; (1,-2) out -2: 3.7 %; (-4,-1) out 8: 3.1 %.  Random large start states
(|L|,|B| < 2^17): -8 6.8 %, 0 13.4 %.  The model sweeps above (4.2-4.3 % for -8, 13.7 % for 0) agree with these
generic statistics; the console's -8 is the least likely of the ten rest states but well inside the distribution,
and "0" (the model's whole-program landing) is not privileged either.

## (4) Conclusions

1. **What the console's -8 is:** the zero-input rest state (L 4, B -4) of the slot filter at the FEG's release
   hold value 0x1BFF, Q 4, exposed by VOFF: -2L = -8.  The input to the filter stopped (either when reg 0x00 := 0
   pointed SA at zero RAM, or when the AEG release RR 31 reached "off" 128 clocks later), the filter decayed for
   ~70-80 samples into the deadband and stayed there; ch_zero_regs (IMXL := 0, ~750 samples later) froze the bus
   with that value.  Every other candidate fails: a held last sample or a frozen filter output would be a random
   value of order 10^5 (the tail data is +-250k right to the end); a stopped slot that outputs 0 would have left 0;
   the ARM reset word KOS puts at wave RAM 0 (0xeafffff8: PCM16 -8, -5377) is a coincidence -- as a played sample it
   gives MIXS -128 through VOFF (x16), and as a filter DC it would rest near L -64, out +128, and the slot's CA was
   thousands of words past address 0 anyway.
2. **What the model does instead:** the same thing.  Its stopped slot feeds 0 to a still-running filter and sends
   -2*low; it lands on one of the ten fixed points of the k 511 map depending on the signal phase at which the input
   stopped: -8 in 4.2-4.3 % of trajectories, 0 in 13.7 %, 2 in 31 %.  The whole-program model run of eg_lock landed
   on (0,0) because the host harness's SH4 timing put the quiet writes at a different phase; this is not a model
   defect and the `MIXS_bank` retention logic is right (the model retains its own last value, as the console does).
   No model change is warranted by this datum.
3. **The one alternative this capture cannot exclude:** a stopped slot feeding a constant +8 in 1/16 units (half a
   sample, x8 = 4) instead of 0 also rests at L = 4 (fixed points with x8 = 4 are (L, B) with B in [-4,0] and
   4 - L - D(B) in [0,4], e.g. (4,0)).  A no-filter VOFF slot decides it directly (below).

## What the orchestrator should look for in the slot_tail console run

- **Constant-FEG filter slots at FLV 0x1B00 Q 4, VOFF 1, RR 31 / RR 24 (KRS 15):** the stop sample is
  predictable from the ring lock: release from a = 0 at R 62 (+8 per clock) -> off on the 128th clock after the
  key-off sample (255/256 samples); at R 48 (+1 per clock) -> the 1024th clock (2047/2048 samples).  Model
  prediction after the stop: the bus keeps **changing for ~95-110 samples** (zero-input decay at k 384 / s 11;
  random-drive statistics: mean 95, max 110 samples to rest), then holds one of the 36 fixed points: an even value
  in {-8, ..., 10}, most likely 8 (15.5 %), -6 (15.2 %), 2 (25.8 % over two states), 0 (25.9 % over two states),
  4 (4.0), -2 (3.6), 6 (3.5), -4 (3.4), 10 (1.8), -8 (1.4).  Different slots / repeats should land on **different**
  small values (that is the deadband); a constant landing (always -8, or always the same L) would point to a
  constant nonzero input after the stop instead.  Refuting shapes: a large constant from the stop sample (frozen
  output or held last sample), or exactly 0 from the stop sample (filter frozen at the stop, or the whole slot
  muted).  With the capture's c0/n_first and the key-off sample, an eg_model-style replay predicts the entire tail
  sample by sample (the replay here was exact through 11,000 samples of the same kind of signal); the tail must
  match if "input 0 into a running filter" is right.
- **No-filter VOFF 1 slot (LPOFF 1):** the model predicts **exactly 0 from the stop sample onward** (`s16 = 0`
  when `enabled` is false).  A held interpolator value would show as 16x the last PCM sample; a half-sample offset
  (the alternative in conclusion 3) as +8; either would also explain -8 differently and would need a model change.
- **Re-enactment of the exact sequence:** the retained value must be one of the ten rest states at 0x1BFF Q 4
  ({-8,-6,-4,-2,0,2,4,6,8}, probabilities as in the sweep table); which one is decided by the signal phase at the
  input stop, so **keep the capture running across the quiet sequence** (do not cap_stop before aica_quiet): then
  the model prediction is sample-exact, and the tail shape tells whether SA/LPCTL are read live or latched at
  key-on: live SA (reg 0x00 := 0 -> the slot plays RAM 0.., zeros after KOS's first word) makes the input stop on
  the write sample and the output settle ~74 samples later, ~180 samples **before** the AEG stop with nothing
  happening at the stop; latched SA keeps the full-scale signal to the stop sample and gives a 67-80-sample tail
  **after** it.  To separate the two effects cleanly, either do the key-off with reg 0x14 := 0x1F only (keep
  reg 0x00), or put a known nonzero pattern in RAM 0..16K.  Note the one-shot side effect of reg 0x00 := 0
  (LPCTL 0): if CA is within 384 words of LEA at the write, the slot stops at the loop end before the AEG does
  (4.6 % of phases here).
- Side fact for S7: in the mixs capture the CPU-written bank is the one read on even-MDEC_CT samples (43981 from
  sample 7193, MDEC_CT 0x9e70); worth checking whether the bank parity is tied to MDEC_CT on the console generally.
