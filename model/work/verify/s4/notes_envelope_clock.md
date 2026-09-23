## Envelope clock, key timing and increment rows (tests/eg_lock, feg_krs; session 4, 2026-09-23)

The envelope clock is **observable**: it is locked to the DSP's ring counter.

- **MDEC_CT is a free-running 16-bit sample counter** (masked to the ring at the DSP address, wrapping at 64K, not
  reset by the RBP/RBL write or a DSP program load); cap.h records sample n at ring address c0 - n = MDEC_CT, so
  every capture carries the counter of every sample (c0 from the case's `cap_start:` log line, n_first from the
  header).
- **The envelope clock ticks on every sample whose MDEC_CT is even**, for the AEG and the FEG alike, and its counter
  is `eg_cnt = K - MDEC_CT/2 (mod 2^14)` with **one constant K per console boot**: K = 6491 (mod 16384; 14683 is not
  excluded by rate 2, which pins mod 8192) for tests/feg_track, aeg_dl0, eg_lock and feg_krs; tests/sgc_loop, captured
  before a reset, has K = 165 mod 1024.  Evidence: tools/eg_phase.cpp (the AEG of every constant-input capture
  predicted from the level law with K the only free parameter: aeg_dl0 4/4 streams, eg_lock att_slow (AR 6/4/2/1,
  487,478 samples each) 4/4, att_mid 4/4), tools/eg_keys.cpp (32 key-on/off cycles on slots 0..3 and 60..63: every
  first envelope step on an even sample, 0 violations), tools/feg_validate.cpp / feg_lock.cpp (FEG), and
  tools/eg_model.cpp: the production model with MDEC_CT set from the capture reproduces **33/33 streams sample by
  sample** (aeg_dl0, eg_lock x 8 runs, sgc_loop lo_2 with LPSLNK, feg_odd, feg_krs x 4, feg_track x 3: 1.17 M
  samples).  Model: `AicaModel::eg_K`; the clock is derived from MDEC_CT in `step()`.
- **Key events take effect on the sample after the KYONEX write, whatever its parity.**  The key-on sample takes no
  envelope step: the key-on level lasts 2 samples when that sample is a clock, 1 when it is not (eg_lock keys: 32
  cycles, both cases).  The old model applied key events at the next clock (level always 2 samples).
- **A key-off sample that is a clock steps toward the release target with the increment of the segment the envelope
  was in** (the rate lookup lags the state change by one clock): feg_krs fk_1 / fk_3 (decay 2 holding short at
  0x19FE with +4, release +1: the first release clock moves +4 to 0x1A02, then +1 per clock); when the key-off sample
  is not a clock the next clock uses the release rate (fk_2, feg_odd).  This closes the old E4 anomaly: in
  feg_track batch 1 the KRS-5 slot "released one clock early" because its old segment (decay 2, +8) had a nonzero
  increment on the key-off clock while its siblings sat in slow attacks whose increment at that counter was 0;
  batch 2 the same (slot 1 in attack R 36 with a +1 tick, slot 0 at R 48 with an invisible +1, slot 2 at rate 0).
  With the rule, all slots of every batch share one key-off sample (eg_model).  Measured on the FEG; the AEG uses
  the same code (its eg_lock key-offs happened from rate-0 segments, increment 0, consistent).
- **The R < 48 counter offset (-1) applies to the AEG too**: the AEG captures and the FEG captures agree on the same
  K only with the same offset (eg_phase / eg_model).
- **Increment rows for R = 1 mod 4 at R >= 48 (rows 5, 9, 13) are {b, 2b, b, b, b, 2b, b, b}**, the double step at
  index 1 and 5, not the YM2612's index 3 and 7; every other row is the OPN table (rows 1, 3, 7, 11, 15 measured at odd
  effective rates through KRS: eg_lock odd_att / odd_att3 / odd_same / odd_dec, feg_odd, feg_krs).  This is the
  other half of E4 (the "counter phase differing by 2 mod 4").
- **R = 63 attack**: the level is 0 from the key-on sample, but the envelope leaves the attack on the next clock (a
  step landing at 0), so decay 1 first steps one clock later (eg_lock odd_dec).
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

## MIXS retention and CPU writes (tests/eg_lock mixs; claim D2 resolved)

- **A MIXS bus that no slot sends to (IMXL 0 on every slot targeting it) keeps its last value**; a configured but
  silent slot (IMXL != 0, off) rewrites its bus with 0 every sample.  In eg_lock mixs, MIXS2 held -8 (the last value
  slot 2 sent in the previous run) for 7193 samples with nothing sending to it.  Model: `MIXS_bank`, a bus is
  rewritten only when a slot with IMXL != 0 targets it.
- **The DSP reads one of two MIXS banks on alternate samples**: a CPU write to an unused bus shows on every other
  sample (0xABCD / -8 alternating for the rest of the capture); a bus with a sender shows the CPU value for at most one
  sample (the sender rewrites the bank).  The SH4 read the high word of its own write back as 0 immediately, the low
  nibble as written.  dsp_basic "F ira 25" (alternating) and dsp_temp T2 (all 128 slots) differ by whether the
  write landed... both are consistent with two banks and a sender-free bus; the sub-sample order of the SH4 write
  against the slot's read-modify-write is not modelled.  Model: CPU writes go to the bank the DSP reads next.
