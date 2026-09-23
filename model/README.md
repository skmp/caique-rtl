# caique AICA model

Behavioural model of the Dreamcast AICA sound generator (64 slots) and DSP, measured against the console.
Slot-filter arithmetic now matches 4,286,180 captured samples across 265 streams, including production-code validation.
Findings, with the test behind each one: **[NOTES.md](NOTES.md)**. Handover / how to verify the open filter study:
**[HANDOVER.md](HANDOVER.md)**. Current model-vs-console status:
**[tests/SUMMARY.txt](tests/SUMMARY.txt)**. Current C++ reproduction commands are in HANDOVER.md.

## Layout

| path | what |
|---|---|
| `src/aica_model.{h,cpp}` | the model: register interface (`write`/`read`), wave RAM, `step()` = one 44.1 kHz sample. Integer only. |
| `src/dsp_asm.h` | DSP instruction encode/decode (shared by model and console tests) |
| `src/dsp_float.h` | DSP 16-bit memory float PACK/UNPACK (verified exhaustively) |
| `cases/*.c` | test cases, written once against `cases/aica_io.h`; each builds for the console and for the model |
| `cases/aica_io.h` | portable AICA access API + helpers (slot config, DSP program buffer, text/binary output) |
| `cases/cap.h` | sample-exact capture of up to 4 MIXS buses through a DSP program + wave RAM ring |
| `hw/` | console back-end (`io_kos.c`, KOS build; `make -C hw CASE` -> `build/hw/CASE.elf`) |
| `host/` | model back-end (`io_model.cpp`: G2 access cost 2.4 us per access, sample stepping; `make -C host CASE` -> `build/host/CASE`) |
| `tests/<case>/hw/` | console results (text, captures `*.hdr`/`*.bin`) |
| `tests/<case>/model/` | the model's results for the same case |
| `tools/` | analysis and comparison, C++ (`make -C tools` -> `build/tools/`); the Python scripts are legacy |
| `work/` | scratch (derived data for searches; scratch programs build to `build/work/`) |
| `build/` | every executable and object file (git-ignored): `hw/`, `host/`, `tools/`, `work/` |

## Running

    ./run_hw.sh CASE...      # build for the console, run one after another through shrike4-rtl/tools/hw/hwrun.sh
    ./run_model.sh CASE...   # build against the model and run
    make -C tools            # analysis tools -> build/tools/ (run them from this directory)
    # C++ filter validation: see HANDOVER.md. Do not use the legacy Python tools.

Console runs take a few seconds each (dcload-ip); only one program can use the console at a time (hwrun.sh locks).

## Comparison tools

- `tools/filt_step.cpp`: C++ capture exporter, byte-identical to the historical datasets.
- `tools/filt_rule.cpp`: exact integer state-set search; form 5 models the discovered coarse damping.
- `tools/filt_validate.cpp`: autonomous arithmetic validation against all filter captures; compile with
  `-DVERIFY_MODEL` and `src/aica_model.cpp` to also check the actual model's MIXS output.
- `tools/filt_compare.cpp`: compare full hardware/model runs with impulse alignment; reports inherited-state differences.
- `tools/filt_edges.cpp`: independent analysis of the fresh endpoint impulses and bypass timing reference.
- `tools/filt_need.cpp`: forced rounding and contradictory-operand diagnostics for older hypotheses.
- `tools/cap_cmp.cpp`: compare two captures (e.g. console vs model) aligned at a stream onset (replaces `cap.py cmp`).
- The other Python comparison tools and `filt_search*.cpp` are historical; current work uses C++ integer math only.

## Not observable from the SH4

Direct outputs (DISDL/DIPAN), the DSP output mixer (EFSDL/EFPAN), MVOL/MONO/DAC18B and the DAC itself only reach
the analog output. The model implements them with the measured send-level law, unverified. EXTS (CD-DA input)
reads 0 without a disc playing.
