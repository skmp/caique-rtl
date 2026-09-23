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
#include "dsp_asm.h"

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
        uint16_t a;        /* 10-bit attenuation, 0.09375 dB units (TL counts 4) */
        bool off;          /* reached the top: monitor reads 0x1FFF, output muted */
        EgState state;
    } AEG;
    struct {
        EgState state;
        uint16_t v;        /* 13-bit filter envelope value (cutoff, FLV scale) */
        int8_t dir;        /* direction of the current segment, latched when it starts (tests/feg_track) */
        bool passed;       /* attack / decay 1: the value has crossed the target (advance on the next clock) */
    } FEG;
    int32_t lpf_low, lpf_band; /* filter state, 1/8 sample units; never cleared (tests/filt_reset) */
    struct {
        uint32_t counter, start_value;
        uint8_t state, alfo_w, alfo_shft, plfo, plfo_shft; /* alfo_w: 8-bit ALFO waveform value */
    } lfo;
    bool enabled;
    int8_t key_pending;   /* +1 key-on / -1 key-off, applied at the next envelope clock */
};

struct AicaModel {
    static const uint32_t RAM_SIZE = 2u << 20;
    uint8_t *ram;              /* RAM_SIZE bytes */
    uint16_t reg[0x8000 / 4];  /* register file, one 16-bit value per 32-bit slot, offset >> 2 */

    Slot slot[64];

    /* DSP state */
    int32_t TEMP[128];  /* 24-bit */
    int32_t MEMS[32];   /* 24-bit */
    int32_t MIXS[16];   /* 20-bit, accumulated by the SGC each sample */
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
    uint32_t eg_cnt;          /* envelope clock: +1 every 2 samples (EG_PHASE picks which) */
    static int EG_PHASE;      /* 0/1: sample parity on which the envelope clock ticks (hardware phase unknown) */

    AicaModel();
    ~AicaModel();
    void reset();
    void write(uint32_t off, uint32_t val);
    uint32_t read(uint32_t off);
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
    uint32_t eff_rate(int ch, uint32_t rate);
    void stream_step(int ch);
    void decode_sample(int ch, bool last, uint32_t CA);
    void decode_initial(int ch);
    void aeg_clock(int ch);
    void feg_clock(int ch);
    int32_t lpf_step(int ch, int32_t x8); /* input and output in 1/8 sample units */
    void lfo_calc(int ch);
    void slot_output(int ch, int32_t &l, int32_t &r, int32_t &d);
    void dsp_step();
    uint32_t sa_addr(int ch) const;
    int fmt(int ch) const;
};

} // namespace caique
