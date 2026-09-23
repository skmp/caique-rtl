# caique AICA model — swarm verification handover (2026-09-23, session 2)

**Scope: verify only the progress made since the previous handover.** The previous handover (the filter
"breakthrough": coarse damping, 235 streams) is the committed state: git `HEAD` = `a07a62d` ("initial slop").
Read it with `git show HEAD:model/HANDOVER.md`. Everything since is uncommitted: `git diff HEAD` plus the new
untracked cases, tools and `tests/<case>/` directories listed below. **General re-verification of earlier results
(DSP, levels, pitch, the breakthrough recurrence itself, ...) is out of scope.**

Findings live in [NOTES.md](NOTES.md). This file gives each new claim an ID, a check, the expected output, and
the controls that must fail.

## What changed since the previous handover

| Area | Change | Claims |
|---|---|---|
| Build layout | every executable/object under `model/build/` (git-ignored); `tools/Makefile`; hw/host Makefiles and run scripts moved; 22 committed tool binaries removed from `work/filt/` | L1 |
| Filter | low cutoffs e=0..11, fractional input = floor(s16/2), VOFF=0 = level after the filter, integrator width >= 23 bits; validator extended (265 streams) | F1–F5 |
| FEG | sample-exact trajectories recovered through the filter; rules fitted; **model rewritten** (`feg_clock`) | E1–E4 |
| AEG | DL=0: decay 1 steps before the DL check; **model fixed** (`aeg_clock`) | A1–A3 |
| DSP / registers | TEMP ring coverage; SH4-written MIXS persists on the console (not modelled); timers write-only | D1–D3 |

New console cases (each with `tests/<case>/{hw,model}/`): `filt_low`, `filt_frac`, `filt_voff`, `filt_wide`,
`feg_track`, `aeg_dl0`, `dsp_temp`, `timer_probe`. New tools: `cap_cmp`, `filt_frac`, `filt_voff`,
`filt_wide`, `feg_track`, `feg_fit`, `feg_validate`, `tools/Makefile`. Scratch programs: `work/filt/lowrange.cpp`,
`work/aeg_dl0_check.cpp`, `work/feg/{dump,steps}.cpp`. Model: `src/aica_model.{h,cpp}` (`git diff HEAD -- src`).

## Rules for swarm agents

1. **Main tree is read-only**, apart from your report (rule 5). Do not edit `src/`, `cases/`, `tools/`, `tests/` or the docs. Do not commit.
2. **Work in a private copy.** The tools write fixed relative paths: `feg_track` → `work/feg/`, `filt_step` →
   `work/filt/`, `run_model.sh` → `tests/<case>/model/`, `run_hw.sh` → `tests/<case>/hw/`. So run everything in
   your own copy:
   ```sh
   cd caique-rtl/model
   mkdir -p build/swarm/$ID && rsync -a --exclude build ./ build/swarm/$ID/ && cd build/swarm/$ID   # ~150 MB, git-ignored
   ```
3. **Console: only the agent assigned group H.** It runs `./run_hw.sh` inside its private copy, never in the main
   tree, which holds the evidence. Everyone else stays off the console. `hwrun.sh` serializes users, so a second
   user would only stall group H's sequence.
4. **C++ with integer math only** for any new check (awk, `cmp` and `sha256sum` are fine). The `tools/*.py` scripts are legacy; don't use them.
   Keep all files under `caique-rtl/model/` (your copy counts); nothing in `/tmp`.
5. **Report** in the main tree at `model/work/verify/<ID>.md`: one section per claim with **CONFIRMED / REFUTED /
   INCONCLUSIVE**, the commands you ran, the key output and anything suspicious. Work independently; don't read
   other agents' reports before finishing yours. Independent re-derivations (your own code from the NOTES formulas)
   are worth more than re-running our tools.

## Setup (every agent, inside the private copy)

```sh
make -C tools -j8        # 28 tools -> build/tools/
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
  - From clean (`rm -rf build/{hw,host,tools}` in your copy): `make -C tools`, `make -C host`, and `make -C hw` with KOS give 28, 37 and 37 files.
  - `run_hw.sh` and `run_model.sh` use `build/hw/<case>.elf` and `build/host/<case>`.
- The 22 tool binaries formerly committed under `work/filt/` show as deleted, which is intended. Each has its source in `tools/` or `work/filt/*.cpp`.

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

**F4 — integrator width.** The integrators hold at least 23 signed bits (1/8-sample units), and the model's int32 states are exact.
- Check: `build/tools/filt_wide` (`expected/filt_wide.txt`).
  - Every stream matches unbounded; |low| and |band| reach about 3.28M (2^21.65), and 20–36 % of samples sit on the rails.
  - Clamping or wrapping at W = 20..22 fails; W = 23..25 matches.
- Question to scrutinize: is Q31 resonance with a full-scale square wave really the largest reachable state? The argument is the L1 norm of the impulse response, about 1/q × 4/π.
  If you can find an input that pushes the states further, say so.

**F5 — validator extension.** `tools/filt_validate.cpp` gained the `low` (18) and `wide` (12) sets; it now covers
265 streams and 4,286,180 samples, the production model included.
- Check: `git diff HEAD -- tools/filt_validate.cpp` touches only those two blocks and the build comment.
- Control: other damping roundings fail. `build/tools/filt_validate <qbias>`:

  | qbias | full streams | samples matched |
  |---|---|---|
  | 0 (floor) | 4/265 | 590,851 |
  | 128 | 117/265 | 2,320,155 |
  | 223 | 154/265 | 3,588,688 |
  | 255 (ceil) | 265/265 | 4,286,180 |

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
  for e in bin hdr; do git show HEAD:model/tests/sgc_aeg/model/dec2.$e > work/head/dec2.$e; done
  build/tools/cap_cmp work/head/dec2 tests/sgc_aeg/model/dec2
  ```
  Do this for dec2, rel_a, dec_a, dec_b, dec_dl and att_31_28: `0/... differ` in every stream after onset alignment.
  The raw files differ by one sample of capture offset because the emulated polling loop sees one fewer monitor change.
- `sgc_keys.txt`: only the `t ... us` transition lines changed against HEAD, and every `end:` line is identical.
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
- The previous handover: `git show HEAD:model/HANDOVER.md`.
