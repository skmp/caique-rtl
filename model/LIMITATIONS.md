# caique AICA model — limitations and sticking points (2026-09-23, after session 6)

Everything the DSP capture path can observe is reproduced sample-exactly on this console boot: filter arithmetic
(265 streams), DSP, levels, pitch, formats, loops, the envelope clock and its counter, key timing, every key-off-clock
rule (AEG and FEG), the slot stop, the MIXS writer and readback semantics (see [HANDOVER.md](HANDOVER.md) T1-T10 and
U1-U4, gate `tools/validate_s5.sh`).  This file lists what is NOT nailed down, grouped by kind, so nobody mistakes a
documented gap for a verified fact.  Each item names the evidence that bounds it and what would close it.

## 1. Sub-sample effects the model cannot express

The model steps whole samples (`AicaModel::step()`), and register writes are applied between steps.  The hardware
processes a sample in phases, and a write's position inside the sample period matters in three measured places.

- **Register writes vs the envelope phase** (tests/eg_latch, eg_latch2, eg_latch3; tests/slot_tail tail_c).  Within a
  sample the envelope update runs before the sample fetch/output; key events (KYONEX) always take effect from the next
  sample; a register write landing before a sample's envelope phase is seen by that sample's envelope clock, one landing
  after it only by the next sample's clock, while the fetch sees it in the sample it lands in.  All envelope registers
  are live (359 informative rewrites, 0 latched), but tail_c's RR/SA group landed between the two phases: the SA switch
  shows at sample 11300, the first release step at 11302.  The model reads every write "live", so `tools/tail_cmp`
  applies that RR write one sample after the SA write (`w14 = w00 + 1`) to reproduce the capture.  Any program that
  rewrites an envelope rate in the same sample as the envelope clock may be off by one clock in the model.  The RTL
  should order the envelope update before the fetch and latch key events at the sample boundary.
- **CPU MIXS write vs the SGC write of the same bus** in the same sample (tests/dsp_basic "F ira 25", excluded from
  the comparison since session 1; tests/eg_lock mixs, tests/mixs_rd R7).  Which of the two lands first decides what
  the DSP reads on the next sample.  Not modelled; the model's CPU write goes to the bank the DSP reads next.
- **MIXS readback bank switch of the low nibble ~5 us before the high word** (tests/mixs_rd R4/R7: `1234/5 -> 1234/0
  -> 0000/0`).  The model switches both halves at the sample boundary.  One line of tests/probe (MIXS0.l
  `w55555555->r00000000` vs the console's `r00000005`) is a model sample boundary falling between the write and the
  read; the console shows the same effect the other way in mixs_rd R1 (`1234/0` on the first read).

## 2. Empirical rules with no mechanism behind them

- **The AEG's exponential attack takes no step on a key-off clock** while every linear segment (AEG decay 1/2, FEG
  attack/decay 1/decay 2) takes exactly the step it would have taken without the key-off, including a segment advance
  due on that clock (tests/aeg_koff koff_att 27/27; koff_d1 18/18, koff_d2(b) 48/48; feg_koffdir, feg_koffatt,
  feg_koffpass).  The data is clean; why the attack differs is a guess (the attack step is not an additive increment).
  Modelled as a special case in `aeg_clock`.
- **Rows 5/9/13 of the increment table** are {b,2b,b,b,b,2b,b,b} (double step at index 1 and 5) where the YM2612 has
  it at 3 and 7 (tests/eg_lock odd_*).  Measured, not derived.
- **The R < 48 rows see the clock counter one step behind the R >= 48 rows** (`slow_off -1`).  On AEG-only data it is
  indistinguishable from K - 1; it is pinned by the joint AEG + FEG fit only (tests/feg_track + eg_lock).

## 3. Details resolved to one or two samples, not exactly

- **Stop = off at a = 0x3C0** (tests/slot_tail, ca_stop).  The fetch stop is sample-exact (one sample after the
  clock, controls at 0x3BF / 0x3C1 / 0x400 and lag 0 / 2 all break streams).  The monitor's 0x1FFF flag and the CA
  reset were timed by polling only: the flag appears within one poll (~31 us) of the 0x3C0 reading and CA reads 0 one
  poll after that, so the CA reset follows the flag by at least a few microseconds.  The model puts both on the sample
  after the clock.  A sample-exact measurement would need the CA to be observable in a capture, which it is not.
- **Whether the attenuation keeps stepping from 0x3C0 to 0x3FF after the stop** rests only on the sign pattern of
  tail_b's VOFF 0 output (-16 / 0 with the filter tail's sign; `floor(x * M / 2^22)` is -1 for any negative x while
  |x| < 2^22 / M, which is 33k..65k depending on M).  The model keeps stepping to 0x3FF; a "frozen at 0x3C0" variant
  (M = 127 throughout) was not re-run as a control after the stop and off were merged.  The difference is observable
  only if a tail sample falls between -65536 and -33026 while a is above 0x3C0.
- **Key events on the sample after the write** (S2) and the **key-off clock rules** were pinned exactly by the witness
  slot; the earlier monitor-based statements (key-on during release "on the next clock", "CA reads 0 at 0x400") were
  wrong by a clock and are superseded.  Any remaining monitor-only claim (sgc_keys K1-K5, feg_probe) has that
  resolution.

## 4. Constants the model cannot derive

- **K = 6491** is this boot's value (bit 13 = 0, tests/eg_kprobe).  Nothing a program does changes it (RBP/RBL,
  timers, MVOL, ARM7 release, DSP load, register sweep); what sets it at power-on or reset is unknown (sgc_loop, from an
  earlier boot, had K = 165 mod 1024).  A fresh boot needs a refit (`tools/kfit -case tests/eg_kprobe/hw`) before any
  envelope replay is trusted.  `AicaModel::eg_K` is a constant 6491.
- **Inherited state.**  The console inherits filter states and retained bus values across programs (tests/mixs_rd R2:
  bus 5 still carried dsp_basic's 0x1234/5 from an earlier session; filt_id2 / filt_coef startup transients).  The model
  starts clean; the replay tools fit the inherited filter state (tools/tail_cmp stage 0) and the validators use the
  ring position, so whole-program model runs differ from console runs in the first few hundred samples of some cases.
- **Whole-program key-on phase.**  The host harness's SH4 timing (2.4 us per access) decides where a KYONEX lands, so
  whole-program model runs key on at their own phase; sgc_krs / sgc_keys / probe monitor-timing text differs from the
  console by design.  Only the ring-locked replays (tools/eg_model, eg_replay, tail_cmp) are sample-exact.
- **Noise LFSR power-on phase** (tests/sgc_formats): the sequence is exact, the offset differs per boot.

## 5. Not observable from the SH4, or not measured

- **Analog side**: DISDL/DIPAN (direct outputs), EFSDL/EFPAN (DSP output mixer), MVOL, DAC18B, MONO and the DAC itself
  only reach the analog output.  Modelled with the measured send-level law, unverified.
- **EXTS** (CD-DA input) reads 0 without a disc; **MEM8MB** was deliberately never written (it could change the wave
  RAM mapping for the rest of the boot).
- **ALFO's contribution to the attenuation** in the level law (NOTES "Slot levels": unmeasured), **PLFO at other base
  pitches** (OCT != 0, FNS overflow past 0x3FF), the LFO counter's relation to the envelope clock.
- **LPSLNK on a live rewrite**, the FEG behaviour after a **target (FLV) rewrite** beyond the few clean flv events of
  tests/eg_latch (most events were "neither": the written target landed at or below v), the **FEG rate registers under
  KRS** (KRS was measured through the AEG and through the FEG's fixed programs, not by a live KRS rewrite on the FEG).
- **Timer counters** are write-only (tests/timer_probe); interrupt behaviour and MIDI were never exercised.
- **ADPCM** is verified on the tested streams (including the long-stream loop over 4096 nibbles); corner cases such
  as odd start addresses or PCMS 3 without a loop were not swept.

## 6. Harness artefacts (not hardware)

- **cap_start head-estimate shift.**  `cases/cap.h` finds the capture head by scanning the ring; the scan's cost
  depends on how many non-zero data words the previous run left in the ring (every slot writes its bus every sample, so
  stopped slots leave their filter rest values).  A model change that alters those rest values moves the head estimate
  by a sample and shifts every sample index of the saved capture (`.hdr` n_first and marks, `.bin`), although the content
  aligned by ring position is identical (session 5: feg_krs / feg_track / feg_probe model captures shifted by one).
  Consequence: `work/model_outputs_*.sha256` differences on capture-bearing cases must be checked with the ring-locked
  tools before being called a regression.  Fix if wanted: clear the data regions before the scan, or anchor n_first to
  a counter word instead of the timed head.
- **cap_mark values are head estimates** that lag or lead the DSP by 30-100 samples and drift with the SH4 timer;
  every fitter uses them only as search windows and pins events with a witness slot or a level jump.
- **MDEC_CT drift against the SH4 clock** is +-90 samples over 1.7 s (tests/eg_kprobe): the two crystals differ, so
  no SH4 timestamp is sample-exact.

## 7. Known console-vs-model text differences (all attributed)

| Case | Lines | Why |
|---|---:|---|
| dsp_basic | 2 | "A ffff -4096 1" (a program-load transient of that console run, never reproduced); "F ira 25" (item 1: CPU vs SGC write order) |
| sgc_level | 1 | L5 filter line: the console's low state is inherited, the model's settles one LSB below (both inside the DC deadband) |
| probe | 1 | MIXS0.l nibble: a sample boundary between write and read (item 1) |
| sgc_krs, sgc_keys, ca_stop, probe monitor lines | many | monitor-poll timing and the whole-program key-on phase (item 4) |

Everything else compared in [tests/SUMMARY.txt](tests/SUMMARY.txt) matches.
