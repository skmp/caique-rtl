## Slot filter (integer arithmetic solved on 235 captured streams, 2026-09-23)

The production model now uses the following deterministic recurrence. Input `x`, band `B` and low `L` are
in **1/8-sample units**. Define `ceildiv(n,s) = -((-n) >> s)` with arithmetic right shifts:

```text
s = 24 - (FLV >> 9)
k = 256 + ((FLV >> 1) & 255)
if FLV >= 0x1FFE: k = 512
D = 2 * ceildiv(q128[Q] * B, 8)
B = B + ((k * (x - L - D)) >> s)
L = L + ceildiv(k * B, s)
filtered_s16 = clamp(-2 * L, -524288, 524287)
```

- **The missing operation was damping quantization to 1/4 sample:** `D = 2*ceil(q*B/2)`, before the
  cutoff multiply. Both integrator states retain 1/8 sample. The band increment floors; the low increment ceils.
  No multiplier-array approximation, stochastic rounding, or extra hidden state is needed on these captures.
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

**235/235 streams; 2,050,454/2,050,454 consecutive samples match**, including raw positive saturation rail.
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
`work/filt/nonfilter_before.sha256`. The known AEG phase differences remain.

Still unverified: e=0..10 with clean captures, filter input from fractional interpolation, VOFF=0 precision and
attenuation order, ultimate internal overflow limits. The old `filt_probe/fp_2` capture has 228 counter errors;
it must not be used as arithmetic evidence. FEG transition/KRS issues are separate. Minicast had no filter.

