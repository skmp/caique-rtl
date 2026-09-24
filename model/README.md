# caique AICA model

Behavioural model of the Dreamcast AICA sound generator (64 slots) and DSP, measured against the console.
Slot-filter arithmetic matches 4,286,180 captured samples across 265 streams; the envelope clock is locked to the
DSP ring counter (K exact per boot), so the amplitude and filter envelopes replay sample-exactly: 89/89 envelope
streams (eg_model), 5 multi-cycle key-off / key-on runs of 16 cycles each (eg_replay, 4/4 streams each), 12/12 slot
stop / off tails (tail_cmp) and 21/21 MIXS writer probes (mixsw_check); the key-off clock, the slot stop sequence
and the MIXS writer rule are measured (session 5); the key-off clock with a passed segment (23/23 cycles, the next
segment's step), the stop = off + CA 0 at a = 0x3C0, the CPU's MIXS readback bank and the live (unlatched) envelope
registers (359 informative witness-pinned rewrites, 0 latched) are measured (session 6).
Findings, with the test behind each one: **[NOTES.md](NOTES.md)**. Handover / how to verify the current claims:
**[HANDOVER.md](HANDOVER.md)**. What is NOT nailed down (sub-sample effects, empirical rules, unmeasured areas, harness artefacts): **[LIMITATIONS.md](LIMITATIONS.md)**. Current model-vs-console status:
**[tests/SUMMARY.txt](tests/SUMMARY.txt)**. Current C++ reproduction commands are in HANDOVER.md. How caique integrates
the ARM7DI (wren7-rtl), and the combined tests that live here: **[../INTEGRATION.md](../INTEGRATION.md)**.

## Layout

| path | what |
|---|---|
| `sample-model/aica_model.{h,cpp}` | the sample model: register interface (`write`/`read`), wave RAM, `step()` = one 44.1 kHz sample. Integer only. Every validator links it. |
| `cycle-model/` | the clocked model (`clock()` = one 22.5792 MHz MCLK, the frame plan of rtl/v1, the SH4 / ARM bus ports); the reference of rtl/v1's co-simulation; `io_cycle.cpp` the harness (`./run_cycle.sh CASE` -> `tests/<case>/cycle/`) |
| `src/dsp_asm.h` | DSP instruction encode/decode (shared by model and console tests) |
| `src/dsp_float.h` | DSP 16-bit memory float PACK/UNPACK (verified exhaustively) |
| `cases/*.c` | test cases (73), written once against `cases/aica_io.h`; each builds for the console and both models |
| `cases/common/replay.c` | the replay preamble every platform's main runs before a case (MDEC_CT, K, envelope clock parity, noise LFSR) |
| `cases/flog.h` | the DSP frame logger: where an SH4 write lands, to one frame |
| `cases/aica_io.h` | portable AICA access API + helpers (slot config and `slot_log`, DSP program buffer, `aica_reset` / `dsp_reset`, text/binary output) |
| `cases/cap.h` | sample-exact capture of up to 4 MIXS buses through a DSP program + wave RAM ring |
| `hw/` | console back-end (`io_kos.c`, KOS build; `make -C hw [HWDIR=name] CASE` -> `build/hw/<HWDIR>/CASE.elf`; `./run_hw.sh` also fits the run's `replay.txt`) |
| `host/` | model back-end (`io_model.cpp`: G2 access cost 2.4 us per access, sample stepping; `make -C host CASE` -> `build/host/CASE`) |
| `tests/<case>/hw/` | console results (text, captures `*.hdr`/`*.bin`); `tests/aeg_koff/hw_run1/` keeps that case's first console run |
| `tests/<case>/hw9/` | the session-9 console run of every case (`HWDIR=hw9 ./run_hw.sh`): with the known start state and the replay preamble (`replay.txt`) |
| `tests/<case>/model/`, `cycle/` | the sample model's and the cycle model's results for the same case (`REPLAY_FROM=hw9` applies that console run's replay parameters) |
| `tools/` | analysis and comparison, C++ (`make -C tools` -> `build/tools/`); the Python scripts are legacy |
| `work/` | scratch (derived data for searches, tracked FEG values `work/eg/*.u`, session reports `work/verify/`; scratch programs build to `build/work/`) |
| `build/` | every executable and object file (git-ignored): `hw/`, `host/`, `tools/`, `work/` |

Session-5 cases: `aeg_koff` (AEG key-off from decay 2 / decay 1 / attack and key-on during release, key-off sample
pinned by a witness slot), `eg_kprobe` (K probe: bit 13 and candidate resetters), `slot_tail` (what a slot sends after
its envelope stops), `feg_koffdir` / `feg_koffatt` (FEG key-off clock with opposite directions, decay 2 and attack),
`mixs_write` (which slots rewrite a MIXS bus).
Session-6 cases: `feg_koffpass` (FEG key-off on the clock after the old segment passed its target: 2 runs x 64
witness-pinned cycles), `ca_stop` (EG / CA monitors polled through a slow decay 2 entered exactly at a = 0x3C0: the
stop is "off"), `mixs_rd` (CPU readback of a MIXS bus right after a write, with / without writers, one half only),
`eg_latch` (DL / KRS / AR / D2R / FD1R / FD2R / FLV3 rewritten on running slots, witness-pinned: all live),
`eg_latch2` (RR from 0 and from 24, with and without a redundant key-off, D2R from 0: all live -- no latch, no rate-0
arming), `eg_latch3` (RR with an SA / LPCTL / LEA rewrite in the same group: all live, CA never restarted; tail_c's
delayed RR was the in-sample write position, see NOTES "Envelope clock").

## Running

    ./run_hw.sh CASE...      # build for the console, run one after another through shrike4-rtl/tools/hw/hwrun.sh
    ./run_model.sh CASE...   # build against the model and run
    make -C tools            # analysis tools -> build/tools/ (run them from this directory)
    tools/validate_s5.sh     # the session-5 gate: eg_model + eg_replay + tail_cmp + mixs_write verdicts, PASS/FAIL (~2 min)
    # C++ filter validation: see HANDOVER.md. Do not use the legacy Python tools.

Console runs take a few seconds each (dcload-ip); only one program can use the console at a time (hwrun.sh locks).

## Comparison tools

- `tools/filt_step.cpp`: C++ capture exporter, byte-identical to the historical datasets.
- `tools/filt_rule.cpp`: exact integer state-set search; form 5 models the discovered coarse damping.
- `tools/filt_validate.cpp`: autonomous arithmetic validation against all filter captures; compile with
  `-DVERIFY_MODEL` and `sample-model/aica_model.cpp` to also check the actual model's MIXS output.
- `tools/filt_compare.cpp`: compare full hardware/model runs with impulse alignment; reports inherited-state differences.
- `tools/filt_edges.cpp`: independent analysis of the fresh endpoint impulses and bypass timing reference.
- `tools/filt_need.cpp`: forced rounding and contradictory-operand diagnostics for older hypotheses.
- `tools/cap_cmp.cpp`: compare two captures (e.g. console vs model) aligned at a stream onset (replaces `cap.py cmp`).
- `tools/eg_model.cpp`: replay a capture's slot programs through the production model with the envelope clock locked
  to the capture's ring position (MDEC_CT from `cap_start` c0 and the header) and compare every sample -- the
  envelope validator (89/89 streams: the 33 session-4 runs, eg_kprobe kp_p0..p7, feg_koffdir kd_0..7 x 3,
  feg_koffatt ka_0..7 x 3).
  `tools/eg_phase.cpp` (AEG from the level law, K search), `tools/eg_keys.cpp` (key timing per slot),
  `tools/feg_lock.cpp` (FEG tracker + ring-locked fit), `tools/feg_validate.cpp`.
- `tools/eg_replay.cpp`: multi-cycle AEG replay through the model with key events at given samples (`-case` derives
  the tests/aeg_koff events from the capture: 5 runs x 16 cycles x 4 streams).  `tools/tail_cmp.cpp` +
  `tail_cmp_all.sh`: ring-locked replay of the slot_tail runs with a stage-wise event search (inherited filter
  state, key-off, RR/SA rewrite, IMXL 0).  `tools/validate_s5.sh`: eg_model + eg_replay + tail_cmp_all + the mixs_write verdict comparison in one go.
- Standalone fitters (NOTES formulas only, no model linked): `tools/kfit.cpp` (K from the eg_kprobe probes, `-expect`,
  controls `-oldr63` / `-noslow`), `tools/koff_fit.cpp` (AEG key-off / key-on-in-release hypotheses on aeg_koff,
  witness-pinned, `-synth` self-tests), `tools/koffdir_check.cpp` + `tools/feg_law.h` (FEG key-off readings on
  feg_koffdir, `-u` writes the tracked u files), `tools/koffatt_check.cpp` (FEG attack key-off on feg_koffatt),
  `tools/mixsw_check.cpp` (MIXS writer rules on mixs_write, verdict + rule table), `tools/koffpass_check.cpp` (the four
  readings of a passed-flag key-off clock on feg_koffpass, per-cycle windows + FULL counts per phase, `-q`, `-K`),
  `tools/latch_check.cpp` (live vs latched register rewrites on eg_latch / eg_latch2 / eg_latch3, per-order verdicts,
  odd-E straddles and the eg_latch3 CA-restart count, `-run`, `-v`, `-K`).  ca_stop and mixs_rd are read by eye (text
  logs).  `tools/tail_cmp.cpp` stage 2 searches the tail_c RR write at d = w00 - w14 in {1, 0, -1} (in-sample write order).
- `tools/filt_negform.cpp` (the sign-flipped all-floor filter form), `tools/filt_reach2.cpp` (state maxima with a
  mid-drive setting switch).
- The other Python comparison tools and `filt_search*.cpp` are historical; current work uses C++ integer math only.

## Not observable from the SH4

Direct outputs (DISDL/DIPAN), the DSP output mixer (EFSDL/EFPAN), MVOL/MONO/DAC18B and the DAC itself only reach
the analog output. The model implements them with the measured send-level law, unverified. EXTS (CD-DA input)
reads 0 without a disc playing.
