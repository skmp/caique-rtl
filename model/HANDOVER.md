# caique AICA model — session 4 handover (2026-09-23, evening)

**Scope: verify the session-4 results.**  Session 3 is commit `fa451ca`; session 4 is uncommitted (`git diff HEAD`
plus the untracked files).  Session 4 first re-ran every session-2/3 check from `work/verify/expected/` (all 11
reproduce byte for byte: `work/verify/s4/baseline.log`), then found that the envelope clock is locked to the DSP's
ring counter, which made the amplitude and filter envelopes sample-exact, and fixed six model behaviours on the way.
Findings are in [NOTES.md](NOTES.md) ("Envelope clock", "MIXS retention", and the edited AEG / FEG / filter
bullets); the session log with every command and output is `work/verify/s4/` and the summary report
`work/verify/s4.md`.  **General re-verification of the filter arithmetic, DSP, levels and pitch is out of scope**
(unchanged; their validators still pass, see S9).

## What changed

| Area | Change | Claims |
|---|---|---|
| Envelope clock | ticks on the samples with even MDEC_CT; `eg_cnt = eg_K - MDEC_CT/2`; MDEC_CT free-running 16 bits; `EG_PHASE` removed | S1 |
| Key events | applied on the next sample (any parity), no step on a key-on sample; a key-off sample that is a clock steps with the previous segment's increment | S2, S3 |
| Increment rows | rows 5 / 9 / 13 = {b,2b,b,b,b,2b,b,b}; AEG uses the R < 48 offset too; R 63 attack leaves the attack on the next clock | S4, S5 |
| Decay 1 → 2 | `a[9:5] == DL` (equality), which keeps an LPSLNK-entered decay 1 going | S6 |
| MIXS | a bus nobody sends to keeps its value; two banks for the DSP read (CPU writes visible on alternate samples) | S7 |
| Filter | sign-flipped all-floor form proven equivalent; mid-drive switch search (F7 refined) | S8 |
| Cases | `eg_lock` (11 runs), `feg_krs` (4 batches), console captures under `tests/<case>/hw/` | — |
| Tools | `eg_model` (ring-locked replay through the production model, the main validator), `eg_phase`, `eg_keys`, `feg_lock`, `filt_negform`, `filt_reach2`; `feg_validate` rewritten for the ring lock | — |
| Scratch | `work/eg/` (adump, udump, lo2_a, fegdbg, peek, rle, run lists), `work/eg/*.u` tracked FEG values | — |

Model diff: `git diff HEAD -- src/`.  New expected outputs: `work/verify/expected/{eg_model_all,eg_keys_keys,eg_keys_keys2,feg_validate,eg_phase_dl0,eg_phase_eg_lock,eg_phase_lo2,filt_negform,filt_reach2}.txt`.
Model-output hashes after session 4: `work/model_outputs_2026-09-23b.sha256` (every capture-bearing case changed, as the key-on now lands one sample earlier; levels are unchanged after onset alignment, see S9).

## Rules for swarm agents

Unchanged from session 3: main tree read-only except your report; work in a private copy (`build/swarm/$ID`, rsync
without `build`); console only for the agent assigned group H, through `./run_hw.sh` in the copy (L2 isolation);
everything you write must be trackable by git (merge cases / tools / captures / results into `cases/`, `tools/`,
`tests/<case>/`, `work/`); C++ integer math only, nothing in `/tmp`; report at `model/work/verify/<ID>.md` with
CONFIRMED / REFUTED / INCONCLUSIVE per claim; independent re-derivations from the NOTES formulas are worth more than
re-running our tools.

## Setup (inside the private copy)

```sh
make -C tools -j8        # 38 tools -> build/tools/
make -C host -j8         # 41 case binaries -> build/host/
mkdir -p build/work
for p in adump udump lo2_a peek rle; do g++ -O2 -std=c++17 -o build/work/$p work/eg/$p.cpp; done
g++ -O2 -std=c++17 -o build/work/fegdbg work/eg/fegdbg.cpp src/aica_model.cpp
build/tools/eg_model | tail -n 1            # sanity: TOTAL full=33/33
```

The ring position of a capture: `cap_start: ... c0 XXXX` in the case's text output (the sync value), `n_first` = word
3 of the `.hdr`; the MDEC_CT of capture sample i is `(c0 - n_first - i) & 0xFFFF`.

## Claims

### S1 — the envelope clock is locked to MDEC_CT (console evidence: tests/eg_lock, feg_track, aeg_dl0, sgc_loop)

The envelope clock ticks on every sample with even MDEC_CT, and `eg_cnt = K - MDEC_CT/2 (mod 2^14)` with one K per boot
(6491 for feg_track / aeg_dl0 / eg_lock / feg_krs; 165 mod 1024 for sgc_loop, captured before a reset).
- `build/tools/eg_phase -v` → `expected/eg_phase_dl0.txt`: aeg_dl0 4/4 streams FULL with K the only parameter; the
  `feg_track` derivation lines show the three batches' fitted counters agree through the ring position (K mod 8 = 3, mod 32 = 27).
- `build/tools/eg_phase work/eg/eg_lock_runs.txt` → `expected/eg_phase_eg_lock.txt`: att_slow (AR 6/4/2/1, 487,478
  samples per stream) and att_mid FULL, K = 6490 mod 8192 (slow_off 0) = 6491 (slow_off -1); the odd_* runs stop at
  +21 (loop-end interpolation, see S9) — use eg_model for them.
- `build/tools/eg_phase -v work/eg/lo2_runs.txt` → `expected/eg_phase_lo2.txt`: sgc_loop lo_2 stream 3 FULL at K = 164
  mod 1024 (its own boot); stream 2 stops at +2012 (S6).
- `build/tools/eg_keys tests/eg_lock/hw/keys 6bb0` and `... keys2 36e8` → `expected/eg_keys_keys*.txt`: 32 cycles, every
  first attack / release step on an even sample, 0 violations, no per-slot differences.
- `build/tools/eg_model` → `expected/eg_model_all.txt`: **33/33 streams FULL** through the production model
  (1.17 M samples), each with the key-off samples that work.
- Independent: derive MDEC_CT per sample from the header and `cap_start` line, pick any capture with a slow rate, and
  fit the clock parity and counter yourself (the AEG level law is in NOTES "Slot levels").
- Controls: `eg_phase -rot` (the YM2612 rows 5/9/13) or a wrong K make the odd-rate FEG streams and feg_track batch 1
  slot 2 fail; a clock on odd MDEC_CT fails everything.

### S2 — key events land on the next sample; no step on a key-on sample

- eg_keys output: even onsets hold the key-on level for 2 samples, odd onsets for 1; the first step is always on the
  next even sample.  feg_track batch 2 (odd onset) fits with kon = 1 in the old frame for the same reason.
- Model: `step()` applies `key_pending` every sample; `Slot::keyed` skips the clock.

### S3 — a key-off on a clock sample steps with the previous segment's increment (closes E4)

- `eg_model fk_1_s0 fk_1_s1 fk_1_s2 fk_3_s0 ...` (feg_krs batches 1 and 3): decay 2 holding short at 0x19FE with +4,
  release +1; the first release clock moves +4 to 0x1A02, then +1 per clock.  fk_2 (key-off on an odd sample) and
  feg_odd start the release cleanly.  Without the rule those six streams fail at the key-off (`work/verify/s4/eg_model_fk.txt`, before the rule).
- feg_track: with the rule every batch has one key-off sample shared by its three slots (`expected/eg_model_all.txt`:
  ft_1 6660, ft_2 803/804); the old "slot 2 one clock early" was its +8 decay-2 increment on the key-off clock.
- `build/tools/feg_validate` → `expected/feg_validate.txt`: 9/9 (rewritten: MDEC_CT from the capture, K 6491,
  key-off searched).
- KRS is not involved: feg_krs batch 0 (the batch-1 programs on swapped slots) keys off together.
- Model: `Slot::keyed_off`, `aeg_prev` / `feg_prev`; `aeg_clock` / `feg_clock` take the rate from the previous state on
  that sample.  Unverified on the AEG (its measured key-offs came from rate-0 segments).

### S4 — increment rows 5 / 9 / 13 and the AEG counter offset

- eg_lock odd_att (R 45/49/53/57), odd_att3 (47/51/55/59), odd_same / odd_same2 (R 49 on slots 0..3 and 5/17/40/63),
  odd_dec (odd rows in decay 1): all FULL in eg_model with `{b,2b,b,b,b,2b,b,b}` for rows 5/9/13 and the OPN table
  elsewhere; `work/eg/adump` prints the increments directly (double step at cnt & 7 = 1, 5).
- The AEG's K equals the FEG's only with the -1 offset on R < 48 rows (eg_phase: 6491 vs 6490).
- Control: `eg_model` with the OPN rows (edit `eg_inc` in src) fails odd_att stream 1 at the first double step.

### S5 — R = 63 attack

- eg_lock odd_dec (AR 31 with KRS 0 / FNS 0x200 → R 63, D1R at odd rows): a = 0 on the first clock after the key-on;
  decay 1 steps from the second.  Old model: 0/68534 (`work/verify/s4/eg_model_aeg.txt`).

### S6 — decay 1 → decay 2 is an equality on a[9:5]

- sgc_loop lo_2 stream 2 (AR 8, LPSLNK at LSA 2000, D1R 20, DL 0): after the link the attenuation keeps rising at the
  R 40 pace for the rest of the capture (`build/work/lo2_a`); with `>=` the model holds (`work/verify/s4/eg_model_aeg.txt`,
  `lo_2 fail 2012/5405`); with `==` FULL.  aeg_dl0 and every sgc_aeg run are unchanged by the compare.

### S7 — MIXS retention and banks (tests/eg_lock mixs)

- `build/work/rle tests/eg_lock/hw/mixs 2`: -8 (the previous run's last send) for 7193 samples with no sender, then
  0xABCD / -8 alternating to the end after the CPU write; `... mixs 3`: a sender (slot 3, silent then playing 4096)
  rewrites the bus every sample, the CPU value shows once.  Model run (`tests/eg_lock/model/mixs`): the same shapes
  (retained value 0 instead of -8).  Not modelled: the sub-sample order of a CPU write against the sender.

### S8 — filter representation and width

- `build/tools/filt_negform` → `expected/filt_negform.txt`: 0 differences in 2^28 cases (build with the noinline
  attributes as in the source; an inlined build lets the compiler fold both forms).
- `build/tools/filt_reach2 -n1 4096 -n2 1024` → `expected/filt_reach2.txt` (about 30 s on 24 cores): max |band|
  5,315,589 and |low| 4,791,874, none ≥ 2^23.

### S9 — regression

- `work/verify/s4/baseline.log`: all 11 session-2/3 expected outputs reproduce (SAME) with the session-3 model.
- After the model changes: `filt_validate_model` 265/265, `filt_overflow_model` 15/15 and 12/12 held out
  (`work/verify/s4/filt_after_model_change.txt`); `feg_validate` 9/9; `eg_model` 33/33.
- Whole-program model runs: `./run_model.sh <all cases>` (`work/verify/s4/run_model_all.log`, 42 cases exit 0);
  every capture-bearing output differs from `work/model_outputs_2026-09-23.sha256` because the key-on now lands one
  sample earlier and eg_lock/feg_krs are new; `cap_cmp` after onset alignment: the sgc_aeg levels agree with the
  console as before (the residual is the console's unknown key-on phase in a whole-program run).
- The eg_phase predictor assumes a constant input: at pitch 1.5 the 32-sample loop interpolates its last sample
  against the RAM beyond LEA (0), so eg_phase stops at +21 on the odd_* runs; the production model reproduces that
  sample (eg_model FULL), which is a check of the loop-end interpolation on the console.

### H — console (ONE agent, private copy)

`./run_hw.sh eg_lock feg_krs` (about 30 s).  Conclusions must reproduce, not bytes: K is a property of the boot
(fit it with `eg_phase` on att_slow first, then pass `-K` to eg_model), and the key-off samples depend on where the
KYONEX write lands.  Worth adding if time allows: a rate-1 / rate-3 attack through KRS (pins K bit 13); an AEG
key-off from a decay 2 with D2R > 0 landing on a clock (S3 on the AEG); a key-on during a release.

## Reference: key paths

- Model: `src/aica_model.cpp` — `step()` (key events, clock, MIXS banks), `aeg_clock`, `feg_clock`, `eg_increment`,
  `key_on` (R 63), `lpf_step`.
- Evidence: `tests/eg_lock/hw/`, `tests/feg_krs/hw/` (session 4), plus the earlier `tests/<case>/hw/`.
- Validators: `tools/eg_model.cpp` (everything envelope, the one to run), `tools/feg_validate.cpp`,
  `tools/filt_validate.cpp` (`filt_validate_model`), `tools/filt_overflow.cpp`.
- Previous handovers: `git show fa451ca:model/HANDOVER.md` (session 3), `git show a07a62d:model/HANDOVER.md` (breakthrough).
