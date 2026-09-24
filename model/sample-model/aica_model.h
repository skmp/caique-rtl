/* aica_model.h -- caique AICA behavioural model: sound generation core (64 slots), DSP, mixer.
 *
 * Register-level interface mirroring what the SH4 sees at 0x00700000: write()/read() of 16-bit registers in 32-bit
 * slots, wave RAM access, and step() = one 44.1 kHz sample.
 *
 * Provenance: the SGC started as a port of minicast libswirl/hw/aica/sgc_if.cpp (tables, EG/LFO/stream logic);
 * the DSP is minicast's interpreter rewritten to the semantics measured on hardware (tests/dsp_*).  Every deviation
 * from minicast is listed in NOTES.md with the test that forced it.
 */
#pragma once
#include <stdint.h>
#include <string.h>
#include "../src/dsp_asm.h"

namespace caique {

enum EgState { EG_ATTACK = 0, EG_DECAY1 = 1, EG_DECAY2 = 2, EG_RELEASE = 3 };

struct Slot {
    /* register fields are decoded on every access from AicaModel::reg */
    uint32_t CA;           /* current sample address (samples from SA) */
    uint32_t step;         /* phase fraction, 14 bits */
    uint32_t update_rate;  /* phase increment per sample, 1/2^14 sample units */
    int32_t s0, s1;        /* samples at CA and CA+1 */
    uint32_t LSA, LEA;
    bool looped;
    bool in_loop;          /* CA has reached LSA since key-on (arms the loop-end check) */
    int32_t adpcm_quant;
    struct {
        uint16_t a;        /* 10-bit attenuation, 0.09375 dB units (TL counts 4); keeps stepping up to 0x3FF after the
                            * sample fetch has stopped at 0x3C0 (tests/slot_tail) */
        bool off;          /* stopped: the monitor reads 0x1FFF from the sample after the clock that reached 0x3C0
                            * (tests/ca_stop); the value still steps to 0x3FF and the output is NOT muted: the saturated
                            * level keeps applying to the (zero-input) filter tail (tests/slot_tail tail_b: -16 on its
                            * negative half-waves) */
        EgState state;
    } AEG;
    struct {
        EgState state;
        uint16_t v;        /* 13-bit filter envelope value (cutoff, FLV scale); after a step: already the next sample's */
        uint16_t vo;       /* the value this sample's filter used (v before the envelope pass of the step) */
        int8_t dir;        /* direction of the current segment, latched when it starts (tests/feg_track) */
        bool passed;       /* attack / decay 1: the value has crossed the target (advance on the next clock) */
    } FEG;
    int32_t lpf_low, lpf_band; /* filter state, 1/8 sample units; never cleared (tests/filt_reset) */
    struct {
        uint32_t counter, start_value;
        uint8_t state, alfo_w, alfo_shft, plfo, plfo_shft; /* alfo_w: 8-bit ALFO waveform value */
    } lfo;
    bool enabled;         /* sample fetch running; cleared (zero input, the filter keeps running, CA reads 0, monitor
                           * 0x1FFF) one sample after the clock on which the AEG reached 0x3C0 (tests/slot_tail, ca_stop),
                           * or by a one-shot end */
    /* slot stop pipeline (tests/slot_tail): a clock that brings the AEG to 0x3C0 arms the stop, which takes effect on the
     * NEXT sample (the clock sample still outputs the sample already fetched), so aeg_clock only sets this countdown and
     * step() commits it after slot_output (0 = nothing armed) */
    uint8_t stop_in;
    bool ca_clr;          /* a one-shot end: CA reads 0 from the next sample (tests/oneshot) */
    bool keyed_fetch;     /* keyed on by the envelope pass of the previous sample: this sample's fetch does not advance */
    bool keyed;           /* key-on applied on this sample: no envelope step on it (an R 63 attack still leaves the attack
                           * state on it when it is a clock, tests/eg_kprobe) */
    bool keyed_off;       /* key-off applied on this sample: a clock on it takes one more step of the segment the envelope
                           * was in -- its increment (tests/feg_krs, feg_track batches 1/2, aeg_koff koff_d2) and, for the
                           * FEG, its direction (tests/feg_koffdir) -- unless that segment was the attack (tests/aeg_koff
                           * koff_att: no step) */
    EgState aeg_prev, feg_prev; /* the states before that key-off */
    int8_t feg_prev_dir;        /* the FEG direction before that key-off (key_off() recomputes FEG.dir toward FLV4) */
    bool feg_prev_passed;       /* the FEG's passed flag before that key-off: the key-off clock then steps the NEXT
                                 * segment's way (tests/feg_koffpass) */
};

struct AicaModel {
    static const uint32_t RAM_SIZE = 2u << 20;
    uint8_t *ram;              /* RAM_SIZE bytes */
    uint16_t reg[0x8000 / 4];  /* register file, one 16-bit value per 32-bit slot, offset >> 2 */

    Slot slot[64];

    /* DSP state */
    int32_t TEMP[128];  /* 24-bit */
    int32_t MEMS[32];   /* 24-bit */
    int32_t MIXS[16];   /* 20-bit, accumulated by the SGC each sample; a bus no slot sends to keeps its value, and
                         * the DSP reads one of two banks on alternate samples (tests/eg_lock mixs) */
    int32_t MIXS_bank[2][16];
    int32_t EXTS[2];    /* 16-bit */
    int32_t EFREG[16];  /* 16-bit */
    uint32_t MDEC_CT;
    int32_t ACC, FRC_REG, Y_REG;
    uint32_t ADRS_REG;
    uint16_t memval;          /* last word read by MRD, as seen by IWT */
    struct { int64_t land; uint16_t val; } mem_q[4]; /* reads in flight: global step at which each lands */
    int mem_qn;
    int mrd_even_masa;        /* even-step MRD waiting for the next (odd) memory slot, -1 = none */
    dsp_inst_t mrd_even;
    uint8_t nofl_pipe[2];     /* NOFL of steps s-1, s-2 */

    int16_t outL, outR;       /* DAC output of the last sample */
    uint64_t samples;         /* samples run */
    uint32_t lfsr;            /* noise: 17-bit LFSR x^17 + x^12 + 1, one step per slot processed */
    /* Envelope clock (tests/eg_lock, feg_track, aeg_dl0): it ticks on every sample whose MDEC_CT is even, and its
     * counter is eg_cnt = eg_K - MDEC_CT/2 (mod 2^14).  MDEC_CT is a free-running 16-bit sample counter (masked to
     * the ring at the DSP address), so the envelope phase of a capture follows from the ring position.  eg_K is a
     * constant of the console boot (6491 on the console used for tests/eg_lock; it changes at a reset). */
    uint32_t eg_cnt;
    uint32_t eg_K;
    /* timers and interrupt registers: as cycle-model/aica_model.h (the edge at every sample's start here) */
    struct { uint16_t scieb, scipd, mcieb, mcipd; uint8_t cnt[3], pre[3]; } irq;
    uint32_t tim_phase;
    uint32_t eg_par;           /* the MDEC_CT parity the envelope clock ticks on (0 so far; a write of 0x2804 bit 15 can
                                * flip it: NOTES "Known start state and replay parameters", TODO 7.1) */
    /* KYONEX (tests/kon_defer, eg_sched2): the write only raises this flag (latched at the next sample boundary); the
     * envelope pass of the following step reads every slot's KYONB and the key events take effect on the sample after
     * (on the chip each slot reads KYONB in its frame, so a KYONB written after the KYONEX still counts until then) */
    bool kyonex_pending;
    /* Channel / DSP collisions (tests/dsp_coll): channel K's fetch owns the wave RAM slot of DSP step 2K - 14; the
     * fetch of slot K >= 7 in this sample, of slot K < 7 in the next (its step is in the last eighth of the DSP sample,
     * after the next sample's sweep started).  coll_claim: the slot fetches; coll_word: the word its fetch leaves
     * (see slot_fetch).  A DSP MWT in that slot is dropped, a DSP MRD returns coll_word. */
    bool coll_claim[64];
    uint16_t coll_word[64];
    bool mrd_even_coll;       /* the waiting even-step MRD was in a channel's slot */
    uint16_t mrd_even_word;

    AicaModel();
    ~AicaModel();
    void reset();
    void write(uint32_t off, uint32_t val);
    uint32_t read(uint32_t off);
    bool sh4_irq() const { return (irq.mcieb & irq.mcipd) != 0; }   /* the AICA's SH4 interrupt line (tests/timer_irq L) */
    uint32_t ram_read32(uint32_t off); /* CPU read of wave RAM (updates the memory read latch) */
    void step();

    /* helpers */
    uint16_t r(uint32_t off) const { return reg[(off & 0x7FFF) >> 2]; }
    uint16_t chr(int ch, uint32_t o) const { return reg[(0x80 * ch + o) >> 2]; }

  private:
    void slot_regwrite(int ch, uint32_t o);
    void key_on(int ch);
    void key_off(int ch);
    void set_aeg_state(int ch, EgState s);
    void update_sa_stream(int ch);
    void update_pitch(int ch);
    void update_lfo(int ch, bool from_write);
    static uint32_t eff_rate(uint32_t rate, uint16_t r14, uint16_t r18); /* from the KRS (r14) and OCT/FNS (r18) given */
    void stream_step(int ch);
    void decode_sample(int ch, bool last, uint32_t CA);
    void decode_initial(int ch);
    void aeg_clock(int ch);
    void feg_clock(int ch);
    int32_t lpf_step(int ch, int32_t x8); /* input and output in 1/8 sample units */
    bool slot_fetch(int ch, bool en, bool keyed, uint32_t ca_old, uint32_t ca, uint16_t &word) const;
    void coll_preview();
    void lfo_calc(int ch);
    void slot_output(int ch, int32_t &l, int32_t &r, int32_t &d);
    void dsp_step();
    uint32_t sa_addr(int ch) const;
    int fmt(int ch) const;
};

} // namespace caique
