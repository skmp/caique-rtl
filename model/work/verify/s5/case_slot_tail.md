# case_slot_tail — what a slot sends after its envelope reaches "off" (console case + replay tool)

Status: case and tool written, model-validated (every stream of every run FULL through the tool on the model's own
captures), console case compiles; the console has NOT been run (orchestrator runs it).

## Files created (nothing existing was edited)

- `cases/slot_tail.c` — the console/model case (3 runs: tail_a, tail_b, tail_c), 4 MIXS streams each, marks 1..8.
- `work/tail/tail_cmp.cpp` — ring-locked replay of one run through `src/aica_model.cpp` with event search + per-sample
  comparison (template tools/eg_model.cpp).
- `work/tail/tail_cmp_all.sh` — runs tail_cmp on the three runs of a `tests/slot_tail/<platform>` directory, taking each
  run's c0 from the `cap_start:` lines of its `slot_tail.txt`.
- `work/verify/s5/case_slot_tail_model_cmp.txt` — full tail_cmp output on the model's captures (this validation).
- Binaries (git-ignored): `build/host/slot_tail`, `build/hw/slot_tail.elf`, `build/work/tail_cmp`.
- Model outputs: `tests/slot_tail/model/{slot_tail.txt, tail_a/b/c.hdr/.bin, input.bin, input2.bin, console.log}`.

## Build / run commands (from `caique-rtl/model`)

```sh
./run_model.sh slot_tail                                  # model run -> tests/slot_tail/model/  (0.6 s, exit 0)
bash -c 'source /opt/toolchains/dc/kos/environ.sh && make -C hw slot_tail'     # -> build/hw/slot_tail.elf (builds clean)
g++ -O2 -std=c++17 -o build/work/tail_cmp work/tail/tail_cmp.cpp src/aica_model.cpp
build/work/tail_cmp [-v] <capture prefix> <c0 hex> <K> <kind a|b|c>
work/tail/tail_cmp_all.sh [-v] tests/slot_tail/model 6491            # all three runs; exit 0 iff every stream FULL
```

Console (orchestrator): `./run_hw.sh slot_tail` then `work/tail/tail_cmp_all.sh tests/slot_tail/hw <K>` (about 25 s).

## The case

Harness = eg_lock feg_run(): 8192-word pseudo-random PCM16 (LCG seed 4242, s = (int16)(seed >> 16), 0 -> 1) at
0x20000, looped [0, 8192); 4 slots, ISEL k -> MIXS k, IMXL 15, TL 0; slot 3 = unfiltered reference (VOFF 1, LPOFF 1,
FLV 0x1FFE, FEG rates 0, AR 31, RR 0, KRS 15: never off, its first non-zero sample is the key-on sample).  Capture
MAXV 1<<20.  Per run: aica_quiet; configure; cap_start; 5 ms; mark 1; key-on all 4 (KYONB x4 + one KYONEX); mark 2;
100 ms (tail_c: 150 ms); mark 3; key-off slots 0..2 (KYONB 0 x3 + one KYONEX); mark 4; then

- tail_a (pitch 1.0, KRS 15): slot 0 VOFF 1 LPOFF 0 Q 4 FLV all 0x1B00, FEG rates 0, AR 31, RR 31 (R 62, +8/clock,
  off 128 clocks after the key-off); slot 1 same with RR 24 (R 48, +1/clock); slot 2 VOFF 1 LPOFF 1 RR 31; 200 ms.
- tail_b: slot 0 VOFF 0 LPOFF 0 Q 4 FLV 0x1B00 RR 31; slot 1 VOFF 0 LPOFF 1 RR 31; slot 2 VOFF 1 LPOFF 0 Q 0 FLV all
  0x1FFE RR 31; 200 ms.
- tail_c (the eg_lock sequence that left -8): slots 0..2 = feg_odd program A (VOFF 1, LPOFF 0, Q 4, FLV 1800 1C00
  1800 1A00 1C00, FAR 24 FD1R 26 FD2R 28 FRR 22, KRS 0, OCT 0, FNS 0x200, AR 31) with RR 0 / 31 / 24 (R --/63/49
  releases); slot 3 also FNS 0x200 (as eg_lock's reference).  After mark 4: 100 ms; mark 5; for slots 2, 1, 0:
  reg 0x14 = 0x001F then reg 0x00 = 0 (KYONB 0, SA -> 0, LPCTL -> 0, exactly aica_quiet's writes), then KYONEX
  (0x8000 to slot 0's reg 0); mark 6; 20 ms; mark 7; reg 0x20 = 0 (IMXL 0) on slots 0..2; mark 8; LOG mixs_rd(0..3);
  50 ms; LOG mixs_rd(0..3); cap_stop; save.
- Because reg 0x00 = 0 moves SA to wave RAM 0 while slot 0 still plays, RAM 0..0x3FFF holds a SECOND known signal
  (seed 4243, saved as input2.bin) and 0x4000..0x7FFF zeros, written after an initial aica_quiet (ARM7 in reset).
  This is the one deliberate departure from eg_lock (where RAM 0 was unknown on the console and zero in the model):
  it makes the SA switch and slot 0's later "off" observable and both platforms deterministic.
- Text log slot_tail.txt: one line per stream per run with the full slot config, the cap_start lines,
  "<run>: N samples, errors E, M marks", the tail_c MIXS readbacks.

Model run (tests/slot_tail/model/slot_tail.txt):
```
tail_a  cap_start: counter word fd4a at e160 (n 693), next fd4b fd4c fd4d, c0 e415, head n 720   -> 13568 samples, errors 0, 4 marks
tail_b  cap_start: counter word fd8c at 9700 (n 627), next fd8d fd8e fd8f, c0 9973, head n 657   -> 13568 samples, errors 0, 4 marks
tail_c  cap_start: counter word fdae at 4cc0 (n 593), next fdaf fdb0 fdb1, c0 4f11, head n 595   -> 14464 samples, errors 0, 8 marks
tail_c: MIXS readback after the IMXL 0 write: 4 2 -6 335376
tail_c: MIXS readback at the end: 4 2 -6 94944
```

## The tool (work/tail/tail_cmp.cpp)

Loads the capture (filt_capture.h cap(), errors must be 0), marks from the header, input.bin / input2.bin from the
capture's directory (input2 regenerated from seed 4243 if missing).  Configures the four slots exactly as the case,
steps 16 samples, sets MDEC_CT to the onset sample's MDEC_CT ((c0 - n_first - onset) & 0xFFFF; onset = first
non-zero sample of stream 3), keys on (KYONB x4 + KYONEX: the next model sample is the onset).  Then, from snapshots,
stage by stage, maximising the summed matched prefix of the affected streams 0..2 (ties listed):

- stage 0 — the filter state each LPOFF-0 slot inherits from the previous program (never reset, NOTES "Slot filter").
  With VOFF 1 a silent slot outputs -2*low every sample, so the last pre-onset sample pins low; the band is searched
  in [-65536, 65535].  With VOFF 0 (pre-onset output is 0) low in [-16, 16] x band in [-64, 64].  Needed: in the
  model's own tail_c, slots 1/2 started at rest states (-6, +2 on the bus) inherited from tail_b; the console will
  inherit from whatever ran before too.
- stage 1 — key-off sample ko in [mark3 - 100, mark4 + 400] (kind c: compared up to mark5 - 100).
- stage 2 (kind c) — reg 0x14 write at w00 - d (d 0/1) and reg 0x00 + KYONEX at w00, w00 in [mark5 - 100, mark6 + 400],
  compared up to mark7 - 100.
- stage 3 (kind c) — IMXL 0 at w2 in [mark7 - 100, mark8 + 400], compared to the end.

Output per stream: FULL or matched prefix / total with the first mismatch (sample, +offset from onset, MDEC_CT and
parity, hw vs model, model slot state a/state/OFF/CA/FEG.v/FEG state/lpf_low/lpf_band, number of mismatching samples,
10 samples around it), the model's "off" sample (relative to onset, key-off, RR rewrite) with the first 12 distinct
model values from off, the final value (hw, model), the console's last 24 distinct values with the index where each
first appears and the same list from the model; kind c adds the retained bus values (hw | model) and, per bus, the
value before the IMXL 0 write and when it last changed.  `-v` dumps 16 samples from each first mismatch.  Exit 0 iff
4/4 streams FULL.

## Validation on the model's own captures (work/verify/s5/case_slot_tail_model_cmp.txt, 24.6 s total)

```
build/work/tail_cmp tests/slot_tail/model/tail_a e415 6491 a   -> RESULT 4/4 streams FULL, ko 4632(even)   [tie 4632/4633]
build/work/tail_cmp tests/slot_tail/model/tail_b 9973 6491 b   -> RESULT 4/4 streams FULL, ko 4633(even)   [tie 4633/4634]
build/work/tail_cmp tests/slot_tail/model/tail_c 4f11 6491 c   -> RESULT 4/4 streams FULL, ko 6849(even) w14 11265 w00 11265 w2 12059
```
Onsets 222 in every run (marks 1/2 at 219/220; mark 3 within 2-3 samples of the found key-off: the windows are ample).
tail_a/tail_b pin the key-off only to an (even, odd+1) pair: the slots sit in decay 2 with D2R 0, so the S3 step on an
even key-off sample has increment 0 and the first release step lands on the same clock either way; the "off" sample
is the same for both.  tail_c pins ko exactly (1 tie): the FEG is in decay 2 holding short of 0x1A00 with +4/+8 and
an even key-off steps with that increment (S3) — visible at once through the filter.  Stage 2 ties d 0/1 when w00 is
even (the reg 0x14 write one sample earlier lands on an odd sample and takes effect at the same clock).  Stage 3 ties
all 501 candidates: every tail is at rest long before the IMXL 0 write, so the freeze is invisible (expected).

## What the MODEL predicts (the console is compared against this)

tail_a (VOFF 1):
- slot 0 (LPOFF 0, 0x1B00, RR 31): off exactly +256 samples after the key-off (128 clocks of +8 from a = 0).  From
  the off sample the filter runs on zero input: -32802, -13414, 2810, 15894, ... a damped ring that decays to the
  rest value 0 by +94 samples after off (index 4982) and stays 0.  Final 0.
- slot 1 (LPOFF 0, RR 24 = R 48): off +2048 after the key-off; tail 141460, 150784, ... decaying to a REST OF -6
  (low = 3, a deadband rest of the 0x1B00 filter) 93 samples after off, then constant -6 to the end.  Final -6.
- slot 2 (LPOFF 1): exactly 0 from the off sample (+256) on; the sample before is -491808.  Final 0.
tail_b:
- slot 0 (VOFF 0, LPOFF 0): the level decays in 16-unit steps (…, -16, 0, -16 at 4874) and the output is exactly 0
  from the off sample (+256) on: the model mutes an off slot regardless of the filter (`V16 = 0` when AEG.off).
- slot 1 (VOFF 0, LPOFF 1): same, exactly 0 from +256.
- slot 2 (VOFF 1, LPOFF 0, Q 0, unity 0x1FFE): the undamped alternating mode (pole -1) was pumped to the H clamp by the
  random input, so from the off sample the output alternates -524288 / +524287 every sample FOREVER (8700 samples to
  the capture end, no decay).  This is a sharp test of the pole and of "a stopped slot feeds exactly zero".
tail_c:
- key-off: FEG release on all three slots at once; slot 1 (R 63) off +256 after ko, tail to a rest of +2 (low -1,
  reached +108 after off); slot 2 (R 49, row 5 {1,2,1,1,1,2,1,1}) off +1640 after ko, tail to a rest of -6 (low 3).
- slot 0 keeps playing at a = 0 (RR 0) with the FEG released toward 0x1C00; at the reg 0x14/0x00 rewrite the sample
  data switches to input2 (RAM 0) on that very sample (the SA switch is what pins w00), the AEG steps +8 per clock and
  the slot goes off +254 samples after the rewrite (it landed on a clock; +256 if it lands on an odd sample); its tail
  decays to a rest of +4 (low -2) 68 samples after off.  LPCTL 0 played no role (CA did not reach LEA first).
- IMXL 0: the buses retain 4 / 2 / -6 (readbacks "4 2 -6" both times; MIXS3 reads the live reference).  So the model
  does NOT generally "leave 0" after a slot stops: the retained value is the filter's zero-input REST STATE, which sits
  in the DC deadband and depends on the trajectory before the stop.  The eg_lock model run left 0 because slot 2 then
  played zeros from RAM 0 for its last 256 samples (model RAM is zero there) while the console played whatever its RAM
  0 held.  tail_c removes that unknown (input2 at RAM 0): if the console's streams are FULL, the -8 open item closes as
  "deadband rest, no missing mechanism"; the retained values are then predicted exactly (4, 2, -6 for the model's
  ring phase; they can differ on the console only through a different ko/rewrite phase, which the tool replays).

Rate/K dependence: tail_a and tail_b use only R 62 (row 16, every clock) and R 48 (row 4, all ones), so their replay
does not depend on K at all; tail_c uses rows 1/5/9/13 (R 45/49/53/57) and depends on K mod 8 (the FEG attack at R 49
mismatches within a few samples of the onset if K is wrong).

## How the orchestrator should read console differences

1. `./run_hw.sh slot_tail`; check `slot_tail.txt` for "errors 0" per run and 4/4/8 marks; then
   `work/tail/tail_cmp_all.sh tests/slot_tail/hw 6491`.  If tail_c stream 0..2 fail within a few samples of the onset
   while tail_a/tail_b are fine, the console rebooted (new K): `for k in 6488 6489 6490 6491 6492 6493 6494 6495; do
   build/work/tail_cmp tests/slot_tail/hw/tail_c <c0> $k c | grep RESULT; done` (K mod 8 is all tail_c can see), or
   refit K properly with eg_phase on an eg_lock att_slow capture from the same boot.
2. Stage 0 "best prefix" (not "matches") for a stream = the inherited filter state was not found: the console's slot
   entered the run with a state outside the searched ranges, or the key-on differs.  Look at "around the first
   mismatch"; if the hw pre-onset values are still changing at the onset, the inherited band is large (a slow low-cutoff
   decay from the previous program) — rerun the previous program or widen the range in stage 0.
3. A stream that is FULL up to exactly the model's "off" sample and mismatches from there: what the console does after
   the stop differs.  Read the two tail lists side by side:
   - console values frozen from the off sample on (constant, equal to the last playing output) -> the filter stops
     with the slot (contradicts NOTES "stopping sample playback does not freeze the filter");
   - console values keep ringing but converge to a different rest, or ring differently from the first sample -> the
     stopped slot feeds the filter something other than 0 (e.g. its last sample, or half a sample: -8 = low 4 would
     need x8 = 4); compare hw and model values at off+1, off+2 to solve for the input;
   - console 0 immediately on a VOFF 1 LPOFF 0 slot -> the output, not the filter input, is muted when off (as the
     model does for VOFF 0);
   - tail_b slot 0/1 non-zero after off (e.g. -16 on negative filter outputs) -> the console applies a = 0x3FF
     instead of muting;
   - tail_b slot 2 alternation decaying or stopping -> the zero-input pole is not exactly -1 or the input is not 0.
4. A stream whose off sample is 1-2 samples away from the model's (the mismatch starts at a large value, the console
   still playing or already silent): release timing.  In tail_c the key-off is pinned exactly by the FEG, so slot 1's
   off at ko+256 checks that an even key-off sample takes NO release step for the AEG (ko+254 would mean it stepped
   with the release increment); slot 0's off at rewrite+254/256 checks that a rewritten RR takes effect at the next
   clock with no key event.
5. tail_c retained values (last line "retained bus values"): if the streams are FULL the values are predicted; if the
   tails match up to w2 but the retained values differ, the retention (two banks, which bank freezes) differs; the hw
   readback lines in slot_tail.txt give the SH4's view of the same buses (the model reads the DSP-side value).
6. Ties: the (even, odd+1) key-off pair in tail_a/b is expected; d 0/1 in stage 2 when w00 is even; 501 in stage 3.

## Observability caveats

- VOFF 1 hides the AEG until "off": in tail_a/c the release itself is invisible, only the stop is.
- The reg 0x00 = 0 write also clears LPCTL; with LEA 8192 and about 384 CA steps until off the loop end is unlikely to
  intervene, and the model tracks CA exactly (both platforms read input2 from RAM 0).
- Pre-onset samples (the 222 before the key-on) are not compared by the tool except through stage 0's pin; the
  header line prints how many are non-zero per stream (inherited-state indicator).
