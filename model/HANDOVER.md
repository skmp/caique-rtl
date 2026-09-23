# caique AICA model — breakthrough handover (2026-09-23)

**The slot-filter arithmetic is now deterministic and bit-exact on 235 captured streams.** Implemented in
`src/aica_model.cpp`. See [NOTES.md](NOTES.md#slot-filter-integer-arithmetic-solved-on-235-captured-streams-2026-09-23)
for the recurrence, derivation, evidence and limits.

The missing operation was **rounding q*band up to 1/4 sample before the cutoff multiply**; band and low retain
1/8-sample units. The band increment floors and the low increment ceils. This explains the unity-cutoff
period-three cycle and resolves all 24 old step streams, including the Q0 failures. No exotic multiplier needed.

Two integration details are also fixed: filtering continues with zero input after sample playback stops, and
output saturation occurs at signed 20-bit precision after conversion to 1/16 sample. Internal states remain
unclipped at that output boundary.

## Ground rules

- All work stays under `caique-rtl/model/`: tools in `tools/`, cases in `cases/`, results in
  `tests/<case>/{hw,model}/`, scratch/derived data in `work/`. No `/tmp` or `caique-rtl/agents` files.
- **C++ with integer math or software-emulated arithmetic only. No Python, including glue. No hardware FPU.**
  The model is integer-only. `tools/filt_step.cpp` replaces the old Python capture exporter.
- Console runs only through `./run_hw.sh CASE...`; it serializes through the existing hardware runner.
  Do not run simultaneous console jobs. New capture: `./run_hw.sh filt_edges`, exit 0, no capture errors.
- Don't commit. The caique-rtl repo has no commits yet.

## Reproduce the result

Run from `caique-rtl/model`:

```sh
g++ -O2 -std=c++17 -o work/filt/filt_step tools/filt_step.cpp
./work/filt/filt_step
md5sum work/filt/step.bin work/filt/decay.bin

g++ -O2 -fopenmp -std=c++17 -o work/filt/filt_rule tools/filt_rule.cpp
./work/filt/filt_rule -selftest
./work/filt/filt_rule -span 1024 -k255 512 -u 3 -form 5 -qshift 1 \
  -rules floor,ceil,ceil -top 1 \
  0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23

g++ -O2 -std=c++17 -DVERIFY_MODEL -o work/filt/filt_validate_model \
  tools/filt_validate.cpp src/aica_model.cpp
./work/filt/filt_validate_model > work/filt/validate_model.txt
tail -n 1 work/filt/validate_model.txt
```

Expected: the old checksums `ecc12441bbdd189d665c1e92177b4542` / `6933100fc9eea543474447467f7ebd77`,
Booth selftest zero mismatches, rule test **24/24**, production validation:

```text
TOTAL qbias=255 full=235/235 consecutive samples=2050454/2050454
```

The validator searches one initial band value per stream and reads initial low from the capture. All subsequent
samples are predicted autonomously with fixed integer arithmetic. `VERIFY_MODEL` checks real `AicaModel::step()`
and raw MIXS, not just the independent recurrence. Sets: cyc 24, id2 60, coef 119, imp 16, edges 12, top 4.
All-Q data reject the c6/c7 damping-rounding alternatives that tie with ceil on Q0/Q4.

For normal model runs (which begin at reset rather than the console's inherited filter state):

```sh
./run_model.sh filt_id2 filt_coef
g++ -O2 -std=c++17 -o work/filt/filt_compare tools/filt_compare.cpp
./work/filt/filt_compare > work/filt/model_compare.txt
```

Current: id2 55/60 streams exact, 619/254980 mismatches; coef 117/119 exact, 17/503438 mismatches. All remaining
differences are startup transients within the first 300 samples; with inferred initial states, every sample matches.
Do not assume the existing "settle to zero" prelude actually resets the filter.

## Assumptions corrected

- Old C3's floored increment with **exact** q*band was only a set-valued necessary-condition fit, not proof of
  the datapath. It inferred identical low-multiply operands requiring different results. The proposed search for
  a stateless multiplier-array rule could not succeed under that assumption.
- `filt_need` now reports those operand conflicts and correctly checks low-output edges in backward pruning.
  Example: `./work/filt/filt_need -inc floor -low pm1 20` reports B'=1 needing dy=0 and dy=1.
- `pm1` actually permits ±1 on exact products; older comments incorrectly called the interval strict.
- A lack of deterministic fits with hidden precision did not prove the absence of hidden precision. The new
  recurrence supplies positive evidence for a sufficient state representation instead.
- k=512 applies at 0x1FFE/0x1FFF, **not** all mantissa-255 exponents. Fresh endpoint impulses have an unfiltered
  timing reference. Existing coefficient captures confirm the distinction down to e=11.
- MIXS is even in unsaturated filter output; positive saturation can be 524287.
- Output clipping does not clip the integrators; stopped playback does not freeze filtering.

Original documents are archived in `work/filt/{HANDOVER,NOTES}-before-breakthrough.md` for historical search
commands. `tools/filt_search*.cpp` and the archived exact-sign search remain exploratory, superseded tools.

## Remaining work

1. Clean low-cutoff (e=0..10) captures. The old `filt_probe/fp_2` has 228 counter errors.
2. Filter input from fractional interpolation; filter + VOFF=0 precision/attenuation order. The already-excluded
   `sgc_level` L5 line still differs; do not label the analog/attenuated paths verified from VOFF captures.
3. Ultimate integrator width/overflow, beyond the observed output-saturation excursion.
4. FEG transitions, KRS and overshoot; unrelated AEG clock-phase differences remain.

The 16 previously covered non-filter cases were re-run without Python. All saved outputs match their old hashes
except the already-excluded L5 filter line. Hashes are in `work/filt/nonfilter_before.sha256`.
`tools/verify.py` is legacy and was not run: use the C++ validation and comparison above. `tests/SUMMARY.txt`
distinguishes inherited-state comparisons from bit-exact seeded arithmetic validation.
