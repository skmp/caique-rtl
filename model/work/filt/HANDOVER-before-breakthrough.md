# caique AICA model — handover (2026-09-23)

Entry point for the next agent. The job: **independently verify the slot-filter rounding findings below**, then
continue the study. The rest of the model is in good shape and is covered by `tools/verify.py`.

Findings live in [NOTES.md](NOTES.md) (the source of truth); this file says how to reproduce and check them.
Layout and workflow: [README.md](README.md).

## Ground rules

- **Everything stays under `caique-rtl/model/`**: tools in `tools/`, test cases in `cases/`, console and model
  results in `tests/<case>/{hw,model}/`, scratch and derived data in `work/`. Nothing goes in `/tmp` or `caique-rtl/agents`.
- **Console runs only through `./run_hw.sh CASE...`.** It wraps `shrike4-rtl/tools/hw/hwrun.sh`, which serializes on a flock. Never run
  two console jobs at once. A round trip takes about 2–5 s; a much longer run means the console deadlocked.
- **The model is integer-only.** The user expects log/shift-style hardware and no floats.
- **Analysis tools are C++ with exact integer math** (numerators over powers of two, `__int128`), built into
  `work/` and brute-forced with OpenMP. Python is only for thin glue (capture export). This is a user
  instruction.
- Don't commit. The caique-rtl repo has no commits yet.

## Project state

`tools/verify.py` re-runs every case on the model and compares it with the console captures in
`tests/*/hw`. It writes `tests/SUMMARY.txt` and takes about 40 s. As of this handover:

| case | status |
|---|---|
| dsp_basic, dsp_mem, dsp_mem2, dsp_mem3, dsp_unpack, dsp_pack | OK (DSP bit-exact, PACK/UNPACK exhaustive) |
| sgc_level, sgc_pitch, sgc_formats, sgc_loop, sgc_keys, sgc_mix, sgc_krs, sgc_lfo, cap_selftest | OK |
| sgc_aeg | PHASE: 39/44 streams agree; the rest differ only by the unobservable envelope-clock phase |
| filt_id2 | APPROX: 72 % of samples differ. The model still has the OLD approximate filter (`lpf_step` in `src/aica_model.cpp`) |

The only open modelling item is the filter's arithmetic. Its coefficients (f, q), structure and output precision are
already exact (NOTES.md, "Slot filter").

## Filter rounding study

### Data

- **Case `cases/filt_cyc.c`**, console results in `tests/filt_cyc/hw/fc_0..5.{hdr,bin}`.
  - Each batch configures 4 slots: VOFF=1, LPOFF=0, FLV0..4 = F, FEG rates 0, one-shot 4095 samples. The sample buffer is 256 samples of the int16 value A, then zeros.
  - Key-on happens a few ms into the capture; each capture is 6848 samples.
  - Before key-on, each slot's filter sits in whatever rest state the SAME slot reached in the previous batch.
- **`tools/filt_step.py`** exports all 24 streams to `work/filt/step.bin`: per stream int32 `F, Q, A, on, N, y[N]`.
  - **y = −MIXS/2**, in 1/8-sample units (MIXS is always even with VOFF=1). This is the filter's `low`; the hardware output is −low.
  - `on` is the first output change after key-on: 139 for batch 0 (streams 0–3), 138 for batches 1–4, 200 for batch 5.
    Batch 0's slots rest in limit cycles, so its onset is read from stream 2; it is one sample later than the other batches.
  - The input is x = 8·A for samples on … on+255, then 0 to the end of the capture. The tools also try on±1.
  - It also writes `work/filt/decay.bin`, the older DC-to-zero windows used by `filt_search6..8`.
- Expected checksums with the current captures:
  - `md5 work/filt/step.bin = ecc12441bbdd189d665c1e92177b4542`
  - `md5 work/filt/decay.bin = 6933100fc9eea543474447467f7ebd77`

Stream index (e = F>>9, k = 256 + F[8:1], q = q128[Q]/128, q128[4] = 128, q128[0] = 192):

| # | F | Q | A | e | k | | # | F | Q | A | e | k |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 1FFE | 4 | 1000 | 15 | 511* | | 12 | 1C00 | 4 | 1000 | 14 | 256 |
| 1 | 1FFE | 4 | −1000 | 15 | 511* | | 13 | 1C00 | 4 | −1000 | 14 | 256 |
| 2 | 1FFE | 4 | 7 | 15 | 511* | | 14 | 1C00 | 0 | 1000 | 14 | 256 |
| 3 | 1FFE | 0 | 1000 | 15 | 511* | | 15 | 1C00 | 0 | −1000 | 14 | 256 |
| 4 | 1FF0 | 4 | 1000 | 15 | 504 | | 16 | 1A00 | 4 | 1000 | 13 | 256 |
| 5 | 1FF0 | 4 | −1000 | 15 | 504 | | 17 | 1A00 | 4 | −1000 | 13 | 256 |
| 6 | 1FF0 | 0 | 1000 | 15 | 504 | | 18 | 1800 | 4 | 1000 | 12 | 256 |
| 7 | 1FFC | 4 | 1000 | 15 | 510 | | 19 | 1800 | 4 | −1000 | 12 | 256 |
| 8 | 1E00 | 4 | 1000 | 15 | 256 | | 20 | 1F55 | 4 | 1000 | 15 | 426 |
| 9 | 1E00 | 4 | −1000 | 15 | 256 | | 21 | 1F55 | 4 | −1000 | 15 | 426 |
| 10 | 1E00 | 0 | 1000 | 15 | 256 | | 22 | 1D55 | 4 | 1000 | 14 | 426 |
| 11 | 1E00 | 0 | −1000 | 15 | 256 | | 23 | 1D55 | 4 | −1000 | 14 | 426 |

\* The console behaves as f = 1.0 (k = 512) here. Pass `-k255 512` (see C2).

### Model family and units

This is the Chamberlin SVF: `band' = band + f(x − low) − f·q·band`, `low' = low + f·band'`, and the output is `low`.

- **Units:** low is L = y (1/8 sample, no hidden bits). The band is b in 2^−u sample; u=3 means the same unit as low. f = k·2^(e−24).
- **Exact products** are integers over powers of two:
  - P1 = f(x−L), in band units: `k(x−L) / 2^s1`, with s1 = 27 − e − u
  - P2 = f·q·b, in band units: `k·qm·b / 2^s2`, with s2 = 31 − e
  - P3 = f·b', in 1/8 units: `k·b' / 2^s3`, with s3 = 21 − e + u
  - At e=15, u=3, k=256 this gives f = 1/2 with s1 = 9, s2 = 16, s3 = 9.
- **Forms** (`-form`):
  - 0: f·x and f·L rounded separately.
  - 1: f(x−L) and f·q·b rounded separately.
  - 2: **one product**, `b' = b + R(k·T / 2^s2)` with `T = (x−L)·2^(s2−s1) − qm·b` (q·b exact).
  - In every form the low update is `L' = L + R3(P3)`.

### Tools (build: `g++ -O2 -fopenmp -std=c++17 -o work/filt/<t> tools/<t>.cpp`, run from `caique-rtl/model`)

- **`tools/filt_rule.cpp`** is an exact test of rounding rules. `-rules r1,r2,r3` tests one combination; `-set a,b,..` tests every triple from the set.
  - The rules are r1 (P1, or the single product in form 2), r2 (P2; unused in form 2) and r3 (P3).
  - L is known at every sample, so the tool tracks the SET of integer band values consistent with the capture. The start set is L = y[on−1] and band ∈ ±SPAN·2^(u−3), with SPAN = 64 (`-span`).
  - **Deterministic rules:** `floor ceil tz away he` (half-even), `c<n>` = floor(v + n/8), `t<n>`, `ceilm1` = ceil(v) − 1.
  - **Booth rules:** `bV<g>`/`hV<g>`/`bC<g>`/`hC<g>` are radix-4 Booth arrays truncated g columns below the product's shift. V means the data operand is recoded, C the coefficient; b/h means floor or half-up at the end. `aVtt`/`AVtt`/`aCtt`/`ACtt` truncate at the absolute column tt.
  - **Rule FAMILIES** (set-valued): `fl1` [v−1, v], `ce1` [v, v+1], `tzb`, `awb`, `pm1` (any integer strictly within 1 of v), `near` (within ½).
  - **A family passes if SOME per-sample choice reproduces the capture.** It is a necessary-condition test, not a rule.
  - **Output format:** `full a/b` is the number of streams reproduced from `on` to the end. `i:m` gives, for stream i, the samples reproduced from `on` (maximised over on±1); a full stream is 6709 for #0–3, 6710 for #4–19 and 6648 for #20–23.
  - **Options:** `-hl n` adds hidden low bits (`-ro` output read, `-lop` operand cut); `-k255 n`; `-selftest` checks that the Booth model without truncation equals floor.
- **`tools/filt_need.cpp`** reads the rounding off the data.
  - It does forward/backward set tracking with `-inc floor|ceil|pm1` on the form-2 product and `-low floor|ceil|pm1|fl1` on P3.
  - Where the band is pinned, it tabulates the result P3 must have had (result − floor, by fraction and sign), plus feature purities for exact P3 products.
  - `-v` / `-vm m r` / `-vexact` list individual samples (operands and numerators). `-dn`, `-span`, `-k255` are as in filt_rule.
- **`tools/filt_sym.cpp`** checks sign symmetry of the ± stream pairs.
- **`tools/filt_lp.py`** (Python, exact rationals, slow) measures how far unrounded arithmetic can explain a capture: it clips a polygon of start states.

Older search tools are superseded; see "Superseded / pitfalls" below.

### Claims to verify (commands and the output they gave at handover)

`ALL="0 1 2 … 23"`, `R=./work/filt/filt_rule`.

**C0 — tools sane.** `$R -selftest` gives `selftest: 0 mismatches`.

**C1 — no deterministic value rule fits with separate products** (forms 0 and 1).
- Command: `$R -span 256 -k255 512 -u 3 -form 0|1 -set floor,ceil,tz,away,he,ceilm1,c1..c7,t1..t7 -top 2 $ALL`
- Result: at most **5/24** streams: #2 (A=7, constant output) and the four 0x1E00 streams #8–11. At f = 1/2 almost anything floor-like works; that is why the old model was exact only at 0x1E00.
- Booth variants in form 1 (relative guards 0–8, absolute columns 0–12, V or C recoded, floor or half-up) are also at most 5/24.

**C2 — band unit and 0x1FFE.**
- Command: `$R -u U -form 1 -rules pm1,pm1,pm1 -top 1 $ALL`, which allows any one-LSB rounding per product. Full streams by U:

  | U | full streams |
  |---|---|
  | 1 (½ sample) | 15/24 |
  | 2 | 21/24 |
  | 3 | 21/24 |
  | 4 | 18/24 (#6 fails at 1, #20 at 266, #21 at 10) |
  | 5 | 16/24 |
  | 6 | 15/24 |
  | scaled (2^−(18−e)) | 21/24 |

- The three misses at U = 2/3/scaled are the 0x1FFE streams #0, #1, #3, which fail at the first input sample with k = 511. With `-k255 512` all four 0x1FFE streams pass (`$R -u 3 -form 1 -k255 512 -rules pm1,pm1,pm1 0 1 2 3` gives `4/4`). **FLV[8:1] = 0xFF means f = 1.0.**
- So the band unit is **1/8 (or 1/4) sample**. Finer units are refuted by #6, #20 and #21, and ½ sample by the e=15 streams. Lower cutoffs cannot discriminate at this level.
- Hidden low bits (`-hl 1..4`, either output read, with or without operand cut) make every deterministic rule fit worse.

**C3 — the band update is ONE product, rounded down.**
- The structure itself: `$R -k255 512 -u 3 -form 2 -rules pm1,floor,pm1 -top 1 $ALL` gives **`full 24/24`**, with any one-LSB rounding of the single product and of the low update. (r2 is unused in form 2.)
- Floored single product: `$R -k255 512 -u 3 -form 2 -rules floor,floor,pm1 -top 1 $ALL` gives **`full 22/24`**. Only #3 and #6 (e=15 **Q0**) fail, at 6–7 samples. This covers k = 256, 504, 510, 426 and 512, e = 12..15, both signs, and 0x1C00/0x1E00 Q0. The result is the same at `-span 64` and `-span 1024`.
- Comparisons, all with `-k255 512`: `pm1,floor,floor` gives 18/24; `ceil,floor,pm1` gives 15/24; `near,near,near` gives 13/24 in form 2 and 8/24 in form 1.

**C4 — no deterministic low-update rule yet.**
- form 2 with every deterministic triple from C1's set: best **1/24**. Booth rules (relative and absolute, both recodings): best **2/24**.
- The nearest miss is `ceil,floor,fl1`, which gives **13/24** with `-k255 512`: #0–2, #8–13 and #16–19. That is floor for P3, with one lower allowed when P3 is an exact integer.
  - `ceil,floor,ceilm1` gives 0/24, so "always one lower" is wrong.
  - `ceil,floor,floor` and `floor,floor,ceil` both give 1/24, with identical per-stream results. Which of the two products carries the round-up is NOT identifiable from these data: an offset of the band can trade one for the other.

**C5 — what the low update must do** (band update floored, deterministic):
`./work/filt/filt_need -u 3 -inc floor -low pm1 <stream>`. The band is pinned at about 6640–6708 of 6710 samples.
- **k = 256** (#12, #14, #16, #18): the result is floor(v) + 1 when frac(v) ≥ 2 LSB of P3's own grid, floor(v) when v is exact, and **either** at exactly 1 LSB.
  - The grid is 1/8 at f=1/8, 1/16 at f=1/16, and so on.
  - Example, #16: frac 256/2048 gives +0 ×12 and +1 ×9; frac ≥ 512 always gives +1; frac 0 always gives +0.
- **k ≠ 256** (#4, #7, #20, #22): the result is **not** a function of the fraction. For example, in #20 (k=426): frac 426/512 → +0 (6381 samples, the rest state); 188 → +1; 332 → +0; 424 → +1. It depends on the operand, so this is where a multiplier-array model must come from.
- With `-inc ceil -low fl1`, the exact cases can be listed with `-vexact`. The feature table shows no single feature deciding v vs v−1: signs, band/low/increment residues, exactness of the increment. Each explains at most about 22 of 26 cases.

**C6 — sign symmetry is strong but NOT exact.** `./work/filt/filt_sym` compares the ± pairs over the whole step response. "N" is y(−A) = ~y(A), "Z" is y(−A) = −y(A).

| F | Q | N | Z | other |
|---|---|---|---|---|
| 1E00 | 4 | 6710 | 0 | 0 |
| 1C00 | 4 | 6710 | 0 | 0 |
| 1FF0 | 4 | 6707 | 3 | 0 |
| 1A00 | 4 | 6666 | 3 | 41 |
| 1800 | 4 | 6524 | 54 | 132 |
| 1F55 | 4 | 6644 | 0 | 4 |
| 1D55 | 4 | 6624 | 2 | 22 |
| 1E00 | 0 | 6700 | 10 | 0 |
| 1C00 | 0 | 23 | 6662 | 25 |
| 1FFE | 4 | 0 | 2237 | 4472 (different limit-cycle phases) |

The ± runs start from different inherited rest states, so exceptions are expected. **Don't treat NOT-symmetry as a law.**

**C7 — exact (unrounded) arithmetic is not enough.** `tools/filt_lp.py 8 12 14 16 18` (about 17 s):

| stream | samples explained | slack needed for all 456 |
|---|---|---|
| #8 | 24 | 7/16 |
| #12 | 19 | 39/16 |
| #14 | 6 | 23/16 |
| #16 | 19 | 17/8 |
| #18 | 17 | 33/8 |

Even the best start state reproduces only about 20 samples, and the whole response needs 0.5–4 LSB of slack. Nominal f and q fit best: a k/q scan on #18 found nothing better.

**C8 — 0x1FFE is f = 1.0.** On stream #0 the output goes from −1 to 8000 on the first input sample (x = 8000).
- With k = 511 the first output would be floor(floor(511/512·8000)·511/512) = floor(7984·511/512) = 7968.
- With k = 512 it is exactly 8000.
- The DC then holds at 7999/8000 in a period-3 pattern, and the zero-input tail cycles (0, −1, −1). These are rounding effects to be reproduced by the final model.

### Open questions (in priority order)

1. **The low-update rounding** given the floored single-product band update (C3, C5).
   - For k = 256 it is a threshold with a 1-LSB ambiguity; for k ≠ 256 it depends on the operand.
   - Next: list the pinned samples with `filt_need -v`, i.e. the operand b', k and the exact numerator. Fit a multiplier model for `k × b'`: which operand is recoded, the truncation column, rounding constants, hot ones. Or find the extra state that decides the ambiguous cases.
   - The v vs v−1 ambiguity on exact products in the `ceil,floor,fl1` view is the same phenomenon seen from the other side of the band offset.
2. **e=15 Q0** (#3, #6) fail after 6–7 samples even with the band update floored. Q0 means q = 1.5, so q·band has a ½ fraction and may be rounded before the multiply. Test a rounded q·b inside T, or `-inc pm1`: `$R -u 3 -form 2 -rules pm1,floor,pm1 6` passes.
3. **The f = 1.0 rule for FLV[8:1] = 0xFF**: is it only at e = 15, or at every exponent? Is it really k = 512, or a different datapath, e.g. a complement multiply? Other exponents are untested.
4. **Band unit ¼ vs ⅛** at e = 15 is not separated by the ±1 test (C2). The deterministic evidence (C3) uses ⅛.
5. Then implement the result in `src/aica_model.cpp` (`lpf_step`). Re-run `tools/verify.py` and `FILT_CASE=filt_id2 tools/filt_cmp.py`, and extend the verification to `tests/filt_id2` / `filt_imp` / `filt_top` (other Q, low exponents, VOFF=0).

Useful console experiments, if needed (via `run_hw.sh` only): a small-amplitude step at several k values at e=15, and a Q sweep at k = 256 to isolate the q·band rounding.

### Superseded / pitfalls

- `tools/filt_search.cpp` … `filt_search12.cpp`: earlier hypothesis searches that rank *partial* fits.
  - `filt_search9–12` simulate REST samples before `on`. Those rest states are inherited limit cycles (period 3 at 0x1FFx Q4, period 6 at 0x1FFE Q0) that no model reproduces, so they reject everything at e = 15.
  - Their "exponent-scaled band (unit 2^−(18−e))" result was an artefact. C2 shows the lower cutoffs can't discriminate the band unit.
  - Keep them for the record; use `filt_rule` / `filt_need` instead.
- **Never relax integer state to real intervals.** The removed `filt_bound.py` let the damping product take non-integer values and falsely "passed" a fixed ⅛ band with floor-type errors.
- **Every rule result depends on the start set and `on`.** The tools take on±1 and a band span; widen `-span` before concluding that a rule fails. The C3 result is the same at span 64 and 1024.
- **The old `decay.bin` was aligned per dataset by drop detection** (off by one for some streams). `filt_step.py` now writes it with a fixed offset (on + 150); the decay searches detect the drop themselves.
- **Fixed during this session:**
  - `filt_need` initially ignored `-inc` because form-2 floor took precedence. Keep an eye on option precedence when extending it.
  - Batch 0's `on` was 138 (one early). Every earlier result used on±1 so none changed, but `filt_need` has no ±1 search. Use `-dn` if you export new captures with an uncertain onset.
