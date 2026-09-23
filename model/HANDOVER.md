# caique AICA model — swarm verification handover (2026-09-23, sessions 2 and 3)

**Scope: verify only the progress made since the breakthrough handover.** That handover (coarse damping, 235
streams) is commit `a07a62d` ("initial slop"); read it with `git show a07a62d:model/HANDOVER.md`.
- **Session 2** is commit `2e19634`: `git diff a07a62d 2e19634` (claims L1, F1–F5, E1–E4, A1–A3, D1–D3).
- **Session 3** (after the swarm's first findings) is uncommitted: `git diff HEAD` plus the untracked files
  (claims L2, F6, F7, and F4 withdrawn).

The swarm found two real problems in session 2: console captures from a private copy were not isolated (fixed:
L2), and the integrator-width claim F4 was wrong (withdrawn; replaced by F6/F7). **General re-verification of earlier results
(DSP, levels, pitch, the breakthrough recurrence itself, ...) is out of scope.**

Findings live in [NOTES.md](NOTES.md). This file gives each new claim an ID, a check, the expected output, and
the controls that must fail.

## What changed since the previous handover

| Area | Change | Claims |
|---|---|---|
| Build layout | every executable/object under `model/build/` (git-ignored); `tools/Makefile`; hw/host Makefiles and run scripts moved; 22 committed tool binaries removed from `work/filt/` | L1 |
| Filter | low cutoffs e=0..11, fractional input = floor(s16/2), VOFF=0 = level after the filter; validator extended (265 streams); integrator width (F4, **withdrawn**) | F1–F5 |
| Filter (session 3) | **24-bit saturation of x − low − damping before the cutoff multiply** (model + validator); worst-case state search; unity-cutoff Q0 growth prediction | F6, F7 |
| Console back-end (session 3) | captures go to the tree the case was built in (`MODEL_ROOT`); save errors fail the run | L2 |
| FEG | sample-exact trajectories recovered through the filter; rules fitted; **model rewritten** (`feg_clock`) | E1–E4 |
| AEG | DL=0: decay 1 steps before the DL check; **model fixed** (`aeg_clock`) | A1–A3 |
| DSP / registers | TEMP ring coverage; SH4-written MIXS persists on the console (not modelled); timers write-only | D1–D3 |

New console cases (each with `tests/<case>/{hw,model}/`): `filt_low`, `filt_frac`, `filt_voff`, `filt_wide`,
`feg_track`, `aeg_dl0`, `dsp_temp`, `timer_probe`. Session 3: `filt_overflow`, `filt_overflow_check` (captured by the swarm's
research copy, merged into the tracked tree). New tools: `cap_cmp`, `filt_frac`, `filt_voff`,
`filt_wide`, `feg_track`, `feg_fit`, `feg_validate`, `tools/Makefile`. Session 3: `filt_overflow` (+ `filt_overflow_model`), `filt_reach`,
`filt_unity_sim`; `work/filt/overflow_trace.cpp` and the `work/filt/overflow_*.txt` / `reach_*.txt` results. Scratch programs: `work/filt/lowrange.cpp`,
`work/aeg_dl0_check.cpp`, `work/feg/{dump,steps}.cpp`. Model: `src/aica_model.{h,cpp}` (session 2: `git diff a07a62d 2e19634 -- src`; session 3: `git diff HEAD -- src`).

## Rules for swarm agents

1. **Main tree is read-only**, apart from your report (rule 6). Do not edit `src/`, `cases/`, `tools/`, `tests/` or the docs. Do not commit.
2. **Work in a private copy.** The tools write fixed relative paths: `feg_track` → `work/feg/`, `filt_step` →
   `work/filt/`, `run_model.sh` → `tests/<case>/model/`, `run_hw.sh` → `tests/<case>/hw/`. So run everything in
   your own copy:
   ```sh
   cd caique-rtl/model
   mkdir -p build/swarm/$ID && rsync -a --exclude build ./ build/swarm/$ID/ && cd build/swarm/$ID   # ~150 MB, git-ignored
   ```
   Since L2 a copy also captures into itself: `run_hw.sh` builds the case inside the copy, and the ELF embeds that
   tree's path.  Before L2 (session 2) every capture went to the main tree whatever the copy.
3. **Console: only the agent assigned group H.** It runs `./run_hw.sh` inside its private copy, never in the main
   tree, which holds the evidence. Everyone else stays off the console. `hwrun.sh` serializes users, so a second
   user would only stall group H's sequence.
4. **Everything you write must end up trackable by git** (user rule). `build/` holds build products only, and anything
   under it is ignored. New cases, tools, captures and results made in a private copy are merged into the main tree's
   `cases/`, `tools/`, `tests/<case>/` and `work/`, and your report lists them. Session 3 merged the research copy
   `build/next_breakthrough/` this way.
5. **C++ with integer math only** for any new check (awk, `cmp` and `sha256sum` are fine). The `tools/*.py` scripts are legacy; don't use them.
   Keep all files under `caique-rtl/model/` (your copy counts); nothing in `/tmp`.
6. **Report** in the main tree at `model/work/verify/<ID>.md`: one section per claim with **CONFIRMED / REFUTED /
   INCONCLUSIVE**, the commands you ran, the key output and anything suspicious. Work independently; don't read
   other agents' reports before finishing yours. Independent re-derivations (your own code from the NOTES formulas)
   are worth more than re-running our tools.

## Setup (every agent, inside the private copy)

```sh
make -C tools -j8        # 32 tools -> build/tools/
make -C host -j8         # model case binaries -> build/host/
mkdir -p build/work
g++ -O2 -std=c++17 -o build/work/lowrange work/filt/lowrange.cpp
g++ -O2 -std=c++17 -o build/work/aeg_dl0_check work/aeg_dl0_check.cpp
build/tools/filt_validate_model | tail -n 1      # sanity: TOTAL qbias=255 full=265/265 consecutive samples=4286180/4286180
```

Group H also needs the KOS environment (`source /opt/toolchains/dc/kos/environ.sh`; `./run_hw.sh` does it).
Expected outputs of every check are in `work/verify/expected/`: diff your output against the named file.

## Claims

### L — build layout (no console)

**L1.** Every executable and object file builds under `model/build/` (`hw/`, `host/`, `tools/`, `work/`), which
`caique-rtl/.gitignore` ignores, and none exists anywhere else.
- Check in the main tree:
  - `find . -path ./build -prune -o -type f -print | xargs file | grep ELF` → empty.
  - `git status --short --ignored` → `model/build/` is ignored.
  - From clean (`rm -rf build/{hw,host,tools}` in your copy): `make -C tools`, `make -C host`, and `make -C hw` with KOS give 32, 39 and 39 files (session 2 alone: 28, 37, 37).
  - `run_hw.sh` and `run_model.sh` use `build/hw/<case>.elf` and `build/host/<case>`.
- The 22 tool binaries formerly committed under `work/filt/` show as deleted, which is intended. Each has its source in `tools/` or `work/filt/*.cpp`.

**L2 — capture isolation (session 3; found by the swarm).** `hw/io_kos.c` used to hard-code the main tree's
`tests/<case>/hw/` path, and it returned success when a save failed.
- Now `hw/Makefile` passes `-DMODEL_ROOT=<tree the case is built in>`, so each copy captures into itself.
- A failed open, write or close now makes the case exit non-zero.
- Check: `git diff HEAD -- hw/`.
- Console test (group H only): in a private copy, run `./run_hw.sh timer_probe`. The files land in the copy's `tests/timer_probe/hw/`, and the main tree's copy is untouched.
- Review: `io_write_file` checks `fopen`, the `fwrite` count and `fclose`, and `main` returns the error.
  `run_hw.sh` creates `tests/<case>/hw/` just before the upload, so a failure needs a genuinely broken path, for example a
  hand-built ELF with a bogus `-DMODEL_ROOT`; `status` must then be non-zero.

### F — filter extensions (no console; evidence in tests/filt_*/hw)

**F1 — low cutoffs.** The recurrence holds at e = 0..11, including k 426/511 and Q 0/4/31. Each slot starts from a
known small state: a zero-input prelude at 0x1FFE leaves it in the (0,−1,−1) cycle or at (0,0), and those states freeze at a low cutoff.
- Check: the 18 `low` lines of `build/tools/filt_validate_model` → all full (`work/verify/expected/filt_validate_model.txt`).
  Also `build/work/lowrange` (`expected/lowrange.txt`): at e=0 low climbs by exactly 1 per sample at first
  (1, 1001, 10001 at +0/+1000/+10000), which is the ceil of a tiny positive low update. 262144 means the output is on the rail.
- Independent: simulate the four candidate start states at the batch's cutoff with zero input and confirm they freeze.
  Re-derive the e=0 slope from the recurrence.
- Case: `cases/filt_low.c` (prelude, key-off/on, full-scale step, 1–4 s captures).

**F2 — fractional input.** The filter takes the interpolated 1/16-sample value as **floor(s16 / 2)**.
- Check: `build/tools/filt_frac` → `full streams per conversion (of 9): floor 9 ceil 0 toward0 0 half-up 0
  half-even 0 away 0` (`expected/filt_frac.txt`). Each batch has about 1400 negative odd s16 values, which is what separates floor from toward-zero.
- Control: the reference stream is s16 itself (LPOFF=1, VOFF=1). End to end, the model's reference stream is identical to the
  console's: `build/tools/cap_cmp tests/filt_frac/hw/ff_1 tests/filt_frac/model/ff_1 3` → stream 3 `0/... differ`.
  Streams 0–2 differ only through the inherited start state; at unity cutoff the band parity sets the cycle phase forever.
- Watch out: the first console run used unsigned data (no negative odd s16). The case was fixed and re-run; the committed captures are the signed run.

**F3 — VOFF=0.** The level multiply comes after the filter, on its clamped 1/16 output, and is truncated to whole
samples: `MIXS = (floor(clamp(-2 low) * M / 2^(7 + (a >> 6))) >> 4) * 16`.
- Check: `build/tools/filt_voff` → `full VOFF=0 streams (of 8): A 8 F 0 C 0 E 0` (`expected/filt_voff.txt`).
  Every VOFF=0 MIXS value is a multiple of 16.
- Independent: confirm `slot_output` in `src/aica_model.cpp` computes candidate A.

**F4 — integrator width. WITHDRAWN (refuted by the swarm).** The claim was that Q31 resonance with a full-scale
square wave is the worst case, so the integrators hold at least 23 signed bits and int32 is exact. It missed the
undamped unity-cutoff Q0 mode, which grows without bound in the unclamped model. The `filt_wide` captures and
`build/tools/filt_wide` output (`expected/filt_wide.txt`) remain valid data: their states (2^21.65) never reach the clamp.
Replaced by F6 and F7.

**F5 — validator extension.** `tools/filt_validate.cpp` gained the `low` (18) and `wide` (12) sets; it now covers
265 streams and 4,286,180 samples, the production model included.
- Check: `git diff a07a62d 2e19634 -- tools/filt_validate.cpp` touches only those two blocks and the build comment.
  Session 3 adds the 24-bit clamp to its reference `step()` (F6); the totals are unchanged.
- Control: other damping roundings fail. `build/tools/filt_validate <qbias>`:

  | qbias | full streams | samples matched |
  |---|---|---|
  | 0 (floor) | 4/265 | 590,851 |
  | 128 | 117/265 | 2,320,155 |
  | 223 | 154/265 | 3,588,688 |
  | 255 (ceil) | 265/265 | 4,286,180 |

**F6 — high-pass saturation (session 3).** The high-pass difference H = x − low − damping saturates to signed 24
bits (±8388608) before the cutoff multiply. The model (`lpf_step`) and the validator reference now include it.
- Evidence: `tests/filt_overflow` (FLV 0x1FFE/1FFC/1FF8, alternating full-scale bursts of 16..30000 samples at Q0,
  then Q → 31) and the held-out `tests/filt_overflow_check` (0x1FFA/1FF6/1FF0, bursts 33..4096). Captured by the
  swarm's research copy, merged unchanged.
- Check:
  - `build/tools/filt_overflow 1 24` → `TOTAL full=15/15` and `… 1 24 heldout` → `12/12`.
  - The same with `build/tools/filt_overflow_model` (production model).
  - Expected files: `expected/filt_overflow*_s1w24*.txt`.
- Controls (`expected/filt_overflow_other_stages.txt`; stage = where a limit is applied; each over all widths
  20..32, clamp and wrap). Only stage 1, the H clamp, matches all streams, and only at W=24 (`work/filt/overflow_stage1.txt`: all 15 FULL lines are `clamp W=24`):

  | Stage | Limit applied to | Full streams |
  |---|---|---|
  | 0 | stored states | 0/405 |
  | 2 | damping | 0/405 |
  | 3 | band increment | 5/405 — only the k=512 streams, where it equals H |
  | 4 | low increment | 0/405 |

- Model check: every older set is unchanged with the clamp (`build/tools/filt_validate_model` → 265/265; their states never reach it).
- Independent: `build/tools/filt_unity_sim` (`expected/filt_unity_sim.txt`) predicts the growth from rest.
  - Unclamped, band grows by about 350k per sample.
  - Clamped, it plateaus near |band| 4.19–4.25M.
  - The captures show |band| 4,252,670.

**F7 — reachable states (session 3).** With the F6 clamp no drive found gets |low| or |band| past 2^23. The
captures pin **band ≥ 24 signed bits** (4,252,670 > 2^22 matched with unbounded integrators) and **low ≥ 23**
(3.6M). Any wider register is unobservable with every drive found.
- Check: `build/tools/filt_reach -n 65536 -emin 0` (`expected/filt_reach_all.txt`; about 15 s on 24 cores). It covers every
  setting (16 exponents × 256 mantissas × 32 Q) with four drives:
  - dc;
  - alternating;
  - time-reversed impulse-response sign (the linear optimum);
  - an adaptive pump in phase with band.
- Result: maximum |band| 4,286,071 (0x1FFC Q0) and |low| 3,628,859 (0x1FF4 Q31).
- Control: `build/tools/filt_reach -noclamp -n 8192` diverges (2.9e9 at 0x1FFE Q0).
- **This is a search, not a proof.** Try to beat it: other drive shapes, mixed Q/FLV changes mid-drive through the FEG, or
  starting from the Q0 plateau and then switching Q. Anything past 2^23 would make the register width observable again.

### E — filter envelope (no console; evidence in tests/feg_track/hw)

The captures come from three batches of three FEG programs, with full-scale random input and Q4. Slot 3 is an unfiltered reference (`cases/feg_track.c`).

**E1 — recovery.** `build/tools/feg_track` follows the bit-exact filter from key-on and recovers u = v >> 1 at every
sample (v bit 0 is unused by the filter). All 9 streams are tracked end to end (`expected/feg_track.txt`: ambiguous-u counts per stream, max set sizes).
It writes `work/feg/ft_<b>_<k>.u`, which E2/E3 need.
- Scrutinize the tracker assumptions:
  - u moves by at most ±4 per sample;
  - the FEG holds FLV0 at the onset;
  - the input is floor(s16/2) of the reference stream;
  - the state-set cap is 400k and was never reached; the max set was 49k.
- The FEG slots use RR=0 so that a released slot keeps playing. With RR=31 the slot stops and its input drops to 0, which broke the first run at key-off.

**E2 — rules.** The fit uses the envelope clock every 2 samples: key-on loads FLV0 and the first step comes on the next clock; key-off steps on the same clock.
- **KRS applies** to FEG rates.
- **One comparator C = (v >= target):**
  - direction is down if C holds at the segment start;
  - attack and decay 1 step until C flips, which allows overshoot, and the next segment steps on the very next clock;
  - decay 2 and release skip any step that would flip C (hold short).
- **R < 48 rows use the clock counter − 1.**

Check with `build/tools/feg_fit` (`expected/feg_fit.txt`; `EGOFF=16383` means −1):

| Command | Expected |
|---|---|
| `EGOFF=16383 build/tools/feg_fit 1 3 1 0 shared` | batch 0: `s0 ALL s1 ALL s2 ALL` |
| `EGOFF=16383 build/tools/feg_fit 1 3 1 1 shared` | batch 2 (`kon 1`: at 1/8 pitch the first interpolated output is 0, so the onset is one sample after key-on): all three ALL |
| `EGOFF=16383 build/tools/feg_fit 1 3 1 0 sets 1 0 3261 3262` | batch 1: slots 0/1 full at key-off clock 3262, slot 2 only at 3261 (see E4) |

Controls, which must fail:

| Command | Fails at |
|---|---|
| `EGOFF=0 … 1 3 1 0 shared` (no counter offset) | batch 0, samples 515–5334 |
| `… 0 3 1 0 shared` (the old model's idle clock) | batch 0, 512/518 |
| `… 1 0 1 0 shared` (the old model's clamp at the target) | batch 0, 508/516/858 |
| `… 1 3 0 0 sets 1 0 3261 3262` (no KRS) | batch 1 slots 1/2, after 17 and 5 samples |

The fitter's `sim()` is the reference for the rules. Compare it line by line with `feg_clock` in `src/aica_model.cpp`.

**E3 — production model.** `build/tools/feg_validate` runs the real `AicaModel` per stream, with the fitted clock parameters
from its `fit[][]` table (c0, key-off clock, kon), and compares FEG.v >> 1 every sample. Expected: `TOTAL full=9/9 samples=77862/77862`.
- Negative control: in a copy of `tools/feg_validate.cpp` inside your private tree, change `m.eg_cnt = fit[b][k].c0 - 1;` to
  `m.eg_cnt = fit[b][k].c0;` (counter off by one), build it, and it gives `full=2/9`.
- Check that the `fit[][]` values equal the `feg_fit` output (E2).

**E4 — open, expected INCONCLUSIVE.** In batch 1, slot 2 needs the shared key-off one envelope clock earlier than slots 0/1
(3261 vs 3262). Its counter start also differs from theirs by 2 mod 4 (3 vs 21 mod 32). The working hypothesis is per-slot timing inside the
2-sample envelope period, where a register write lands between slots. Alternatives to test: a rotated R=49/R=57 increment row, or a slot-dependent counter.
A slot-number sweep with one FEG program would decide; that needs the console, and only group H may run it.

### A — amplitude envelope (no console; evidence in tests/aeg_dl0/hw and the committed tests/sgc_aeg)

**A1 — DL=0.** On a clock that starts in decay 1, the decay-1 step is applied first, and only then does a >= DL<<5 switch to
decay 2. This happens on every such clock, including clocks without a step, but not on the clock that enters decay 1.
- Check: `build/work/aeg_dl0_check tests/aeg_dl0/hw/dl0` (`expected/aeg_dl0_check_hw.txt`):
  - stream 0 (D1R 31) is at full level 520176 at +18 and drops to 487408 (a = 8) at +20;
  - streams 1/2 (D1R 20/10) stay full until key-off;
  - stream 3 (AR 20) drops at +524.
- The model now matches on these points (`expected/aeg_dl0_check_model.txt`).
- Monitor log `tests/aeg_dl0/hw/dl0_eg.txt`: `476 0000, 477 2000, 478 4008`.

**A2 — level-neutral for DL > 0.** The AEG change moves only the monitor's state timing, not the levels.
- Check (inside your private copy):
  ```sh
  mkdir -p work/head
  for e in bin hdr; do git show a07a62d:model/tests/sgc_aeg/model/dec2.$e > work/head/dec2.$e; done
  build/tools/cap_cmp work/head/dec2 tests/sgc_aeg/model/dec2
  ```
  Do this for dec2, rel_a, dec_a, dec_b, dec_dl and att_31_28: `0/... differ` in every stream after onset alignment.
  The raw files differ by one sample of capture offset because the emulated polling loop sees one fewer monitor change.
- `sgc_keys.txt`: only the `t ... us` transition lines changed against `a07a62d`, and every `end:` line is identical.
- New model-output baseline: `work/model_outputs_2026-09-23.sha256`. Run `./run_model.sh` in your copy, then `sha256sum -c`.

**A3 — monitor timing.** `tests/sgc_aeg/hw/dec_a_eg.txt` shows decay 2 within the crossing clock (`536 1/100 → 537 2/100`, a=0x100,
DL 8), where the old model showed it one clock later. This is consistent with A1.

### D — DSP and registers (no console; evidence in tests/dsp_temp/hw, tests/timer_probe/hw)

**D1 — TEMP ring.** The TWT writer moves down one slot per sample and covers all 128 slots in 128 samples, on both the console and the model.
- Check: `expected/dsp_temp_hw_summary.txt`: dump 1 has 16 slots written (68..83), dump 2 has 84, dump 3 has all 128.
- The stale slot seen once in `dsp_basic "A ffff -4096 1"` did not reproduce; the claim is that it was a load transient.

**D2 — documented difference, not fixed.** A value the SH4 writes into MIXS keeps feeding the console's DSP. In dsp_temp T2 all 128 TEMP slots hold 0xedcbb0 = −0x123450 after 4 ms; in dsp_basic `F ira 25`, alternate samples do.
The SH4 reads MIXS back as 0 after 2 ms. The model clears MIXS every sample (0 slots).
Verify the reading of the data; the hypothesis (a double-buffered input latch) is open.

**D3 — timers.** TIMA/TIMB/TIMC read back 0 at every prescale, whatever was written (`tests/timer_probe/hw/timer_probe.txt`).

### H — console re-capture (ONE agent, private copy, sequential)

Re-run the new cases in the private copy: `./run_hw.sh filt_low filt_frac filt_voff filt_wide feg_track aeg_dl0 dsp_temp timer_probe`.
That takes about 3 minutes; every capture must report `errors 0`. Then re-run F1–F4, E1, E2, A1 and D1–D3 on the new captures.
- **Conclusions must reproduce, not bytes.** Inherited filter state and clock phases differ per run.
- `feg_validate` has the fitted parameters of the committed capture hard-coded. For a re-capture, re-fit with `feg_fit`. Report
  whether the batch-1 slot-2 anomaly (E4) reappears.
- `filt_validate` reads fixed onsets for `filt_cyc` only; the new sets find their onsets from the reference stream.
- If time allows, E4's slot sweep: one FEG program on slots 0..3 over several batches with a shared key-off.

## Open items (not claims)

- Per-slot envelope timing (E4).
- The R<48 counter offset is applied to the FEG only. The AEG captures are compared phase-independently, so it is unmeasured there.
- SH4-written MIXS persistence (D2).
- sgc_level L5 remains an inherited-state difference (DC deadband). It is not evidence either way.

## Reference: key paths

- Model: `src/aica_model.cpp` — `feg_clock`, `aeg_clock`, `eg_increment(…, slow_off)`, `slot_output` (filter input floor(s16/2), VOFF path).
- Evidence: `tests/<case>/hw/` from the console, `tests/<case>/model/` from the model.
- Expected outputs: `work/verify/expected/`. Validator output: `work/filt/validate_model.txt`.
- The breakthrough handover: `git show a07a62d:model/HANDOVER.md`.
