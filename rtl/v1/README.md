# caique rtl/v1 — the AICA sound core

rtl/v1 is a clock-accurate RTL model of the AICA's sound half. It contains the 64-slot sound generator (SGC), the effect DSP, the register map, the output mixer, and the wave RAM / register arbiter for the SH4, the ARM7DI and a test port. It runs on the AICA bus clock, 22.5792 MHz = 512 clocks per 44.1 kHz sample. Every rule it implements is a transcription of the clocked model `model/cycle-model/aica_model.cpp`, which is validated on the console (`model/NOTES.md`), down to the frame in which each register is read (`model/NOTES.md`, "Cycle model and the per-frame schedule"). The arbiter follows wren7's console-measured bus model (`wren7-rtl/model/src/dc_arm_map.cpp`, `DcWaits::dreamcast()`).

| File | Contents |
|---|---|
| `aica_pkg.sv` | frame schedule constants (`DSP_OFFSET`, `EDGE_PH`, `FIX_STEP0`), the combinational laws (envelope rates, send levels, ADPCM, LFO waves, DSP pack/unpack) |
| `aica_ram.sv` | the one RAM primitive (registered read, write lanes, `re` hold); every buffer is one of these |
| `aica_sgc.sv` | one slot engine, time-multiplexed over the 64 slots: fetch, decode, level, envelope and send stages |
| `aica_dsp.sv` | the DSP: 128 steps of 4 clocks; also reports the step's wave RAM / buffer-port owners to the arbiter |
| `aica_bus.sv` | register map, the two-lane access executor, the arbiter, the ARM / SH4 / test ports |
| `aica_core.sv` | phase counter, MDEC_CT, the shared multiplier, the wave RAM clock owners, the output mixer |
| `tb/` | `tb_top.sv` (core + behavioural 2 MB RAM), `cosim_cycle.cpp` + `gate_cycle.sh` (clock-level co-simulation with the cycle model), `cosim.cpp` + `gate_all.sh` (the session-8 sample-boundary co-simulation), `arb_tb.cpp` (ARM port vs wren7), `armjob_tb.cpp` + `armjob_all.sh` + `armjob_console.cpp` (wren7's ARM7DI running the console job suites on this RTL), `vcdq.cpp` (VCD query) |

## Frame plan

`ph` counts 0..511 within a sample. Frame k (`ph[8:3]`, 8 clocks) belongs to slot k, whose work runs through a pipeline of stages in later frames. The frame in which each stage reads its registers is the console's (`model/tests/sub_frame`, `sub_sched`, `sub_env`): a CPU write acts in this sample when it lands before that frame starts.

- **Stage A, frame k.** It reads slot k's registers 0x00–0x1C and state, advances the phase and the loop, and fetches the wave words. It also advances the LFO, commits a pending stop, and applies the key events (KYONEX latched at the sample boundary, KYONB as read in this frame).
- **Stage B, frame k+1.** It decodes (PCM, ADPCM or noise) and interpolates, into FIFO 1.
- **Level, frame k+4.** The filter (Q, LPOFF) and the level (TL, VOFF, AEG, ALFO), with 0x28 from a copy read at c7 of frame k+3, into FIFO 2.
- **Envelope pass, frame k+5.** The envelopes of the next sample, with 0x10/0x14/0x18/0x30–0x44 from a copy read at c7 of frame k+4 and the clock of the slot's own sample (recorded by stage A's key events). State and EG monitor are written at c7.
- **Send, frame k+7.** ISEL/IMXL and the direct sends (DISDL, DIPAN), with 0x20/0x24 from a copy read at c7 of frame k+6. The MIXS write lands at c7 of frame k+8, so a sweep's MIXS writes fill exactly one DSP sample window (slot 0 at ph 71, slot 63 at ph 63 of the next sample). The bank the DSP reads is therefore never written while it reads it. The CPU's MIXS port uses the DSP's bank, so the CPU never sees a partial sum. The console confirms this: MIXS0 read about one sample apart, at every phase, always shows the whole value (wren7 `tests/hw/collide2`), and `model/tests/sub_sched` finds the CPU's MIXS writes and reads in the DSP's bank.

Slots 60–63's level, 59–63's envelope pass and 57–63's send run in the next sample's first frames.

In the table, B is stage B's slot (frame − 1), L the level stage's (frame − 4), E the envelope pass's (frame − 5) and S the send's (frame − 7).

| c | engine (stage A) | stages B / L / E / S | multiplier | wave RAM | DSP step (t) | bus |
|---|---|---|---|---|---|---|
| 0 | row 1 read, state A latched | B: ADPCM steps 1–3, decode | damping (L) | the slot's own step: its last word | even t0 | step boundary: requests sampled |
| 1 | row 0 read | B: ADPCM steps 4–6, noise override | PLFO (A) | DSP read | even t1 | grants (SGC claim of slot k−1) |
| 2 | (CPU register port) | B: ADPCM steps 7–8 and s1 | DSP even t2 | bus RAM lane | even t2 | X0: both lanes issue |
| 3 | row 4 read, stepping latched | B: interpolation | interpolation (B) | DSP write, even step | even t3 | X1: data back |
| 4 | row 2 read | L: k·H | k·H (L) | SGC fetch 1 | odd t0 | step boundary |
| 5 | row 3 read | L: k·B | k·B (L) | SGC fetch 2 | odd t1 | grants |
| 6 | key events, state + CA monitor write | (CPU register port) | DSP odd t2 | bus RAM lane | odd t2 | X0 |
| 7 | state read for the next frame | B: state, FIFO 1; L: level, filter state, FIFO 2; E: envelope pass, state + EG monitor; S: send, the previous send's MIXS write; register copies read | level (L) | DSP write, odd step | odd t3 | X1 |

The hardware is reused as follows:

- One 25×13 multiplier serves both the SGC and the DSP.
- One RAM primitive type holds every buffer. The slot state RAM's two ports are time-multiplexed: stage A's read at c7 and write at c6, the envelope pass's read at c6 and write at c7.
- One register port on the channel RAM serves the engine on five clocks and the CPU on the other three.
- The wave RAM does one access per clock and returns two consecutive words ({word(a+1), word(a)}), as two 16-bit banks would.

**The DSP** runs 128 steps of 4 clocks. Step s occupies ph `DSP_OFFSET + 4s` (mod 512) with `DSP_OFFSET` = 64, so even steps fall on c0–c3 and odd steps on c4–c7. MDEC_CT decrements at the DSP's sample boundary. The DSP reads the MIXS bank the slots filled in the previous sample. The output mixer takes the DSP's EFREG the clock after that boundary and the direct sum latched at ph 8. It emits one output pair 18 clocks later.

**Replay parameters (simulation).** `eg_k` and `eg_par` (the MDEC_CT parity the envelope clock ticks on) are inputs; `ld` in the clock before a sample's ph 0 loads MDEC_CT (`ld_mdec`) and the noise LFSR (`ld_lfsr`), so a co-simulation can start the RTL in a console run's state (`model/cases/common/replay.c`, the trace's `'P'` record).

**Phase anchors.** Two console measurements fix where the DSP sits relative to the sweep:

- The console gives slot K's fetch the wave RAM slot of DSP step 2K − 14 (wren7 `tests/hw/sgc`).
- Here slot K claims the step of its stage B frame (c0–c3 of frame K+1). That puts DSP step 0 at ph 64.
- The one-sample interval edge (SCIPD bit 10) is 40 clocks before DSP step 0 (wren7 `tests/hw/dspport`, `blkphase`). That places it at ph 24 (`EDGE_PH`), for the v2 interrupt controller.

## Arbiter and master ports

The console's arbitration unit is the DSP step (4 clocks):

- **Channel and DSP collisions (console: `model/tests/dsp_coll`, wren7 collide2).** In a playing channel's slot the DSP's MWT is dropped. An MRD there returns the channel's word, the 16-bit word holding its sample CA + 1. Stage B reads that word at c0 of the slot's own step. The first fetch after a key-on is in the sample that outputs CA 0.
- **Wave RAM has one shared slot per step.** A DSP MRD or MWT of that step holds it, on any step, odd or even. So does slot K's fetch on step 2K − 14. PCM16 and PCM8 fetch every sample while the slot plays, and noise never fetches. ADPCM holds one 16-bit word: it fetches when the new position's word differs from the old one's, or when the look-ahead nibble lies in the next word, and never at OCT 3–7. That rule gives the console's measured shares exactly (5/16, 3/8, 1/2, 1/2, 1, 0) and its per-kernel costs within 0.04 MCLK; wren7's `DcArmBus::adpcm_fetch` now uses it too.
- **The fixed pair** holds steps 109 and 111. Where the DSP holds one of those, it moves one step earlier or later, and the two stay at least 2 steps apart. `aica_dsp` reads MPRO rows 108–112 ahead, on the MPRO port's free clock at t3 of steps 100–104, and places the pair with wren7's rule (`fix_place`).
- **The SH4, then the ARM,** get the free slots. An SH4 write takes one slot. An SH4 read takes two slots at least 2 steps apart and returns data from the first.
- **Registers need no wave RAM slot.** A TEMP (0x4000–0x43FF) or EFREG (0x4580–0x45BF) access waits while the step's TWT or EWT holds that buffer's CPU port. The ARM wins the register lane over the SH4, because SH4 register traffic costs the ARM nothing on the console.

The executor has two lanes, RAM and register, so one step can serve an SH4 wave RAM write and an ARM register access together. A request present at t0 competes for that step. Grants are decided at t1 from the step's owners (`slot_dsp`, `slot_fix`, `slot_sgc`, `port_temp`, `port_efreg`). Both lanes issue at t2 and return data at t3.

### ARM7DI port contract (for the wren7 core)

- `arm_req` goes high in the clock the core wants to start a memory cycle. `arm_addr`, `arm_we`, `arm_ram`, `arm_lock`, `arm_be` and `arm_wdata` are held until `arm_ack`.
- The cycle starts at the first step boundary (ph ≡ 0 mod 4) at or after the request whose resources are free. It lasts 8 clocks, or 4 for a SWP's locked write.
- `arm_ack` is high in the cycle's last clock, and `arm_rdata` is valid in that clock. The core may start its next cycle in the following clock.
- Internal (I) cycles are simply clocks without a request.
- L and M (0x2D00 / 0x2D04) are local to the ARM interface. They take no slot and are acknowledged in the request's own clock, combinationally. In v1, L reads 0 and M writes are ignored.

```
clock     S (t0)   S+1 (t1)  S+2 (t2)  S+3 (t3)  S+4 .. S+6   S+7          S+8
arm_req   1        1         1         1         1            1            next request / 0
          sampled  granted   X0 issue  X1 data                ack, rdata   next cycle may start (t0)
```

The integration with wren7 (ownership, interfaces, combined tests, change protocol) is in `../../INTEGRATION.md`.

`arb_tb` checks this contract against wren7's `DcArmBus` linked in unchanged, cycle for cycle. Each run uses random DSP programs (MRD/MWT/TWT/EWT), keyed PCM channels at random pitches, SH4 wave RAM traffic, and random ARM streams (RAM, registers, TEMP/EFREG, L/M, SWP pairs, idle gaps).

## Verification

```
make lint                  # Verilator -Wall on aica_core
make run-cycle CASE=<case>            # run a case on the cycle model, co-simulate it through the SH4 port
tb/gate_cycle.sh [-j N] [case ...]    # every case in model/cases -> build/rtl_v1/gate_cycle.txt
CAIQUE_SEED=n tb/gate_cycle.sh        # the same with every access delayed by a seeded random 0..2047 ns
make run CASE=<case> / tb/gate_all.sh # session 8: the sample model's accesses at the sample boundary, engine frozen
make arb                   # the ARM port against wren7's measured bus model, 12 seeds
tb/armjob_all.sh [-j N]    # every wren7 console job suite on wren7's ARM7DI model with this RTL as its bus
build/rtl_v1/armjob_console tests/hw/SUITE TOL < out    # per kernel: console vs RTL vs wren7 (from wren7-rtl/model)
make cosim-vcd / arb-vcd   # traced builds; query with build/rtl_v1/vcdq <vcd> sig,sig,...
```

**The co-simulation contract (`cosim_cycle`).** The cycle model runs the case; its host harness puts every SH4 access on the model's SH4 port at a clock derived from the SH4 time, and the trace records the request clock, the ack clock and the read data. The co-simulation drives the RTL's SH4 port with the same requests at the same clocks, the engine never frozen, and compares the ack clock and data of every access and every output sample. `CAIQUE_SEED` adds a random delay to every access, so accesses land at every phase of the frame and the DSP step.

The session-8 contract (`cosim.cpp`: the sample model's accesses replayed at the RTL's sample boundary, engine frozen, at ph 0 and ph 64) predates the output pipeline. Between boundaries the sample model no longer describes the RTL, so `gate_cycle.sh` is the gate.

Status on 2026-09-24:

- **Co-simulation.** All 65 cases in `model/cases` are identical to the cycle model through the SH4 port: every ack clock, every read and every output sample. With random access delays (three seeds) as well: seed 1 65/65, seed 2 64/65 (one trace run lost to a model rebuild during the run), seed 3 67/68 with the reset and replay preamble in every case (adpcm_hi: ADPCM above OCT +2, outside the stepping limits, TODO 6).
- **The cycle model against the console.** Every register-stage frame above, the KYONB window and the CPU's MIXS accesses are measured by per-event replays (`model/NOTES.md`): sub_frame 11333/11333 determinate events, sub_env 2864/2864, sub_sched 5363 of 5366 (3 key-ons near the sample boundary are open). The one-shot end is `model/tests/oneshot`.
- **ARM port.** 52 random `arb_tb` seeds match wren7's model on every cycle.
- **Real ARM programs.** wren7's ARM7DI model (`Arm7DI::bus_cycle`) ran all 19 console job suites (3424 jobs) with this RTL as its bus. Every job ends on the same clock as with wren7's `DcArmBus`, and all 143.6 M memory cycles have the same wait. That includes SH4 streams at 8 phases, FIQ-timed probes, ADPCM channels and DSP programs. (Session 8; not re-run after the session-9 SGC changes, which do not touch the bus.)
- **Functional readouts.** wren7's collide2 readouts pass 32/32 with the ARM in reset.
- **Channel and DSP collisions.** The console case `model/tests/dsp_coll` has 18 runs, and the model matches the console sample for sample. The RTL co-simulates the case bit-exactly.
- **Controls.** Removing the fixed pair or the TEMP/EFREG port rule makes every seed fail. Reading MIXS from the bank being filled fails all 12 channel cases of collide2.

Details are in `model/HANDOVER.md`, "Session 8 addendum" and "Session 9 addendum".

## Known approximations

- **Stepping limits.** ADPCM steps up to 8 nibbles per sample, which covers OCT +2 (the console stops an ADPCM channel at OCT +3). It allows one loop wrap per sample, and the nibbles must fit the two fetched words. PCM and noise above pitch 2 use a closed form with at most one loop wrap per sample. Every case outside these limits raises `warn_step`.
- **Filter state** is 25 bits.
- **The fixed pair look-ahead** reads MPRO rows 108–112 at steps 100–104. A CPU rewrite of those rows later in the sample takes effect in the next sample. wren7 applies it at once.
- **Shared read latch (measured, not implemented).** With the ARM running, an ARM read can replace an even-step MRD's result (wren7 NOTES, collide). The RTL keeps the DSP's own word.
- **The KYONB window edge.** 3 of 1369 console key-ons whose KYONEX landed within a few clocks of the sample boundary started one sample later than the model says (`model/tests/sub_sched` exp 0).
- **Registers timed only through their stage.** 0x24, the ALFO bits of 0x1C, FLV and the FEG rates, Q and the monitor update points are read where their stage reads its measured neighbours; no capture isolates them.
- **ADPCM at OCT 3 and above.** The console fetches nothing, and the RTL claims no slot. The RTL still plays the channel, and what the console outputs there is not measured.
- **SWP lock.** LOCK does not keep the SH4 out of the wave RAM between a SWP's read and its write. wren7's model has no exclusion either, and the console behaviour is not measured.
- **Mixer.** The output mixer's level law and its EFREG snapshot time cannot be observed digitally. It is the model's law, unverified.

## v2

- **The ARM's FIQ.** The timers, SCIEB/SCIPD/SCIRE, SCILV0–2 (stored write-only), MCIEB/MCIPD/MCIRE and the SH4's `sh4_irq` are in v1 (aica_bus; `model/NOTES.md` "Timers and interrupts"). v2 adds the FIQ delivery: the lowest pending enabled bit's level from SCILV0–2 and the L/M handshake of wren7 `tests/hw/fiqdiag` (reading L releases nFIQ, writing M bit 0 re-arms), with an nFIQ output for the ARM core.
- **ARM byte writes to registers.** wren7 writes lanes 0/1 as 8-bit writes and drops lanes 2/3. The register lane currently writes 16 bits.
- **EXTS input** (CD-DA).
- **Synthesis.** A Quartus fit on the DE10 is run by the user.
