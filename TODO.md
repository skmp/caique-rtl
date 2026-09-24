# caique — session 9 todo (2026-09-24)

Goal: whole console programs reproduce byte for byte (sample and timing accurate), not only through the ring-locked
validators.  Items are worked in this order; each has its gate.  Status: `[ ]` open, `[~]` in progress, `[x]` done
(with the evidence).

## 1. Cycle-based model and sub-sample co-simulation

- [x] 1.1 The clocked model in `model/cycle-model/` (the sample model moves to `model/sample-model/`, `model/src/` keeps
      the shared DSP headers): `clock()` = one MCLK (22.5792 MHz, 512 per sample), `step()` = 512 clocks.  Same
      frame plan as rtl/v1: slot k's stage A in frame k (rows 0-1 sampled at c0, rows 2-4 at c5, fetch at c4, key
      events at c6), level k + 4, envelope pass k + 5, send k + 7, the MIXS queue, DSP step s at ph 64 + 4s (t0 fetch,
      t1 operand reads, t2 multiply, t3 writes), the bus arbiter with the SH4 and ARM ports.
- [x] 1.2 Backdoor accesses (at the current clock, like the RTL's frozen test port) keep every validator working:
      eg_model, eg_replay, tail_cmp, feg_validate, filt_validate_model, filt_overflow, validate_s5, coll_check.
      Gate: every case's output identical to the sample model's when its accesses are delivered at the boundary.
      Done: `cycle-model/replay_all.sh` 60/61 identical (sub_frame differs only by the pipeline);
      `CAIQUE_MODEL=cycle tools/validate_s5.sh` PASS.
- [x] 1.3 Host harness: every access goes through the model's SH4 port at a clock derived from the SH4 time; the
      trace records request clock, ack clock and data.  Done: `cycle-model/io_cycle.cpp`, `./run_cycle.sh`.
- [x] 1.4 Co-simulation through the RTL's SH4 port at the recorded clocks, engine never frozen; compare ack clock,
      read data and every output sample.  Gate: every case cycle- and bit-exact.  Done: `tb/gate_cycle.sh` 65/65 cases
      identical (ack clocks, reads, outputs); 72/73 after the later session-9 changes (adpcm_hi: TODO 6.1).
- [x] 1.5 Randomised access timing (every phase of the frame and the DSP step) in the co-simulation.  Done:
      CAIQUE_SEED 1 / 2 / 3: 65/65, 64/65 (a trace run lost to a mid-run model rebuild), 67/68 (adpcm_hi: TODO 6).
- [x] 1.6 Console check of the sub-sample effects: a DSP logger that records when marker writes land (frame
      resolution), writes bracketed by markers, and per-event prediction by the cycle model: register write vs the
      slot's frame, the KYONB window, the envelope of slot k computed in frame k of the previous sample, CPU MIXS
      writes/reads against the bank switch and the SGC's write.  Gate: every determinate event predicted.
      Done (NOTES "Cycle model and the per-frame schedule"): sub_frame 11333/11333, sub_env 2864/2864, sub_sched exp 1
      1459/1459, exp 2/3 0 fail, exp 0 (KYONB window) 1366/1369 -- open: 3 key-ons within a few clocks of the sample
      boundary came one sample late on the console.  tests/oneshot: the one-shot end (CA reads LEA for one sample,
      then 0; the envelope keeps running; a key-on waits for a key-off), console = model in every CA sequence.

## 2. Known DSP and ring state before every test

- [x] 2.1 `cases/aica_io.h`: `aica_reset(rbp, rbl)` (`aica_quiet()` = the default ring): ARM held, every channel keyed
      off and zeroed, then `dsp_reset` -- all 128 MPRO steps NOP (written high to low), COEF / MADRS / EFREG / TEMP /
      MEMS 0, the ring cleared and selected, the MIXS buses 0 (0x20 of slots 0-15 restored) -- and every slot's LP
      flag cleared (an EG monitor read per slot: tests/oneshot's first read saw LP 1 left by an earlier program).
- [~] 2.2 Every case starts with it (the DSP cases with their own ring: `aica_reset(RBP_BYTE, rbl)`), and every capture
      / log start (`cap_start`, `flog_start`) runs `dsp_reset`; before, `cap_start` zeroed only the steps this run had
      loaded, so a longer program of an earlier case (flog: 128 steps) stayed in the upper steps.  The DSP cases' own
      sub-tests keep loading their programs through `prog_load` (which zeroes every step the run used) and set their
      rings themselves.  Every case's model output moves (the start phase moves): all 65 exit 0 on the sample model.
      Console re-run into `tests/<case>/hw9/` (with the preamble of item 3): in progress.

## 3. Replay parameters recorded by every console run

- [x] 3.1 Model parameters: MDEC_CT, the envelope counter constant K (and the MDEC_CT parity its clock ticks on: a
      0x2804 bit-15 write can flip it, item 7.1) and the noise LFSR, set in both models at the
      preamble's sync point (`CAIQUE_REPLAY=<replay.txt>`, cycle-model/io_cycle.cpp, host/io_model.cpp); rtl/v1 loads
      them through `ld` / `ld_mdec` / `ld_lfsr` in the clock before that sample's ph 0 (the cycle trace's 'P' record;
      co-simulation of oneshot and sgc_formats with a synthetic replay, also with odd parity: 0 mismatches).  hw9: all
      66 cases fit uniquely (50 K 0 even, 16 K 10923 odd, after tests/probe); both models given the console's odd-parity
      replay_check parameters measure them back at the second sync.
- [x] 3.2 Console preamble (`cases/common/replay.c`, run by every platform's main before `test_main`, ~1.1 s): a
      4-stream capture -- decay 1 at R 3 / 13 / 45 for K, a noise slot for the LFSR -- ending at a sync point (the SH4
      waits for the next counter word).  `tools/replay_fit` writes `replay.txt` ("mdec X lfsr L K k"); `run_hw.sh` runs
      it after every case.  Console (tests/replay_check): K = 0, one LFSR state fits all 43728 noise samples under the
      model's convention (so the console's noise LFSR is x^17 + x^12 + 1 at 64 steps per sample), and a second
      measurement 62187 samples later is consistent with the first (`replay_fit -same`).
- [x] 3.3 The model harnesses apply `replay.txt` at the same program point (`run_model.sh` / `run_cycle.sh` take
      `tests/<case>/<REPLAY_FROM>/replay.txt`, default hw).  tests/replay_check: both models, given the console's
      preamble parameters, measure the console's K and LFSR / MDEC_CT relation at the second sync point.
- [ ] 3.4 Console access timeline: every console run records its accesses with SH4 timestamps, synchronised to the
      sample edge (MCIPD bit 10); the model replays the console's own timeline, reads compared one by one.  Gate:
      whole-program outputs byte-identical.  (Until then the models' harness timing differs from the console's after
      the sync point: captures align by their own MDEC_CT, and K and the LFSR relation carry over.)
      First user: tests/oneshot, whose remaining console/model differences are slow-rate envelope steps (D1R 12, AR 6)
      at the boot's K and phase.

## 4. Timers and the interrupt controller

- [x] 4.1 Console (SH4 side): tests/timer_irq (storage, SCIPD / MCIPD separate, bit 5 settable, the line, the edge
      spacing, overflow intervals for every timer at P 0-5), tests/timer_phase (the prescaler's MDEC_CT phase with the
      frame logger), tests/irq_bit9.  NOTES "Timers and interrupts".  Open: what sets pending bit 9.
- [x] 4.2 Both models and rtl/v1 (aica_bus: registers, timers at the sample edge, `sh4_irq`); `tim_phase` a parameter
      (20 mod 32 in the hw9 session).  `tools/timer_irq_check`: console, cycle model and sample model all pass the
      rules; co-simulation clean on timer_irq, timer_probe, timer_phase, probe, eg_kprobe, k_jump.  timer_probe:
      console = model except the timer A / B overflows that fall within the case's harness-timing difference (3.4).
- [ ] 4.3 The ARM side: FIQ from SCIEB & SCIPD through SCILV0-2 and the L / M handshake (wren7 tests/hw/fiqdiag) in the
      models and rtl/v1 (nFIQ output), co-simulated with wren7's ARM7DI.

## 5. Full sample-exact coverage of the pitch LFO, sgc_aeg, sgc_pitch and sgc_formats

The tool: `tools/stream_replay` replays each captured stream through the cycle model from the case's logged slot
register images (`slot_log`), RAM files and capture line, with MDEC_CT, K, the envelope clock parity and the LFSR from
the run's replay parameters, the key-on sample searched (and with -lfo the LFO state and counter at the key-on).
Console runs: hw9 (the cases instrumented with `slot_log` / `ramfile`).
- [~] 5.1 PLFO / ALFO: every stream of sgc_lfo (24), sgc_lfo2 run 0 (4: other base pitches) and lfo_noise (8)
      matches every sample with the stream's LFO (state, counter) fitted.  Fixed on the way: the PLFO triangle peak,
      the ALFO / PLFO noise bytes, the LFO running free (NOTES "LFOs", "The noise LFO bytes").  Open: the counters'
      phase at a case's start is unknown per-slot state (a replay parameter or a known-state procedure would be needed
      for whole-program replay); every base pitch (OCT -8..7) x waveform x depth not yet swept.
- [x] 5.2 sgc_aeg: all 44 streams of the 11 runs match every sample up to the key-off window (the release after it
      not compared yet).  (Earlier: 39/44 by a phase-independent comparison.)
- [~] 5.3 sgc_pitch: all 16 streams match every sample; sgc_formats: all 8 (PCM8, ADPCM, ADPCM long, noise -- the noise
      with the run's LFSR, from before the key-on).  Open: a regression gate that runs these replays.
- [ ] 5.4 sgc_lfo2 run 1 (the FEG through the filter): the slots' filter state before the key-on is unknown (left by
      earlier programs; aica_reset cannot clear it): the replay needs a filter-state search (as tail_cmp's stage 0) or
      a known-state procedure.

## 6. ADPCM at OCT >= 3 and the ARM read latch

- [~] 6.1 Console: what an ADPCM slot outputs at OCT 3..7 (and PCMS 3, odd start addresses); model and RTL.
      Measured (tests/adpcm_hi, adpcm_pitch; NOTES "ADPCM above 4 nibbles per sample"): exact up to 4 nibbles a sample;
      above, the console's decoder runs out of fetched nibbles (one 16-bit word a sample); OCT 3..6 hold the key-on
      value; OCT 7 FNS 3FF moves slowly; an odd SA starts on the other nibble.  Next: the decoder's word buffer rule
      from structured nibble patterns (a ramp of nibbles instead of random bytes), then model and RTL.
- [ ] 6.2 Console: which ARM read replaces the DSP's read latch (wren7 collide); model and RTL.

## 7. Future phase (found on the way, not investigated)

- [ ] 7.1 0x2804 bit 15 (probably TESTB0): every write with it set moves MDEC_CT against the envelope counter (K and
      the envelope clock's MDEC_CT parity change; tests/probe does it, tests/k_jump isolates it: NOTES "Known start
      state and replay parameters").  Which counter moves, held reset or edge, what else it affects, and whether it
      explains K 6491 on the earlier boot.  Until then: the replay parameters carry the parity ("par p").
- [ ] 7.2 The PLFO noise byte is 67 LFSR steps (one sample and 3 frames) before the slot's stage A (the ALFO's is 3
      frames after it, at the level stage).  Candidate: the phase increment is computed a sample ahead, in a pitch
      stage 3 frames before stage A (or the phase accumulator running ahead of the interpolation: SA's own-sweep
      threshold rules out the whole fetch being a sample ahead).  Tests (NOTES "The noise LFO bytes"): sub_frame-style
      thresholds for 0x18 and 0x1C writes (a sample + 3 frames vs 3 frames before SA's); one slot with both ALFO and
      PLFO on; the increment of the first sample after a key-on.
- [ ] 7.3 When a slot reads wave RAM (SDRAM): find each fetch's clock by racing it.  An SH4 (or ARM) write replaces a
      sample word the slot is about to play, at a clock bracketed by frame-logger markers (cases/flog.h, as
      sub_frame / sub_sched); the output shows whether the old or the new word was used, and narrowing the window over
      many events (every slot, pitches below / at / above 1, PCM16 / PCM8 / ADPCM, loop wraps, the key-on sample) pins
      the fetch clock to a frame.  Tells whether any sample is pre-fetched (read before the frame the model fetches
      it in, or a sample ahead -- ties in with 7.2's phase accumulator), whether one fetch serves two samples (the
      ADPCM word buffer of 6.1), and the fetch of the key-on sample.  Known so far: slot K's wave RAM slot is DSP step
      2K - 14 (wren7 tests/hw/sgc), the word it leaves holds sample CA + 1 (tests/dsp_coll), the first fetch after a
      key-on is in the sample that outputs CA 0; the model places the fetch at frame k (stage A) and the SA register
      is read there (tests/sub_frame).  Alternative approaches if the write race is too coarse: a DSP MWT at a chosen
      step into the sample word (the DSP's step is known to the clock), or the ARM (4-MCLK grid) as the writer.
