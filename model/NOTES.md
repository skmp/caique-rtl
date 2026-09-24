# caique AICA model — findings log

Everything measured on the console lives under `tests/<case>/hw/`, the model's run of the same case under
`tests/<case>/model/`. A test case (`cases/<case>.c`) is written once against `cases/aica_io.h` and built for both
(`hw/` KOS back-end, `host/` model back-end; every executable is built under `build/`, git-ignored). Run: `./run_hw.sh CASE...` (console, through shrike4 `hwrun.sh`) and
`./run_model.sh CASE...` (model), then diff the two output directories.

Model: `sample-model/aica_model.{h,cpp}` (one `step()` per sample; the clocked model is `cycle-model/`, NOTES "Cycle model"). Baseline = minicast `libswirl/hw/aica` (SGC port, DSP interpreter). Deviations from
minicast are listed below with the test that forced them.

## Integration with wren7 (the ARM7DI) — [../INTEGRATION.md](../INTEGRATION.md)

caique integrates wren7-rtl, the AICA's sound CPU: the ARM sits on the AICA's bus, which caique owns (the arbiter and
the ARM port in `rtl/v1/aica_bus.sv`).  **Every combined test lives here**: co-simulation of wren7's `Arm7DI` with
rtl/v1 (`rtl/v1/tb/armjob_tb.cpp`, `armjob_all.sh`), the arbiter against wren7's measured `DcArmBus` (`make arb`),
AICA readouts after ARM jobs (`armjob_tb -w` + wren7 `hw_suite check collide2 DIR`), and console cases where the
ARM, the channels and the DSP interact (`cases/dsp_coll.c`).  wren7 owns the core, the ARM-side console measurements
and `DcArmBus`; its console job suites are inputs to caique's tests.  AICA-internal behaviour goes in this file even
when an ARM job found it (e.g. "Channel / DSP collisions"); ARM bus timing stays in wren7's NOTES / TIMING.md.
Status 2026-09-24: 19 wren7 suites (3424 jobs) cycle-identical on rtl/v1, collide2 32/32, dsp_coll 18/18.

## Access / register map (tests/probe)

- VER = 1 (0x2800 reads 0x0010). MVOL/DAC18B/MEM8MB/MONO are write-only. RBP/RBL (0x2804) read 0. TIMA/B/C read 0 (tests/timer_probe: at every prescale, whatever was written -- write-only, so no sample-rate timestamps from them; claim D3)
  (the counters are not readable); their overflow bits do show up in SCIPD/MCIPD (0x5C0 at boot).
- G2 access cost from the SH4: ~2.4 us per 32-bit read (register or wave RAM).
- Channel register stored bits (write FFFF, read back): +00 47FF (KYONEX is an action; bits 13:11 not stored),
  +04/08/0C FFFF, +10 FFDF, +14 7FFF, +18 FFFF, +1C FFFF (LFORE reads back 1), +20 00FF, +24 FFFF, +28 FFFF,
  +2C..3C 1FFF, +40/44 1F1F, +48..7C nothing. Reserved bits in +18/+24/+28 are stored.
- DSP register files as seen by the SH4:
  - COEF bits 15:3; MADRS, MPRO 16 bits.
  - TEMP: full 24 bits R/W (+0: bits 7:0, +4: bits 23:8).
  - MEMS: +4 (bits 23:8) R/W; +0 (bits 7:0) is **not CPU-writable** but reads back what the DSP wrote.
  - MIXS: R/W 20 bits (+0: 3:0, +4: 19:4).  Every slot writes its ISEL bus every sample (IMXL 0 writes 0), so a
    bus keeps its last value only when NO slot points at it; a CPU-written value is seen by the DSP on alternate
    samples (two banks): see "MIXS retention" below (tests/eg_lock mixs, tests/mixs_write; resolves the dsp_temp T2 /
    dsp_basic F observations; modelled).  [claims D2, T9]  **CPU readback** (tests/mixs_rd, session 6): a read
    returns the bank of the CURRENT sample parity -- the bank the CPU's own writes go to -- so a written value reads
    back until the next sample boundary; from there a bus with writers shows the SGC value (0 / the sender's) and a
    bus NO slot points at alternates every sample between the written bank and the untouched one (period 2 samples).
    The low nibble's bank switch is seen ~5 us before the high word's inside the sample (not modelled).  Model:
    `read()` returns `MIXS_bank[samples & 1]`.  [claim U3]
  - EFREG 16 bits R/W. EXTS reads 0, not writable (no CD playing).
- Monitors: MSLC (0x280C bits 13:8) selects the slot. 0x2810 = LP(15) SGC state(14:13) EG(12:0). EG is **13 bits**:
  0x1FFF when the slot is off/released, 0 at full volume; otherwise the 10-bit attenuation (see Amplitude envelope).
  0x2814 = CA (reads 0 from the slot stop on -- the sample after the clock that brought a to 0x3C0, the same event
  that makes the EG monitor read 0x1FFF: tests/ca_stop, session 6; session 5 had read tests/sgc_keys K4 as "reset at
  off past 0x3FF", see Amplitude envelope).
  LP (bit 15) is cleared by the read.

## Slot levels (tests/sgc_level — model matches every value)

- Attenuation a (10 bits, 0.09375 dB units): TL contributes 4*TL, the AEG its 10-bit level (ALFO: unmeasured).
  Linear gain: 7-bit mantissa M = 127 - a[5:0], exponent a[9:6]:  **V = floor(sample * M / 2^(7 + a>>6))**.
  So TL steps are linear within each 6 dB octave (-1/32 per TL step), and TL=0 is 127/128, not 1.
- VOFF=1 bypasses the multiply entirely (V = sample).
- Send level to the DSP: MIXS += floor(V * 16 * s(IMXL)), s = 2^-(n>>1) * (n odd ? 3/4 : 1), n = 15 - IMXL;
  IMXL = 0 sends 0 -- a pure gain: the slot still writes its bus every sample (tests/mixs_write, see "MIXS
  retention").  The send multiply is applied to the full-precision product (TL 16 → 260080 but IMXL 13 →
  260088 for the same nominal -6 dB).
- DISDL/DIPAN do not affect MIXS (the direct outputs are not observable digitally).
- **MIXS accumulation wraps at 20 bits** (two full-scale slots give -32; order-independent), no saturation
  (tests/sgc_mix, model identical).  minicast clamped.
- minicast used float-derived 2^(-x/16) tables and a 16-bit MIXS scale; both replaced.

## Amplitude envelope (tests/sgc_aeg, aeg_dl0, aeg_koff, slot_tail, eg_kprobe, ca_stop; tools/aeg*.py are legacy)

Captured with a constant 0x7FFF sample; the attenuation of every sample follows from the level law, and the EG
monitor (0x2810 with MSLC) reads the same attenuation directly (bits 12:0), state in bits 14:13.

- It is the OPN (YM2612-style) envelope generator clocked every **2 samples**: a 10-bit attenuation, effective rate
  **R = 2 * rate** (KRS = 15), rate 0 = no change.
  - R < 48: a tick every 2^(11 - R/4) EG clocks, increment from row R&3 of the OPN eg_inc table (AR 1: spacings
    4096, 4096, 8192 samples).  R >= 48: every clock, rows 4.. (R 60-63 all 8).
  - Attack: `a += (~a * inc) >> 4` (= a - (a >> s) - 1 with inc 8/4/2/1 ↔ s 1/2/3/4); at 0 → decay 1.
  - Decay 1 / decay 2 / release: `a += inc`.  Decay 1 → decay 2 when **a[9:5] == DL** (an equality, not >=: a decay 1
    entered through LPSLNK above DL << 5 never reaches decay 2, tests/sgc_loop lo_2; for an attack-entered decay 1 the
    two are the same), **checked after that clock's decay 1 step** (every clock that starts in decay 1, also without
    a step; not on the clock that enters decay 1):
    decay 2's rate applies from the next clock, no skipped tick.  tests/aeg_dl0: with **DL = 0** the first decay 1
    clock still takes its step (D1R 31: attenuation 0 → 8, level 32767·119/128) and then holds in decay 2; D1R 20
    and 10 have no step on that clock and never move.  The EG monitor shows the new state within the crossing
    clock (tests/sgc_aeg dec_a).  The model switched one clock later and skipped the DL = 0 step; fixed (levels
    unchanged for DL > 0: the sgc_aeg model captures are identical to the previous ones after onset alignment).
    [claims A1-A3]
  - **Slot stop = "off"** (tests/slot_tail, 12/12 streams sample-exact through tools/tail_cmp, session 5; tests/ca_stop,
    session 6): when a reaches **0x3C0** (a[9:6] == 15) on a clock, the sample fetch stops, the EG monitor reads
    0x1FFF and CA reads 0, all on the sample AFTER that clock (tail_a: the fetch stop after the 120th +8 clock, the
    960th +1 clock; tail_c: 768 clocks of row 5, and 120 clocks after an RR rewrite).  The AEG value keeps stepping
    to 0x3FF and saturates there (tail_b); nothing else is observable at 0x3FF / 0x400.  **The output is NOT
    muted**: with VOFF 0 the level law with a = 0x3FF keeps applying to the zero-input filter tail (tail_b stream 0:
    -16 on every negative half-wave for 100 samples past the saturation, 0 on the positive ones); with VOFF 1 the
    filter tail runs to its rest value (see "Slot filter").  The FEG keeps stepping after the stop (tail_c).  The
    state is kept: decay 2 stays decay 2 (monitor 0x5FFF), and the decay 1 -> decay 2 compare still runs on the stop
    clock (ca_stop: DL 30 lands decay 1 exactly on 0x3C0, the monitor then reads state 2 with 0x1FFF); **minicast**
    switches decay 2 → release instead.  History: session 5 stated "fetch stop at 0x3C0; off (monitor 0x1FFF, CA 0)
    when a passes 0x3FF, the 128th +8 clock; each one sample after its clock" -- the captures only see the fetch
    stop, and the sgc_keys K4 poll (CA 0 logged in the same poll as EG 0x5FFF, 5916 us) was read as a reset at off.
    tests/ca_stop (session 6; decay 1 at +8 through DL 30 onto a = 0x3C0, then decay 2 at D2R 10 = +1 per 64 clocks,
    so 0x3C0 → 0x400 would take 186 ms; EG + CA monitors polled every ~31 us; runs S1 pitch 1.0 loop, S2 pitch 0.5,
    S3 one-shot) never shows a in 0x3C1..0x3FF: the poll after the 0x3C0 reading reads EG 0x5FFF (state 2, 0x1FFF)
    with the OLD CA, and the poll after that reads CA 0 (S1: a 0x3C0 at 5873 us, 0x5FFF + CA 0x0103 at 5905, CA 0 at
    5937; S3: 5864 / 5895 / 5927; S2: 0x3B8 at 5859, 0x5FFF + CA 0x0081 at 5891, CA 0 at 5936).  So the flag and
    the CA reset both belong to the 0x3C0 stop, within ~1-2 samples of its clock (the CA reset trails the flag by
    between one G2 read and one poll, below the poll's resolution; not modelled), and the session-5 question "does
    CA hold or advance between the stop and off" is moot.  Controls that each break streams: thresholds 0x3BF /
    0x3C1 / 0x400, lag 0 / 2 samples, a mute at the stop or at 0x3FF (work/verify/s5/model_fixes.md 2.3).  Model:
    `slot_stop()` sets `enabled = false`, `AEG.off` (monitor 0x1FFF) and `CA = 0` together, committed by `step()` one
    sample after the 0x3C0 clock (`Slot::stop_in`; session 5's separate `off_in` pipeline and `slot_off()` are gone);
    the sgc_keys K4 and sgc_aeg monitor logs reproduce (the model's K4 "EG 5fff CA 0000" line moved from 6231 to
    5868 us, console 5916 us).  [claims T8, U2]
- **Key-on loads a = 0x280 (-60 dB)**, not 0x3FF.  The key event takes effect on the sample after the KYONEX write,
  whatever its parity; that sample takes no envelope step, so the key-on level lasts 2 samples when it is a clock and
  1 when it is not (tests/eg_lock keys; the earlier "always one clock" came from key-ons that happened to land on
  clocks).  See "Envelope clock" below for the clock itself.
- **Key-off on a clock sample** (tests/aeg_koff, key-off sample pinned by a witness slot, session 5): from **decay 1
  or decay 2 the AEG takes ONE MORE step with the old segment's increment** on the key-off clock and the release
  increments from the next clock (koff_d2 30/30, koff_d2b 18/18, koff_d1 18/18 informative stream-cycles; "release
  increment on that clock" 0/30, 0/18, 0/18; "no step" fits only where the old increment was 0); from the **ATTACK
  it takes NO step** on the key-off clock (koff_att 27/27; "one more attack step" and "a += attack increment" 3/27 =
  only the cycles whose attack had already ended; release increment 0/27).  A key-off on an odd sample does nothing
  on that sample; the release starts on the next clock.  Model: `aeg_clock` (`keyed_off`, `aeg_prev`).  [claim T3]
- **minicast** used millisecond tables and an instant AR=31; replaced by the above.
- Model vs console (tools/aeg_cmp.py, phase-independent: level path + tick-spacing histogram): all 44 streams agree
  except where the envelope clock's phase at key-on differs and capture-end truncation.  Since session 4 the phase
  IS observable (see "Envelope clock"): tools/eg_model.cpp replays aeg_dl0, the eg_lock AEG runs and sgc_loop lo_2
  through the model sample-exactly (whole-program model runs still key on at their own phase).
- **R = 63 (instant) attack**: a = 0 from the key-on sample, and the attack → decay 1 transition happens **on the
  first clock at or after the key-on sample** -- the key-on sample itself when it is a clock, with no increment step
  there -- so decay 1 steps from the next clock (tests/eg_kprobe p5/p6/p7, even onsets: a = 1 at onset + 2; the
  session-4 wording "leaves the attack on the NEXT clock" fits 29/32 kprobe streams, this rule 32/32 (tools/kfit,
  `-oldr63` control); every even witness key-on of tests/aeg_koff (KRS 1; 10 + 6 + 6 + 9 + 7 over the five runs)
  shows the same; tests/eg_lock odd_dec is consistent with both).  [claim T7]

## Pitch and interpolation (tests/sgc_pitch — model bit-exact on all 16 pitches)

- Phase accumulator: **14 fraction bits**; increment = (1024 + FNS) << (OCT + 4), OCT signed -8..7 (truncated for
  OCT < -4).  (**minicast**: 10 bits, loses precision for every negative octave.)
- Interpolation uses only the **top 6 bits** of the fraction and keeps **4 fraction bits**:
  s16 = 16*s0 + floor((s1 - s0) * frac6 / 4)  (1/16 sample units).  With VOFF=1, LPOFF=1 MIXS = s16 exactly.
  (**minicast**: 10-bit fraction, integer result, two separately floored products.)

## Sample formats (tests/sgc_formats — model bit-exact)

- PCM8: sample = byte << 8 (= minicast).
- ADPCM (PCMS 2, and 3 long stream, including their loop behaviour over 4096 nibbles): Yamaha delta with each term
  shifted separately: `d = step>>3 + (b0 ? step>>2) + (b1 ? step>>1) + (b2 ? step)` (**minicast** used
  (2n+1)*step >> 3, off by one).  Step table / clamps = minicast.
- **Noise (SSCTL=1)**: one global 17-bit LFSR, s[n] = s[n-12] ^ s[n-17] (x^17 + x^12 + 1), stepped once per slot
  processed (64 steps per sample, adjacent slots one step apart).  A noise slot outputs the signed byte of 8
  consecutive sequence bits (bit j = j-th newer bit) << 8, independent of pitch, not interpolated.  Verified bit
  for bit over 6000 samples x 2 slots; the power-on LFSR phase is unknown, so model and console differ only by a
  constant sequence offset.  (**minicast**: a multiplicative congruential placeholder.)  Session 9: with the run's
  LFSR from the replay preamble, both noise streams of tests/sgc_formats hw9 match every sample (13568 each), and **a
  noise slot outputs noise whether it plays or not** -- from before its key-on (the models had gated it on the slot
  playing; fixed in both and rtl/v1).

## Loops and key events (tests/sgc_loop — bit-exact; tests/sgc_keys)

- Loop end: the check is **armed once CA has reached LSA since key-on** (arming happens after that step's check);
  then CA >= LEA → CA -= (LEA - LSA), CA 16-bit.  Normal loops = minicast (CA = LSA, overshoot kept one step at a
  time); LSA == LEA plays straight through; LEA < LSA plays to LSA, then jumps forward by LSA - LEA + 1 per sample,
  and after CA wraps past 0xFFFF the armed loop triggers at LEA.  One-shot (LPCTL=0) end: only the fetch stops --
  CA reads LEA for one sample, then 0; the envelope keeps running (tests/oneshot, session 9; minicast's "release,
  a = 0x3FF, off" is wrong).
- LPSLNK: the attack stops the moment CA reaches LSA and decay 1 starts from the current level (= minicast).
- Key-on during sustain: ignored.  **Key-on during a release that has not reached off: a = 0x280 is loaded on the
  key-on sample, that sample takes no step, and CA restarts on that sample** (tests/aeg_koff kon_rel, witness-pinned:
  48/48 stream-cycles; "step on the key-on sample" 27/48 and "load on the next clock" 21/48 each fit only the parity
  where they coincide with the rule; CA restart on the next clock 0/18; the old monitor-based "EG loads 0x280 on the
  next envelope clock" is superseded).  [claim T6]  **Decay 2 reaching the top stops the slot**: at a = 0x3C0 the
  fetch stops, the monitor reads 0x1FFF and CA reads 0, one sample after that clock (tests/ca_stop, session 6; session
  5 had put the monitor / CA part at "past 0x3FF"; see Amplitude envelope); the state stays decay 2, and a key-on is
  then ignored until a key-off.  **KYONB is never cleared by the
  hardware** (minicast cleared it on release).

## LFOs (tests/sgc_lfo)

- LFORE is a **held reset** of the state (the LFO stays at state 0 while the bit is set; it reads back 1).  minicast
  treated it as a one-shot.  The noise waveforms ignore it.
- One LFO state per slot, +1 every O samples (O from LFOF as in minicast: 2..1016 measured, identical).
- **Session 9 (tests/sgc_lfo, sgc_lfo2, lfo_noise hw9; tools/stream_replay -lfo): every stream matches every sample**
  (lf_0..lf_5: 24 streams of 26593..264737 samples; l2_0: the PLFO at OCT 2 / -3, FNS past 0x3FF and FNS - 128 < 0;
  ln_0 / ln_1: the noise waveforms on 8 slots) once each stream's LFO (state, counter) at the key-on is fitted --
  exactly one pair fits each.  What that shows: **the counter is per slot, free-running and never reloaded by a
  write** (slots configured and released together key on with different counters, e.g. 13 / 16 / 23 of 28), **the
  LFO runs whether the slot plays or not** (at periods 1 and 3 the state had stepped 3 and 1 times between the LFORE
  release and the key-on), and **LFORE holds the state, not the counter**.  A counter above a shortened period is cut
  to it (every fitted counter lay within its period 441 samples after LFOF 0 -> 20; a clamp or an immediate wrap,
  unmeasured).  Both models and rtl/v1 now do this (they had reloaded the counter on a 0x1C write and stepped only
  while playing).  The counters' phase is per-slot state left by earlier programs, like the filter state: a case's
  model run cannot know it (TODO 5.1).
- Amplitude LFO: attenuation **(w & 0xFE) >> (7 - ALFOS)** (ALFOS 0 = none): 0..254 at 7, 0..127 at 6, ..., 0..3 at 1.
  w: saw = state, square = 0/255, triangle = minicast.  **Noise = the global noise LFSR byte at that slot, every
  sample** (verified against the m-sequence), not tied to the LFO clock.  (minicast: 4x the value, fake noise.)
- Pitch LFO (**minicast never applied it**): it modulates FNS before the octave shift:
  **inc = (1024 + FNS + ((w & ~1) >> (7 - PLFOS))) << (OCT + 4)** with a signed 8-bit w: saw = (int8)state,
  square = +127/-128, triangle = 0, +2 per state to 126, **126 again at state 64**, down by 2 to -128, -128 again at
  192, back up (session 9, lf_4 / lf_5 at PLFOS 1..7: the model had 128 at state 64, which wrapped to -128; bit 0 is not
  used, so 126 / 127 and -128 / -127 are the same), noise = the LFSR byte **67 steps back, XOR 0x80** (see "The noise LFO
  bytes").  ALFO noise: the byte **two steps after the slot's own step**.
  Verified at PLFOS 1/3/5/6/7 (square at 7: exactly 1024+126 and 1024-128).  Modulation at other base pitches
  (OCT != 0, FNS overflow past 0x3FF) unmeasured.

**The noise LFO bytes, and why the PLFO's is 67 steps back (session 9; open, TODO 7.2).**  tests/sgc_lfo and
lfo_noise (8 slots, every sample of each capture): the ALFO noise waveform takes the LFSR byte two steps after the
slot's own step; the PLFO noise waveform the byte 67 steps before the slot's stage-A point, XOR 0x80 (offset binary).
With one LFSR step per frame (64 per sample; the value after frame f's step), the three noise bytes of slot k in
sample n come from:

| byte | LFSR value of | against slot k's stage A (frame k of sample n) |
|---|---|---|
| SSCTL (the sample) | frame k (its own step) | the stage A / B frames |
| ALFO | frame k + 2, read by frame k + 3 | + 3 frames: the start of the level stage (frame k + 4), where its 0x28 copy is read |
| PLFO | frame k - 4, read by frame k - 3 of sample n - 1 | - 67 frames: one sample and 3 frames earlier |

The ALFO's is where its stage is.  A natural reading of the PLFO's: **the phase increment is computed one sample
ahead** -- a pitch stage 3 frames before slot k's stage A of sample n - 1 computes (1024 + FNS + PLFO) << OCT for
sample n, and stage A of sample n adds the stored increment (the same "compute the next sample's value" structure as
the envelope pass).  Consequences that would confirm it, none measured yet: (1) FNS / OCT (0x18) and PLFOWS / PLFOS
(0x1C) writes act about a sample and 3 frames earlier than the SA write (tests/sub_frame measured SA, 0x28 and 0x20
only; the frame table's "0x00-0x1C in frame k" assumes the rest); (2) the PLFO's LFO state is the one of a sample
earlier than the ALFO's: one slot with both ALFO and PLFO on (one LFO) fits one (state, counter) only with that lag
(each alone fits either way, which is why the sgc_lfo replays cannot tell); (3) the first sample after a key-on steps
by an increment computed before the key-on.  Alternatives it would rule out: a separate per-slot PLFO noise latch
refreshed at some other point, or a second LFSR tap.  The models apply the measured offset directly.
The same family (the user's variant): the slot's address side runs ahead of its interpolation / output side (slot N
interpolates sample n - 1's data while fetching sample n).  tests/sub_frame bounds it: an SA write acts in the SAME sweep
as the output it changes (threshold at slot k's frame, 11333/11333 events), so the fetch itself (SA + CA) is not a
sample ahead of the interpolation; what can be ahead is the phase accumulator (CA += increment, with FNS / OCT / PLFO),
the SA add and the fetch staying at frame k.  The three candidates -- (a) the phase step a sample and 3 frames ahead of
the fetch, (b) a pitch stage 3 frames before stage A in the same sample using a per-slot PLFO noise latch one sample
old, (c) the whole slot a sample ahead (ruled out by SA) -- differ in where an FNS / OCT write acts: (a) about a sample
and 3 frames before SA's threshold, (b) 3 frames before it.

## Key rate scaling (tests/sgc_krs, tools/krs.py — model matches all 320 combinations)

- k = KRS + OCT (signed 4-bit); s = k < 0 ? 0 : 2 * min(k, 15) + FNS[9]; **R = min(63, 2 * rate + s)**; KRS = 15
  disables scaling.  rate 0 = no change.  (**minicast** adds KRS unscaled and never clamps.)
- **R = 63 attack is instant** (EG = 0 from the key-on sample, and the attack is left on that sample when it is a
  clock, see Amplitude envelope); R = 62 still ramps (s = 1 steps).

## Filter envelope (FEG) (tests/feg_probe, feg_track, feg_krs, feg_koffdir, feg_koffatt, feg_koffpass, slot_tail)

- The monitor with AFSEL = 1 reads the 13-bit FEG value, state in bits 14:13 (the FEG's own state, not the AEG's).
- Key-on loads FLV0.  The value then moves **linearly** (±inc per tick, same clock and increment tables as the
  AEG at the rate's R) toward FLV1 (FAR), FLV2 (FD1R), FLV3 (FD2R, then holds); key-off heads for FLV4 (FRR).  It
  moves up or down as needed.  Rate 0 holds.  FLV changes take effect only through the envelope (no key-on → no
  new FLV0).
- **Sample-exact behaviour** (tests/feg_track; tools/feg_track.cpp recovers v >> 1 at every sample through the
  bit-exact filter with a full-scale random input -- v bit 0 is unused by the filter; tools/feg_fit.cpp fits the
  envelope clock; tools/feg_validate.cpp runs the production model: 9/9 streams, 77,862/77,862 samples)
  [claims E1-E4]:
  - the envelope clock ticks every 2 samples (the even-MDEC_CT samples, "Envelope clock"); key-on loads FLV0 on the
    key-on sample and the first step comes on the next clock; key-off switches to release on the key-off sample, and
    if that sample is a clock it takes the step it would have taken without the key-off: one more step of the OLD
    segment -- its increment AND its direction, hold check against FLV4 -- or, when the old segment had passed its
    target on the previous clock, the NEXT segment's step (bullet "Key-off clock"); a key-off on an odd sample does
    nothing on that sample.
  - **KRS applies to FEG rates** exactly as to the AEG (R = 2 rate + KRS scaling; KRS 0 and 5 at OCT +3 fit only
    with it).
  - **One comparator C = (v >= target).**  A segment moves down if C holds when it starts, else up (a segment that
    starts exactly on its target moves one step down).  Attack and decay 1 step until C flips -- upward they end
    at or past the target, downward strictly below it (overshoot by up to one step: 0x1C04 +4 -> 0x1C08 with
    FLV1 0x1C05) -- and the next segment steps on the very next clock (no idle clock).  Decay 2 and release skip
    any step that would flip C and hold short of the target (0x1AFA with FLV3 0x1B00 at +8; 0x1BFF, 0x1FF8).
  - The R < 48 increment rows see the clock counter one step behind the R >= 48 rows (streams mixing both need
    an offset = 3 mod 4).  The AEG shares the offset (session 4: the AEG and FEG captures agree on one ring-locked
    counter constant only with it).
  - **Key-off clock** (session 5, tests/feg_koffdir with decay 2 and release moving in OPPOSITE directions,
    tests/feg_koffatt with the attack; key-off samples pinned by the S3 witness program / an AEG witness slot): **one
    more step of the old segment with its increment and its direction, then the release; the hold check is against
    FLV4**.  feg_koffdir: 8 batches, the 4 even key-offs (kd_0 / kd_1 / kd_5 / kd_6) fit "old step" on all 3 slots,
    the session-4 wording "old increment toward the release target" is refuted on all 4 (slots 0 and 2), "release
    increment" and "no step" refuted; the 4 odd key-offs are indifferent.  feg_koffatt (attack DOWN with release UP
    and the mirror): 6 even batches, "old step" 6/6, "no step" 0/6, "old increment toward release" 0/6, "release
    increment" 0/6.  So the AEG's exponential attack is the only segment that takes no step on the key-off clock;
    every linear segment (AEG decay 1 / 2, FEG attack / decay 1 / decay 2) takes one more of its own.  Clearing
    KYONB without a KYONEX leaves the FEG holding (feg_koffdir batches 6 / 7): the target follows the state, not
    the register.  The FEG keeps stepping after the slot's fetch stop and after off (tests/slot_tail tail_c).
    Model: `feg_clock`, rate from `feg_prev`, direction from `feg_prev_dir`.  [claim T4]
    **Passed flag set at the key-off** (session 6, tests/feg_koffpass, tools/koffpass_check; 2 runs x 64 witness-pinned
    cycles, 0 errors; all three slots cross on the same clock N -- s0 attack UP 0x1800 → 0x1810 at +2, s1 attack DOWN
    0x1C00 → 0x1BF0 at -2 with FLV1 0x1BF2, s2 decay 1 UP 0x1808 → 0x1816 at +2 after a one-clock R 60 attack; kp_b
    4 higher / lower -- and the key-off spacing sweeps E - key-on over 15..20 / 22..29 samples): an attack / decay 1
    that crossed its target on clock N sets `passed`, and a normal clock N+1 advances the segment first and then steps
    with the NEW segment's rate and direction.  A key-off landing on clock N+1 does exactly that too: **the next
    segment's step** ("nextSeg" 10/10 kp_a + 13/13 kp_b informative N+1 cycles, all 3 streams FULL to the cycle end);
    session 5's model rule "one more step of the passed segment" 0/23, "no step" 0/23, "the release step" 0/23, no
    N+1 cycle with zero or two readings fitting.  The other phases (E = N 18/18, pre 2/2, post 20/20 -- decay 1's and
    decay 2's plain old step, new evidence for decay 1 --, odd 41/41, odd* 24/24 = passed flag pending on an odd
    key-off sample: nothing on the sample, release from the next clock) are FULL under the established rule for every
    reading.  Hence the general statement: **on the key-off clock the FEG (and the AEG's linear segments) take exactly
    the step they would have taken had the key-off not happened, a segment advance due on that clock included; the
    AEG's exponential attack takes none.**  Model: `Slot::feg_prev_passed` saved by `key_off()`, `feg_clock` takes
    seg / dir from the passed segment's successor (its target decides the direction), the hold rule stays the
    release's.  Gates after the change: eg_model 89/89, tail_cmp 12/12, feg_validate 9/9, eg_replay 5 x 4/4;
    koffpass_check on tests/feg_koffpass/model nextSeg 11/11 + 8/8 (before the change oldStep 19/19 there,
    work/koffpass/check_model.txt).  Report: work/verify/s6/case_feg_koffpass.md; console output
    work/koffpass/check_hw.txt.  [claim U1]
  - The batch-1 slot-2 anomaly (its key-off one clock early, its counter phase 2 mod 4 off) is resolved: the rows
    for R = 49/57 differ from the OPN table, and a key-off sample that is a clock takes one more step of the previous
    segment (see "Envelope clock"; tests/eg_lock, feg_krs).
  - The model had clamping at the target and an idle clock at each transition; both are fixed (sample-model/aica_model.cpp
    feg_clock).

## Slot filter (integer arithmetic solved; 265 captured streams, 2026-09-23)

The production model now uses the following deterministic recurrence. Input `x`, band `B` and low `L` are
in **1/8-sample units**. Define `ceildiv(n,s) = -((-n) >> s)` with arithmetic right shifts:

```text
s = 24 - (FLV >> 9)
k = 256 + ((FLV >> 1) & 255)
if FLV >= 0x1FFE: k = 512
D = 2 * ceildiv(q128[Q] * B, 8)
H = clamp(x - L - D, -8388608, 8388607)      // signed 24 bits, before the cutoff multiply
B = B + ((k * H) >> s)
L = L + ceildiv(k * B, s)
filtered_s16 = clamp(-2 * L, -524288, 524287)
```

- **The missing operation was damping quantization to 1/4 sample:** `D = 2*ceil(q*B/2)`, before the
  cutoff multiply. Both integrator states retain 1/8 sample. The band increment floors; the low increment ceils.
  No multiplier-array approximation, stochastic rounding, or extra hidden state is needed on these captures.
- **The high-pass difference H = x - L - D saturates to signed 24 bits before the cutoff multiply** (session 3,
  tests/filt_overflow + held-out tests/filt_overflow_check, tools/filt_overflow.cpp).  Unity cutoff at Q 0 has an
  undamped alternating mode (poles 0.5 and -1), so full-scale alternating input grows the state until H saturates;
  after the burst Q is rewritten to 31 and the decay reveals the state.  The H clamp at 24 bits reproduces all 15
  streams and the 12 held-out ones (FLV 0x1FFE/1FFC/1FF8 and 0x1FFA/1FF6/1FF0, bursts 16..30000 samples), the
  production model too.  Clamping or wrapping the stored states, the damping, or the low increment at any width
  20..32 fits nothing; clamping the band increment fits only the k = 512 streams, where it is the same operation.
  States reach |band| 4.25M there.  [claim F6]
- `q128` is the previously measured table: Q' = Q + 4,
  `q128 = (16 - (Q' & 7)) << (4 - (Q' >> 3))`; q = q128/128, Q 0..31.
- The cutoff is `(256 + FLV[8:1]) * 2^(e-24)`, except **only 0x1FFE/0x1FFF use unity**.
  Bit 0 is unused. The fresh `filt_edges` console experiment distinguishes 0x1DFE from 0x1C00:
  0x1DFE retains k=511, not 512. `filt_coef` also validates this over e=11..15.
- **Output saturation is after conversion to 1/16 units**, with signed 20-bit rails. The positive rail is
  524287 (odd): the previous claim that MIXS is always even was only true away from clipping.
  The integrator states are **not** clipped to the output range. The resonant 0x1F80/Q31 `filt_id2` stream
  validates both clipping and the subsequent recovery.
- The state is not reset by key-on, key-off or LPOFF. **LPOFF freezes the filter; stopping sample playback
  does not.** A stopped slot supplies zero while its filter continues evolving, visible with VOFF=1.
  `slot_output` previously returned early for stopped slots; this was a second model bug, exposed once the
  arithmetic was exact. Low-cutoff deadbands and inherited rest states remain observable.  Confirmed sample by
  sample in session 5 (tests/slot_tail: every zero-input tail after the fetch stop, the deadband rests it lands on,
  and a unity-cutoff Q 0 VOFF 1 slot whose undamped mode keeps alternating between the rails after the stop).

### Why the old search got stuck

The previous handover's C3 ("the band increment floors with exact q*B") was a necessary-condition fit with
an independently selectable rounding error every sample, not an identified datapath. It passed 22/24 streams
but inferred impossible low multipliers: at F=0x1F55 the **same** inferred B'=1 required both dy=0 and dy=1.
There are analogous conflicts at 0x1C00, 0x1800, 0x1FF0 and 0x1FFE. `filt_need` now reports these witnesses.
Its backward pruning also now checks the low-output edge, not just reachability of the next band value.
The old "operand-dependent multiplier effect" conclusion was therefore not justified.

At unity cutoff, Q4, the coarse damping is `D = B + (B & 1)`. Thus:

```text
B' = x - L - (B & 1)
L' = x - (B & 1)
```

For zero input, `(L,B) = (0,1) -> (-1,-1) -> (-1,0) -> (0,1)` explains the measured period-three
`0,-1,-1` cycle. This parity argument suggested the coarser damping product. Adding it to `filt_rule` immediately
gave **24/24 full streams with deterministic floor/ceil/ceil**, including both previously failing Q0 streams.
The existing all-Q captures distinguish damping ceil from the c6/c7 rules that tie on Q0/Q4 alone.

### Validation and reproduction (C++ only)

`tools/filt_validate.cpp` independently simulates the recurrence from known input sequences and searches only
an initial band in [-256,256], with initial low read from the capture. There are no later state corrections,
per-sample rounding choices, fitted coefficients, or omitted mismatches. `-DVERIFY_MODEL` additionally seeds
those initial states into **the real AicaModel**, supplies each input via its slot, and checks raw MIXS every sample.

| Capture set | Streams | Coverage |
|---|---:|---|
| filt_cyc | 24 | Original steps, both signs, limit cycles and Q0 failures |
| filt_id2 | 60 | Impulse + random input, six Q settings, saturation/recovery |
| filt_coef | 119 | All 32 Q settings; mantissa sweep, e=11..15 |
| filt_imp | 16 | Full-range signed impulses, 511 impulses per stream, four batches |
| filt_edges | 12 | Fresh console captures, endpoint exponents, ±1000/±7/±1 impulses |
| filt_top | 4 | Unity cutoff, inherited states, DC, looped full-range random input |
| filt_low | 18 | e = 0..11 from a KNOWN start state (unity-cutoff prelude), full-scale step, 1-4 s each; Q 0/4/31, k 256/426/511 |
| filt_wide | 12 | Resonant Q 16..31 driven by a full-scale square wave at the resonance: states to 2^21.7, 20-36 % railed |

The overflow sets (filt_overflow 15, filt_overflow_check 12 held out) are validated separately by
tools/filt_overflow.cpp (Q write inside the capture): 27/27 full, production model included.

**265/265 streams; 4,286,180/4,286,180 consecutive samples match** (production model included), including the
raw positive saturation rail.  (235 / 2,050,454 before filt_low and filt_wide were added.)
Commands and build flags are in HANDOVER.md. Results: `work/filt/validate_model.txt`.
The impulse fixture has 65536 sample addresses and the capture repeats at the 16-bit address wrap; the validator
models that observed wrap rather than assuming silence after the supplied buffer.

Normal whole-program model runs start with reset state, whereas the console inherits filter state from preceding
programs. `tools/filt_compare.cpp` compares aligned impulse-to-capture-end windows:

- filt_id2: **55/60 streams completely identical; 619/254980 samples differ**, all in the first 300 samples
  after the initial impulse, maximum difference 8 MIXS units. The old report was 72.1% different over its shorter window.
- filt_coef: **117/119 streams completely identical; 17/503438 samples differ**, all within the first 17 samples.
- With the inferred initial state, the production model matches every sample of both sets, as above.
  Do not "fix" these inherited-state differences with arithmetic hacks or claim the settling prelude resets to zero.

All 16 existing non-filter model cases were re-run without Python. Their saved outputs are byte-identical to
pre-change outputs except `sgc_level.txt`'s already-excluded L5 filter line. Baseline hashes:
`work/filt/nonfilter_before.sha256`. The known AEG phase differences remain.  After the AEG decay-1 fix (session
2) the sgc_aeg / sgc_keys model outputs differ from that baseline only in monitor timing (levels identical after
alignment); the current baseline is `work/model_outputs_2026-09-23.sha256`.

Session 2 (since the breakthrough handover; claims F1-F5 in HANDOVER.md):

- **Low cutoffs e = 0..11** (tests/filt_low) [F1]: each slot first plays zeros at 0x1FFE, which leaves it in the
  (0,-1,-1) cycle or at (0,0); at a low cutoff those states freeze, so the start of the step is known.  All 18
  streams match end to end.  At e = 0 the low output climbs by exactly 1 per sample from the first sample (the
  low update ceil of a tiny positive product), and overshoot reaches the output rail (-MIXS/2 = 262144).
- **Fractional input** (tests/filt_frac, tools/filt_frac.cpp) [F2]: the filter takes the interpolated 1/16-sample
  value as **floor(s16 / 2)**.  Three fractional pitches x three filters, with an LPOFF reference slot giving s16
  exactly (about 1400 negative odd s16 per batch): floor reproduces all 9 streams, ceil / toward zero / nearest
  fail within about 20 samples.  The model already did this.
- **VOFF = 0 with the filter on** (tests/filt_voff, tools/filt_voff.cpp) [F3]: the level multiply comes AFTER the
  filter, on its clamped 1/16 output, and the result is truncated to whole samples:
  `MIXS = (floor(clamp(-2 low) * M / 2^(7 + (a >> 6))) >> 4) * 16` (a = 4 TL + AEG + ALFO, M = 127 - (a & 63)).
  8/8 streams (TL 0..0xA3, Q 4/16/31); level before the filter, filter cut to whole samples before the level,
  and a 1/16-precision result all fail.  The model already did this.  (sgc_level L5 is not evidence either way:
  its console value needs a settled low of exactly x, the model settles one LSB below, both inside the DC deadband
  and the console state is inherited.)

- **Sign convention** (session 4, tools/filt_negform.cpp): with the band state sign-flipped (Bh = -B) every rounding
  of the recurrence is a plain arithmetic right shift -- `Dh = 2 * ((q128 * Bh) >> 8)`, `H = clamp24(x - L + Dh)`,
  `Bh -= (k * H) >> s`, `L -= (k * Bh) >> s` (and with Lh = -L too, `out = 2 Lh`); 2^28 random states over all
  131072 settings give 0 differences.  The floor / ceil / ceil asymmetry is an artefact of the sign of the stored band,
  not a datapath feature.
- **Integrator width** [F4 as first stated was WRONG; corrected in F6/F7].  The first claim (tests/filt_wide:
  "no FLV/Q setting is unstable", Q31 resonance is the worst case, >= 23 bits) missed the undamped unity-cutoff Q0
  mode; without the H clamp that mode grows without bound (tools/filt_unity_sim.cpp).  filt_wide itself stays valid
  data (its states, 2^21.65, never reach the clamp).  With the H clamp: the captures reach |band| 4,252,670 > 2^22
  and match with unbounded integrators, so **band holds at least 24 signed bits** and **low at least 23** (3.6M).
  **tools/filt_reach.cpp** searched every setting (all 16 exponents x 256 mantissas x 32 Q) with four full-scale
  drives (dc, alternating, time-reversed impulse-response sign = the linear optimum, and an adaptive pump in phase
  with band) for 65536 samples: the largest states are |band| 4,286,071 (2^22.03, 0x1FFC Q0) and |low| 3,628,859
  (0x1FF4 Q31); nothing reaches 2^23 (work/filt/reach_clamp_all.txt).  Without the clamp the same search diverges
  (2.9e9 at 0x1FFE Q0).  So wider registers are not observable with any drive found; this is a search, not a
  proof.  The model's int32 states are exact for everything reachable in that search.  [claim F7]
  Session 4 (tools/filt_reach2.cpp, a setting switch mid-drive: pump at Q 31 / alternate at Q 0 unity, then every
  setting with four drives): |band| 5,315,589 (0x1FEE Q31 pump -> 0x1FFE Q0 dc) and |low| 4,791,874, larger than
  the single-setting maxima but still below 2^23 -- the conclusion stands.
- The validator (tools/filt_validate.cpp) gained the filt_low and filt_wide sets: 265 streams, 4,286,180 samples;
  other damping roundings fail (qbias 0: 4/265, 128: 117/265, 223: 154/265) [F5].
- The old `filt_probe/fp_2` capture has 228 counter errors; it must not be used as arithmetic evidence.  minicast
  had no filter.

## Envelope clock, key timing and increment rows (tests/eg_lock, feg_krs, eg_kprobe; sessions 4-5, 2026-09-23)

The envelope clock is **observable**: it is locked to the DSP's ring counter.

- **MDEC_CT is a free-running 16-bit sample counter** (masked to the ring at the DSP address, wrapping at 64K, not
  reset by the RBP/RBL write or a DSP program load); cap.h records sample n at ring address c0 - n = MDEC_CT, so
  every capture carries the counter of every sample (c0 from the case's `cap_start:` log line, n_first from the
  header).
- **The envelope clock ticks on every sample whose MDEC_CT is even**, for the AEG and the FEG alike, and its counter
  is `eg_cnt = K - MDEC_CT/2 (mod 2^14)` with **one constant K per console boot**: **K = 6491 exactly** (bit 13 = 0;
  session 4 had it mod 8192, rate 2 pins no more; tests/eg_kprobe pins it mod 16384 through an R 3 decay, whose row
  is indexed by eg_cnt bits 13:11, on 8 probes -- tools/kfit, and eg_model kp_p0..p7 8/8) for tests/feg_track,
  aeg_dl0, eg_lock, feg_krs and every session-5 case (same boot); tests/sgc_loop, captured before a reset, has
  K = 165 mod 1024.  The number is bound to the -1 counter offset of the R < 48 rows (offset 0 with K 6490 is the
  same law on AEG-only data; work/verify/s5/S1.md).  Evidence: tools/eg_phase.cpp (the AEG of every constant-input capture
  predicted from the level law with K the only free parameter: aeg_dl0 4/4 streams, eg_lock att_slow (AR 6/4/2/1,
  487,478 samples each) 4/4, att_mid 4/4), tools/eg_keys.cpp (32 key-on/off cycles on slots 0..3 and 60..63: every
  first envelope step on an even sample, 0 violations), tools/feg_validate.cpp / feg_lock.cpp (FEG), and
  tools/eg_model.cpp: the production model with MDEC_CT set from the capture reproduces **33/33 streams sample by
  sample** (aeg_dl0, eg_lock x 8 runs, sgc_loop lo_2 with LPSLNK, feg_odd, feg_krs x 4, feg_track x 3: 1.17 M
  samples; 89/89 with the session-5 eg_kprobe, feg_koffdir and feg_koffatt runs).  Independently re-derived in session 5
  (work/verify/s5/S1.md, s1_indep.cpp: 690 envelope steps over 12 streams of att_slow / att_mid / dl0, all on even
  MDEC_CT, K in {6491, 14683} from AEG-only data; a clock on odd MDEC_CT, K off by one and the YM rows fail as
  required).  Model: `AicaModel::eg_K`; the clock is derived from MDEC_CT in `step()`.
- **What sets K is not any register a program touches** (tests/eg_kprobe, 8 probes 1.7 s apart with an action before
  each): a repeat, an RBP/RBL rewrite, TIMA/B/C writes, MVOL writes, a 5 ms ARM7 release (vectors = `b .`), a 128-step
  DSP program load and a 64-slot register sweep with KYONEX all leave K at 6491 (dK +0; MDEC_CT continuity within
  ~90 samples of the SH4-clock prediction).  The offset between the two counters is set before any program runs
  (power-on / BIOS boot); a controlled reboot is the next probe.  [claim T1]
- **Every envelope register is read LIVE -- there is no one-sample register latch** (session 6, tests/eg_latch,
  eg_latch2, eg_latch3 through tools/latch_check; every capture 0 errors).  Session 5 had stated "the EG reads its rate
  registers one sample late" from tests/slot_tail tail_c (RR 0 → 31 rewritten together with reg 0x00 on clock sample
  11300: the fetch switched to the new SA at 11300, the first +8 release step came at 11302) and the model latched
  r10 / r14 / r18 / r40 / r44 (`Slot::egreg`, `eg_latch`) -- REFUTED.  Method: each rewrite is paired with a witness
  slot's KYONEX (write order alternating per cycle), the witness onset E pins the sample the pair takes effect on; a
  live register acts at clock E, a latched one at clock E + 2 (informative = even E).  eg_latch (6 runs x 16 cycles
  x 3 rewrites): DL 6 + 8, KRS 11 + 12, AR 8 + 10, D2R 11 + 10, FD1R / FD2R 14 + 15, FLV3 target 1 + 4 informative
  events (order 0 + order 1) ALL live, 0 latched, LATE straddles 0 everywhere (work/latch/check_hw.txt).  eg_latch2
  (work/latch/check2_hw.txt): RR 0 → 30 on a held release 12 + 14 live, RR 24 → 30 10 + 12, RR 0 → 30 with the slot's
  own reg 0x00 (KYONB 0) rewritten + KYONEX = tail_c's group without the SA change 10 + 10, RR 24 → 30 with a redundant
  key-off 11 + 11, D2R 0 → 28 on a held decay 2 6 + 16 -- all live: no latch, no "rate 0 → nonzero arming", a
  redundant key-off on a released slot changes nothing.  eg_latch3 (work/latch/check3_hw.txt): the same RR 0 → 30
  probe with reg 0x00 SA[22:16] changed 14 + 15, reg 0x04 SA low 13 + 11, LPCTL 1 → 0 12 + 11, LEA 14 + 11, control
  none 16 + 18 -- all live, and `CA restarted 0/48` in every run: an SA / LPCTL / LEA rewrite does not restart the
  stream, CA runs on.  Model: `egreg` / `eg_latch` removed, `aeg_clock` / `feg_clock` / `eff_rate` read r10, r14
  (DL, KRS, RR), r18, r40, r44 and the FLV targets from the registers.  [claims T8 (latch clause refuted), U4]
- **In-sample ordering: the envelope update precedes the sample fetch / output within a sample period** (session 6;
  the explanation of tail_c's "delayed" RR).  Key events (KYONEX) always take effect from the next sample.  A register
  write landing BEFORE a sample's envelope phase acts on that sample's clock; one landing AFTER it is seen by that
  sample's fetch but only by the next sample's envelope clock.  tail_c's write group (RR 0 → 31, reg 0x00 := 0 = SA 0 /
  LPCTL 0, then KYONEX) landed between the two phases of sample 11300: the SA switch shows at 11300, the first +8
  release step at 11302.  Statistic (eg_latch; the checker's "EARLY" straddles = the register acting one sample BEFORE
  the same group's key event, in both write orders): krs 5 of 25 odd-E events, ar 5 of 28, d2r 1 of 27, feg_rate 1 of
  19, eg_latch2 / eg_latch3 1-3 per run -- writes that landed before the envelope phase of their sample and were seen by
  that sample's clock while the KYONEX next to them acted a sample later.  **Not modelled** (a known sub-sample
  limitation): the model steps whole samples and applies writes between steps, so it cannot place a write inside a
  sample; tools/tail_cmp therefore lets the RR write be applied one sample after the SA write (d = w00 - w14 in {1, 0,
  -1}) and tail_c fits with w00 11300 / w14 11301 (12/12 streams).  RTL rule: EG update first, then fetch / output;
  register writes take effect at the next phase boundary, key events at the next sample.  [claim U4]
- **Key events take effect on the sample after the KYONEX write, whatever its parity.**  The key-on sample takes no
  envelope step: the key-on level lasts 2 samples when that sample is a clock, 1 when it is not (eg_lock keys: 32
  cycles, both cases).  The old model applied key events at the next clock (level always 2 samples).
- **A key-off sample that is a clock takes one more step of the segment the envelope was in** (the rate lookup lags
  the state change by one clock): feg_krs fk_1 / fk_3 (decay 2 holding short at 0x19FE with +4, release +1: the
  key-off clock moves +4 to 0x1A02, then +1 per clock); when the key-off sample is not a clock the next clock uses
  the release rate (fk_2, feg_odd).  This closes the old E4 anomaly: in feg_track batch 1 the KRS-5 slot "released
  one clock early" because its old segment (decay 2, +8) had a nonzero increment on the key-off clock while its
  siblings sat in slow attacks whose increment at that counter was 0; batch 2 the same (slot 1 in attack R 36 with
  a +1 tick, slot 0 at R 48 with an invisible +1, slot 2 at rate 0).  With the rule, all slots of every batch share
  one key-off sample (eg_model).  **Session 5 corrected the wording**: the step is the old segment's increment AND
  direction, not "toward the release target" (in every session-4 event the old segment and the release moved the
  same way; tests/feg_koffdir separates them, see "Filter envelope"), and on the AEG the decays take the old step
  while the exponential attack takes none (tests/aeg_koff, see "Amplitude envelope" and the section below).
  Odd key-off samples: nothing on the sample, release from the next clock (work/verify/s5/S3alt.md lists the refuted
  alternatives).  [claims T3, T4, T5]  **Session 6 generalised it** (tests/feg_koffpass, "Filter envelope"): the
  key-off clock takes exactly the step the envelope would have taken without the key-off -- when the old segment had
  passed its target on the previous clock, that is the segment advance and the NEXT segment's step (23/23), not one
  more step of the passed segment; only the AEG's exponential attack takes nothing.  [claim U1]
- **The R < 48 counter offset (-1) applies to the AEG too**: the AEG captures and the FEG captures agree on the same
  K only with the same offset (eg_phase / eg_model).
- **Increment rows for R = 1 mod 4 at R >= 48 (rows 5, 9, 13) are {b, 2b, b, b, b, 2b, b, b}**, the double step at
  index 1 and 5, not the YM2612's index 3 and 7; every other row is the OPN table (rows 1, 3, 7, 11, 15 measured at odd
  effective rates through KRS: eg_lock odd_att / odd_att3 / odd_same / odd_dec, feg_odd, feg_krs).  This is the
  other half of E4 (the "counter phase differing by 2 mod 4").
- **R = 63 attack**: the level is 0 from the key-on sample, and the envelope leaves the attack on the first clock at
  or after the key-on sample (the key-on sample itself when it is a clock, no increment step), so decay 1 steps from
  the next clock (eg_kprobe p5/p6/p7 even onsets; the session-4 "leaves the attack on the next clock" came from
  eg_lock odd_dec, which is consistent with both).  [claim T7]
- **Decay 1 -> decay 2 when the top 5 bits of a EQUAL DL**, not >=: a slot that enters decay 1 through LPSLNK above
  DL << 5 keeps decaying at D1R and never reaches decay 2 (sgc_loop lo_2 stream 2, DL 0, entered at a = 0x20D:
  it decays to "off").  For an attack-entered decay 1 the two compares are identical (steps of at most 8 cannot skip
  the DL window), which is why aeg_dl0 (DL 0, immediate decay 2) and every sgc_aeg run agree with both.
- **KRS is not a timing factor** (feg_krs: the feg_track batch-1 programs on swapped slots key off together).
- The old model's `EG_PHASE` is gone; whole-program model runs still key on at a phase set by the emulated SH4
  timing, so a console capture is reproduced exactly only through eg_model (MDEC_CT taken from the capture).
- Loop-end interpolation (side finding, model already correct): at pitch 1.5 with a 32-sample loop the sample before
  LEA interpolates against the RAM word beyond LEA (0 there), halving one output sample every loop; tools/eg_phase.cpp
  assumes a constant input and therefore stops at +21 on the eg_lock odd runs -- use eg_model for those.

## Key-off clock, slot stop and MIXS writers (session 5, 2026-09-23)

Same console boot as session 4 (K 6491); six new cases (aeg_koff, eg_kprobe, slot_tail, feg_koffdir, feg_koffatt,
mixs_write), every capture with 0 counter errors; reports with every number in `work/verify/s5/*.md`.  Claims T1-T10
in HANDOVER.md.

- **Why the AEG decay-2 runs alone cannot pin the key-off sample** (work/verify/s5/S3alt.md section 3): a key-off on
  an odd sample is a no-op and under the session-4 rule the key-off clock does exactly what one more decay-2 clock
  would do, so "one more old step on an even key-off sample n" and "release increment / no step on the odd sample
  n+1" print the same level sequence for the whole run (S3-even-K == noS3-odd-K+1).  The key-off sample is not in
  the capture (cap_mark is 30-100 samples late, the counter is not readable), so the AEG can only decide the rule
  where the two segments move differently -- and only a witness can pin the sample.
- **The witness slot** (cases/aeg_koff.c, feg_koffatt.c): KYONEX applies the KYONB of every slot at once, so the
  write that keys the test slots OFF also keys a witness slot ON -- slot 3 with AR 31 and KRS 1 (k 1, s 2 → R 63 at
  pitch 1.0), whose level jumps to 520176 (a 0) on its key-on sample and which decays to off by itself (D1R 31 to
  DL 31, D2R 31) so every witness key-on is fresh.  The witness onset = the test slots' key-off sample; in kon_rel the
  same trick pins the second key-on.  The pin rests on one KYONEX applying key-ons and key-offs on the same sample
  (eg_lock keys: identical onsets on 4 slots x 32 cycles; feg_track: one key-off sample shared by 3 slots).  The
  witness itself is an R 63 probe (T7: its decay 1 steps at +2 after every even key-on).  Pitfall: KRS 0 / FNS 0x200
  also gives R 63 but pitch 1.5 halves one sample per loop (loop-end interpolation), so KRS 1 / FNS 0.
- **The K probe** (cases/eg_kprobe.c, tools/kfit): four constant-input slots keyed on together for 1.5 s with an R 63
  attack and D1R 1 / 6 / 14 / 22 (decay 1 at R 3 / 13 / 29 / 45, +1 per tick).  R 3 ticks every 2048 clocks and its
  row 3 = {0,1,1,1,1,1,1,1} is indexed by ((cnt-1) >> 11) & 7 = eg_cnt bits 13:11, so it skips one tick per 32768
  samples: 16 ticks with 2 skips pin K modulo 16384, bit 13 included; the other three streams pin K mod 2048 / 128 /
  8 and the fitter intersects the sets.  Result: K = 6491 unique on all 8 probes, dK +0 after every action (T1).  The
  probe's key-offs are from rate-0-at-that-counter segments and say nothing about the key-off rule (the koff_same
  ambiguity above).  The constant fill continues past LEA (24 words for the [0,32) loop) because pitch 1.5
  interpolates against the word beyond the loop.
- **AEG key-off on a clock** (T3; tools/koff_fit, 16 random-timed cycles per run, second console run in
  tests/aeg_koff/hw, the first with identical verdicts archived in tests/aeg_koff/hw_run1): decay 2 → release: ONE
  MORE decay-2 step on the key-off clock, then the release increments (koff_d2 30/30, koff_d2b 18/18 informative
  stream-cycles; release increment on that clock 0/30 + 0/18; "no step" only where the decay-2 increment was 0);
  decay 1 → release: one more decay-1 step (koff_d1 18/18); ATTACK → release: NO step on the key-off clock (koff_att
  27/27; one more attack step 3/27 and a += attack increment 3/27 = only where the attack had already ended; release
  increment 0/27).  Through the production model: tools/eg_replay, all 5 runs 4/4 streams FULL, 16/16 clean cycles
  (174080 / 168704 / 174592 / 163328 / 215808 samples).
- **FEG key-off on a clock** (T4; cases/feg_koffdir.c, tools/koffdir_check; cases/feg_koffatt.c, tools/koffatt_check):
  one more step of the OLD segment with its increment AND its direction, then the release, hold check against FLV4
  (see "Filter envelope": feg_koffdir even batches kd_0/1/5/6 4/4 "old step", "old increment toward the release
  target" 0/4; feg_koffatt 6 even batches 6/6 old step, the three alternatives 0/6; KYONB cleared without KYONEX:
  the FEG holds).  So the AEG's exponential attack is the only segment that takes no step; every linear segment
  takes one more of its own.  Through the production model: eg_model kd_0..7 x 3 = 24/24 and ka_0..7 x 3 = 24/24 (u files
  work/eg/kd_*.u, ka_*.u from koffdir_check / koffatt_check -u).  Session 6 closed the T4 INCONCLUSIVE (the key-off
  clock with the old segment's `passed` flag set): it takes the NEXT segment's step, not one more of the passed one
  (tests/feg_koffpass 23/23; "Filter envelope", claim U1).
- **Odd key-off samples** (T5): nothing on the sample, release from the next clock -- every surviving reading agrees.
  Refuted alternatives (S3alt.md, 24 FEG streams re-derived with a standalone law, one shared key-off sample per
  batch): a release increment or no step on the key-off clock (fk_1 / fk_3: the +4 out of the 0x19FE hold has no
  other source; ft_1: no single key-off sample serves its three slots); a one-clock rate lag at ALL transitions (the
  internal transitions refute it: ft_0 s0's attack overshoots FLV1 to 0x1C08 and the very next clock steps -2, the
  decay-1 increment, not -4; fk_1's decay 1 ends at 0x17FE and the next clock steps +4, decay 2's, not +2); a pending
  step applied on the first clock AFTER the key-off, whatever its parity (fk_2: hold 0x19FE then +1 at the next clock,
  no +4; ft_0, feg_odd, fk_0 likewise); a two-clock lag (fk_1: +4 then +1, not +4 +4); the state change landing only
  on even samples; a KYONB-driven target (feg_koffdir batches 6 / 7: the held values do not move for 100 ms after the
  KYONB clears; disfavoured ~60:1 by the write-window statistics before that).
- **Key-on during a release that has not reached off** (T6; kon_rel): a = 0x280 loaded on the key-on sample, no step
  on it, CA restarts on that sample (48/48; "load on the next clock" and "step on the key-on sample" refuted; CA
  restart on the next clock 0/18).
- **R 63 attack on a key-on sample that is a clock** (T7): attack → decay 1 on that clock, decay 1 steps on the next
  (eg_kprobe p5/p6/p7; kfit 32/32 vs 29/32 with the session-4 rule; eg_model kp 8/8; the aeg_koff witnesses).
- **Slot stop / off** (T8; cases/slot_tail.c, tools/tail_cmp, 12/12 streams FULL): the fetch stops at a = 0x3C0 and
  the slot is off past 0x3FF, each visible one sample after its clock; no mute; the FEG runs on; the rate registers
  reach the EG one sample late -- details in "Amplitude envelope" and "Envelope clock".  Controls (model_fixes.md
  2.3): thresholds 0x3BF / 0x3C1 / 0x400, lag 0 / 2, a mute at off or at the stop each break streams.  **Session 6
  superseded the "off past 0x3FF" half** (tests/ca_stop, claim U2): the monitor's 0x1FFF and the CA reset belong to
  the 0x3C0 stop (within ~1-2 samples, the poll cannot split them); the AEG value still steps to 0x3FF but nothing
  observable happens there; "CA between the stop and off" is moot (CA reads 0 from the stop).  The tail_cmp streams
  are unchanged by this (12/12).  **The "rate registers one sample late" clause is REFUTED** (tests/eg_latch /
  eg_latch2 / eg_latch3, claim U4, "Envelope clock"): every envelope register is live; tail_c's RR write landed after
  the envelope phase of sample 11300 (in-sample ordering, not a latch); tail_cmp now places it at w14 11301 and stays
  12/12.
- **MIXS writers** (T9; cases/mixs_write.c, tools/mixsw_check, 21 probe rows): every slot writes its ISEL bus every
  sample, IMXL is a gain, a bus retains only when no slot points at it (H_G 21/21; the session-4 rule 4/21; every
  conditioned variant refuted) -- see "MIXS retention".  The checker's verdict tables are identical for
  tests/mixs_write/hw and /model.
- **The -8 of eg_lock mixs** (work/verify/s5/analysis_minus8.md): bus 2 retained because nothing pointed at it (every
  slot at ISEL 0 after ch_zero_regs, which is why bus 0 read 0); the value is the slot filter's zero-input rest state
  (L 4, B -4, out -2L = -8 at IMXL 15) at the FEG's release hold 0x1BFF (k 511, s 11), Q 4, through the VOFF path:
  one of the 25 zero-input fixed points there (none at 0x1C00: L 4 is not a rest state at the target, so the FEG did
  hold short), no limit cycles below 0x1C00; the model lands on -8 in ~4 % of the possible trajectories (0 in ~14 %,
  the phase of the input stop decides) and reproduces the console's tail_c retained values 0 2 2 exactly.  A held last
  sample or a frozen filter output would be of order 10^5; a muted slot would leave 0.
- **Model** (sample-model/aica_model.{cpp,h}, model_fixes.md): `stop_in` / `off_in` one-sample pipeline (session 6: `off_in`
  removed, `slot_stop()` does fetch stop + monitor 0x1FFF + CA 0), no mute, the FEG runs
  on, `egreg` one-sample rate latch (`eff_rate` is static now; session 6: `egreg` / `eg_latch` REMOVED, every register
  read live, U4), key-off clock rules for the AEG (attack: none; decays:
  old increment) and the FEG (old increment + old direction via `feg_prev_dir`; session 6: `feg_prev_passed`, the
  next segment's step when the old one had passed), the R 63 transition on the key-on
  clock, every slot writes its bus.  Macros `CAIQUE_STOP_A` / `CAIQUE_STOP_LAG` / `CAIQUE_MUTE` exist only for the
  controls.  Validation after the last change: eg_model 89/89 (session4 33/33, eg_kprobe 8/8, feg_koffdir 24/24, feg_koffatt 24/24),
  eg_replay 5 x 4/4, tail_cmp 12/12, tools/validate_s5.sh PASS, feg_validate 9/9, filt_validate_model 265/265,
  filt_overflow_model 15/15 + 12/12 held out, ./run_model.sh on all 47 cases exit 0; console-vs-model text diffs as
  before (dsp_basic 3 known lines, sgc_level L5, monitor timing / key-on phase in the whole-program runs).
- **Standalone fitters** (no model linked, NOTES formulas only): tools/koff_fit.cpp, tools/kfit.cpp,
  tools/koffdir_check.cpp + tools/feg_law.h, tools/koffatt_check.cpp, tools/mixsw_check.cpp; replays through the
  production model: tools/eg_replay.cpp, tools/tail_cmp.cpp (+ tail_cmp_all.sh), tools/eg_model.cpp (extended),
  tools/validate_s5.sh (the one-shot gate).

## Session 6 (2026-09-23 night, same boot, K 6491): passed-flag key-off, stop = off, MIXS readback, registers live

Six new cases (feg_koffpass, ca_stop, mixs_rd, eg_latch, eg_latch2, eg_latch3; every capture 0 counter errors), two
standalone checkers (tools/koffpass_check, tools/latch_check); reports in `work/verify/s6/` (case_feg_koffpass.md,
case_eg_latch.md, docs_s6a.md, docs_s6b.md, bitcheck.txt); claims U1-U4 in HANDOVER.md "Session 6 addendum".  All four
session-5 open envelope items closed, details in the sections above / below:
- **U1** the FEG key-off clock with the old segment's `passed` flag set takes the NEXT segment's step (feg_koffpass
  23/23; "Filter envelope").  General rule: the key-off clock takes exactly the step the envelope would have taken
  without the key-off, segment advance included; only the AEG's exponential attack takes none.
- **U2** the slot stop at a = 0x3C0 IS "off": monitor 0x1FFF and CA 0 one sample after that clock, no second stage at
  0x400 (ca_stop, three runs; "Amplitude envelope").
- **U3** the CPU reads the MIXS bank of the current sample parity (mixs_rd, 7 runs; "MIXS retention").
- **U4** every envelope register is read live -- session 5's "one sample late" latch is refuted (eg_latch: DL, KRS,
  AR, D2R, FD1R / FD2R, FLV3; eg_latch2: RR from 0 and from 24, with and without a redundant key-off, D2R from 0;
  eg_latch3: RR with SA / LPCTL / LEA rewrites, CA never restarted; 110 + 112 + 137 = 359 informative even-E events, 0 latched).  tail_c's
  delayed RR is in-sample ordering: the envelope update runs before the fetch within a sample, so a write landing
  between the two phases is seen by that sample's fetch and by the next sample's clock ("Envelope clock").  Model:
  `egreg` / `eg_latch` removed; the sub-sample position of a write is not modelled (tail_cmp searches d = w00 - w14 in
  {1, 0, -1}; tail_c: w00 11300 / w14 11301).
- Model after session 6: eg_model 89/89, eg_replay 5 runs 4/4 (16/16 clean cycles), tail_cmp 12/12 (tail_c w14
  11301), feg_validate 9/9, validate_s5 PASS (transcript work/verify/s6/bitcheck.txt); koffpass_check on
  tests/feg_koffpass/model nextSeg 11/11 + 8/8; whole-program diffs against session 5: tests/probe MIXS0.l/.h readback
  lines (now match the console except one nibble where a model sample boundary fell between the write and the read),
  tests/sgc_keys K4 (CA 0 logged with the 0x1FFF, 5868 us).

## Session 7 (2026-09-23/24, REBOOTED console, K 0): the per-sample schedule (tests/eg_sched, eg_sched2, kon_defer, dsp_wslot)

Frame facts from outside the captures: the AICA bus runs at 22.5792 MHz (512 cycles per 44.1 kHz sample); the ARM7 gets
one memory slot per 8 cycles (measured on the console: 2.8224 MHz of ARM memory cycles); the manual's DSP buffer table
gives a DSP step two clock phases (T0 DSP read / T1 DSP write + CPU port).  So one 8-cycle frame = one SGC slot pass = two
DSP steps, 64 frames per sample.  What the captures add (all runs errors 0; every rate a constant row, so K-free):

- **K after a clean reboot is 0** (tests/eg_kprobe re-run: kfit 8/8 probes, K = 0 exactly, bit 13 = 0; the boot-1
  captures with K 6491 are kept in `tests/eg_kprobe/hw_boot1/`, eg_model replays both sets: kp_ 6491, kp2_ 0).  The two
  counters start aligned; the 6491 of the first boot accumulated during that boot, and none of the seven register actions
  of the probe moves it on either boot.  What shifted them there is still unknown (a DSP halt would do it).
- **Register writes act at the slot's own frame** (tests/eg_sched2: a VOFF-1 slot's SA[22:16] toggled between two
  constant blocks, anchored by a CPU MIXS write in the same write group -- its value appears on the first sample M after
  the next boundary, tests/mixs_rd).  The SA switch shows on M when the write landed after the slot's frame and on **M - 1**
  (the write's own sample) when it landed before it: the fraction of "M - 1" per slot, 96 events each, order 0 / order 1:
  slot 0 0.00 / 0.00, 8 0.06 / 0.06, 16 0.27 / 0.31, 24 0.42 / 0.27, 32 0.35 / 0.31, 40 0.44 / 0.50, 48 0.73 / 0.71, 56
  0.69 / 0.83 -- a line of slope about one sample per 64 slots: **frame k sits at phase ~k/64 of the sample, the sweep
  starting at the DSP's boundary**.  The fetch of slot k for sample n happens in that frame and is output in the same
  sample (no interpolator lag: the two words at CA, CA+1 are read every frame).  tools/sched2_check.
- **Key events: KYONEX is latched at the next boundary, the KYONB bits are read per slot at its frame in the FOLLOWING
  sample, and the slot starts on the sample after that** (tests/kon_defer: a KYONB written d us after the KYONEX is still
  honoured with probability falling from 7/8 at d = 0 to 0 at d = 21 us for slot 1, but 8/8 up to d = 24 us and 2/8 at
  d = 33 us for slot 62 -- the window is one sample plus the slot's frame position; the reverse test, KYONB cleared d us
  after the KYONEX, is the complement).  tests/eg_sched2: the witness key-on is at **E = M + 1** (order 1: 41-44 of 48
  events, the rest +2 = the KYONEX landed after the anchor's boundary), for every witness slot; two witnesses (slot 1..4
  and 60..63) keyed by one KYONEX always start on the same sample (tests/eg_sched, dh = 0 in 380 of 381 events).  So a
  key event shows one sample later than a register write from the same instant.  The model: `kyonex_pending` (the
  KYONB read at the next step, applied the step after); the sub-sample per-frame part of the window is not modelled.
  Consequence for test writers: never write a KYONB within ~2 samples after a KYONEX unless you mean it (the first
  eg_sched run keyed its witnesses off/on with the wrong KYONEX exactly this way).
- **The envelope value of slot k for sample n is computed at frame k of sample n - 1** (tests/eg_sched2 e_ runs, RR 0 -> 30
  on a held release, 24 events per slot): when the anchor sample M is a clock the first +8 step is on M for slot 56
  (10/10) and on M + 2 for slot 0 (13/13), the middle slots in between (8: 1/14, 16: 5/12, 24: 5/10, 32: 6/13, 40: 7/14,
  48: 10/12 at M); when M is not a clock the step is on M + 1 for every slot (the value for M + 1 is computed during M,
  after any write of M - 1).  This is the mechanism behind session 5's key-off-clock rule: the step applied on the key
  event's sample was computed a sample earlier with the old state.  It also explains tail_c (RR one clock behind SA).
  Whole-sample model (session 7): the envelope clock ran at the top of step() on the live registers, i.e. as if computed
  at phase 0 of the previous sample for every slot -- exact for slot 0, one clock early for writes that land before frame
  k of a high slot (sub-sample).  Session 8 moved it to its hardware place: see "Model step order".
- **DSP writes are posted with their own memory slot** (tests/dsp_wslot, 13 checks identical on console and model): an MWT
  at even step 2 next to an MRD at odd step 3 both complete, 15 writes at even steps interleaved with 15 reads at odd
  steps all complete (W5 15/15 + 15/15), the read latency stays s + 2 / s + 3.  (A first version of W6 lacked NOFL on
  step 5 and both platforms returned the float decode 0x105a00 of word 45 -- the NOFL-two-steps-before rule, not a slot
  effect.)
- **Model changes**: (1) KYONEX raises `kyonex_pending`; the KYONB bits are read at the next step() and the key events land
  the step after (E = M + 1 as measured; the model-linked validators write their key events one step earlier: eg_model,
  eg_replay, tail_cmp, feg_validate); (2) within a step every slot fetches (`stream_step`) BEFORE it outputs, except on its
  key-on sample (the SA effect on M instead of M + 1; the key-on sample still outputs the word at CA 0); (3) nothing else.
  Gates after the change: eg_model 97/97 (33 + 16 kprobe over two boots + 24 + 24), eg_replay 5 runs 4/4, tail_cmp 12/12
  (tail_c now w00 = w14 = 11301), feg_validate 9/9, filt_validate_model 265/265, filt_overflow 15/15 + 12/12, 61 cases
  exit 0, dsp_wslot / mixs_write / kon_defer / kon_probe(2) / kon_first console and model agree (the kon_ monitor traces
  differ only in poll timing).
- Tools: tools/sched_check (eg_sched: offsets from the witness), tools/sched2_check (eg_sched2: offsets from the MIXS
  anchor); cases/kon_probe.c, kon_probe2.c, kon_first.c (key-acceptance sequences: every sequence accepted on both
  platforms, incl. key-on after a key-off from "off" at any delay, one-write KYONB|KYONEX, KYONEX via another slot's
  register; a key-on after off without a key-off stays ignored).

## MIXS retention and CPU writes (tests/eg_lock mixs, tests/mixs_write, tests/mixs_rd; claim D2 resolved, T9, U3)

- **Every slot writes its ISEL bus every sample; IMXL is a pure gain (IMXL 0 sends 0); a bus keeps its last value
  only when NO slot points at it** (tests/mixs_write, 21 probe rows -- single IMXL-0 slots on a bus, 63 zeroed slots,
  slot 0 alone, VOFF / LPOFF / playing / SA variants, IMXL-15 and nobody-points-at-it controls -- tools/mixsw_check:
  rule H_G 21/21; the session-4 rule "a bus is rewritten only by a slot with IMXL != 0" 4/21; slot-0-always-writes
  9/21, VOFF- / LPOFF- / off- / SA-conditioned IMXL-0 writers 17-18/21, "bus 0 special" 5/21).  So a silent or off
  slot with any IMXL rewrites its bus with 0 every sample.  In eg_lock mixs, MIXS2 held -8 (the last value slot 2
  sent in the previous run) for 7193 samples because after `ch_zero_regs` every slot pointed at bus 0 (reg 0x20 = 0
  is ISEL 0 as well as IMXL 0) -- which is why bus 0 read 0 -- and nothing pointed at bus 2; the -8 itself is the slot
  filter's zero-input rest state (L 4, B -4) at the FEG's release hold 0x1BFF, Q 4, through the VOFF path
  (work/verify/s5/analysis_minus8.md; see the section below).  tail_c's retained values 0 2 2 (tests/slot_tail) are
  reproduced.  Model: `step()` marks every slot's bus written (`sent[]`), `MIXS_bank` keeps the others.
- **The DSP reads one of two MIXS banks on alternate samples**: a CPU write to an unused bus shows on every other
  sample (0xABCD / -8 alternating for the rest of the capture); a bus with a sender shows the CPU value for at most one
  sample (the sender rewrites the bank).  The SH4 read the high word of its own write back as 0 immediately, the low
  nibble as written.  dsp_basic "F ira 25" (alternating) and dsp_temp T2 (all 128 slots) differ by whether the
  write landed... both are consistent with two banks and a sender-free bus; the sub-sample order of the SH4 write
  against the slot's read-modify-write is not modelled.  Model: CPU writes go to the bank the DSP reads next.
- **The CPU reads the bank of the CURRENT sample parity -- the one its own writes go to** (tests/mixs_rd, session 6:
  write hi 0x1234 / lo 0x5, then 96 back-to-back hi/lo read pairs ~5.5 us apart = 4 pairs per sample, plus one read
  30 ms later; 7 runs).  On a bus rewritten every sample the written value reads back until the next sample boundary,
  then the SGC value: bus 0 with 64 zeroed slots pointing at it (R1: `1234/0` once, then `0000/0`; R5 lo written
  first: `1234/5` once), bus 5 with a playing writer (R3: `1234/5` for 3 pairs, then `0100/0` = 0x0100 x 16 at IMXL
  15) or a silent writer (R4: `1234/5` x 3, `1234/0`, then `0000/0`).  On a bus NO slot points at, the readback
  alternates every sample between the written bank and the untouched bank -- period 2 samples = 4 pairs each way:
  R6 (hi only) `1234/0` x 4 / `0000/0` x 4 ..., R7 (lo only, hi 0x1234 left in one bank by R6) `1234/5 1234/5 1234/0
  0000/0 0000/0 0000/0 0000/5` repeating; after 30 ms R6 reads `1234/0`, R7 `0000/0` (parity luck).  The low nibble's
  switch is seen one read (~5 us) before the high word's inside the sample (R4 `1234/5 -> 1234/0 -> 0000/0`, R7) --
  sub-sample, not modelled (the model switches both at the boundary).  Session 5's remark "the SH4 read the high word
  back as 0 immediately, the low nibble as written" (eg_lock, a bus every zeroed slot pointed at) was such a
  boundary-straddling read pair.  **Retention across programs**: bus 5 still held dsp_basic's 0x1234/5 write from a
  much earlier session before R2 (`R2 bus 5 before: 1234/5`, unchanged through 96 reads and 30 ms) -- a bus keeps its
  value across programs and hours as long as nothing points at it (see "Test-writing notes").  Model: `read()` of
  0x4500.. returns `MIXS_bank[samples & 1]` (was the DSP-side `MIXS[]` of the last step); tests/probe's MIXS0.l/.h
  readback lines now match the console except one nibble where a model sample boundary fell between the write and
  the read (`w55555555->r00000000`, console `r00000005`); tests/mixs_rd/model reproduces every run's pattern up to
  that sub-sample phase.  [claim U3]

## DSP (tests/dsp_basic — model matches hardware on every vector)

- Multiply: `ACC = floor(X * Y / 4096) + B` (X 24-bit, Y 13-bit signed; arithmetic shift, i.e. floor).
- Shifter (on the previous step's ACC): SHIFT 0 = clamp24(ACC), 1 = clamp24(2*ACC), 2 = wrap24(2*ACC),
  3 = wrap24(ACC). **minicast** computes X*Y>>10 into a 4x-scaled ACC and clamps SHIFT 0/1 to 20 bits — wrong.
- Pipeline: the SHIFTED value used by TWT/EWT/MWT/FRCL/ADRL at step s is from ACC of step s-1. B = ACC (BSEL)
  also reads ACC of step s-1. Step 127's ACC carries into step 0 of the next sample.
- COEF is indexed by step number.
- EFREG and NOFL memory writes store SHIFTED[23:8]. Float writes store PACK(SHIFTED), with the mantissa masked to
  11 bits for exponent 12 (MAME PACK; **minicast** is missing the mask).
- INPUTS: MEMS as stored (24-bit), MIXS << 4, EXTS << 8.
- Y: YSEL 0 = FRC_REG (13-bit signed), 1 = COEF, 2 = Y_REG[23:11] signed, 3 = Y_REG[15:4] **unsigned** 12-bit.
  FRCL: SHIFT 3 → SHIFTED[11:0] (unsigned), else SHIFTED[23:11]. YRL loads INPUTS. (= minicast)
- Memory read pipeline: an MRD at an odd step s delivers its word to IWT at step s+2 (it stays latched through s+3,
  until the next read lands). An MRD at an even step lands at s+3. The 16→24-bit conversion at IWT uses the NOFL
  bit of the instruction two steps before the IWT (not the MRD's own NOFL): with NOFL only on the MRD steps, IWT at
  s+2 gets the raw word and IWT at s+3 the float-decoded one.
- UNPACK: for exponent > 11 there is no hidden bit and bit 22 = sign (sign extension). **minicast/MAME** put 0
  there, which is wrong for negative words (0xFFFF → 0xFFFFFF on hardware, 0xFFF7FF in minicast).
- MWT (odd or even step) writes that step's SHIFTED, with that step's NOFL and address (tests/dsp_mem M4).
- Memory reads use the odd-step slots: an even-step MRD is served in the next odd slot (lands at s+3) and is lost if
  that odd step has its own MRD; two reads in flight (MRD at 3 and 4) both land, at 5 and 7 (tests/dsp_mem M5).
- An MRD in the same instruction as an MWT performs no read: its IWT then sees the memory read latch, which holds
  the last 16-bit word any master read — observed as the upper half of the CPU's previous 32-bit wave RAM read
  (tests/dsp_mem3).  Modelled (CPU reads update the latch); programs should never rely on it.
- Addressing (tests/dsp_mem, dsp_mem2): ADDR = MADRS[MASA] + (ADREB ? sext12(ADRS_REG) : 0) + NXADR; TABLE=0 adds
  MDEC_CT and masks to the ring (RBL 0..3 = 8K/16K/32K/64K words), TABLE=1 masks to 16 bits; byte address =
  RBP * 2048 + 2 * ADDR.  MDEC_CT decrements every sample.
- ADRS_REG is 12 bits, sign-extended when added: ADRL loads INPUTS[23:16] (SHIFT != 3; an 8-bit signed value) or
  SHIFTED[23:12] (SHIFT = 3).  A new ADRS_REG is used by the very next step.  (**minicast** adds it unsigned.)
- ACC is 26 bits: an accumulation chain wraps at 2^26 (SHIFT 0 then saturates the wrapped value) (tests/dsp_mem M7d).
- EFREG is overwritten, not accumulated (two EWTs in one sample: the later wins).
- INPUTS reads MEMS before the same step's IWT writes it; Y reads Y_REG before the same step's YRL loads it.
- NOFL for the IWT conversion is exactly the NOFL of step s-2 (only that bit matters; tests/dsp_mem2 E).
- Float format verified exhaustively: UNPACK for all 65536 words (tests/dsp_unpack), PACK for all 2^24 values
  (tests/dsp_pack, on-console comparison, CRC ef84ca79 on both platforms).  Shared code: src/dsp_float.h.
- TEMP ring (tests/dsp_temp, all 128 slots dumped): the TWT writer moves down one slot per sample (MDEC_CT) and
  rewrites every slot within 128 samples, as in the model.  The one stale slot seen once in dsp_basic
  "A ffff -4096 1" did not reproduce (a program-load transient of that run).  [claim D1]

## Model step order (session 8, 2026-09-24; for rtl/v1)

`step()` now runs one sample in the hardware's order (rtl/v1's frame plan), so that every register access between two
steps is exactly an access at the hardware's sample boundary -- the contract the rtl/v1 co-simulation relies on:
1. every slot fetches and outputs sample n (the key-on sample fetches CA 0), with the envelope value computed during
   sample n - 1 (Session 7: the envelope of slot k for sample n is computed at frame k of sample n - 1);
2. a stop armed by the previous envelope pass takes effect after this output (tests/slot_tail);
3. the slots' sends fill MIXS bank n & 1 (a bus nobody points at keeps its value);
4. a KYONEX written before this sample was latched at its boundary: every slot reads its KYONB now and the key events
   take effect on sample n + 1; the envelope pass computes sample n + 1 with that sample's clock and counter
   (MDEC_CT - 2);
5. the DSP of sample n runs on the bank the slots filled in sample n - 1 (`MIXS_bank[(samples - 1) & 1]`; before it
   read `samples & 1`), channel collisions applied (next section).
`Slot::FEG.vo` keeps the cutoff sample n used (after step() FEG.v is already sample n + 1's).  The model-linked
validators follow: eg_model / feg_validate / tail_cmp anchor MDEC_CT + 2 at the effect sample, eg_replay + 1, and the
FEG compare uses FEG.vo.  Regression (`work/verify/s8/bitcheck_s8.txt`): eg_model 97/97, eg_replay 5 x 4/4, tail_cmp
12/12 (tail_c FULL with its two writes in the same step, 11301/11301, tied with 11300/11301), feg_validate 9/9, filt_validate_model 265/265, filt_overflow 15/15 + 12/12,
mixs_write identical, coll_check 18/18, all 61 cases exit 0.  Against the session-7 baseline 274 of 563 output files
(48 capture-based cases) changed -- the captured streams move with the new order; the validators that compare them with
the console all pass, and the files identical to the console are the same 30 of 582 before and after.  New baseline:
`work/model_outputs_2026-09-24.sha256` (582 files, reproduced by a second full run).

## Channel / DSP collisions (tests/dsp_coll, session 8, 2026-09-24; console = model 18/18 runs, `build/tools/coll_check`)

The wave RAM has one shared slot per DSP step; a playing channel K owns the slot of step 2K - 14 (wren7-rtl
tests/hw/sgc).  wren7 tests/hw/collide2 showed that a DSP access there loses.  tests/dsp_coll pins the rule sample by
sample with the DSP logging, every sample, what an MRD at the channel's step returned and the channel's own output
(MIXS0, VOFF, so MIXS0 / 16 is the sample word), plus an MWT there (and two steps later as a control):
- **An MWT in the channel's slot is dropped; an MRD there returns the channel's word instead of its own.**  Reads and
  writes at other steps are untouched, including a read one or two steps before the channel's (latch_odd /
  latch_even: the channel's fetch between the DSP's read and its IWT does not replace the DSP's word).
- **The channel's word is the 16-bit word holding its sample CA + 1** (the interpolation's second sample, after this
  sample's step): the last word it reads.  PCM16 at pitch 1, 1/4 and 1.33 (the read runs 2 words ahead of MIXS0 at
  pitch 1, i.e. s1 of the fetch), PCM8 (the word holding byte CA + 1), ADPCM OCT -2..+2 (the word holding nibble
  CA + 1).
- **Which samples**: PCM16 / PCM8 every sample while the slot plays (MWT dropped 1584/1584).  ADPCM holds one 16-bit word
  and fetches when CA enters another word or CA + 1 lies in the next one: the MWT is dropped in exactly those samples
  (OCT -1: 3 of every 8), which confirms at sample level the rule found from wren7's timing kernels (tests/hw/sgcadp).
- **The key-on sample**: the first fetch is in the sample that outputs CA 0 (the word holding sample 1), not in the
  sample of the envelope pass that keys on (PCM16 K 20 and K 3, PCM8, ADPCM).  The model now decodes the initial
  samples there (step(): decode_initial on the key-on sample; key_on() only resets).
- **Channels 0-6** (steps 114..126, the last eighth of the DSP sample) collide with their fetch of the NEXT sample: the
  K 3 read runs 3 words ahead of MIXS0 where K 20's runs 2.  So the DSP sample boundary lies between channel 6's and
  channel 7's fetch -- the frame alignment rtl/v1 derived from wren7's measurements (DSP step 0 at ph 64).
- Model: `slot_fetch` (claim + word), recorded per slot in the SGC loop; `coll_preview` computes slots 0-6's next
  fetch before the DSP runs, with this sample's registers (a register or RAM write between the two samples that
  changes that fetch is the one thing it cannot see); `dsp_step` drops a colliding MWT and lands a colliding even-step
  MRD with the channel's word.  Every other case's model output is unchanged by this (61 cases compared).
- Not covered: with the ARM running, ARM reads also reach the DSP's read latch (wren7 NOTES, collide: MEMS0 of even-step
  reads); which ARM read does it is not pinned.  The case holds the ARM in reset (aica_quiet).

## Cycle model and the per-frame schedule (session 9, 2026-09-24; tests/sub_frame, sub_sched, sub_env, oneshot)

The model is now clocked (`cycle-model/aica_model.{h,cpp}`: `clock()` = one MCLK, `step()` = 512 clocks); the
sample-based model it replaces stays in `sample-model/` (the validators build against both: `build/tools/<name>` and
`<name>_cycle`).  Every access goes through the model's SH4 port at a clock (host harness `cycle-model/io_cycle.cpp`,
`./run_cycle.sh CASE`, outputs in `tests/<case>/cycle/`), and rtl/v1 co-simulates it clock for clock through its SH4
port (`rtl/v1/tb/cosim_cycle.cpp`, `tb/gate_cycle.sh`).  The frame plan is rtl/v1's below the console's resolution and
the console's where a capture resolves it.  A write "acts in this sample" when its X0 is before the start of its
stage's frame + 2 clocks (T below, relative to slot k's frame start 8k):

| Stage | Frame | T | Registers | Measured by |
|---|---|---|---|---|
| Fetch (phase, loop, fetch, LFO step, pending stop) | k | 2 | 0x00-0x1C | sub_frame SA |
| Key events (KYONEX latched at the boundary, KYONB read per slot) | k | 2 | KYONB | sub_sched exp 0 |
| Filter and level | k + 4 | 34 | 0x28 (TL, VOFF, LPOFF, Q) | sub_frame TL / VOFF / LPOFF |
| Envelope pass (the envelopes of the NEXT sample) | k + 5 | 42 | 0x10 0x14 0x18, 0x30-0x44 | sub_env, sub_sched exp 1 (RR) |
| Send and direct outputs | k + 7 | 58 | 0x20, 0x24 | sub_frame IMXL |

- The MIXS write of slot k's send lands at c7 of frame k + 8; slots 60-63's level, 59-63's envelope pass and 57-63's
  send run in the next sample's first frames.  The sweep's MIXS writes still fill exactly one DSP sample window (the
  DSP boundary is at ph 64).
- Inside a frame (model and RTL): registers 0x00-0x1C at c0, the fetch at c4, 0x20-0x4C at c5, the key events and the
  CA monitor at c6; the envelope pass samples its registers at c1 of frame k + 5 and computes (and writes the EG
  monitor) at c7; the level and send stages read their registers at c1 of their frame.
- Measured only through their stage's neighbours, not on their own: 0x24 (with the send), the ALFO depth / waveform
  (0x1C, fetch), FLV and the FEG rates (with the envelope pass), Q (with LPOFF), the monitor update points.
- The DSP (step s at ph 64 + 4 s) and the bus (DcArmBus rules, X0 at t2 of a step) as in rtl/v1.

**Method.**  A DSP logger (`cases/flog.h`) records MEMS31 in every frame (8-clock resolution) and chosen buses every
sample.  Each access under test is queued between two MEMS31 marker writes in one G2 burst (`io_wn`), so its X0 is
bracketed; the SH4's writes reach the AICA 28-32 clocks apart even when queued back to back, and the markers'
midpoint sits about 5 clocks before the true X0.  The checkers (`build/tools/sub_check`, `sched_check`) replay every
event through the cycle model with the access at every clock the markers allow; an event is determinate when every
candidate predicts the same observation.

**tests/sub_frame** (5 experiments x 64 slots x 40 events; SA, IMXL, TL, VOFF, LPOFF): 12800 events, 11333
determinate, **11333 agree, 0 fail**.  The previous frame plan (0x20-0x4C all read in frame k) failed 143 of the TL /
VOFF / IMXL events.  Writes early in a sample change the previous sample's send of slots 60-63 (observed).

| Write | Markers' midpoint switches between | Model T |
|---|---|---|
| SA (0x00) | -4 / 0 | 2 |
| TL, VOFF, LPOFF (0x28) | 28 / 32 | 34 |
| IMXL (0x20) | 52 / 56 | 58 |

**Even and odd slots are alike** (the Saturn-style "two slot processors" question): the best midpoint threshold is
the same for even and odd slots in every experiment within the estimate's 4-clock grain (SA -3 / -3, IMXL 53 / 53,
VOFF 29 / 29, LPOFF 29 / 29, TL 29 / 25 with 4 of 1132 odd events misplaced), and the exact replay needs one T for
all 64 slots.  That rules out an even/odd phase offset and two 32-slot halves running side by side; two identical
engines interleaved frame by frame cannot be told from one 8-clock pipeline (the send at +56 clocks, 3.5 x 16, fits
the single pipeline slightly better).

**tests/sub_sched** (per slot: exp 0 12 x 2, exp 1 12 x 2, exp 2 / 3 32 events):
- exp 0, KYONB window: a KYONEX burst, then KYONB 1 d = 0..35 us later.  KYONB is read per slot at its frame (T 2),
  the KYONEX at the sample boundary: 1369 determinate, **1366 agree, 3 fail**.  Open: the 3 failures are key-ons whose
  KYONEX landed within a few clocks of the sample boundary on a slot not yet released; the console keyed on one sample
  late.
- exp 1, envelope pass: a held slot (RR 0) gets RR 30; the first sample below the held level shows which pass used
  it: 1459 determinate, **1459 agree** with the pass at frame k + 5.  tests/sub_env (exp 1 only, 4 batches): 2864 /
  2864.  The old rule (envelope computed in frame k of the previous sample) fails these.
- exp 2 / 3, CPU MIXS write + read on a bus with no writer / with a writer: the CPU's writes and reads go to the bank
  the DSP reads (dsp_bank): 1259 and 1279 determinate, **0 fail**.

**tests/oneshot: the one-shot end** (LPCTL 0, CA reaching LEA; slot 0, LEA 64, runs H / D / A, EG and CA monitors
polled into a buffer through the end, a KYONEX with KYONB still 1, a key-off, a key-on).  Only the fetch stops: CA
reads the stepped value (LEA) for one sample and 0 from the next, LP is set, and the envelope keeps its state and
level and keeps stepping (decay 1 at D1R 12 keeps rising after the end).  A KYONEX with KYONB 1 changes nothing (the
state is not RELEASE); the key-off releases from the current level; the key-on then restarts normally.  The slot is
not "off" until the level reaches 0x3C0 (the stop is armed by `!off`, not by `enabled`).  minicast's rule ("release,
a = 0x3FF, off") is wrong.  Console = model in every CA sequence of all 12 windows (both models).  The EG sequences
differ in two known ways: slow-rate steps (D1R 12, AR 6) fall on the boot's K and MDEC_CT phase (TODO 3), and the
monitor shows (attack, a = 0) for one sample before decay 1 (TODO 5.2).

**Tools' view.**  `MIXS[]` after `step()` holds the sweep `step()` just ran; the sends of slots 57-63 happen early in
the next sample, so `publish_mixs` completes them with the registers as they are at the boundary.  A write to 0x20 / 0x24
/ 0x28 of those slots at the boundary acts on their sends of the finished sweep in the model and the RTL (as on the
console), not in that snapshot; no validator does that.  Likewise an EG monitor read at the boundary of slots 59-63
shows the envelope pass of the previous sample (kon_defer, kon_probe2 monitor slot 62).

**Gates (2026-09-24, after the envelope pass moved to frame k + 5 and the one-shot rule).**
- `cycle-model/replay_all.sh` (the sample model's traces replayed into the cycle model at the sample boundary): 60 of
  65 cases identical in every read and output sample; all 65 identical in every output sample.  The 5 read
  differences are the pipeline: logged ring words one sample apart (sub_frame 976, sub_sched 677, sub_env 1224) and
  the slot-62 EG monitor read at the boundary (kon_defer 38, kon_probe2 15).
- Validators, both models: validate_s5 PASS (eg_model 97/97, eg_replay 5 x 4/4, tail_cmp 12/12, mixs_write
  identical), feg_validate 9/9, filt_validate_model 265/265, filt_overflow 15/15 + 12/12.
- Console checkers on the cycle model: sub_check 11333/11333, sched_check sub_sched 3 fail (the KYONB edge above) /
  sub_env 0 fail; on the model's own runs 0 fail everywhere.
- rtl/v1 through its SH4 port at the cycle model's clocks (`tb/gate_cycle.sh`): 65 / 65 cases, ack clocks, read data
  and output samples identical.  After all session-9 changes (reset, preamble, timers, LFO / noise rules): 72 / 73
  cases, adpcm_hi the exception (ADPCM above 4 nibbles a sample: outside the stepping limits, TODO 6.1).  With random
  0..2047 ns delays on every access: seed 1 65 / 65; seed 2 64 / 65 (sgc_formats' trace run failed: its model binary was rebuilt during the run); seed 3 (the cases with the item-2 reset and the replay preamble, the RTL with the replay load port) 67 / 68 -- adpcm_hi differs, ADPCM above OCT +2 being outside the RTL's stepping limits (`warn_step`; TODO 6).

## Known start state and replay parameters (session 9, 2026-09-24; TODO 2-3; tests/replay_check)

**Start state.**  Every case starts with `aica_reset(rbp, rbl)` (`aica_quiet()` uses the default ring at 0x1E0000,
64K words): the ARM held, every channel keyed off and zeroed, then `dsp_reset` -- all 128 MPRO steps NOP from step 127
down, COEF / MADRS / EFREG / TEMP 0, the ring cleared and selected, MEMS 0 in all 24 bits (a one-sample IWT program
after a CPU read of a zero ring word), every MIXS bus 0 in both banks (slots 0-15 pointed at buses 0-15 for three
samples, their 0x20 restored) -- and every slot's LP flag cleared (one EG monitor read per slot).  `cap_start` and
`flog_start` run `dsp_reset` too: before, `cap_start` zeroed only the MPRO steps its own run had loaded, so a longer
program left by an earlier case (flog's 128 steps) kept running in the upper steps.

**Replay parameters.**  What a console run cannot set -- MDEC_CT, the envelope constant K and the noise LFSR -- is
measured by a preamble every platform's main runs before `test_main` (`cases/common/replay.c`, ~1.1 s): a cap.h
capture of 4 streams, decay 1 at effective R 3 / 13 / 45 from an R 63 attack (eg_kprobe's probe) and a noise slot at
VOFF / LPOFF / IMXL 15 (its bus = the LFSR byte << 12), 1 s keyed on, then a sync point: the SH4 waits for the next
counter word, whose ring address is that DSP sample's MDEC_CT X.  `tools/replay_fit DIR` (run by `run_hw.sh` after
every case) fits K with kfit's rules (`tools/kfit_core.h`) and the LFSR by trying all 2^17 states, and writes
`replay.txt`: "mdec X lfsr L K k", L = the LFSR at the sample boundary after the sync sample (the ph 0 with MDEC_CT
X - 1).  The models apply it (`CAIQUE_REPLAY`, set by `run_model.sh` / `run_cycle.sh` from tests/<case>/<REPLAY_FROM>/)
at their own next ph 0 after the sync point; rtl/v1 takes it through `ld` in the clock before (MDEC_CT + 1: the RTL
counter is the running DSP sample's; the LFSR one step back: the RTL steps a slot's LFSR at its stage B frame, the
model at the end of the slot's frame).  LFSR convention (both models): one step per slot, slot k's byte =
low8(step^(k+1)(L)) with L the value at ph 0; the DSP sample X reads the previous sweep, so capture sample X shows
low8(step^4(f(X + 1))) for slot 3, f(X) = the LFSR at the ph 0 at which MDEC_CT = X.

**Console (tests/replay_check, K 0 boot).**
- K = 0 (as eg_kprobe measured on this boot); R 3 gives one K, R 13 eight, R 45 2048, intersected: one.
- **The noise LFSR is the model's**: one 17-bit state fits all 43728 noise samples of the preamble capture (and of
  the second one), so x^17 + x^12 + 1, one step per slot, 64 per sample, and the slot-byte position are right.
- **It free-runs**: the second measurement, 62187 samples later with a full `aica_reset` in between, is the first one
  stepped 64 x 62187 times (`replay_fit -same`).  So the LFSR is not reset by anything the program does, and it steps
  while no slot plays.
- Both models given the console's preamble parameters measure the console's K and LFSR / MDEC_CT relation at the
  second sync (their sync sample is 675 samples earlier: harness timing, TODO 3.4).

**0x2804 bit 15 (probably TESTB0) moves MDEC_CT against the envelope counter -- found, NOT investigated (TODO 7.1).**
The hw9 console session (all 66 cases re-run with the preamble, `work/verify/hw9_replay.txt`): every run up to
tests/probe's own preamble fits K 0 with the envelope clock on even MDEC_CT, every run after it K 10923 on ODD
MDEC_CT.  probe's rw_mask writes 0x2804 with FFFF / 0 / 5555 / AAAA (then restores the old value); eg_kprobe's
actions only ever wrote RBP/RBL there.  tests/k_jump (one run, `tests/k_jump/hw9`, a replay measurement after each
write; `replay_fit tests/k_jump/hw9 kj<N>`):

| step | write before the measurement | K | clock on MDEC_CT |
|---|---|---|---|
| kj0 | none | 10923 | odd |
| kj1 | 0x2804 = 0x8000, then the default ring | 2230 | even |
| kj2 | 0x2804 = 0x1000 | 2230 | even |
| kj3 | 0x2804 = 0x5555 | 2230 | even |
| kj4 | 0x2804 = 0xAAAA | 9791 | even |
| kj5 | 0x2804 = 0xFFFF | 1225 | odd |
| kj6-8 | EXTS0/1; MIXS0 / EFREG0 / EFREG15; TEMP / MEMS / COEF / MADRS / MPRO words | 1225 | odd |

Every write with bit 15 set moved the relation (by an amount that looks arbitrary); no other write did.  Not yet known:
which counter moves (MDEC_CT or the envelope counter: each kj log's sync line has MDEC_CT at an SH4 time, the data to
check continuity), whether bit 15 is a reset held while set or an edge, what else it touches, and whether this is what
set K 6491 on the earlier boot (the "what offsets K during a boot" open item).  Replay needs the clock parity:
`replay_fit` now searches K and the parity (`kfit_core.h` k_par) and writes "par p" into replay.txt.

## Timers and interrupts (session 9, 2026-09-24; TODO 4; tests/timer_irq, timer_phase, irq_bit9)

All measured from the SH4 (hw9 session); both models and rtl/v1 (aica_bus) implement it, `tools/timer_irq_check` checks
a timer_irq log against these rules -- the console's and both models' pass -- and the co-simulation of timer_irq,
timer_probe, timer_phase, probe, eg_kprobe and k_jump is clean (7.4 M reads in timer_irq, mostly MCIPD polls).
- **Registers.**  TIMA/B/C (0x2890/94/98: count 7:0, prescale 10:8), SCIRE, SCILV0-2 and MCIRE read 0 (write-only);
  SCIEB / MCIEB store 11 bits.  SCIPD (0x28A0, the ARM's) and MCIPD (0x28B8, the SH4's) are **separate** pending
  registers set by the same sources: MCIRE clears only MCIPD, SCIRE only SCIPD.  A CPU write to a pending register
  sets bit 5 (SCPU) and nothing else.
- **The SH4's line** (SB_ISTEXT bit 1) is (MCIEB & MCIPD) != 0.  (A line read right after an MCIRE write can still see
  the old value: the write is posted in the G2 FIFO, SB_ISTEXT is read directly.)
- **Bit 10** sets at every sample edge (one sample apart).  **Timers**: at the edge of the samples whose MDEC_CT =
  tim_phase mod 2^prescale the count increments; when it wraps to 0 bit 6 / 7 / 8 sets (in both pending registers);
  the count continues from 0 (the second overflow is 256 x 2^P samples after the first: no reload of the start value).
  One prescaler for all three timers, never restarted by a write (a prescale change included): the first overflow
  after a write of count S comes within ((255 - S) 2^P, (256 - S) 2^P] samples.  minicast restarts a timer's
  prescaler on a prescale change: wrong.
- **Phase** (tests/timer_phase: the DSP frame logger brackets each timer write and each detection): tick samples have
  MDEC_CT = 1 mod 4, 5 mod 8, 21 mod 32 under the logger's labelling, consistently across P and across A/B/C.  With the
  edge at ph 24 (wren7's: 40 clocks before DSP step 0; the poll latency, ~60 clocks uncertain, cannot place it
  better), tim_phase = 20 mod 32 in this console state.  Both models and the RTL take tim_phase as a parameter
  (default 0).  Whether it follows MDEC_CT or the envelope counter across a 0x2804 bit-15 write: TODO 7.1.
- **Bit 9** (MIDI out in the manual) set once in tests/timer_irq's register sweep, at the end of timer_probe and at one
  fixed step of irq_bit9 (after TIMA = 0x0700; the same step in two runs), but no single register write reproduces it
  (every timer x prescale x count, SCIEB/SCILV/MCIEB/pending/0x2808 writes).  Not modelled.
- **SH4 timing artefacts** (the checker's tolerances): the SH4's microsecond timer runs 0.26 % fast against the AICA
  (22.618 us per sample); the SH4 is held now and then (~60 us often, ~2.5 ms sometimes, with the time base off by up
  to the hold's length), so every poll records its last and longest gap.
- Not done: the ARM side (FIQ from SCIEB & SCIPD through SCILV0-2, the L / M handshake of wren7 tests/hw/fiqdiag) --
  rtl/v1's L reads 0 and M is ignored (TODO 4.3).

## ADPCM above 4 nibbles per sample (session 9; TODO 6.1; tests/adpcm_hi, adpcm_pitch hw9)

Bypassed slots (VOFF, LPOFF) on random bytes, replayed through the cycle model (`tools/stream_replay`):
- **Up to 4 nibbles per sample the model is exact**, PCMS 2 and 3: OCT 1 FNS 000 (2), OCT 1 FNS 200 (3), OCT 2 FNS
  000 (4) -- every sample of 2594..2785 (with sgc_formats' 1.0 and 1.37).
- **Above 4 the console differs from the second or third sample on** (the model decodes every nibble it steps over):
  OCT 1 FNS 3FF (3.998 per sample: steps of 3, then 4 with a carry) plays two samples, then reads 0 for good; OCT 2
  FNS 300 (7) cycles through four values (0, 4240, 864, -2768); 5 and 6 per sample mix plausible values, 0s and
  saturation; 8 per sample (OCT 2 FNS 3FF) looks like a plausible decode.  The key-on sample (the nibble at CA 0) is
  right in every case.  This fits wren7's fetch rule (the channel holds one 16-bit word, 4 nibbles, and fetches at most
  one word per sample): past 4 the decoder runs out of fetched nibbles.  Not modelled yet; the models and rtl/v1 still
  step up to 8 (rtl/v1 raises `warn_step` beyond).
- **OCT 3..6: the slot holds its key-on sample's value** (-2768 / 4240 in these runs) for good -- no fetch (wren7
  tests/hw/sgcadp), no further decode.  OCT 7 FNS 3FF moves slowly (6688, 6400, 6130, ... -270 a sample) -- another
  case.  The models decode hundreds of nibbles a sample there.
- **An odd SA** (SA + 1: the start nibble in the byte's other half) decodes a different first nibble than the model
  (4240 against 1744 on the key-on sample, at every pitch).

## Open items (after session 7)

- K at boot: 0 after a clean reboot, 6491 on the previous boot; nothing a program does moves it (eg_kprobe on both boots).
  What offset the envelope counter from MDEC_CT during boot 1 is unknown.
- Sub-sample effects: measured and modelled in session 9 (the cycle model; "Cycle model and the per-frame schedule").
  Still open there: 3 KYONB-window key-ons near the sample boundary; the low-nibble/high-word MIXS write order.
- The absolute position of frame 0 against the DSP boundary is known only to about a frame (the fetch fraction at slot 0
  is 0.00, at slot 8 0.06).  Session 8: the DSP boundary lies between channel 6's and channel 7's fetch (tests/dsp_coll).
- Filter: nothing open.  The cap_start head estimate still moves whole-program model captures by a sample when the ring's
  leftover words change (harness).
- Where ADPCM / noise / high-pitch slots take their memory words in the 8-cycle frame, and the CPU/DMA slot phase, are RTL
  details no capture can see.

## Test-writing notes

- Load/unload DSP programs from the last step down (cases/aica_io.h prog_load): the DSP keeps running while the
  CPU rewrites MPRO, and a later step (IWT) must never outlive the earlier one it depends on (MRD, NOFL).
- Every sub-test must start from a known DSP state; a previous program's IWTs/in-flight reads contaminate MEMS.
- A MIXS bus that no slot points at keeps its value across programs -- for hours, across console runs of unrelated
  cases (tests/mixs_rd R2 found dsp_basic's 0x1234/5 still on bus 5).  A test that reads or captures a bus must
  point a slot at it (any IMXL) or write it first; `aica_quiet` alone (ISEL 0 everywhere) only clears bus 0.
- 32-bit wave RAM accesses must be 4-byte aligned (address error on the console; the model aborts too).
- Console output path: hw/io_kos.c writes to /pc + MODEL_ROOT (the tree the case was built in, set by hw/Makefile),
  so a copy of the tree captures into itself; a failed open/write/close makes the case exit non-zero (before
  session 3 the path was hard-coded to the main tree and save errors still reported success).
