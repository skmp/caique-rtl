# caique ↔ wren7 integration

caique-rtl is the AICA: the 64-slot sound generator, the effect DSP, the registers, wave RAM and the bus arbiter. wren7-rtl is the ARM7DI, the AICA's sound CPU. On the Dreamcast the ARM sits on the AICA's bus, and the AICA owns that bus. So **caique integrates wren7**. This document describes how, who owns what, and where the combined tests live. The counterpart in wren7 is `wren7-rtl/INTEGRATION.md`.

## Ownership

| Area | Owner |
|---|---|
| The ARM7DI core: instruction semantics and bus-cycle sequences (C++ model now, RTL later) | wren7 |
| ARM-side console measurements: timing kernels, the FIQ handshake, multiplier semantics | wren7 |
| The measured reference bus model: `DcArmBus`, `DcWaits::dreamcast()` | wren7 |
| The AICA: slots, DSP, registers, MIXS and wave RAM | caique |
| The bus arbiter and the ARM port (`rtl/v1/aica_bus.sv`) | caique |
| The interrupt controller and timers (v2) | caique |
| **Every combined test** (see below) | caique |

A combined test is anything that needs the ARM and the AICA together:

- co-simulation of the ARM with the AICA RTL;
- the arbiter checked against wren7's bus model;
- AICA state read back after ARM jobs;
- console cases where the ARM, the channels and the DSP interact.

These live in caique: in `rtl/v1/tb/` for simulation, and in `model/cases/` for console cases. wren7 keeps no test that needs AICA behaviour beyond `DcArmBus`. wren7's console job suites (`tests/hw/<suite>`) stay in wren7, because they measure ARM timing. caique runs them as inputs.

Where findings are recorded:

- **Bus timing measured with ARM programs** goes in wren7's `model/NOTES.md` and `model/TIMING.md`.
- **AICA-internal behaviour** goes in caique's `model/NOTES.md`, even when it was found with ARM jobs. An example is the channel/DSP collision rule.
- **Combined-test results** go in caique's `model/HANDOVER.md` and `rtl/v1/README.md`. wren7's NOTES keeps a short pointer.

## Interfaces

### Model level (now)

- **`Arm7DI`** (`wren7-rtl/model/src/arm7di.h`):
  - `bus_cycle()` performs exactly one bus cycle, so the caller owns time.
  - `at_boundary()` is true between instructions.
  - `set_fiq()` drives nFIQ.
- **`Arm7Bus`**, implemented by the memory system:
  - `cycle(const BusCycle &, BusResult &)` receives each cycle: `t` (MCLK at the cycle's start), `addr`, `wdata`, type N/S/I/C, and flags WRITE / BYTE / OPC / LOCK / USER. It returns `rdata`, `wait` (extra MCLK) and `abort`.
  - `tick(t)` runs at every instruction boundary.
- **caique's side:** `RtlBus` in `rtl/v1/tb/armjob_tb.cpp` implements `Arm7Bus`. It drives every N/S cycle into the RTL's ARM port, and the wait it reports is the RTL's. I and C cycles take one clock and make no request.
- **The shadow oracle:** a `DcArmBus` receives the same cycles. Its wait for each cycle is compared with the RTL's.
  - It also stands in for the interrupt controller until caique has one: SCIEB..MCIRE, L/M and nFIQ.
  - The ARM gets the shadow's data for those registers and caique's for everything else.
- **`tools/hwjob.h`:**
  - `parse_jobs`;
  - `run_job_model(job, waits, max_cycles, trace, mul_carry, sh4_phase, adpcm_origin)`;
  - `sh4_setup`;
  - `HwJob::rregs`, the runner's register readouts.

### RTL level (for the wren7 core)

The ARM port of `aica_core` is the interface a wren7 RTL core connects to. Its authoritative contract is `rtl/v1/README.md`, "ARM7DI port contract".

| Signal | Direction | Meaning |
|---|---|---|
| `arm_req` | in | a memory cycle wants to start in this clock; held until `arm_ack` |
| `arm_we`, `arm_lock` | in | write (nRW); LOCK (SWP) |
| `arm_ram` | in | 1: wave RAM, 0: AICA register |
| `arm_addr[20:0]` | in | wave RAM: `A & 0x1FFFFC` (RAM mirrors every 2 MB); register: `A & 0x7FFC` |
| `arm_be[3:0]`, `arm_wdata[31:0]` | in | byte lanes (nBW: one lane), write data |
| `arm_rdata[31:0]`, `arm_ack` | out | data and acknowledge, both in the cycle's last clock |

The address map follows ARM address bits A[23:0]. Addresses below 0x800000 are wave RAM; the rest are AICA registers at `A & 0x7FFF`.

Timing:

- **Start.** A cycle starts at the first DSP step boundary (every 4 clocks) at or after the request whose resources are free.
- **Resources.** The wave RAM slot matters for a RAM access; for TEMP and EFREG, the buffer's CPU port matters.
- **Length.** A cycle lasts 8 clocks, or 4 for a SWP's locked write. The next cycle can start the clock after the ack.
- **L and M** (0x2D00 / 0x2D04) are local and are acknowledged in the request's own clock.
- **Internal cycles** are clocks without a request.

The real ARM7DI announces a cycle's address one cycle early. A core can present the request in the cycle itself, which is what the contract describes, as long as it keeps the same cycle boundaries.

- **Clock.** Both sides run on one clock: MCLK = 22.5792 MHz = 512 per 44.1 kHz sample. That the ARM core runs at the bus clock was measured by wren7. `ce` freezes the AICA only; a combined build must freeze the core with it.
- **Interrupts.** nFIQ comes from caique's interrupt controller, which is v2. It follows wren7's measured handshake:
  - a level of 0 never interrupts;
  - reading L acknowledges and releases nFIQ;
  - writing M bit 0 re-arms.
  The one-sample interval edge is at `EDGE_PH` (ph 24).
- **Reset.** ARMRST (0x2C00 bit 0) is written by the SH4. The reset release is frame-synchronous on the console, but its phase against the sample edge is a console constant that is not known.

### Time base

wren7's `DcArmBus` puts the sample edge at t ≡ 0 (mod 512) and DSP step 0 at t ≡ 40. caique's DSP step 0 is at ph 64, so its edge is at ph 24. The harness uses `t_rtl = t_wren7 + 24 + 512 × WARM`, with WARM = 8 samples of warm-up after the job's register setup. It aligns ADPCM positions with `DcArmBus::adpcm_origin = 1 - WARM`.

## What caique uses from wren7

caique and wren7 are sibling checkouts (`dreamster/caique-rtl`, `dreamster/wren7-rtl`). `rtl/v1/Makefile` points at `WREN7 := ../../../wren7-rtl/model/src`. caique uses:

- **Model sources:** `model/src/arm7di.{h,cpp}`, `model/src/dc_arm_map.{h,cpp}` and `model/tools/hwjob.h`, compiled into caique's harnesses unchanged.
- **Console job suites:** `model/tests/hw/<suite>/jobs.txt` and the console's results in `model/tests/hw/<suite>/hw/`.
- **ARM kernels:** `wren7-rtl/build/hw/arm/*.bin`, built by wren7 (`make -C model/hw arm`, dca3 toolchain).
- **Functional checks:** wren7's `hw_suite` checks run on caique's readouts, for example `hw_suite check collide2 DIR`.

The combined tools run from `wren7-rtl/model`, because the job files' paths are relative to it.

## Combined tests

All run from caique. The last column gives the status on 2026-09-24.

| Command | What | Result |
|---|---|---|
| `make -C rtl/v1 arb` | random ARM access streams (RAM, registers, TEMP/EFREG, L/M, SWP) with random DSP programs, keyed channels and SH4 traffic, against `DcArmBus` cycle for cycle | 12 seeds, 0 mismatches (52 seeds swept) |
| `rtl/v1/tb/armjob_all.sh` | `Arm7DI` running every wren7 console job suite with the RTL as its bus. Each cycle's wait is checked against `DcArmBus`; each job's cycles and readback against wren7's own run. SH4 suites run at 8 stream phases | 19 suites, 3424 jobs, 0 wait mismatches in 143.6 M memory cycles |
| `build/rtl_v1/armjob_console tests/hw/SUITE TOL < out` (from `wren7-rtl/model`) | per timing kernel: console, RTL and wren7, in MCLK per iteration, as `hw_suite check_timed` computes it | the RTL is within tolerance wherever wren7 is |
| `armjob_tb JOBS -w DIR`, then `hw_suite check collide2 DIR` | AICA state read back after ARM jobs, with the ARM in reset | collide2 32/32 |
| `model/cases/dsp_coll.c` on the console, then `model/build/tools/coll_check` | channel/DSP slot collisions, logged sample by sample by the DSP | model = console on 18/18 runs; the RTL co-simulates it bit-exactly |
| `rtl/v1/tb/gate_cycle.sh` (and `CAIQUE_SEED=1..3`) | every model case run on the cycle model and co-simulated against the RTL through the SH4 port, clock for clock (no ARM) | 65/65: every ack clock, read and output sample identical |

In `armjob_all.txt`, "readbacks differ from the model" is expected for suites that read DSP results, such as collide and collide2. `DcArmBus` does not run the DSP. Compare those readbacks with the console captures instead.

## Change protocol

- **A wren7 change can affect the bus.** That covers the timing rules in `DcArmBus` / `DcWaits`, `Arm7Bus` / `bus_cycle`, and `hwjob.h`. Run `make -C rtl/v1 arb` and `rtl/v1/tb/armjob_all.sh` in caique after such a change.
- **A caique change can affect the bus too.** That covers the arbiter, the slot claims (SGC fetch rules, the DSP's MRD/MWT, the fixed pair) and the DSP memory path. Run the same two, plus `gate_cycle.sh`.
- **When the two disagree, one side is wrong, and console data decides.** Use the per-kernel costs from `armjob_console`, or write a new console case in caique. Example from 2026-09-24: the RTL's ADPCM fetch rule matched the console's kernels where `DcArmBus`'s spread rate did not. wren7 took the RTL's rule (`DcArmBus::adpcm_fetch`), and caique's `dsp_coll` then confirmed it sample by sample.
- **Console access.** Only one console run at a time, through shrike4's `hwrun.sh` (both repos' `run_hw.sh` use it).
- **Commits.** The repos are separate. A change that spans both is committed in both, and each commit message mentions the other.

## Known gaps

- **Interrupt controller, timers and ARMRST** are not in the RTL yet (v2). The harness uses `DcArmBus`'s controller.
- **ARM byte writes to AICA registers.** wren7 writes lanes 0 and 1 as 8-bit writes and drops lanes 2 and 3; the RTL writes 16 bits. The harness counts such writes per job.
- **The shared read latch.** With the ARM running, an ARM read can replace an even-step DSP read's result (wren7 NOTES, collide). Neither side models it.
- **LOCK exclusion** of the SH4 between a SWP's read and write is not measured, and neither side models it.
- **ARMRST phase.** Its phase against the sample edge is unknown; the harnesses put the edge at t = 0.
- **The wren7 RTL core.** Once it exists, `armjob_tb` swaps `Arm7DI` for it. The expected result is cycle-identical to the model run on every suite above.
