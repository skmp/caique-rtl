/* aica_model.h -- caique AICA behavioural model: sound generation core (64 slots), DSP, mixer, bus.
 *
 * Clocked: clock() runs one AICA clock (MCLK, 22.5792 MHz, 512 clocks per 44.1 kHz sample); step() runs 512 clocks
 * (one sample, from ph 0 to ph 0).  The frame plan is the hardware's as far as the console shows it (to a frame:
 * tests/eg_sched2, kon_defer, dsp_coll, sub_frame) and rtl/v1's below that (NOTES.md "Cycle model"):
 *   - ph counts 0..511; frame k = ph >> 3 (8 clocks), c = ph & 7.  Frame k is slot k's stage A: it samples the slot's
 *     registers 0x00-0x1C at c0 and 0x2C-0x4C (FLV, FEG rates) at c5, steps the phase and fetches the wave words at c4,
 *     and computes the key events and envelopes of the NEXT sample at c6 (state and monitor written there); at c7 it
 *     interpolates.  The filter and level run in frame k + 4 (0x28 read at its start), the send in frame k + 7 (0x20 /
 *     0x24 read at its start), the MIXS write at c7 of frame k + 8: slots 60-63's level and 57-63's send run in the next
 *     sample's first frames, and the sweep's MIXS writes fill exactly one DSP sample window (tests/sub_frame).
 *   - The DSP runs step s at ph 64 + 4 s (mod 512): t0 instruction fetch, t1 operand reads, t2 multiply, t3 writes.
 *     Its sample boundary (MDEC_CT) is at ph 64; it reads the MIXS bank the slots filled in the previous sample.
 *   - The bus serves the SH4 and the ARM7DI in DSP-step units (wren7-rtl DcArmBus rules): a request present at t0
 *     competes for that step, grants at t1, the access at t2 (X0), data at t3 (X1).  A write or a register access
 *     takes one step, an SH4 wave RAM read two slots >= 2 steps apart, an ARM cycle 8 clocks (a locked write 4).
 *   - Every access acts at its X0 clock: a unit's read in that same clock sees the old value, a read in a later
 *     clock the new one.
 * Backdoor accesses (write / read / ram_read32 / ram_write32) act at once, between two clocks, as rtl/v1's frozen test
 * port does; between step() calls that is the sample boundary (ph 0).  The validators use them, and read MIXS[] after
 * each step(): the sums of the sweep step() just ran, completed with the stages of slots 57-63 that run early in the
 * next sample (computed with the registers as they are at the boundary; see publish_mixs).
 *
 * Provenance: the SGC started as a port of minicast libswirl/hw/aica/sgc_if.cpp, the DSP as minicast's interpreter;
 * every rule is now the one measured on the console (NOTES.md, with the test behind each).  rtl/v1 implements the same
 * clock for clock (rtl/v1/tb/cosim_cycle.cpp compares the two through the SH4 port).  The sample-based model it
 * replaces is model/sample-model/ (the same rules, applied a whole sample at a time).
 */
#pragma once
#include <stdint.h>
#include <string.h>
#include "../src/dsp_asm.h"

namespace caique {

enum EgState { EG_ATTACK = 0, EG_DECAY1 = 1, EG_DECAY2 = 2, EG_RELEASE = 3 };

struct Slot {
    /* ---- state A: read at the slot's frame, written at its c6 ---- */
    uint32_t CA;           /* current sample address (samples from SA), 16 bits */
    uint32_t step;         /* phase fraction, 14 bits */
    bool in_loop;          /* CA has reached LSA since key-on (arms the loop-end check) */
    bool enabled;          /* sample fetch running; cleared (zero input, the filter keeps running, CA reads 0, monitor
                            * 0x1FFF) one sample after the clock on which the AEG reached 0x3C0 (tests/slot_tail, ca_stop),
                            * or by a one-shot end */
    bool keyed_fetch;      /* keyed on by the envelope pass of the previous sample: this sample fetches CA 0, no advance */
    bool stop_in;          /* a clock brought the AEG to 0x3C0: the stop takes effect after the next sample's fetch */
    bool ca_clr;           /* a one-shot end: CA reads 0 from the next sample (tests/oneshot) */
    struct {               /* the key stage (frame k) -> the envelope pass of the same sample (frame k + 5) */
        bool keyed, keyed_off, prev_passed, clk;
        EgState aeg_prev, feg_prev;
        int8_t prev_dir;
        uint32_t cnt;
    } egp;
    struct {
        uint16_t a;        /* 10-bit attenuation, 0.09375 dB units (TL counts 4); keeps stepping up to 0x3FF after the
                            * sample fetch has stopped at 0x3C0 (tests/slot_tail) */
        bool off;          /* stopped: the monitor reads 0x1FFF (tests/ca_stop); the output is NOT muted */
        EgState state;
    } AEG;
    struct {
        EgState state;
        uint16_t v;        /* 13-bit filter envelope value (cutoff, FLV scale); after the slot's c6: the next sample's */
        uint16_t vo;       /* the value this sample's filter uses (v at the slot's stage A) */
        int8_t dir;        /* direction of the current segment, latched when it starts (tests/feg_track) */
        bool passed;       /* attack / decay 1: the value has crossed the target (advance on the next clock) */
    } FEG;
    struct {
        uint32_t counter;  /* 10 bits: samples to the next state step */
        uint8_t state;
    } lfo;
    /* ---- state B: the output stage's ---- */
    int32_t s0, s1;        /* samples at CA and CA+1 */
    int32_t adpcm_quant;
    int32_t lpf_low, lpf_band; /* filter state, 1/8 sample units; never cleared (tests/filt_reset) */
    bool looped;           /* monitor LP: a loop end since the last monitor read or key-on */
};

/* one master port (SH4 / ARM7DI): the master raises req with the other inputs held until ack */
struct BusPort {
    bool req, we, ram, lock;   /* ram: 1 wave RAM (byte address), 0 register (offset); lock: SWP (ARM) */
    uint32_t addr, wdata;
    uint8_t be;                /* byte lanes of a wave RAM write (bit i = byte i) */
    bool ack;                  /* high in the clock the access completes (SH4: the clock after its last slot's t3) */
    uint32_t rdata;            /* valid with ack */
};

struct AicaModel {
    static const uint32_t RAM_SIZE = 2u << 20;
    static const int SAMPLE_CLKS = 512;
    static const int DSP_OFFSET = 64;            /* DSP step 0 and its sample boundary */
    static const int EDGE_PH = DSP_OFFSET - 40;  /* the one-sample interval edge (wren7 tests/hw/dspport) */
    static const int FIX_STEP0 = 108;            /* the fixed pair lives in steps 108..112 (nominally 109 / 111) */

    uint8_t *ram;              /* RAM_SIZE bytes */
    uint16_t reg[0x8000 / 4];  /* register file, one 16-bit value per 32-bit slot, offset >> 2 */

    Slot slot[64];

    /* ---- time ---- */
    uint32_t ph;               /* phase of the next clock (0..511) */
    uint64_t clocks;           /* clocks run */
    uint64_t samples;          /* sweeps completed (at the end of ph 511) */

    /* ---- DSP ---- */
    int32_t TEMP[128];  /* 24-bit */
    int32_t MEMS[32];   /* 24-bit */
    int32_t EFREG[16];  /* 16-bit */
    int32_t EXTS[2];    /* 16-bit, 0 (no CD-DA input) */
    /* MDEC_CT: the counter of the DSP sample that starts at ph 64 of the current sample; it decrements at the end of
     * every sample.  From ph 0 to 63 the DSP still runs the previous sample (counter MDEC_CT + 1).  A free-running
     * 16-bit counter (masked to the ring at the DSP address); the envelope clock follows it (see eg_K). */
    uint32_t MDEC_CT;
    struct {
        bool run;              /* started (the first ph 64) */
        dsp_inst_t fetch;      /* MPRO of this step (read at t0) */
        int32_t coef_f;        /* COEF of this step (read at t0) */
        dsp_inst_t in;         /* latched at t1 */
        int32_t coef;
        int32_t ACC, SHIFTED, INPUTS, Y_REG, FRC_REG;
        uint32_t ADRS_REG;
        int32_t tempr, mems_q; /* TEMP / MEMS read at t1 */
        uint16_t ma_r, ma_w;   /* MADRS of the waiting even-step read (t0) / of this step (t1) */
        uint16_t memval;       /* the memory read latch (what IWT stores): the last word landed or read by a master */
        uint16_t mem_pend; bool mem_land;   /* a read's word, landing at the next odd step's t0 */
        uint8_t nofl_pipe;     /* NOFL of steps s-1 (bit 0), s-2 (bit 1) */
        bool ev_valid; dsp_inst_t ev_in; bool ev_coll; uint16_t ev_word;   /* an even-step MRD waiting for the odd slot */
        bool rd_post; uint32_t rd_addr; bool rd_coll; uint16_t rd_word;    /* the read of an odd step (at c1 next frame) */
        bool coll_q;           /* this even step's wave RAM slot is a channel's */
        uint8_t fix_d;         /* MRD | MWT of MPRO rows 108..112, read ahead at t3 of steps 100..104 */
    } dsp;

    /* ---- slot engine ---- */
    uint16_t ra[8];            /* the stage A slot's registers 0x00..0x1C, sampled at c0 */
    uint16_t rb[12];           /* ... 0x20..0x4C, sampled at c5 (FLV0 / FLV1 / FLV4 for key events) */
    uint16_t re[18];           /* the envelope stage's slot's registers 0x00..0x44, sampled at c1 of frame k + 5 */
    bool eg_valid[64];         /* the slot has had a key stage (its envelope pass is due in frame k + 5) */
    bool reload_q;             /* the slot's LFO reload flag, sampled at c0 */
    bool lfo_reload[64];       /* a 0x1C write: counter reload (and LFORE) at the slot's next stage A */
    bool kyonex_pending;       /* a KYONEX written: latched at the next sample boundary (ph 0) */
    bool kx_cur;               /* ... latched: this sample's slots read their KYONB at their c6 (tests/kon_defer) */
    bool eg_clk;               /* the envelope clock of the sample the c6 pass computes */
    uint32_t eg_cnt;
    /* Envelope clock (tests/eg_lock, feg_track, aeg_dl0): it ticks on every sample whose MDEC_CT is even, and its
     * counter is eg_cnt = eg_K - MDEC_CT/2 (mod 2^14).  eg_K is a constant of the console boot (6491 on the first boot
     * of tests/eg_lock, 0 after a clean reboot; tests/eg_kprobe). */
    uint32_t eg_K;
    /* timers and interrupt registers (tests/timer_irq, timer_phase; NOTES "Timers and interrupts"): TIMA/B/C count
     * 7:0, prescale 10:8, write-only; SCIPD (ARM) and MCIPD (SH4) are separate pending registers set by the same
     * sources, cleared by SCIRE / MCIRE; bit 5 is the only bit a CPU write sets; bit 10 at every sample edge; the
     * timers tick at the edge of the samples whose MDEC_CT = tim_phase mod 2^prescale (one prescaler for all three,
     * never restarted by a write) and set bit 6 / 7 / 8 when the count wraps to 0 (no reload) */
    struct { uint16_t scieb, scipd, mcieb, mcipd; uint8_t cnt[3], pre[3]; } irq;
    uint32_t tim_phase;        /* the prescaler's MDEC_CT phase (a console constant; 20 mod 32 in the hw9 session) */
    /* experiment knobs (TODO 5.1; the defaults are the measured rules: ALFO noise +2, PLFO noise -67 XOR 0x80, triangle
     * 126 / -128): the LFSR steps between the model's sampling point and the noise LFO waveforms' byte, the PLFO noise
     * XOR, and the PLFO triangle's values at state 64 (peak) and 192 (trough) */
    int x_alfo_noff, x_plfo_noff, x_tri_a, x_tri_b, x_plfo_nxor;
    uint32_t eg_par;           /* the MDEC_CT parity the envelope clock ticks on (0 so far; a write of 0x2804 bit 15 can
                                * flip it: NOTES "Known start state and replay parameters", TODO 7.1) */
    uint32_t lfsr;             /* noise: 17-bit LFSR x^17 + x^12 + 1, one step per slot (at its output stage) */
    struct {                   /* stage A -> output stage (the frame's slot) */
        bool en_out;
        uint32_t frac6;
        uint8_t alfo_w;
        uint16_t a, v;
        bool claim;            /* this sample's fetch holds the slot's wave RAM step (DSP step 2K - 14) */
        uint32_t lw_addr;      /* byte address of the word it leaves (the one holding sample CA + 1) */
    } ho;
    struct {                   /* the previous frame's slot, for its wave RAM step (c0-c3 of this frame) */
        bool valid, claim;
        uint32_t lw_addr;
        uint16_t lw;           /* read at c0 */
    } own;
    struct OutPipe {           /* one slot's sample on its way through the output stages (aica_model.cpp "output pipeline") */
        int32_t s16;           /* interpolated (frame k) */
        uint16_t a, v;         /* the envelope values of this sample */
        uint8_t alfo_w, lfsr_b;
        uint16_t r1c;          /* 0x1C as the fetch read it (ALFO depth and waveform) */
        uint8_t bank;          /* the sweep's MIXS bank */
        int32_t V16;           /* after the filter and level (frame k + 4) */
    } pipe[64];
    bool pipe_valid[64];
    struct { bool v; uint8_t bank, isel; int32_t val; } mq[2];   /* a send (frame k + 7) until its MIXS write (k + 8) */
    int32_t mixs[2][16];       /* the two MIXS banks (20-bit) */
    uint16_t mixs_w[2];        /* bus written in this sweep (the first writer overwrites: tests/mixs_write) */
    uint8_t sbank;             /* the bank this sweep fills */
    uint8_t dsp_bank;          /* the bank the DSP reads (and the CPU accesses) */
    int32_t dl_acc, dr_acc, dsum_l, dsum_r;   /* direct outputs: running sum, the last complete sweep's */
    struct { uint16_t a; uint8_t ast; bool off; uint16_t v; uint8_t fst; uint16_t ca; } mon[64];   /* monitor copy (c6) */

    /* ---- observables for the tools (not hardware state) ---- */
    int32_t MIXS[16];          /* the bus values of the sweep that ended at ph 511 (sums of its sends -- slots 57..63's
                                * completed with the registers at the boundary; a bus nobody points at: the bank's
                                * retained value), published at the end of ph 511 (publish_mixs) */
    int32_t mixs_acc[16]; uint16_t mixs_sent;
    int16_t outL, outR;        /* DAC output (ph 82) */
    uint64_t outs;             /* output samples produced */

    /* ---- output mixer ---- */
    int32_t efs[16], ml, mr;

    /* ---- bus ---- */
    BusPort sh4, arm;
    struct {
        bool g_pend, r_pend; uint8_t g_src, r_src;   /* granted at t1, X0 at t2 */
        bool g1, r1, r1_we; uint8_t g1_src, r1_src; uint32_t g1_data, r1_data;   /* X1 */
        bool arm_busy, arm_ack_q; uint8_t arm_left; uint32_t arm_rdata;
        bool sh_busy, sh_ack_q; uint8_t sh_rd2; bool sh_fin; uint32_t sh_rdata;
        bool arm_t0, sh_t0;
        uint8_t mslc_slot; bool mslc_afsel;         /* MSLC copy (0x280C) */
    } bus;

    AicaModel();
    ~AicaModel();
    void reset();
    void clock();                              /* one MCLK */
    void run(uint64_t n) { while (n--) clock(); }
    void step();                               /* to the next ph 0 */
    /* backdoor (at once, between two clocks) */
    void write(uint32_t off, uint32_t val);
    uint32_t read(uint32_t off);
    bool sh4_irq() const { return (irq.mcieb & irq.mcipd) != 0; }   /* the AICA's SH4 interrupt line (tests/timer_irq L) */
    uint32_t ram_read32(uint32_t off);         /* also updates the DSP's memory read latch, as a bus read does */
    void ram_write32(uint32_t off, uint32_t v, uint8_t be = 0xF);

    /* helpers */
    uint16_t r(uint32_t off) const { return reg[(off & 0x7FFF) >> 2]; }
    uint16_t chr(int ch, uint32_t o) const { return reg[(0x80 * ch + o) >> 2]; }
    uint32_t dsp_ct() const { return (ph < (uint32_t)DSP_OFFSET ? MDEC_CT + 1 : MDEC_CT) & 0xFFFF; }   /* the running DSP sample's */

  private:
    uint16_t rs(uint32_t o) const { return o < 0x20 ? ra[o >> 2] : rb[(o - 0x20) >> 2]; }   /* the sampled registers */
    void sgc_clock(uint32_t f, uint32_t c);
    void stage_a(int k);
    void stage_key(int k);
    void stage_eg(int k);
    void stage_interp(int k);
    int32_t level_of(int k, uint16_t r28, bool upd);
    void stage_level(int k);
    void stage_send(int k);
    void publish_mixs();
    void stream_step(int ch);
    void decode_sample(int ch, bool last, uint32_t CA);
    void aeg_clock(int ch, bool keyed, bool keyed_off, EgState aeg_prev, uint32_t cnt);
    void feg_clock(int ch, bool keyed, bool keyed_off, EgState feg_prev, int8_t prev_dir, bool prev_passed, uint32_t cnt);
    int32_t lpf_step(int ch, int32_t x8, uint16_t v, uint32_t q);   /* input and output in 1/8 sample units */
    void dsp_clock(uint32_t s, uint32_t t);
    uint32_t dsp_addr(uint16_t madrs, const dsp_inst_t &m) const;
    void bus_x0_read(uint32_t &data);
    void bus_x0_write();
    void bus_edge(uint32_t t, uint32_t c);
    uint32_t reg_read(uint32_t off);
    void reg_write(uint32_t off, uint16_t v);
    uint32_t sa_addr(uint16_t r00, uint16_t r04) const;
};

} // namespace caique
