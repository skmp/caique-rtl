# caique AICA model — findings log

Everything measured on the console lives under `tests/<case>/hw/`, the model's run of the same case under
`tests/<case>/model/`. A test case (`cases/<case>.c`) is written once against `cases/aica_io.h` and built for both
(`hw/` KOS back-end, `host/` model back-end; every executable is built under `build/`, git-ignored). Run: `./run_hw.sh CASE...` (console, through shrike4 `hwrun.sh`) and
`./run_model.sh CASE...` (model), then diff the two output directories.

Model: `src/aica_model.{h,cpp}`. Baseline = minicast `libswirl/hw/aica` (SGC port, DSP interpreter). Deviations from
minicast are listed below with the test that forced them.

## Access / register map (tests/probe)

- VER = 1 (0x2800 reads 0x0010). MVOL/DAC18B/MEM8MB/MONO are write-only. RBP/RBL (0x2804) read 0. TIMA/B/C read 0 (tests/timer_probe: at every prescale, whatever was written -- write-only, so no sample-rate timestamps from them; claim D3)
  (the counters are not readable); their overflow bits do show up in SCIPD/MCIPD (0x5C0 at boot).
- G2 access cost from the SH4: ~2.4 us per 32-bit read (register or wave RAM).
- Channel register stored bits (write FFFF, read back): +00 47FF (KYONEX is an action; bits 13:11 not stored),
  +04/08/0C FFFF, +10 FFDF, +14 7FFF, +18 FFFF, +1C FFFF (LFORE reads back 1), +20 00FF, +24 FFFF, +28 FFFF,
  +2C..3C 1FFF, +40/44 1F1F, +48..7C nothing. Reserved bits in +18/+24/+28 are stored.
- DSP register files as seen by the SH4:
  - COEF bits 15:3; MADRS, MPRO 16 bits.
  - TEMP: full 24 bits R/W (+0: bits 7:0, +4: bits 23:8).
  - MEMS: +4 (bits 23:8) R/W; +0 (bits 7:0) is **not CPU-writable** but reads back what the DSP wrote.
  - MIXS: R/W 20 bits (+0: 3:0, +4: 19:4); the CPU reads a written value back as 0 after 2 ms.  But the console's
    DSP keeps seeing a CPU-written MIXS value: on all 128 TEMP slots over a 4 ms run (tests/dsp_temp T2), on
    alternate samples in tests/dsp_basic F.  Not modelled (the model clears MIXS every sample); probably a
    double-buffered input latch; no real software writes MIXS.  [claim D2]
  - EFREG 16 bits R/W. EXTS reads 0, not writable (no CD playing).
- Monitors: MSLC (0x280C bits 13:8) selects the slot. 0x2810 = LP(15) SGC state(14:13) EG(12:0). EG is **13 bits**:
  0x1FFF when the slot is off/released, 0 at full volume; otherwise the 10-bit attenuation (see Amplitude envelope).
  0x2814 = CA.  LP (bit 15) is cleared by the read.

## Slot levels (tests/sgc_level — model matches every value)

- Attenuation a (10 bits, 0.09375 dB units): TL contributes 4*TL, the AEG its 10-bit level (ALFO: unmeasured).
  Linear gain: 7-bit mantissa M = 127 - a[5:0], exponent a[9:6]:  **V = floor(sample * M / 2^(7 + a>>6))**.
  So TL steps are linear within each 6 dB octave (-1/32 per TL step), and TL=0 is 127/128, not 1.
- VOFF=1 bypasses the multiply entirely (V = sample).
- Send level to the DSP: MIXS += floor(V * 16 * s(IMXL)), s = 2^-(n>>1) * (n odd ? 3/4 : 1), n = 15 - IMXL;
  IMXL = 0 is off.  The send multiply is applied to the full-precision product (TL 16 → 260080 but IMXL 13 →
  260088 for the same nominal -6 dB).
- DISDL/DIPAN do not affect MIXS (the direct outputs are not observable digitally).
- **MIXS accumulation wraps at 20 bits** (two full-scale slots give -32; order-independent), no saturation
  (tests/sgc_mix, model identical).  minicast clamped.
- minicast used float-derived 2^(-x/16) tables and a 16-bit MIXS scale; both replaced.

## Amplitude envelope (tests/sgc_aeg, aeg_dl0; tools/aeg*.py are legacy)

Captured with a constant 0x7FFF sample; the attenuation of every sample follows from the level law, and the EG
monitor (0x2810 with MSLC) reads the same attenuation directly (bits 12:0), state in bits 14:13.

- It is the OPN (YM2612-style) envelope generator clocked every **2 samples**: a 10-bit attenuation, effective rate
  **R = 2 * rate** (KRS = 15), rate 0 = no change.
  - R < 48: a tick every 2^(11 - R/4) EG clocks, increment from row R&3 of the OPN eg_inc table (AR 1: spacings
    4096, 4096, 8192 samples).  R >= 48: every clock, rows 4.. (R 60-63 all 8).
  - Attack: `a += (~a * inc) >> 4` (= a - (a >> s) - 1 with inc 8/4/2/1 ↔ s 1/2/3/4); at 0 → decay 1.
  - Decay 1 / decay 2 / release: `a += inc`.  Decay 1 → decay 2 when a >= DL << 5, **checked after that clock's
    decay 1 step** (every clock that starts in decay 1, also without a step; not on the clock that enters decay 1):
    decay 2's rate applies from the next clock, no skipped tick.  tests/aeg_dl0: with **DL = 0** the first decay 1
    clock still takes its step (D1R 31: attenuation 0 → 8, level 32767·119/128) and then holds in decay 2; D1R 20
    and 10 have no step on that clock and never move.  The EG monitor shows the new state within the crossing
    clock (tests/sgc_aeg dec_a).  The model switched one clock later and skipped the DL = 0 step; fixed (levels
    unchanged for DL > 0: the sgc_aeg model captures are identical to the previous ones after onset alignment).
    [claims A1-A3]
  - Reaching the top (past 0x3FF) the slot goes "off": the monitor reads 0x1FFF and the output is silent.  The
    state is kept: decay 2 stays decay 2 (monitor 0x5FFF); **minicast** switches decay 2 → release instead.
- **Key-on loads a = 0x280 (-60 dB)**, not 0x3FF, and the key event takes effect on an envelope clock: the key-on
  level always lasts exactly one clock (2 samples) before the first attack step.  Model: key events are latched
  by KYONEX and applied at the next envelope clock.
- **minicast** used millisecond tables and an instant AR=31; replaced by the above.
- Model vs console (tools/aeg_cmp.py, phase-independent: level path + tick-spacing histogram): all 44 streams agree
  except where the envelope clock's phase at key-on differs (which half of an alternating 1,2 / 2,4 / 4,8 pattern
  comes first; the hardware counter phase is not observable in advance) and capture-end truncation.

## Pitch and interpolation (tests/sgc_pitch — model bit-exact on all 16 pitches)

- Phase accumulator: **14 fraction bits**; increment = (1024 + FNS) << (OCT + 4), OCT signed -8..7 (truncated for
  OCT < -4).  (**minicast**: 10 bits, loses precision for every negative octave.)
- Interpolation uses only the **top 6 bits** of the fraction and keeps **4 fraction bits**:
  s16 = 16*s0 + floor((s1 - s0) * frac6 / 4)  (1/16 sample units).  With VOFF=1, LPOFF=1 MIXS = s16 exactly.
  (**minicast**: 10-bit fraction, integer result, two separately floored products.)

## Sample formats (tests/sgc_formats — model bit-exact)

- PCM8: sample = byte << 8 (= minicast).
- ADPCM (PCMS 2, and 3 long stream, including their loop behaviour over 4096 nibbles): Yamaha delta with each term
  shifted separately: `d = step>>3 + (b0 ? step>>2) + (b1 ? step>>1) + (b2 ? step)` (**minicast** used
  (2n+1)*step >> 3, off by one).  Step table / clamps = minicast.
- **Noise (SSCTL=1)**: one global 17-bit LFSR, s[n] = s[n-12] ^ s[n-17] (x^17 + x^12 + 1), stepped once per slot
  processed (64 steps per sample, adjacent slots one step apart).  A noise slot outputs the signed byte of 8
  consecutive sequence bits (bit j = j-th newer bit) << 8, independent of pitch, not interpolated.  Verified bit
  for bit over 6000 samples x 2 slots; the power-on LFSR phase is unknown, so model and console differ only by a
  constant sequence offset.  (**minicast**: a multiplicative congruential placeholder.)

## Loops and key events (tests/sgc_loop — bit-exact; tests/sgc_keys)

- Loop end: the check is **armed once CA has reached LSA since key-on** (arming happens after that step's check);
  then CA >= LEA → CA -= (LEA - LSA), CA 16-bit.  Normal loops = minicast (CA = LSA, overshoot kept one step at a
  time); LSA == LEA plays straight through; LEA < LSA plays to LSA, then jumps forward by LSA - LEA + 1 per sample,
  and after CA wraps past 0xFFFF the armed loop triggers at LEA.  One-shot (LPCTL=0) end = minicast.
- LPSLNK: the attack stops the moment CA reaches LSA and decay 1 starts from the current level (= minicast).
- Key-on during sustain: ignored.  Key-on during release: restarts (CA resets at once, the EG loads 0x280 on the next
  envelope clock).  **Decay 2 reaching "off" stops the slot (CA reads 0)**, state stays decay 2, and a key-on is
  then ignored until a key-off.  **KYONB is never cleared by the hardware** (minicast cleared it on release).

## LFOs (tests/sgc_lfo)

- LFORE is a **held reset** (the LFO stays at state 0 while the bit is set; it reads back 1).  minicast treated it
  as a one-shot.  The noise waveforms ignore it.
- One LFO state per slot, +1 every O samples (O from LFOF as in minicast: 2..1016 measured, identical).
- Amplitude LFO: attenuation **(w & 0xFE) >> (7 - ALFOS)** (ALFOS 0 = none): 0..254 at 7, 0..127 at 6, ..., 0..3 at 1.
  w: saw = state, square = 0/255, triangle = minicast.  **Noise = the global noise LFSR byte at that slot, every
  sample** (verified against the m-sequence), not tied to the LFO clock.  (minicast: 4x the value, fake noise.)
- Pitch LFO (**minicast never applied it**): it modulates FNS before the octave shift:
  **inc = (1024 + FNS + ((w & ~1) >> (7 - PLFOS))) << (OCT + 4)** with a signed 8-bit w: saw = (int8)state,
  square = +127/-128, triangle = 0, +2 per state to 126, down to -128, back (zero-centred), noise = the LFSR byte.
  Verified at PLFOS 1/3/5/6/7 (square at 7: exactly 1024+126 and 1024-128).  Modulation at other base pitches
  (OCT != 0, FNS overflow past 0x3FF) unmeasured.

## Key rate scaling (tests/sgc_krs, tools/krs.py — model matches all 320 combinations)

- k = KRS + OCT (signed 4-bit); s = k < 0 ? 0 : 2 * min(k, 15) + FNS[9]; **R = min(63, 2 * rate + s)**; KRS = 15
  disables scaling.  rate 0 = no change.  (**minicast** adds KRS unscaled and never clamps.)
- **R = 63 attack is instant** (EG = 0 at the key-on clock); R = 62 still ramps (s = 1 steps).

## Filter envelope (FEG) (tests/feg_probe, feg_track)

- The monitor with AFSEL = 1 reads the 13-bit FEG value, state in bits 14:13 (the FEG's own state, not the AEG's).
- Key-on loads FLV0.  The value then moves **linearly** (±inc per tick, same clock and increment tables as the
  AEG at the rate's R) toward FLV1 (FAR), FLV2 (FD1R), FLV3 (FD2R, then holds); key-off heads for FLV4 (FRR).  It
  moves up or down as needed.  Rate 0 holds.  FLV changes take effect only through the envelope (no key-on → no
  new FLV0).
- **Sample-exact behaviour** (tests/feg_track; tools/feg_track.cpp recovers v >> 1 at every sample through the
  bit-exact filter with a full-scale random input -- v bit 0 is unused by the filter; tools/feg_fit.cpp fits the
  envelope clock; tools/feg_validate.cpp runs the production model: 9/9 streams, 77,862/77,862 samples)
  [claims E1-E4]:
  - the envelope clock ticks every 2 samples; key-on loads FLV0 at a clock, the first step comes on the next clock;
    key-off switches to release and steps on the same clock.
  - **KRS applies to FEG rates** exactly as to the AEG (R = 2 rate + KRS scaling; KRS 0 and 5 at OCT +3 fit only
    with it).
  - **One comparator C = (v >= target).**  A segment moves down if C holds when it starts, else up (a segment that
    starts exactly on its target moves one step down).  Attack and decay 1 step until C flips -- upward they end
    at or past the target, downward strictly below it (overshoot by up to one step: 0x1C04 +4 -> 0x1C08 with
    FLV1 0x1C05) -- and the next segment steps on the very next clock (no idle clock).  Decay 2 and release skip
    any step that would flip C and hold short of the target (0x1AFA with FLV3 0x1B00 at +8; 0x1BFF, 0x1FF8).
  - The R < 48 increment rows see the clock counter one step behind the R >= 48 rows (streams mixing both need
    an offset = 3 mod 4).  Applied to the FEG only; the AEG tests are phase-independent, so it is unmeasured there.
  - Open: in one batch slot 2 took the shared key-off one clock earlier than slots 0/1, and its counter phase
    differs from theirs by 2 (mod 4): the envelope work is probably spread over the slots within the 2-sample
    period, so a register write can land between slots.  A slot-number sweep with one FEG program would pin it.
  - The model had clamping at the target and an idle clock at each transition; both are fixed (src/aica_model.cpp
    feg_clock).

## Slot filter (integer arithmetic solved; 265 captured streams, 2026-09-23)

The production model now uses the following deterministic recurrence. Input `x`, band `B` and low `L` are
in **1/8-sample units**. Define `ceildiv(n,s) = -((-n) >> s)` with arithmetic right shifts:

```text
s = 24 - (FLV >> 9)
k = 256 + ((FLV >> 1) & 255)
if FLV >= 0x1FFE: k = 512
D = 2 * ceildiv(q128[Q] * B, 8)
H = clamp(x - L - D, -8388608, 8388607)      // signed 24 bits, before the cutoff multiply
B = B + ((k * H) >> s)
L = L + ceildiv(k * B, s)
filtered_s16 = clamp(-2 * L, -524288, 524287)
```

- **The missing operation was damping quantization to 1/4 sample:** `D = 2*ceil(q*B/2)`, before the
  cutoff multiply. Both integrator states retain 1/8 sample. The band increment floors; the low increment ceils.
  No multiplier-array approximation, stochastic rounding, or extra hidden state is needed on these captures.
- **The high-pass difference H = x - L - D saturates to signed 24 bits before the cutoff multiply** (session 3,
  tests/filt_overflow + held-out tests/filt_overflow_check, tools/filt_overflow.cpp).  Unity cutoff at Q 0 has an
  undamped alternating mode (poles 0.5 and -1), so full-scale alternating input grows the state until H saturates;
  after the burst Q is rewritten to 31 and the decay reveals the state.  The H clamp at 24 bits reproduces all 15
  streams and the 12 held-out ones (FLV 0x1FFE/1FFC/1FF8 and 0x1FFA/1FF6/1FF0, bursts 16..30000 samples), the
  production model too.  Clamping or wrapping the stored states, the damping, or the low increment at any width
  20..32 fits nothing; clamping the band increment fits only the k = 512 streams, where it is the same operation.
  States reach |band| 4.25M there.  [claim F6]
- `q128` is the previously measured table: Q' = Q + 4,
  `q128 = (16 - (Q' & 7)) << (4 - (Q' >> 3))`; q = q128/128, Q 0..31.
- The cutoff is `(256 + FLV[8:1]) * 2^(e-24)`, except **only 0x1FFE/0x1FFF use unity**.
  Bit 0 is unused. The fresh `filt_edges` console experiment distinguishes 0x1DFE from 0x1C00:
  0x1DFE retains k=511, not 512. `filt_coef` also validates this over e=11..15.
- **Output saturation is after conversion to 1/16 units**, with signed 20-bit rails. The positive rail is
  524287 (odd): the previous claim that MIXS is always even was only true away from clipping.
  The integrator states are **not** clipped to the output range. The resonant 0x1F80/Q31 `filt_id2` stream
  validates both clipping and the subsequent recovery.
- The state is not reset by key-on, key-off or LPOFF. **LPOFF freezes the filter; stopping sample playback
  does not.** A stopped slot supplies zero while its filter continues evolving, visible with VOFF=1.
  `slot_output` previously returned early for stopped slots; this was a second model bug, exposed once the
  arithmetic was exact. Low-cutoff deadbands and inherited rest states remain observable.

### Why the old search got stuck

The previous handover's C3 ("the band increment floors with exact q*B") was a necessary-condition fit with
an independently selectable rounding error every sample, not an identified datapath. It passed 22/24 streams
but inferred impossible low multipliers: at F=0x1F55 the **same** inferred B'=1 required both dy=0 and dy=1.
There are analogous conflicts at 0x1C00, 0x1800, 0x1FF0 and 0x1FFE. `filt_need` now reports these witnesses.
Its backward pruning also now checks the low-output edge, not just reachability of the next band value.
The old "operand-dependent multiplier effect" conclusion was therefore not justified.

At unity cutoff, Q4, the coarse damping is `D = B + (B & 1)`. Thus:

```text
B' = x - L - (B & 1)
L' = x - (B & 1)
```

For zero input, `(L,B) = (0,1) -> (-1,-1) -> (-1,0) -> (0,1)` explains the measured period-three
`0,-1,-1` cycle. This parity argument suggested the coarser damping product. Adding it to `filt_rule` immediately
gave **24/24 full streams with deterministic floor/ceil/ceil**, including both previously failing Q0 streams.
The existing all-Q captures distinguish damping ceil from the c6/c7 rules that tie on Q0/Q4 alone.

### Validation and reproduction (C++ only)

`tools/filt_validate.cpp` independently simulates the recurrence from known input sequences and searches only
an initial band in [-256,256], with initial low read from the capture. There are no later state corrections,
per-sample rounding choices, fitted coefficients, or omitted mismatches. `-DVERIFY_MODEL` additionally seeds
those initial states into **the real AicaModel**, supplies each input via its slot, and checks raw MIXS every sample.

| Capture set | Streams | Coverage |
|---|---:|---|
| filt_cyc | 24 | Original steps, both signs, limit cycles and Q0 failures |
| filt_id2 | 60 | Impulse + random input, six Q settings, saturation/recovery |
| filt_coef | 119 | All 32 Q settings; mantissa sweep, e=11..15 |
| filt_imp | 16 | Full-range signed impulses, 511 impulses per stream, four batches |
| filt_edges | 12 | Fresh console captures, endpoint exponents, ±1000/±7/±1 impulses |
| filt_top | 4 | Unity cutoff, inherited states, DC, looped full-range random input |
| filt_low | 18 | e = 0..11 from a KNOWN start state (unity-cutoff prelude), full-scale step, 1-4 s each; Q 0/4/31, k 256/426/511 |
| filt_wide | 12 | Resonant Q 16..31 driven by a full-scale square wave at the resonance: states to 2^21.7, 20-36 % railed |

The overflow sets (filt_overflow 15, filt_overflow_check 12 held out) are validated separately by
tools/filt_overflow.cpp (Q write inside the capture): 27/27 full, production model included.

**265/265 streams; 4,286,180/4,286,180 consecutive samples match** (production model included), including the
raw positive saturation rail.  (235 / 2,050,454 before filt_low and filt_wide were added.)
Commands and build flags are in HANDOVER.md. Results: `work/filt/validate_model.txt`.
The impulse fixture has 65536 sample addresses and the capture repeats at the 16-bit address wrap; the validator
models that observed wrap rather than assuming silence after the supplied buffer.

Normal whole-program model runs start with reset state, whereas the console inherits filter state from preceding
programs. `tools/filt_compare.cpp` compares aligned impulse-to-capture-end windows:

- filt_id2: **55/60 streams completely identical; 619/254980 samples differ**, all in the first 300 samples
  after the initial impulse, maximum difference 8 MIXS units. The old report was 72.1% different over its shorter window.
- filt_coef: **117/119 streams completely identical; 17/503438 samples differ**, all within the first 17 samples.
- With the inferred initial state, the production model matches every sample of both sets, as above.
  Do not "fix" these inherited-state differences with arithmetic hacks or claim the settling prelude resets to zero.

All 16 existing non-filter model cases were re-run without Python. Their saved outputs are byte-identical to
pre-change outputs except `sgc_level.txt`'s already-excluded L5 filter line. Baseline hashes:
`work/filt/nonfilter_before.sha256`. The known AEG phase differences remain.  After the AEG decay-1 fix (session
2) the sgc_aeg / sgc_keys model outputs differ from that baseline only in monitor timing (levels identical after
alignment); the current baseline is `work/model_outputs_2026-09-23.sha256`.

Session 2 (since the breakthrough handover; claims F1-F5 in HANDOVER.md):

- **Low cutoffs e = 0..11** (tests/filt_low) [F1]: each slot first plays zeros at 0x1FFE, which leaves it in the
  (0,-1,-1) cycle or at (0,0); at a low cutoff those states freeze, so the start of the step is known.  All 18
  streams match end to end.  At e = 0 the low output climbs by exactly 1 per sample from the first sample (the
  low update ceil of a tiny positive product), and overshoot reaches the output rail (-MIXS/2 = 262144).
- **Fractional input** (tests/filt_frac, tools/filt_frac.cpp) [F2]: the filter takes the interpolated 1/16-sample
  value as **floor(s16 / 2)**.  Three fractional pitches x three filters, with an LPOFF reference slot giving s16
  exactly (about 1400 negative odd s16 per batch): floor reproduces all 9 streams, ceil / toward zero / nearest
  fail within about 20 samples.  The model already did this.
- **VOFF = 0 with the filter on** (tests/filt_voff, tools/filt_voff.cpp) [F3]: the level multiply comes AFTER the
  filter, on its clamped 1/16 output, and the result is truncated to whole samples:
  `MIXS = (floor(clamp(-2 low) * M / 2^(7 + (a >> 6))) >> 4) * 16` (a = 4 TL + AEG + ALFO, M = 127 - (a & 63)).
  8/8 streams (TL 0..0xA3, Q 4/16/31); level before the filter, filter cut to whole samples before the level,
  and a 1/16-precision result all fail.  The model already did this.  (sgc_level L5 is not evidence either way:
  its console value needs a settled low of exactly x, the model settles one LSB below, both inside the DC deadband
  and the console state is inherited.)

- **Integrator width** [F4 as first stated was WRONG; corrected in F6/F7].  The first claim (tests/filt_wide:
  "no FLV/Q setting is unstable", Q31 resonance is the worst case, >= 23 bits) missed the undamped unity-cutoff Q0
  mode; without the H clamp that mode grows without bound (tools/filt_unity_sim.cpp).  filt_wide itself stays valid
  data (its states, 2^21.65, never reach the clamp).  With the H clamp: the captures reach |band| 4,252,670 > 2^22
  and match with unbounded integrators, so **band holds at least 24 signed bits** and **low at least 23** (3.6M).
  **tools/filt_reach.cpp** searched every setting (all 16 exponents x 256 mantissas x 32 Q) with four full-scale
  drives (dc, alternating, time-reversed impulse-response sign = the linear optimum, and an adaptive pump in phase
  with band) for 65536 samples: the largest states are |band| 4,286,071 (2^22.03, 0x1FFC Q0) and |low| 3,628,859
  (0x1FF4 Q31); nothing reaches 2^23 (work/filt/reach_clamp_all.txt).  Without the clamp the same search diverges
  (2.9e9 at 0x1FFE Q0).  So wider registers are not observable with any drive found; this is a search, not a
  proof.  The model's int32 states are exact for everything reachable in that search.  [claim F7]
- The validator (tools/filt_validate.cpp) gained the filt_low and filt_wide sets: 265 streams, 4,286,180 samples;
  other damping roundings fail (qbias 0: 4/265, 128: 117/265, 223: 154/265) [F5].
- The old `filt_probe/fp_2` capture has 228 counter errors; it must not be used as arithmetic evidence.  minicast
  had no filter.

## DSP (tests/dsp_basic — model matches hardware on every vector)

- Multiply: `ACC = floor(X * Y / 4096) + B` (X 24-bit, Y 13-bit signed; arithmetic shift, i.e. floor).
- Shifter (on the previous step's ACC): SHIFT 0 = clamp24(ACC), 1 = clamp24(2*ACC), 2 = wrap24(2*ACC),
  3 = wrap24(ACC). **minicast** computes X*Y>>10 into a 4x-scaled ACC and clamps SHIFT 0/1 to 20 bits — wrong.
- Pipeline: the SHIFTED value used by TWT/EWT/MWT/FRCL/ADRL at step s is from ACC of step s-1. B = ACC (BSEL)
  also reads ACC of step s-1. Step 127's ACC carries into step 0 of the next sample.
- COEF is indexed by step number.
- EFREG and NOFL memory writes store SHIFTED[23:8]. Float writes store PACK(SHIFTED), with the mantissa masked to
  11 bits for exponent 12 (MAME PACK; **minicast** is missing the mask).
- INPUTS: MEMS as stored (24-bit), MIXS << 4, EXTS << 8.
- Y: YSEL 0 = FRC_REG (13-bit signed), 1 = COEF, 2 = Y_REG[23:11] signed, 3 = Y_REG[15:4] **unsigned** 12-bit.
  FRCL: SHIFT 3 → SHIFTED[11:0] (unsigned), else SHIFTED[23:11]. YRL loads INPUTS. (= minicast)
- Memory read pipeline: an MRD at an odd step s delivers its word to IWT at step s+2 (it stays latched through s+3,
  until the next read lands). An MRD at an even step lands at s+3. The 16→24-bit conversion at IWT uses the NOFL
  bit of the instruction two steps before the IWT (not the MRD's own NOFL): with NOFL only on the MRD steps, IWT at
  s+2 gets the raw word and IWT at s+3 the float-decoded one.
- UNPACK: for exponent > 11 there is no hidden bit and bit 22 = sign (sign extension). **minicast/MAME** put 0
  there, which is wrong for negative words (0xFFFF → 0xFFFFFF on hardware, 0xFFF7FF in minicast).
- MWT (odd or even step) writes that step's SHIFTED, with that step's NOFL and address (tests/dsp_mem M4).
- Memory reads use the odd-step slots: an even-step MRD is served in the next odd slot (lands at s+3) and is lost if
  that odd step has its own MRD; two reads in flight (MRD at 3 and 4) both land, at 5 and 7 (tests/dsp_mem M5).
- An MRD in the same instruction as an MWT performs no read: its IWT then sees the memory read latch, which holds
  the last 16-bit word any master read — observed as the upper half of the CPU's previous 32-bit wave RAM read
  (tests/dsp_mem3).  Modelled (CPU reads update the latch); programs should never rely on it.
- Addressing (tests/dsp_mem, dsp_mem2): ADDR = MADRS[MASA] + (ADREB ? sext12(ADRS_REG) : 0) + NXADR; TABLE=0 adds
  MDEC_CT and masks to the ring (RBL 0..3 = 8K/16K/32K/64K words), TABLE=1 masks to 16 bits; byte address =
  RBP * 2048 + 2 * ADDR.  MDEC_CT decrements every sample.
- ADRS_REG is 12 bits, sign-extended when added: ADRL loads INPUTS[23:16] (SHIFT != 3; an 8-bit signed value) or
  SHIFTED[23:12] (SHIFT = 3).  A new ADRS_REG is used by the very next step.  (**minicast** adds it unsigned.)
- ACC is 26 bits: an accumulation chain wraps at 2^26 (SHIFT 0 then saturates the wrapped value) (tests/dsp_mem M7d).
- EFREG is overwritten, not accumulated (two EWTs in one sample: the later wins).
- INPUTS reads MEMS before the same step's IWT writes it; Y reads Y_REG before the same step's YRL loads it.
- NOFL for the IWT conversion is exactly the NOFL of step s-2 (only that bit matters; tests/dsp_mem2 E).
- Float format verified exhaustively: UNPACK for all 65536 words (tests/dsp_unpack), PACK for all 2^24 values
  (tests/dsp_pack, on-console comparison, CRC ef84ca79 on both platforms).  Shared code: src/dsp_float.h.
- TEMP ring (tests/dsp_temp, all 128 slots dumped): the TWT writer moves down one slot per sample (MDEC_CT) and
  rewrites every slot within 128 samples, as in the model.  The one stale slot seen once in dsp_basic
  "A ffff -4096 1" did not reproduce (a program-load transient of that run).  [claim D1]

## Open items

- AEG: the counter phase between the R < 48 and R >= 48 rows (measured on the FEG, see Filter envelope) is not
  applied to the AEG, whose captures are compared phase-independently.  (KRS, LPSLNK, key-on while not released,
  decay 2 "off" + key-on and DL = 0 are measured: see the sections above.)
- Filter: nothing open in the arithmetic (see Slot filter).  sgc_level L5 stays an inherited-state difference.
- FEG: per-slot envelope timing within the 2-sample period (see Filter envelope, claim E4); the rest is measured.
- DSP: SH4-written MIXS persists as a DSP input on the console, not modelled (see Access / register map, claim D2);
  dsp_basic keeps its "F ira 25" line excluded.

## Test-writing notes

- Load/unload DSP programs from the last step down (cases/aica_io.h prog_load): the DSP keeps running while the
  CPU rewrites MPRO, and a later step (IWT) must never outlive the earlier one it depends on (MRD, NOFL).
- Every sub-test must start from a known DSP state; a previous program's IWTs/in-flight reads contaminate MEMS.
- 32-bit wave RAM accesses must be 4-byte aligned (address error on the console; the model aborts too).
- Console output path: hw/io_kos.c writes to /pc + MODEL_ROOT (the tree the case was built in, set by hw/Makefile),
  so a copy of the tree captures into itself; a failed open/write/close makes the case exit non-zero (before
  session 3 the path was hard-coded to the main tree and save errors still reported success).
