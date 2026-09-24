/* aica_model.cpp -- caique AICA behavioural model, clocked (one clock() = one MCLK).  See aica_model.h and NOTES.md. */
#include "aica_model.h"
#include "../src/dsp_asm.h"
#include "../src/dsp_float.h"
#include <stdlib.h>

namespace caique {

static inline int32_t sext(int32_t v, int bits) { return (int32_t)((uint32_t)v << (32 - bits)) >> (32 - bits); }
static inline int32_t clampi(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }

enum { SRC_NONE = 0, SRC_SH = 2, SRC_ARM = 3 };

/* ------------------------------------------------------------------------------------------------------------------
 * Tables (minicast sgc_if.cpp)
 * ---------------------------------------------------------------------------------------------------------------- */
static const int32_t adpcm_qs[8] = {0x0e6, 0x0e6, 0x0e6, 0x0e6, 0x133, 0x199, 0x200, 0x266};

/* Slot stop (tests/slot_tail): the sample fetch stops once the AEG reaches CAIQUE_STOP_A on a clock, one sample after
 * that clock.  A macro only so that the threshold controls of work/verify/s5/model_fixes.md (0x3BF / 0x3C1 / 0x400,
 * each breaks a tail stream) can be rebuilt with -D. */
#ifndef CAIQUE_STOP_A
#define CAIQUE_STOP_A 0x3C0
#endif

/* ------------------------------------------------------------------------------------------------------------------
 * Register masks (measured: tests/probe)
 * ---------------------------------------------------------------------------------------------------------------- */
static uint16_t chan_mask(uint32_t o) {
    switch (o) {
    case 0x00: return 0x47FF; /* KYONEX (bit 15) is an action, bits 13:11 not stored */
    case 0x04: case 0x08: case 0x0C: return 0xFFFF;
    case 0x10: return 0xFFDF;
    case 0x14: return 0x7FFF;
    case 0x18: case 0x1C: return 0xFFFF;
    case 0x20: return 0x00FF;
    case 0x24: case 0x28: return 0xFFFF;
    case 0x2C: case 0x30: case 0x34: case 0x38: case 0x3C: return 0x1FFF;
    case 0x40: case 0x44: return 0x1F1F;
    default: return 0;
    }
}

/* ------------------------------------------------------------------------------------------------------------------ */
AicaModel::AicaModel() {
    ram = (uint8_t *)calloc(RAM_SIZE, 1);
    reset();
}
AicaModel::~AicaModel() { free(ram); }

/* power-on: everything 0 except the slot defaults (released, attenuation 0x3FF, off, LFO counter of LFOF 0), the
 * counters (MDEC_CT 1 for the first DSP sample, K, LFSR seed 1) and the MIXS bank pointers; wave RAM is kept */
void AicaModel::reset() {
    uint8_t *keep = ram;
    memset((void *)this, 0, sizeof *this);
    ram = keep;
    MDEC_CT = 1;
    eg_K = 6491;
    eg_par = 0;
    memset(&irq, 0, sizeof irq);
    tim_phase = 0;
    x_alfo_noff = 2; x_plfo_noff = -67; x_tri_a = 126; x_tri_b = -128; x_plfo_nxor = 0x80;   /* the measured rules */
    lfsr = 1;
    dsp_bank = 1;
    for (int ch = 0; ch < 64; ch++) {
        slot[ch].AEG.state = EG_RELEASE;
        slot[ch].AEG.a = 0x3FF;
        slot[ch].AEG.off = true;
        slot[ch].lfo.counter = 1020;
        mon[ch].a = 0x3FF;
        mon[ch].ast = EG_RELEASE;
        mon[ch].off = true;
    }
}

static inline uint16_t ram16w(const uint8_t *ram, uint32_t w) {   /* 16-bit word w (20-bit word address) */
    w = (w & 0xFFFFF) << 1;
    return (uint16_t)(ram[w] | (ram[w + 1] << 8));
}
static inline int16_t ram16(const uint8_t *ram, uint32_t byteaddr) {
    byteaddr &= AicaModel::RAM_SIZE - 1;
    return (int16_t)(ram[byteaddr & ~1u] | (ram[(byteaddr & ~1u) + 1] << 8));
}

/* ------------------------------------------------------------------------------------------------------------------
 * SGC helpers
 * ---------------------------------------------------------------------------------------------------------------- */
static inline int fmt(uint16_t r00) { return ((r00 >> 10) & 1) ? 4 : (r00 >> 7) & 3; } /* SSCTL -> noise */
uint32_t AicaModel::sa_addr(uint16_t r00, uint16_t r04) const {
    uint32_t a = ((uint32_t)(r00 & 0x7F) << 16) | r04;
    if (((r00 >> 7) & 3) == 0) a &= ~1u;
    return a;
}

/* effective EG rate R (0..63) from a 5-bit rate register (tests/sgc_krs, all 320 KRS x OCT x FNS combinations):
 *   k = KRS + OCT (signed); s = k < 0 ? 0 : 2 * min(k, 15) + FNS[9];  R = min(63, 2 * rate + s);  KRS = 15: s = 0.
 * rate 0 = no change.  r14 / r18 are the register values the slot sampled (tests/eg_latch: no latch). */
static uint32_t eff_rate(uint32_t re, uint16_t r14, uint16_t r18) {
    if (re == 0) return 0;
    uint32_t KRS = (r14 >> 10) & 0xF, FNS = r18 & 0x3FF, OCT = (r18 >> 11) & 0xF;
    int32_t s = 0;
    if (KRS != 0xF) {
        int32_t k = (int32_t)KRS + ((OCT & 8) ? (int32_t)OCT - 16 : (int32_t)OCT);
        s = k < 0 ? 0 : 2 * (k > 15 ? 15 : k) + (int32_t)((FNS >> 9) & 1);
    }
    int32_t R = (int32_t)re * 2 + s;
    return (uint32_t)(R > 63 ? 63 : R);
}

/* LFO (tests/sgc_lfo): one state per slot, +1 every O samples, O from LFOF as in minicast (2..1016 measured) */
static inline uint32_t lfo_period(uint32_t n) {
    uint32_t S = n >> 2, M = (~n) & 3, G = 128 >> S, L = (G - 1) << 2;
    return L + G * (M + 1);
}
/* ALFO waveforms (8-bit): saw = state, square = 0 / 255, triangle as minicast; noise is taken per sample (stage_out) */
static inline uint32_t alfo_wave(uint32_t ws, uint32_t st) {
    switch (ws) {
    case 0: return st & 0xFF;
    case 1: return st & 0x80 ? 255 : 0;
    case 2: return (((st & 0x7f) ^ (st & 0x80 ? 0x7F : 0)) << 1) & 0xFF;
    default: return 0;
    }
}
/* PLFO waveforms are signed 8-bit (tests/sgc_lfo): saw = (int8)state, square = +127 / -128, triangle starts at 0,
 * rises 2 per state to 126, falls to -128, rises back (state 64 reads -128); noise is the LFSR byte (stream_step) */
static inline int32_t plfo_wave(uint32_t ws, uint32_t st, int tri_a = 126, int tri_b = -128) {
    int32_t pw = 0;
    switch (ws) {
    case 0: pw = (int8_t)st; break;
    case 1: pw = st & 0x80 ? -128 : 127; break;
    /* triangle: 0, +2 per state to 126 (state 63), 126 again at 64 and down by 2 to -128 (191), -128 again at 192 and up
     * (tests/sgc_lfo lf_4 / lf_5 at PLFOS 1..7; the value's bit 0 is not used, so 126 / 127 and -128 / -127 cannot be told
     * apart); the model had 128 (wrapping to -128) at 64 */
    case 2: pw = st < 64 ? 2 * (int32_t)st : st < 192 ? tri_a - 2 * ((int32_t)st - 64) : tri_b + 2 * ((int32_t)st - 192); break;
    default: pw = 0; break;
    }
    return (int8_t)pw;
}
/* LFSR jumps as GF(2) matrices (the LFSR is linear): row i = the input bits whose parity is output bit i */
struct LfsrJump {
    uint32_t row[17];
    explicit LfsrJump(int d) {   /* d steps on; negative: back (the period is 2^17 - 1) */
        const uint32_t n = (uint32_t)(((d % 131071) + 131071) % 131071);
        for (int b = 0; b < 17; b++) row[b] = 0;
        for (int j = 0; j < 17; j++) {   /* column j: the image of bit j */
            uint32_t l = 1u << j;
            for (uint32_t k = 0; k < n; k++) l = (l >> 1) | ((((l >> 0) ^ (l >> 5)) & 1) << 16);
            for (int b = 0; b < 17; b++) if ((l >> b) & 1) row[b] |= 1u << j;
        }
    }
    uint32_t operator()(uint32_t l) const {
        uint32_t o = 0;
        for (int b = 0; b < 17; b++) o |= (uint32_t)__builtin_parity(l & row[b]) << b;
        return o;
    }
};
/* the noise LFO waveforms (tests/sgc_lfo, lfo_noise: 8 slots, every sample of 66273 / 13345): the ALFO takes the LFSR
 * byte two steps after the slot's own step, the PLFO the byte 67 steps before the slot's stage-A point (after the steps
 * of the slots before it in this sample), XOR 0x80 (offset binary; the ALFO uses it unsigned) */
static const LfsrJump lfsr_alfo_j(2), lfsr_plfo_j(-67);
static inline uint32_t lfsr_adv(uint32_t l, int d) {   /* d steps on (negative: back; the period is 2^17 - 1) */
    uint32_t n = (uint32_t)(((d % 131071) + 131071) % 131071);
    while (n--) l = (l >> 1) | ((((l >> 0) ^ (l >> 5)) & 1) << 16);
    return l;
}

static int32_t decode_adpcm(uint32_t sample, int32_t prev, int32_t &quant) {
    int32_t sign = 1 - 2 * (int32_t)(sample / 8);
    uint32_t data = sample & 7;
    /* Yamaha delta: each term shifted separately (tests/sgc_formats: nibble 6 at step 127 gives 205, not 206) */
    int32_t rv = (quant >> 3) + ((data & 1) ? quant >> 2 : 0) + ((data & 2) ? quant >> 1 : 0) + ((data & 4) ? quant : 0);
    if (rv > 0x7FFF) rv = 0x7FFF;
    rv = sign * rv + prev;
    quant = (quant * adpcm_qs[data]) >> 8;
    quant = clampi(quant, 127, 24576);
    return clampi(rv, -32768, 32767);
}

void AicaModel::decode_sample(int ch, bool last, uint32_t CA) {
    Slot &c = slot[ch];
    int f = fmt(ra[0]);
    if (!last && (f < 2 || f == 4)) return;
    uint32_t sa = sa_addr(ra[0], ra[1]);
    int32_t s0 = 0, s1 = 0;
    switch (f) {
    case 4: /* noise: taken at the output stage from the global LFSR (stage_out), not decoded here */
        s0 = s1 = 0;
        break;
    case 0:
        s0 = ram16(ram, sa + 2 * CA);
        s1 = ram16(ram, sa + 2 * CA + 2);
        break;
    case 1:
        s0 = (int8_t)ram[(sa + CA) & (RAM_SIZE - 1)] << 8;
        s1 = (int8_t)ram[(sa + CA + 1) & (RAM_SIZE - 1)] << 8;
        break;
    case 2: case 3: {
        uint8_t ad1 = ram[(sa + (CA >> 1)) & (RAM_SIZE - 1)];
        uint8_t ad2 = ram[(sa + ((CA + 1) >> 1)) & (RAM_SIZE - 1)];
        uint8_t sf = (CA & 1) * 4;
        ad1 = (ad1 >> sf) & 0xF;
        ad2 = (ad2 >> (4 - sf)) & 0xF;
        int32_t q = c.adpcm_quant;
        s0 = decode_adpcm(ad1, c.s0, q);
        c.adpcm_quant = q;
        s1 = last ? decode_adpcm(ad2, s0, q) : 0;
        break;
    }
    }
    c.s0 = s0;
    c.s1 = s1;
}

/* The phase step of one sample (stage A), with the registers the slot sampled at c0. */
void AicaModel::stream_step(int ch) {
    Slot &c = slot[ch];
    const uint16_t r00 = ra[0], r14 = ra[5], r18 = ra[6], r1c = ra[7];
    const uint32_t LSA = ra[2], LEA = ra[3];
    const int f = fmt(r00);
    const bool LPCTL = (r00 >> 9) & 1, LPSLNK = (r14 >> 14) & 1;
    /* pitch (tests/sgc_pitch): 14 fraction bits, increment = (1024 + FNS) << (OCT + 4), OCT signed -8..7, truncated for
     * OCT < -4 (minicast used 10 bits).  The pitch LFO (tests/sgc_lfo, sgc_lfo2) scales the 11-bit mantissa before the
     * octave shift:  m = 1024 + FNS;  m' = m + floor(m * w' / 1024),  w' = (w & ~1) >> (7 - PLFOS);  inc = m' << (OCT + 4)
     * with w the signed 8-bit waveform, PLFOS 0 = none (square at PLFOS 7, FNS 0: 1024 + 126 / 1024 - 128) */
    const uint32_t OCT = (r18 >> 11) & 0xF, FNS = r18 & 0x3FF, plfos = (r1c >> 5) & 7;
    const int oct = (OCT & 8) ? (int)OCT - 16 : (int)OCT, sh = oct + 4;
    int32_t m = 1024 + (int32_t)FNS;
    if (plfos) {
        int32_t w = ((r1c >> 8) & 3) == 3 ? (int32_t)(int8_t)(((x_plfo_noff == -67 ? lfsr_plfo_j(lfsr) : lfsr_adv(lfsr, x_plfo_noff)) & 0xFF) ^ x_plfo_nxor)
                                          : plfo_wave((r1c >> 8) & 3, c.lfo.state, x_tri_a, x_tri_b);
        int32_t mod = (w & ~1) >> (7 - plfos);
        m = m + ((m * mod) >> 10);
    }
    const uint32_t inc = (uint32_t)(sh >= 0 ? m << sh : m >> -sh);
    c.step += inc;
    uint32_t ip = c.step >> 14;
    c.step &= 0x3FFF;
    while (ip > 0) {
        ip--;
        uint32_t CA = (c.CA + 1) & 0xFFFF;
        uint32_t ca_t = CA;
        if (f == 3) ca_t &= ~3u;
        if (LPSLNK && c.AEG.state == EG_ATTACK && CA >= LSA) c.AEG.state = EG_DECAY1;
        /* loop end (tests/sgc_loop): armed once CA has reached LSA since key-on (checked before this step arms it);
         * then CA >= LEA -> CA -= LEA - LSA (16-bit).  Same as CA = LSA for a normal loop; LSA == LEA plays straight
         * through; LEA < LSA jumps forward, and after CA wraps past 0xFFFF the armed loop triggers at LEA. */
        bool armed = c.in_loop;
        if (CA >= LSA) c.in_loop = true;
        if (ca_t >= LEA && armed) {
            c.looped = true;
            CA = (uint32_t)((int32_t)CA - ((int32_t)LEA - (int32_t)LSA)) & 0xFFFF;
            if (LPCTL) {
                if (f == 2) { c.adpcm_quant = 127; c.s0 = 0; }
            } else {
                /* one-shot end (tests/oneshot): only the fetch stops -- CA reads LEA on this sample and 0 from the next;
                 * the envelope keeps its state and level and keeps stepping (so a key-on is ignored until a key-off), and
                 * it is not "off" until the level reaches 0x3C0 */
                c.CA = (c.CA + 1) & 0xFFFF;   /* the stepped CA, not wrapped */
                c.enabled = false;
                c.ca_clr = true;
                break;
            }
        }
        c.CA = CA;
        decode_sample(ch, ip == 0, CA);
    }
}

/* ------------------------------------------------------------------------------------------------------------------
 * Slot engine, one frame per slot (NOTES "Cycle model"; rtl/v1 aica_sgc).  Frame k = slot k:
 *   c0  registers 0x00..0x1C sampled (a write that lands before frame k is used in this sample: tests/eg_sched2), the
 *       LFO reload flag; the previous slot's wave RAM step reads the word its fetch left (c0-c3 = DSP step 2(k-1) - 14)
 *   c1  the level stage of slot k - 4, the send stage of slot k - 7 (their registers read now), the envelope registers
 *       of slot k - 5
 *   c4  stage A: phase step, loop, fetch and decode, the step's wave RAM claim, LFO step, a pending stop
 *   c5  registers 0x20..0x4C sampled
 *   c6  key events (KYONEX latched at the sample boundary, KYONB as sampled: tests/kon_defer, sub_sched exp 0); CA
 *       monitor written
 *   c7  the envelope pass of slot k - 5 (the envelopes of its NEXT sample: tests/eg_sched2, sub_env), EG monitor
 *       written; the MIXS write of slot k - 8's send; stage B of slot k: noise, interpolation (the output pipeline)
 * ---------------------------------------------------------------------------------------------------------------- */
void AicaModel::stage_a(int k) {
    Slot &c = slot[k];
    const uint16_t r1c = ra[7];
    const uint32_t per = lfo_period((r1c >> 10) & 0x1F);
    if (reload_q && (r1c & 0x8000)) c.lfo.state = 0;   /* a 0x1C write setting LFORE: state 0 from this sample's use */
    if (c.ca_clr) { c.ca_clr = false; c.CA = 0; }   /* the sample after a one-shot end (it set CA = LEA) */
    const bool en = c.enabled, keyed = c.keyed_fetch;
    const uint32_t ca_old = c.CA;
    ho.a = c.AEG.a;
    ho.v = c.FEG.v;
    c.FEG.vo = c.FEG.v;   /* the cutoff of this sample (c6 computes the next one) */
    /* the key-on sample fetches and outputs the word at CA 0 without advancing, from a reset decoder (tests/sgc_pitch,
     * sgc_loop, dsp_coll kon runs: the first fetch is in the sample that outputs CA 0) */
    if (en && !keyed) stream_step(k);
    else if (en) { c.s0 = 0; c.adpcm_quant = 127; decode_sample(k, true, 0); }
    c.keyed_fetch = false;
    /* The step's wave RAM slot and the word the fetch leaves (tests/dsp_coll, console: PCM16 at pitch 1 / 1/4 / 1.33,
     * PCM8, ADPCM OCT -2..+2, key-on): the 16-bit word holding the sample after the position, CA + 1 (the
     * interpolation's second sample) -- the last word the slot reads.  PCM16 / PCM8 fetch every sample while the slot
     * plays, the key-on sample (CA 0) included.  ADPCM holds one 16-bit word: it fetches on the key-on sample, when CA
     * enters another word, or when CA + 1 lies in the next one (at most one fetch; wren7 tests/hw/sgcadp shares 5/16,
     * 3/8, 1/2, 1/2, 1); none at OCT 3..7 (sgcadp: the channel stops).  Noise never fetches. */
    {
        const int f = fmt(ra[0]);
        const uint32_t sa = sa_addr(ra[0], ra[1]), ca = c.CA;
        bool claim = en && f != 4;
        uint32_t b = 0;
        if (f == 0) b = sa + 2 * (ca + 1);
        else if (f == 1) b = sa + ca + 1;
        else if (f != 4) {
            const uint32_t o4 = (ra[6] >> 11) & 15;
            auto wd = [&](uint32_t n) { return (sa + (n >> 1)) >> 1; };
            if (o4 >= 3 && o4 <= 7) claim = false;
            if (!keyed && wd(ca) == wd(ca_old) && wd(ca + 1) == wd(ca)) claim = false;
            b = sa + ((ca + 1) >> 1);
        }
        ho.claim = claim;
        ho.lw_addr = b & (RAM_SIZE - 1) & ~1u;
    }
    ho.en_out = c.enabled;
    ho.frac6 = (c.step >> 8) & 63;
    ho.alfo_w = (uint8_t)alfo_wave((r1c >> 3) & 3, c.lfo.state);
    /* the LFO (tests/sgc_lfo hw9, every stream fitted with one (state, counter) at its key-on): a per-slot counter that
     * runs every sample, playing or not, and is never reloaded by a write (slots configured and released together key on
     * with different counters; at periods 1 / 3 the state had already stepped between the LFORE release and the key-on);
     * it reloads with the current period when it wraps, a counter above a shortened period is cut to it (every fitted
     * counter lay within its period 441 samples after LFOF 0 -> 20: a clamp or an immediate wrap, unmeasured); LFORE holds
     * the state at 0, not the counter */
    {
        if (c.lfo.counter > per) c.lfo.counter = per;
        if (((c.lfo.counter - 1) & 0x3FF) == 0) { c.lfo.counter = per; if (!(r1c & 0x8000)) c.lfo.state++; }
        else c.lfo.counter = (c.lfo.counter - 1) & 0x3FF;
        if (r1c & 0x8000) c.lfo.state = 0;
    }
    /* a stop armed by the previous sample's envelope pass takes effect after this sample's fetch (tests/slot_tail: the
     * clock sample still outputs the fetched sample, zero input from the one after): the fetch stops, the monitor reads
     * 0x1FFF, CA reads 0 (tests/ca_stop) */
    if (c.stop_in) { c.stop_in = false; c.enabled = false; c.AEG.off = true; c.CA = 0; }
}

/* Envelope (tests/sgc_aeg, eg_lock): the OPN envelope generator clocked every 2 samples (the samples with even
 * MDEC_CT).  Rate R < 48: every 2^(11 - R/4) clocks, increment from row R&3; R >= 48: every clock, rows 4.. (R 60-63
 * all +8).  The rows for R = 1 mod 4 above 48 (5, 9, 13) differ from the YM2612 table: the double step sits at
 * index 1 and 5, not 3 and 7 (tests/eg_lock odd_att / odd_same, feg_track batch 1 slot 2, feg_odd); the rest is
 * the OPN table (rows 1, 3, 7, 11, 15 measured at odd effective rates, tests/eg_lock).
 * Attack: a += (~a * inc) >> 4, to 0 -> decay 1 (unless LPSLNK).  Decay 1 -> decay 2 once a[9:5] == DL, checked after
 * that clock's decay 1 step (tests/aeg_dl0), so decay 2's increment applies from the next clock.  Decay 2 / release:
 * a += inc; at 0x3C0 the sample fetch stops, the slot is "off" (monitor 0x1FFF), from the next sample on
 * (tests/slot_tail, ca_stop); the state is kept. */
static const uint8_t eg_inc[17][8] = {
    {0, 1, 0, 1, 0, 1, 0, 1}, {0, 1, 0, 1, 1, 1, 0, 1}, {0, 1, 1, 1, 0, 1, 1, 1}, {0, 1, 1, 1, 1, 1, 1, 1},
    {1, 1, 1, 1, 1, 1, 1, 1}, {1, 2, 1, 1, 1, 2, 1, 1}, {1, 2, 1, 2, 1, 2, 1, 2}, {1, 2, 2, 2, 1, 2, 2, 2},
    {2, 2, 2, 2, 2, 2, 2, 2}, {2, 4, 2, 2, 2, 4, 2, 2}, {2, 4, 2, 4, 2, 4, 2, 4}, {2, 4, 4, 4, 2, 4, 4, 4},
    {4, 4, 4, 4, 4, 4, 4, 4}, {4, 8, 4, 4, 4, 8, 4, 4}, {4, 8, 4, 8, 4, 8, 4, 8}, {4, 8, 8, 8, 4, 8, 8, 8},
    {8, 8, 8, 8, 8, 8, 8, 8}};
/* slow_off: the R < 48 rows see the clock counter one step behind the R >= 48 rows (tests/feg_track: slots mixing
 * both kinds of rate need an offset = 3 mod 4, i.e. -1).  The AEG shares it: with the ring-locked counter the AEG
 * captures (tests/eg_lock att_slow / att_mid, aeg_dl0) and the FEG captures agree on the same eg_K only with the
 * same offset. */
static inline uint32_t eg_increment(uint32_t R, uint32_t cnt, uint32_t slow_off = 0) {
    if (R == 0) return 0;
    if (R < 48) {
        cnt += slow_off;
        uint32_t shift = 11 - (R >> 2);
        if (cnt & ((1u << shift) - 1)) return 0;
        return eg_inc[R & 3][(cnt >> shift) & 7];
    }
    return eg_inc[R >= 60 ? 16 : 4 + (R - 48)][cnt & 7];
}

void AicaModel::aeg_clock(int ch, bool keyed, bool keyed_off, EgState aeg_prev, uint32_t cnt) {
    Slot &c = slot[ch];
    if (c.AEG.off && c.AEG.a >= 0x3FF) return;   /* the value keeps stepping to 0x3FF after the stop (tests/slot_tail tail_b) */
    const uint16_t r10 = re[4], r14 = re[5];   /* live: a rewritten AR / D1R / D2R / RR / DL / KRS acts on the very clock
                                                * the slot computes after it lands (tests/eg_latch, eg_latch2: no latch) */
    if (keyed) {
        /* no increment step on a key-on sample (tests/eg_lock keys), but an R 63 attack (a = 0 from key-on) still
         * leaves the attack state on this clock: tests/eg_kprobe p5/p6/p7 (even onsets, decay 1 at R 45) step decay 1
         * at onset + 2, where "attack leaves on the first clock after the key-on" (tests/eg_lock odd_dec, odd onsets)
         * would give onset + 4.  Both: the R 63 attack leaves on the first clock at or after the key-on sample. */
        if (c.AEG.state == EG_ATTACK && c.AEG.a == 0 && !((r14 >> 14) & 1)) c.AEG.state = EG_DECAY1;
        return;
    }
    /* key-off on a clock sample (tests/aeg_koff, the key-off sample pinned by a witness slot keyed on by the same
     * KYONEX): from an ATTACK no step at all (koff_att, 12/12 even key-offs; neither the attack step nor the release
     * increment); from DECAY 2 one more step with decay 2's increment before the release rate takes over (koff_d2 /
     * koff_d2b, 51/51) or DECAY 1 (koff_d1, 18/18: one more decay-1 step); the FEG does the same for every
     * segment including its attack (tests/feg_krs fk_1 / fk_3, feg_track batches 1 and 2, feg_koffdir, feg_koffatt). */
    if (keyed_off && aeg_prev == EG_ATTACK) return;
    /* decay 1 -> decay 2: on a clock that starts in decay 1, the decay 1 step is applied first, then a[9:5] == DL
     * switches (every such clock, also without a step; not on the clock that enters decay 1).  tests/aeg_dl0: with
     * DL 0 the first decay 1 clock still takes its step (D1R 31: +8) before decay 2; tests/sgc_aeg dec_a: the
     * monitor shows decay 2 within the crossing clock. */
    bool was_d1 = c.AEG.state == EG_DECAY1;
    EgState rs = keyed_off ? aeg_prev : c.AEG.state;   /* key-off sample: the previous segment's rate */
    uint32_t rate = rs == EG_ATTACK ? (r10 & 0x1F) : rs == EG_DECAY1 ? ((r10 >> 6) & 0x1F)
                  : rs == EG_DECAY2 ? ((r10 >> 11) & 0x1F) : (r14 & 0x1F);
    uint32_t inc = eg_increment(eff_rate(rate, r14, re[6]), cnt, (uint32_t)-1);
    if (!inc) {
        if (was_d1 && (c.AEG.a >> 5) == ((r14 >> 5) & 0x1F)) c.AEG.state = EG_DECAY2;
        return;
    }
    int32_t a = c.AEG.a;
    if (c.AEG.state == EG_ATTACK) {
        a += ((~a) * (int32_t)inc) >> 4;
        if (a <= 0) {
            a = 0;
            if (!((r14 >> 14) & 1)) c.AEG.state = EG_DECAY1;
        }
    } else {
        a += inc;
        /* slot stop (tests/slot_tail, ca_stop): once a reaches 0x3C0 (a[9:6] == 15) the sample fetch stops -- zero
         * input from then on, the filter keeps running on it (tail_a: the 120th +8 clock, the 960th +1 clock; tail_c:
         * 768 clocks of row 5) -- the monitor reads 0x1FFF and CA reads 0 (ca_stop).  The value keeps stepping and
         * saturates at 0x3FF; the level keeps applying (tail_b: the VOFF 0 output stays -16 / 0 with the sign of the
         * filter tail long past the stop, no mute).  The stop takes effect after the NEXT sample's fetch (the clock
         * sample still outputs the fetched sample): stage_a commits it.  The state is kept: decay 2 stays decay 2
         * (tests/sgc_keys K4). */
        if (a >= CAIQUE_STOP_A && !c.AEG.off && !c.stop_in) c.stop_in = true;   /* also after a one-shot end (tests/oneshot A) */
        if (a > 0x3FF) a = 0x3FF;
    }
    c.AEG.a = (uint16_t)a;
    /* decay 1 -> decay 2 when the top 5 bits of a EQUAL DL (not >=): a slot that enters decay 1 through LPSLNK
     * above DL << 5 keeps decaying at D1R (tests/sgc_loop lo_2 stream 2, DL 0, a 0x20D); for the attack-entered
     * decay 1 the two compares are the same (steps of at most 8 cannot skip the DL window) */
    if (was_d1 && (a >> 5) == ((r14 >> 5) & 0x1F)) c.AEG.state = EG_DECAY2;   /* also on the stop clock: tests/ca_stop DL 30 reads state 2 */
}

/* Filter envelope (tests/feg_probe, feg_track): a 13-bit value loaded with FLV0 at key-on that moves linearly
 * toward FLV1 (attack, FAR), then FLV2 (decay 1, FD1R), then FLV3 (decay 2, FD2R); key-off heads for FLV4 (FRR).
 * Steps come from the amplitude envelope's clock and increment tables at the rate's effective R (KRS applies,
 * as for the AEG).  Measured sample-exactly through the filter (tools/feg_track.cpp, feg_fit.cpp):
 *   - one comparator C = (v >= target).  A segment moves down if C holds when it starts, else up (so a segment
 *     that starts exactly on its target moves down);
 *   - attack / decay 1 step until C flips (up: ends at or past the target; down: strictly below it -- overshoot by
 *     up to one step), and the next segment moves on the very next clock (no idle clock);
 *   - decay 2 / release skip any step that would flip C: they hold short of the target.
 * Key-on loads FLV0 on the key-on sample and the value first moves on the next clock; key-off switches to release on
 * the key-off sample, and when that sample is a clock the FEG takes ONE MORE STEP OF THE OLD SEGMENT -- its increment
 * and its direction -- with the release's hold check against FLV4 (tests/feg_krs fk_1 / fk_3, feg_track batches 1 / 2:
 * +4 out of a decay-2 hold at 0x19FE; tests/feg_koffdir kd_0/1/5/6/7: a decay 2 holding at 0x1A04 going DOWN steps to
 * 0x1A00 although the release goes up, one holding at 0x19FE going UP steps to 0x1A02 although the release goes down;
 * "old increment toward the release target" is refuted there).  KYONB cleared without KYONEX changes nothing (kd_6/7). */
void AicaModel::feg_clock(int ch, bool keyed, bool keyed_off, EgState feg_prev, int8_t prev_dir, bool prev_passed, uint32_t cnt) {
    Slot &c = slot[ch];
    /* the FEG keeps stepping after the sample fetch has stopped and after the AEG is "off" (tests/slot_tail tail_c
     * streams 1 / 2: the zero-input filter tails match only with the release still moving toward FLV4 through the stop
     * (+3 samples) and through off (+6)) */
    if (keyed) return;
    const uint16_t r40 = re[16], r44 = re[17];   /* live (tests/eg_latch feg_rate) */
    static const uint8_t tgt_reg[4] = {0x30, 0x34, 0x38, 0x3C};
    if (c.FEG.passed && c.FEG.state < EG_DECAY2) {
        c.FEG.state = (EgState)(c.FEG.state + 1);
        c.FEG.dir = c.FEG.v >= (re[tgt_reg[c.FEG.state] >> 2] & 0x1FFF) ? -1 : 1;
        c.FEG.passed = false;
    }
    /* key-off sample: the step is the one the FEG would have taken on this clock without the key-off -- the previous
     * segment's rate and direction (tests/feg_koffdir), or, when that segment had already passed its target on the
     * previous clock, the NEXT segment's rate and direction (tests/feg_koffpass: 23/23 such cycles take the next
     * segment's step; "one more step of the passed segment", "no step" and "the release step" are refuted) */
    EgState seg = keyed_off ? feg_prev : c.FEG.state;
    int32_t dir = keyed_off ? prev_dir : c.FEG.dir;
    if (keyed_off && prev_passed && seg < EG_DECAY2) {
        seg = (EgState)(seg + 1);
        dir = c.FEG.v >= (re[tgt_reg[seg] >> 2] & 0x1FFF) ? -1 : 1;
    }
    uint32_t rate;
    switch (seg) {
    case EG_ATTACK: rate = (r40 >> 8) & 0x1F; break;
    case EG_DECAY1: rate = r40 & 0x1F; break;
    case EG_DECAY2: rate = (r44 >> 8) & 0x1F; break;
    default: rate = r44 & 0x1F; break;
    }
    int32_t target = re[tgt_reg[c.FEG.state] >> 2] & 0x1FFF;   /* the target and the hold rule are the release's */
    uint32_t inc = eg_increment(eff_rate(rate, re[5], re[6]), cnt, (uint32_t)-1);
    if (!inc || c.FEG.passed) return;
    bool C = c.FEG.v >= target;
    int32_t nv = (int32_t)c.FEG.v + dir * (int32_t)inc;
    nv = nv < 0 ? 0 : nv > 0x1FFF ? 0x1FFF : nv;
    if (c.FEG.state >= EG_DECAY2) {
        if ((nv >= target) == C) c.FEG.v = (uint16_t)nv;   /* hold short of the target */
    } else {
        c.FEG.v = (uint16_t)nv;
        if ((nv >= target) != C) c.FEG.passed = true;
    }
}

/* c6 of frame k: key events.  KYONEX (tests/kon_defer, eg_sched2) is latched at the sample boundary; each slot reads its
 * KYONB at its frame (as sampled at c0; tests/sub_sched exp 0) and the key event takes effect on the next sample, which
 * takes no envelope step: the key-on level lasts 2 samples when that sample is a clock, else 1 (tests/eg_lock keys,
 * feg_track batch 2).  The envelope pass of this sample runs in frame k + 5 (stage_eg) with what this records. */
void AicaModel::stage_key(int k) {
    Slot &c = slot[k];
    auto &p = c.egp;
    p.keyed = p.keyed_off = false;
    p.aeg_prev = c.AEG.state;
    p.feg_prev = c.FEG.state;
    p.prev_dir = c.FEG.dir;
    p.prev_passed = c.FEG.passed;
    p.clk = eg_clk;
    p.cnt = eg_cnt;
    if (kx_cur) {
        const bool on = (ra[0] & 0x4000) != 0;
        if (!on && c.AEG.state != EG_RELEASE) {
            /* key-off: a clock on this sample steps the OLD segment (aeg_clock / feg_clock) */
            c.AEG.state = EG_RELEASE;
            c.FEG.state = EG_RELEASE;
            c.FEG.dir = c.FEG.v >= (rb[7] & 0x1FFF) ? -1 : 1;
            c.FEG.passed = false;
            p.keyed_off = true;
        }
        if (on && c.AEG.state == EG_RELEASE) {
            /* key-on: a = 0x280 (-60 dB, tests/sgc_aeg); R 63: 0 from the key-on sample (tests/sgc_krs); FEG from FLV0
             * (tests/feg_probe); CA restarts (tests/aeg_koff kon_rel: also from a release that has not reached off).
             * Not from a one-shot end, which keeps the envelope running (tests/oneshot). */
            c.enabled = true;
            c.AEG.state = EG_ATTACK;
            c.AEG.a = eff_rate(ra[4] & 0x1F, ra[5], ra[6]) >= 63 ? 0 : 0x280;
            c.AEG.off = false;
            c.stop_in = false;
            c.ca_clr = false;
            c.FEG.state = EG_ATTACK;
            c.FEG.v = rb[3] & 0x1FFF;
            c.FEG.dir = c.FEG.v >= (rb[4] & 0x1FFF) ? -1 : 1;
            c.FEG.passed = false;
            c.CA = 0;
            c.step = 0;
            c.in_loop = false;
            c.looped = false;
            c.keyed_fetch = true;
            p.keyed = true;
        }
    }
    mon[k].ca = (uint16_t)c.CA;
    eg_valid[k] = true;
}

/* frame k + 5: the envelopes of the next sample (tests/sub_env: an RR rewrite acts in this sample's pass when its X0
 * is before the start of frame k + 5, the level stage's frame + 1; the registers 0x10 / 0x14 / 0x18 / 0x30 .. 0x44 are
 * read at the frame start, c1), with the clock of the slot's own sample; computed and the EG monitor written at c7
 * (rtl/v1 aica_sgc: the state RAM's port is the key stage's at c6) */
void AicaModel::stage_eg(int k) {
    Slot &c = slot[k];
    const auto &p = c.egp;
    if (p.clk) {
        aeg_clock(k, p.keyed, p.keyed_off, p.aeg_prev, p.cnt);
        feg_clock(k, p.keyed, p.keyed_off, p.feg_prev, p.prev_dir, p.prev_passed, p.cnt);
    }
    mon[k].a = c.AEG.a;
    mon[k].ast = (uint8_t)c.AEG.state;
    mon[k].off = c.AEG.off;
    mon[k].v = c.FEG.v;
    mon[k].fst = (uint8_t)c.FEG.state;
}

/* Slot filter: two integrators in 1/8-sample units, but the damping product
 * is rounded UP to 1/4 sample before the cutoff multiply. The band increment
 * rounds down and the low increment rounds up. This intermediate quantization
 * explains the small limit cycles, including period three at f=1, q=1.
 * Verified on filt_cyc, filt_id2, filt_imp and filt_edges (see NOTES.md).
 * FLV bit 0 is unused; only 0x1FFE/0x1FFF use unity instead of 511/512.
 * The inverted OUTPUT saturates to signed 20 bits after conversion to 1/16
 * sample units; the integrator states do not
 * saturate there (verified by the clipped, resonant filt_id2 stream). */
static const uint8_t lpf_q128[32] = {192, 176, 160, 144, 128, 120, 112, 104, 96, 88, 80, 72, 64, 60, 56, 52,
                                     48, 44, 40, 36, 32, 30, 28, 26, 24, 22, 20, 18, 16, 15, 14, 13};
static inline int64_t ceil_shr(int64_t v, int sh) { return -((-v) >> sh); }

int32_t AicaModel::lpf_step(int ch, int32_t x8, uint16_t v, uint32_t q) {
    Slot &c = slot[ch];
    int64_t k = v >= 0x1FFE ? 512 : 256 + ((v >> 1) & 0xFF);
    int sh = 24 - (int)(v >> 9);
    int64_t damping = 2 * ceil_shr((int64_t)lpf_q128[q & 0x1F] * c.lpf_band, 8);
    /* The high-pass difference saturates to signed 24 bits BEFORE multiplying
     * by cutoff (filt_overflow and held-out filt_overflow_check). */
    int64_t high = (int64_t)x8 - c.lpf_low - damping;
    high = high < -8388608 ? -8388608 : high > 8388607 ? 8388607 : high;
    int64_t band = c.lpf_band + ((k * high) >> sh);
    int64_t low = c.lpf_low + ceil_shr(k * band, sh);
    c.lpf_band = (int32_t)band;
    c.lpf_low = (int32_t)low;
    return (int32_t)-low;
}

/* Volume (tests/sgc_level): attenuation a in 0.09375 dB units (TL counts 4) -> 7-bit mantissa M = 127 - a[5:0],
 * exponent a >> 6:  V = floor(sample * M / 2^(7 + (a >> 6))).  VOFF bypasses it (V = sample).
 * The ALFO contribution to a is minicast's (AEG10 + ALFO); unmeasured. */
static inline int32_t att_apply(int32_t sample, uint32_t a) {
    if (a >= 0x3FF) a = 0x3FF;
    uint32_t M = 127 - (a & 63), k = a >> 6;
    return (int32_t)(((int64_t)sample * M) >> (7 + k));
}
/* send levels (IMXL; DISDL assumed the same): n = 15 - L, x 2^-(n>>1) x (n odd ? 3/4 : 1); L = 0 is off.
 * Applied at MIXS scale (x16) with a single floor. */
static inline int32_t send_level(int32_t v16, uint32_t L) {
    if (L == 0) return 0;
    uint32_t n = 15 - L;
    int64_t x = (int64_t)v16 * ((n & 1) ? 3 : 4);
    return (int32_t)(x >> (2 + (n >> 1)));
}

/* The output pipeline (tests/sub_frame, console: the sample in which a register write acts, against the slot's frame --
 * 0x00..0x1C in frame k, TL / VOFF / LPOFF (0x28) in frame k + 4, IMXL (0x20) in frame k + 7, each read at the start
 * of its frame: a write whose X0 is at c2 of that frame acts one sample later):
 *   frame k c7      stage_interp: the noise generator's step, noise, interpolation
 *   frame k + 4 c1  stage_level:  0x28 read; filter (Q, LPOFF) and level (TL, VOFF, AEG, ALFO)
 *   frame k + 7 c1  stage_send:   0x20 / 0x24 read; the send to the ISEL bus and the direct outputs
 *   frame k + 8 c7  the MIXS write (the bank the DSP reads during the next sample; slot 63's lands at ph 63)
 * The stages of slots 60..63 (level) and 57..63 (send) run in the next sample's first frames. */
void AicaModel::stage_interp(int k) {
    Slot &c = slot[k];
    OutPipe &p = pipe[k];
    /* noise generator (tests/sgc_formats): one global 17-bit LFSR, s[n] = s[n-12] ^ s[n-17], stepped once per slot
     * (64 steps per sample); a noise slot outputs the signed byte of 8 consecutive sequence bits << 8 (bit j = the
     * j-th newer bit), independent of pitch, no interpolation.  The power-on state is unknown (model seed 1). */
    uint32_t nb = ((lfsr >> 0) ^ (lfsr >> 5)) & 1;
    lfsr = (lfsr >> 1) | (nb << 16);
    /* a noise slot outputs the LFSR byte whether it plays or not (tests/sgc_formats hw9 fm_1: from before the key-on) */
    const bool noise = (ra[0] >> 10) & 1;
    if (noise) c.s0 = c.s1 = (int32_t)(int8_t)(lfsr & 0xFF) << 8;
    /* interpolation (tests/sgc_pitch): only the top 6 bits of the 14-bit phase fraction are used, and the result
     * keeps 4 fraction bits:  s16 = 16*s0 + floor((s1 - s0) * frac6 / 4)   (1/16 sample units).  A stopped sample
     * supplies zero; its filter keeps evolving (filt_id2 tails; tests/slot_tail) */
    p.s16 = (ho.en_out || noise) ? c.s0 * 16 + (((c.s1 - c.s0) * (int32_t)ho.frac6) >> 2) : 0;
    p.a = ho.a;
    p.v = ho.v;
    p.alfo_w = ho.alfo_w;
    p.lfsr_b = (uint8_t)((x_alfo_noff == 2 ? lfsr_alfo_j(lfsr) : lfsr_adv(lfsr, x_alfo_noff)) & 0xFF);   /* the ALFO noise waveform */
    p.r1c = ra[7];
    p.bank = sbank;
}

/* the filter and level of slot k with register 0x28 = r28; the filter state is updated when upd */
int32_t AicaModel::level_of(int k, uint16_t r28, bool upd) {
    const OutPipe &p = pipe[k];
    int32_t s16 = p.s16;
    /* the filter works in 1/8 units (tests/filt_id); a fractional (interpolated) sample enters as floor(s16 / 2)
     * (tests/filt_frac: floor reproduces every sample at three fractional pitches, ceil/toward zero/nearest fail).
     * LPOFF alone freezes the filter. */
    if (!((r28 >> 5) & 1)) {
        Slot save = slot[k];
        s16 = clampi(lpf_step(k, s16 >> 1, p.v, r28 & 0x1F) * 2, -524288, 524287);
        if (!upd) slot[k] = save;
    }
    /* the volume stage outputs whole samples (tests/sgc_level); VOFF passes the fractional signal through
     * (tests/filt_id, sgc_pitch).  Nothing is muted: an "off" slot's level law with a saturated at 0x3FF keeps
     * applying to the zero-input filter tail (tests/slot_tail tail_b), VOFF passes it whole (tail_a). */
    if ((r28 >> 6) & 1) return s16;
    /* amplitude LFO (tests/sgc_lfo): attenuation (w & 0xFE) >> (7 - ALFOS) with w the 8-bit waveform; the noise
     * waveform is the global LFSR byte at this slot, every sample (it ignores the LFO clock) */
    const uint32_t ws = (p.r1c >> 3) & 3, alfos = p.r1c & 7;
    const uint32_t w = ws == 3 ? p.lfsr_b : p.alfo_w;
    const uint32_t alfo = alfos ? ((w & 0xFE) >> (7 - alfos)) : 0; /* ALFOS 7: 0..254, 6: 0..127, ..., 1: 0..3 */
    return (att_apply(s16, ((r28 >> 8) & 0xFF) * 4 + p.a + alfo) >> 4) * 16;
}

void AicaModel::stage_level(int k) {
    pipe[k].V16 = level_of(k, chr(k, 0x28), true);
}

/* every slot writes its ISEL bus every sample, IMXL is a pure gain (IMXL 0 sends 0): tests/mixs_write, 21/21 probes;
 * MIXS is 20-bit (sample scale x16), DSP INPUTS = MIXS << 4 */
void AicaModel::stage_send(int k) {
    const OutPipe &p = pipe[k];
    const uint16_t r20 = chr(k, 0x20), r24 = chr(k, 0x24);
    const int32_t d = send_level(p.V16, (r20 >> 4) & 0xF);
    auto &q = mq[k & 1];
    q.v = true;
    q.bank = p.bank;
    q.isel = r20 & 0xF;
    q.val = d;
    if (k == 0) { memset(mixs_acc, 0, sizeof mixs_acc); mixs_sent = 0; dl_acc = dr_acc = 0; }
    mixs_acc[r20 & 0xF] += d;
    mixs_sent |= (uint16_t)(1u << (r20 & 0xF));
    /* direct outputs (not observable digitally; 16-bit scale): DISDL level, DIPAN attenuates one side by
     * 3 dB per step (minicast law), 0x1F/0x0F = that side off */
    const int32_t V = p.V16 >> 4;
    const int32_t dir = send_level(V, (r24 >> 8) & 0xF);
    const uint32_t pan = r24 & 0x1F;
    const int32_t side = (pan & 0xF) == 0xF ? 0 : send_level(dir, 15 - (pan & 0xF));
    if (pan & 0x10) { dl_acc += dir; dr_acc += side; }
    else { dl_acc += side; dr_acc += dir; }
    if (k == 63) { dsum_l = dl_acc; dsum_r = dr_acc; }   /* the sweep's direct sum, for the mixer at ph 65 */
}

/* the tools' MIXS of the sweep whose stage A just ended (ph 511): the sends done so far plus those of slots 57..63,
 * computed now with the registers as they are (their stages run in the next sample's first frames; a write to
 * 0x20 / 0x24 / 0x28 of those slots at the sample boundary acts on them there, not in this snapshot) */
void AicaModel::publish_mixs() {
    int32_t acc[16];
    uint16_t sent = mixs_sent;
    memcpy(acc, mixs_acc, sizeof acc);
    for (int k = 57; k < 64; k++) {
        const int32_t V16 = k >= 60 ? level_of(k, chr(k, 0x28), false) : pipe[k].V16;
        const uint16_t r20 = chr(k, 0x20);
        acc[r20 & 0xF] += send_level(V16, (r20 >> 4) & 0xF);
        sent |= (uint16_t)(1u << (r20 & 0xF));
    }
    for (int i = 0; i < 16; i++) MIXS[i] = (sent >> i) & 1 ? sext(acc[i], 20) : mixs[sbank][i];
}

void AicaModel::sgc_clock(uint32_t f, uint32_t c) {
    switch (c) {
    case 0:
        for (int i = 0; i < 8; i++) ra[i] = reg[((0x80 * f) >> 2) + i];
        reload_q = lfo_reload[f];
        lfo_reload[f] = false;
        /* the previous slot's wave RAM step (DSP step 2(f-1) - 14, c0-c3 of this frame): the word its fetch left */
        if (own.valid && own.claim) own.lw = (uint16_t)ram16(ram, own.lw_addr);
        if (ph == 0) {
            /* the sample boundary: KYONEX latched; the envelope clock and counter of the sample the c6 passes compute
             * (n + 1): the samples whose capture (the DSP of the following sample) has even MDEC_CT, i.e.
             * MDEC_CT(n + 1) - 1 = MDEC_CT(n) - 2 even (tests/eg_lock, feg_track, aeg_dl0) */
            kx_cur = kyonex_pending;
            kyonex_pending = false;
            const uint32_t md = (MDEC_CT - 2) & 0xFFFF;
            eg_clk = (md & 1) == eg_par;
            eg_cnt = (eg_K - (md >> 1)) & 0x3FFF;
        }
        break;
    case 1:
        if (pipe_valid[(f - 4) & 63]) stage_level((int)((f - 4) & 63));
        if (pipe_valid[(f - 7) & 63]) stage_send((int)((f - 7) & 63));
        if (eg_valid[(f - 5) & 63]) for (int i = 0; i < 18; i++) re[i] = reg[((0x80 * ((f - 5) & 63)) >> 2) + i];
        break;
    case 4: stage_a((int)f); break;
    case 5:
        for (int i = 0; i < 12; i++) rb[i] = reg[((0x80 * f + 0x20) >> 2) + i];
        break;
    case 6: stage_key((int)f); break;
    case 7: {
        if (eg_valid[(f - 5) & 63]) stage_eg((int)((f - 5) & 63));
        /* the MIXS write of the send of the previous frame (slot f - 8): every slot writes its ISEL bus, the first
         * writer of the sweep overwrites, a bus nobody points at keeps its value (tests/mixs_write, eg_lock mixs);
         * 20-bit wrap, no saturation (tests/sgc_mix) */
        auto &q = mq[(f - 8) & 1];
        if (q.v) {
            int32_t &b = mixs[q.bank][q.isel];
            b = sext(((mixs_w[q.bank] >> q.isel) & 1 ? b : 0) + q.val, 20);
            mixs_w[q.bank] |= (uint16_t)(1u << q.isel);
            q.v = false;
        }
        stage_interp((int)f);
        pipe_valid[f] = true;
        own.valid = true;
        own.claim = ho.claim;
        own.lw_addr = ho.lw_addr;
        if (ph == DSP_OFFSET - 1) {   /* the DSP's sample boundary: it reads the bank the last sweep filled */
            mixs_w[sbank] = 0;
            dsp_bank = sbank ^ 1;
        }
        if (ph == SAMPLE_CLKS - 1) {
            publish_mixs();
            sbank ^= 1;
        }
        break;
    }
    default: break;
    }
}

/* ------------------------------------------------------------------------------------------------------------------
 * DSP (measured semantics: tests/dsp_basic, dsp_mem, dsp_mem2, dsp_mem3, dsp_wslot, dsp_coll; see NOTES.md "DSP").
 * Step s at ph 64 + 4 s: t0 MPRO / COEF read (a landed read enters the memory latch at odd steps), t1 operand reads
 * (TEMP, MEMS, MADRS) and SHIFTED of the previous step's ACC, t2 multiply (MIXS read), t3 writes (TEMP, MEMS,
 * FRC, ADRS, EFREG, wave RAM: even steps at c3, odd at c7); an odd step's read runs at c1 of the next frame.
 * ---------------------------------------------------------------------------------------------------------------- */
uint32_t AicaModel::dsp_addr(uint16_t madrs, const dsp_inst_t &m) const {
    /* ADDR = MADRS[MASA] + (ADREB ? sext12(ADRS_REG) : 0) + NXADR; TABLE=0 adds MDEC_CT and masks to the ring
     * (RBL 0..3 = 8K/16K/32K/64K words), TABLE=1 masks to 16 bits; word address RBP * 1024 + ADDR */
    const uint16_t rbpl = r(0x2804);
    uint32_t a = madrs;
    if (m.ADREB) a += (uint32_t)sext((int32_t)dsp.ADRS_REG, 12);
    if (m.NXADR) a++;
    const uint32_t RBL = (8192u << ((rbpl >> 13) & 3)) - 1;
    if (!m.TABLE) a = (a + dsp_ct()) & RBL;
    else a &= 0xFFFF;
    return (a + (rbpl & 0xFFF) * 1024u) & 0xFFFFF;
}

static uint8_t fix_place(uint8_t d) {   /* wren7 dc_arm_map.cpp slot_used; bit i = step FIX_STEP0 + i */
    static const int fos[3] = {1, 0, 2}, sos[3] = {3, 2, 4};
    for (int fi = 0; fi < 3; fi++) {
        const int fo = fos[fi];
        if ((d >> fo) & 1 || (fo != 1 && !((d >> 1) & 1))) continue;
        for (int si = 0; si < 3; si++) {
            const int so = sos[si];
            if ((d >> so) & 1 || (so != 3 && !((d >> 3) & 1)) || so - fo < 2) continue;
            return (uint8_t)((1u << fo) | (1u << so));
        }
    }
    return 0x0A;
}

void AicaModel::dsp_clock(uint32_t s, uint32_t t) {
    auto &d = dsp;
    const uint32_t c = ph & 7;
    /* the odd step's read, at c1 of the next frame (a read in a channel's slot returns the channel's word) */
    if (c == 1 && d.rd_post) {
        d.mem_pend = d.rd_coll ? d.rd_word : ram16w(ram, d.rd_addr);
        d.mem_land = true;
        d.rd_post = false;
    }
    switch (t) {
    case 0: {
        if ((s & 1) && d.mem_land) { d.memval = d.mem_pend; d.mem_land = false; }   /* reads land at odd steps */
        const uint16_t w[4] = {r(0x3400 + 16 * s), r(0x3404 + 16 * s), r(0x3408 + 16 * s), r(0x340C + 16 * s)};
        dsp_decode(w, &d.fetch);
        d.coef_f = r(0x3000 + 4 * s);
        d.ma_r = r(0x3200 + 4 * d.ev_in.MASA);   /* the waiting even-step read's MADRS */
        break;
    }
    case 1: {
        const dsp_inst_t &in = d.fetch;
        /* a playing channel's fetch owns the wave RAM slot of its even step 2K - 14 (tests/dsp_coll, wren7 collide2) */
        d.coll_q = !(s & 1) && own.valid && own.claim;
        d.in = in;
        d.coef = d.coef_f;
        switch (in.SHIFT) {   /* the shifter, on the previous step's ACC */
        case 0: d.SHIFTED = clampi(d.ACC, -0x800000, 0x7FFFFF); break;
        case 1: d.SHIFTED = clampi(d.ACC * 2, -0x800000, 0x7FFFFF); break;
        case 2: d.SHIFTED = sext(d.ACC * 2, 24); break;
        default: d.SHIFTED = sext(d.ACC, 24); break;
        }
        d.tempr = TEMP[(in.TRA + dsp_ct()) & 0x7F];
        d.mems_q = MEMS[in.IRA & 0x1F];
        d.ma_w = r(0x3200 + 4 * in.MASA);
        break;
    }
    case 2: {
        const dsp_inst_t &in = d.in;
        int32_t INPUTS;
        if (in.IRA <= 0x1F) INPUTS = d.mems_q;
        else if (in.IRA <= 0x2F) INPUTS = mixs[dsp_bank][in.IRA - 0x20] << 4;   /* the bank of the previous sweep */
        else if (in.IRA <= 0x31) INPUTS = EXTS[in.IRA - 0x30] << 8;
        else INPUTS = 0;
        INPUTS = sext(INPUTS, 24);
        int32_t B = in.ZERO ? 0 : in.BSEL ? d.ACC : d.tempr;
        if (in.NEGB) B = -B;
        const int32_t X = in.XSEL ? INPUTS : d.tempr;
        int32_t Y;
        switch (in.YSEL) {
        case 0: Y = sext(d.FRC_REG, 13); break;
        case 1: Y = sext(d.coef >> 3, 13); break;
        case 2: Y = sext(d.Y_REG >> 11, 13); break;
        default: Y = (d.Y_REG >> 4) & 0xFFF; break;
        }
        if (in.YRL) d.Y_REG = INPUTS;
        d.ACC = sext((int32_t)(((int64_t)X * Y) >> 12) + B, 26);
        d.INPUTS = INPUTS;
        break;
    }
    default: {   /* t3 */
        const dsp_inst_t &in = d.in;
        const int32_t SH = d.SHIFTED;
        if (in.FRCL) d.FRC_REG = in.SHIFT == 3 ? (SH & 0xFFF) : ((SH >> 11) & 0x1FFF);
        if (in.EWT) EFREG[in.EWA] = (int16_t)(SH >> 8);
        if (in.TWT) TEMP[(in.TWA + dsp_ct()) & 0x7F] = SH;
        if (in.IWT) MEMS[in.IWA] = (d.nofl_pipe & 2) ? sext((int32_t)d.memval << 8, 24) : dsp_unpack(d.memval);
        /* Memory (tests/dsp_mem, dsp_mem2, dsp_wslot): MWT writes at its own step, with its own SHIFTED/NOFL/address,
         * even or odd.  Reads use the odd-step memory slots: an odd-step MRD's word reaches IWT two steps later; an
         * even-step MRD waits for the next odd slot (landing three steps later) and is lost if that odd step reads
         * too.  An MRD in the same instruction as an MWT does not read (IWT then sees the memory read latch, which
         * holds the last word any master read -- tests/dsp_mem3).  ADRS_REG is 12 bits, sign-extended into the
         * 16-bit address sum.  A channel's slot (tests/dsp_coll): an MWT there is dropped, an MRD there returns the
         * word the channel's fetch left. */
        const uint32_t mw = dsp_addr(d.ma_w, in);
        if (in.MWT && !d.coll_q) {
            const uint16_t v = in.NOFL ? (uint16_t)(SH >> 8) : dsp_pack(SH);
            ram[mw << 1] = (uint8_t)v;
            ram[(mw << 1) + 1] = (uint8_t)(v >> 8);
        }
        if (s & 1) {
            const bool deferred = d.ev_valid && !in.MRD;
            if ((in.MRD && !in.MWT) || deferred) {
                d.rd_post = true;
                d.rd_addr = deferred ? dsp_addr(d.ma_r, d.ev_in) : mw;
                d.rd_coll = deferred && d.ev_coll;
                d.rd_word = d.ev_word;
            }
            d.ev_valid = false;
        } else if (in.MRD && !in.MWT) {
            d.ev_valid = true;
            d.ev_in = in;
            d.ev_coll = d.coll_q;
            d.ev_word = own.lw;
        }
        if (in.ADRL) d.ADRS_REG = in.SHIFT == 3 ? ((SH >> 12) & 0xFFF) : ((uint32_t)(d.INPUTS >> 16) & 0xFFF);
        d.nofl_pipe = (uint8_t)(((d.nofl_pipe << 1) | in.NOFL) & 3);
        /* the fixed pair: MPRO rows 108..112 read ahead (wren7 NOTES "Contention") */
        if (s >= 100 && s <= 104) {
            const uint16_t w2 = r(0x3408 + 16 * (s + 8));
            const uint8_t bit = (uint8_t)(1u << (s - 100));
            d.fix_d = (uint8_t)(((w2 >> 13) & 3) ? d.fix_d | bit : d.fix_d & ~bit);
        }
        break;
    }
    }
}

/* ------------------------------------------------------------------------------------------------------------------
 * Register interface (the bus's register lane; model AicaModel::read of the sample model, rtl/v1 aica_bus)
 * ---------------------------------------------------------------------------------------------------------------- */
uint32_t AicaModel::reg_read(uint32_t off) {
    off &= 0x7FFC;
    if (off < 0x2000) return reg[off >> 2] & chan_mask(off & 0x7F);
    if (off < 0x3000) {
        switch (off) {
        case 0x2800: return 0x0010; /* VER = 1; MVOL/DAC18B/MEM8MB/MONO write-only */
        case 0x2804: return 0;
        case 0x2808: return 0x0900;
        case 0x2810: {   /* EG monitor of the MSLC slot: LP (cleared by the read), state, value (AFSEL: the FEG) */
            const int ch = bus.mslc_slot;
            const uint32_t lp = slot[ch].looped ? 1u : 0u;
            slot[ch].looped = false;
            if (bus.mslc_afsel) return (lp << 15) | ((uint32_t)mon[ch].fst << 13) | mon[ch].v;
            return (lp << 15) | ((uint32_t)mon[ch].ast << 13) | (mon[ch].off ? 0x1FFFu : mon[ch].a);
        }
        case 0x2814: return mon[bus.mslc_slot].ca;
        case 0x2890: case 0x2894: case 0x2898: return 0;   /* timers: write-only (tests/timer_probe, timer_irq) */
        case 0x289C: return irq.scieb;
        case 0x28A0: return irq.scipd;
        case 0x28B4: return irq.mcieb;
        case 0x28B8: return irq.mcipd;
        case 0x28A4: case 0x28A8: case 0x28AC: case 0x28B0: case 0x28BC: return 0;   /* SCIRE, SCILV0-2, MCIRE */
        default: return reg[off >> 2];
        }
    }
    if (off >= 0x4500 && off < 0x4580) {
        /* the CPU accesses the DSP's bank (tests/mixs_rd; rtl/v1: whole values at any phase, wren7 collide2) */
        const int32_t v = mixs[dsp_bank][(off >> 3) & 15];
        return (off & 4) ? ((v >> 4) & 0xFFFF) : (v & 0xF);
    }
    if (off < 0x3200) return reg[off >> 2];                       /* COEF (bits 15:3) */
    if (off < 0x3C00) return reg[off >> 2];                       /* MADRS, MPRO */
    if (off >= 0x4000 && off < 0x4400) {
        const int i = (off - 0x4000) >> 3;
        return (off & 4) ? ((TEMP[i] >> 8) & 0xFFFF) : (TEMP[i] & 0xFF);
    }
    if (off >= 0x4400 && off < 0x4500) {
        const int i = (off - 0x4400) >> 3;
        return (off & 4) ? ((MEMS[i] >> 8) & 0xFFFF) : (MEMS[i] & 0xFF);
    }
    if (off >= 0x4580 && off < 0x45C0) return EFREG[(off - 0x4580) >> 2] & 0xFFFF;
    return 0;   /* 0x3C00..0x3FFF, EXTS (0 without a disc), beyond */
}

void AicaModel::reg_write(uint32_t off, uint16_t v) {
    off &= 0x7FFC;
    if (off < 0x2000) {
        const int ch = off >> 7;
        const uint32_t o = off & 0x7F;
        if (o >= 0x48) return;
        reg[off >> 2] = v & chan_mask(o);
        if (o == 0x00 && (v & 0x8000)) kyonex_pending = true;   /* KYONEX: latched at the next sample boundary */
        if (o == 0x1C) lfo_reload[ch] = true;
        return;
    }
    if (off < 0x3000) {
        reg[off >> 2] = v;
        if (off == 0x280C) { bus.mslc_slot = (v >> 8) & 0x3F; bus.mslc_afsel = (v >> 14) & 1; }
        switch (off) {
        case 0x2890: case 0x2894: case 0x2898: {
            const int t = (off - 0x2890) >> 2;
            irq.cnt[t] = (uint8_t)v;
            irq.pre[t] = (v >> 8) & 7;
            break;
        }
        case 0x289C: irq.scieb = v & 0x7FF; break;
        case 0x28A0: irq.scipd |= v & 0x20; break;   /* SCPU: the only bit a write sets */
        case 0x28A4: irq.scipd &= ~v; break;
        case 0x28B4: irq.mcieb = v & 0x7FF; break;
        case 0x28B8: irq.mcipd |= v & 0x20; break;
        case 0x28BC: irq.mcipd &= ~v; break;
        default: break;
        }
        return;
    }
    if (off < 0x3200) { reg[off >> 2] = v & 0xFFF8; return; }     /* COEF */
    if (off < 0x3C00) { reg[off >> 2] = v; return; }               /* MADRS, MPRO */
    if (off >= 0x4000 && off < 0x4400) {
        const int i = (off - 0x4000) >> 3;
        if (off & 4) TEMP[i] = sext((int32_t)(((uint32_t)v << 8) | (TEMP[i] & 0xFF)), 24);
        else TEMP[i] = (TEMP[i] & ~0xFF) | (v & 0xFF);
        return;
    }
    if (off >= 0x4400 && off < 0x4500) {
        const int i = (off - 0x4400) >> 3;
        if (off & 4) MEMS[i] = sext((int32_t)(((uint32_t)v << 8) | (MEMS[i] & 0xFF)), 24); /* low byte not CPU-writable */
        return;
    }
    if (off >= 0x4500 && off < 0x4580) {
        int32_t &b = mixs[dsp_bank][(off >> 3) & 15];
        if (off & 4) b = sext((int32_t)(((uint32_t)v << 4) | (b & 0xF)), 20);
        else b = (b & ~0xF) | (v & 0xF);
        return;
    }
    if (off >= 0x4580 && off < 0x45C0) { EFREG[(off - 0x4580) >> 2] = (int16_t)v; return; }
}

void AicaModel::write(uint32_t off, uint32_t val) { reg_write(off, (uint16_t)val); }
uint32_t AicaModel::read(uint32_t off) { return reg_read(off); }

uint32_t AicaModel::ram_read32(uint32_t off) {
    off &= RAM_SIZE - 4;
    uint32_t v = ram[off] | (ram[off + 1] << 8) | (ram[off + 2] << 16) | ((uint32_t)ram[off + 3] << 24);
    dsp.memval = (uint16_t)(v >> 16); /* 16-bit memory bus: the upper half is read last and stays in the read latch */
    return v;
}
void AicaModel::ram_write32(uint32_t off, uint32_t v, uint8_t be) {
    off &= RAM_SIZE - 4;
    for (int i = 0; i < 4; i++)
        if ((be >> i) & 1) ram[off + i] = (uint8_t)(v >> (8 * i));
}

/* ------------------------------------------------------------------------------------------------------------------
 * Bus (rtl/v1 aica_bus; wren7-rtl DcArmBus / DcWaits::dreamcast()): the arbitration unit is the DSP step.
 *   t0  requests present now compete for this step
 *   t1  grants: wave RAM has one slot per step, held by the DSP's MRD / MWT of the step, by slot K's fetch at step
 *       2K - 14, or by the fixed pair (steps 109 / 111, moved around the DSP's); the SH4 then the ARM get a free one.
 *       Registers need no RAM slot; TEMP / EFREG wait while the step's TWT / EWT holds that port; the ARM wins the
 *       register lane.
 *   t2  X0: the granted accesses act (RAM lane and register lane)
 *   t3  X1: data to the master.  SH4: ack at t3 of its (last) slot's step -- a RAM read takes a second slot >= 2 steps
 *       after the first (data from the first).  ARM: ack in the last of 8 clocks from its step (a locked write 4); L / M
 *       (0x2D00 / 0x2D04) are local: acknowledged in the request's own clock (the interrupt controller: v2).
 * ---------------------------------------------------------------------------------------------------------------- */
static inline bool port_free(uint32_t a, bool ptemp, bool pefreg) {
    const uint32_t o = a & 0x7FFC;
    return !((o >= 0x4000 && o < 0x4400 && ptemp) || (o >= 0x4580 && o < 0x45C0 && pefreg));
}

void AicaModel::bus_x0_read(uint32_t &gdata) {
    auto &b = bus;
    if (b.r_pend) {
        const BusPort &p = b.r_src == SRC_SH ? sh4 : arm;
        const uint32_t w = (p.addr >> 1) & 0xFFFFF;
        const uint32_t b0 = w << 1, b1 = ((w + 1) & 0xFFFFF) << 1;
        b.r1_data = ram[b0] | (ram[b0 + 1] << 8) | (ram[b1] << 16) | ((uint32_t)ram[b1 + 1] << 24);
        if (p.we) {
            const uint32_t ba[4] = {b0, b0 + 1, b1, b1 + 1};
            for (int i = 0; i < 4; i++)
                if ((p.be >> i) & 1) ram[ba[i]] = (uint8_t)(p.wdata >> (8 * i));
        }
    }
    if (b.g_pend) {
        const BusPort &p = b.g_src == SRC_SH ? sh4 : arm;
        if (!p.we) gdata = reg_read(p.addr);
    }
}

void AicaModel::bus_x0_write() {
    auto &b = bus;
    if (!b.g_pend) return;
    const BusPort &p = b.g_src == SRC_SH ? sh4 : arm;
    if (p.we) reg_write(p.addr, (uint16_t)p.wdata);
}

void AicaModel::bus_edge(uint32_t t, uint32_t c) {
    auto &b = bus;
    const auto o = bus;   /* the values during this clock */
    const bool g0 = t == 2 && o.g_pend, r0 = t == 2 && o.r_pend;
    b.sh_ack_q = false;
    b.arm_ack_q = false;
    /* X1 of the previous clock's X0 */
    if (o.g1) {
        if (o.g1_src == SRC_SH) b.sh_rdata = o.g1_data;
        else if (o.g1_src == SRC_ARM) b.arm_rdata = o.g1_data;
    }
    if (o.r1) {
        if (o.r1_src == SRC_SH) b.sh_rdata = o.r1_data;
        else if (o.r1_src == SRC_ARM) b.arm_rdata = o.r1_data;
        if (!o.r1_we) dsp.memval = (uint16_t)(o.r1_data >> 16);   /* a master's read reaches the DSP's read latch */
    }
    b.g1 = g0; b.g1_src = o.g_src;
    b.r1 = r0; b.r1_src = o.r_src;
    b.r1_we = r0 && (o.r_src == SRC_SH ? sh4.we : arm.we);
    /* the ARM's cycle: ack in its last clock */
    if (o.arm_busy) {
        if (o.arm_ack_q) b.arm_busy = false;
        else if (o.arm_left == 2) b.arm_ack_q = true;
        b.arm_left = (uint8_t)(o.arm_left - 1);
    }
    if (t == 3 && o.sh_fin) { b.sh_ack_q = true; b.sh_busy = false; b.sh_fin = false; }
    const bool arm_local = arm.req && !arm.ram && (arm.addr & 0x7FF8) == 0x2D00;
    if (t == 0) {
        b.arm_t0 = arm.req && !arm_local && !o.arm_busy;
        b.sh_t0 = sh4.req && !o.sh_busy && !o.sh_ack_q;
    }
    if (t == 1) {
        const bool slot_dsp = dsp.run && (dsp.fetch.MRD || dsp.fetch.MWT);
        const uint32_t s = ((ph - DSP_OFFSET) & 511) >> 2;
        const bool slot_fix = dsp.run && s >= (uint32_t)FIX_STEP0 && s < (uint32_t)FIX_STEP0 + 5 &&
                              ((fix_place(dsp.fix_d) >> (s - FIX_STEP0)) & 1);
        const bool ptemp = dsp.run && dsp.fetch.TWT, pefreg = dsp.run && dsp.fetch.EWT;
        bool ram_taken = slot_dsp || slot_fix || (c == 1 && own.valid && own.claim);
        if (o.sh_rd2 == 2) b.sh_rd2 = 1;
        else if (o.sh_rd2 == 1 && !ram_taken) { b.sh_rd2 = 0; b.sh_fin = true; ram_taken = true; }
        b.r_pend = false;
        b.g_pend = false;
        if (o.sh_t0 && sh4.ram && !ram_taken) {
            b.r_pend = true; b.r_src = SRC_SH; b.sh_busy = true; ram_taken = true;
            if (sh4.we) b.sh_fin = true;
            else b.sh_rd2 = 2;
        }
        if (o.arm_t0 && arm.ram && !ram_taken) {
            b.r_pend = true; b.r_src = SRC_ARM; b.arm_busy = true;
            b.arm_left = (arm.lock && arm.we) ? 2 : 6;
        }
        if (o.arm_t0 && !arm.ram && port_free(arm.addr, ptemp, pefreg)) {
            b.g_pend = true; b.g_src = SRC_ARM; b.arm_busy = true;
            b.arm_left = (arm.lock && arm.we) ? 2 : 6;
        } else if (o.sh_t0 && !sh4.ram && port_free(sh4.addr, ptemp, pefreg)) {
            b.g_pend = true; b.g_src = SRC_SH; b.sh_busy = true; b.sh_fin = true;
        }
        b.arm_t0 = false;
        b.sh_t0 = false;
    }
    if (t == 2) { b.g_pend = false; b.r_pend = false; }
    /* port outputs for the next clock */
    sh4.ack = b.sh_ack_q;
    sh4.rdata = b.sh_rdata;
    arm.ack = b.arm_ack_q;
    arm.rdata = b.arm_rdata;
}

/* ------------------------------------------------------------------------------------------------------------------
 * One clock
 * ---------------------------------------------------------------------------------------------------------------- */
/* DAC-side mixing (EFSDL/EFPAN for EFREG, MVOL, MONO, DAC18B): not observable digitally, so unverified.  Levels use the
 * measured send-level law (3 dB steps, send_level); pan attenuates the other side the same way. */
static void volpan(int32_t value, uint32_t vol, uint32_t pan, int32_t &outl, int32_t &outr) {
    int32_t temp = send_level(value, vol);
    int32_t Sc = (pan & 0xF) == 0xF ? 0 : send_level(temp, 15 - (pan & 0xF));
    if (pan & 0x10) { outl += temp; outr += Sc; }
    else { outl += Sc; outr += temp; }
}

void AicaModel::clock() {
    const uint32_t f = ph >> 3, c = ph & 7, t = ph & 3;
    /* (1) the X0 of this clock: its reads see the state before this clock's writes */
    uint32_t gdata = 0;
    if (t == 2) bus_x0_read(gdata);
    /* (2) the slot engine */
    sgc_clock(f, c);
    /* (3) the DSP (it reads MIXS at t2 before a CPU write of the same clock lands) */
    if (dsp.run || ph == (uint32_t)DSP_OFFSET) {
        dsp.run = true;
        dsp_clock(((ph - DSP_OFFSET) & 511) >> 2, t);
    }
    /* (4) the X0's writes, X1, arbitration */
    if (t == 2) {
        bus_x0_write();
        bus.g1_data = gdata;
    }
    bus_edge(t, c);
    /* (5) the output mixer: the direct sum of the last sweep and the EFREG of the DSP sample that ended at ph 63 */
    if (ph == (uint32_t)DSP_OFFSET + 1) {
        for (int i = 0; i < 16; i++) efs[i] = EFREG[i];
        ml = dsum_l;
        mr = dsum_r;
    } else if (ph >= (uint32_t)DSP_OFFSET + 2 && ph < (uint32_t)DSP_OFFSET + 18) {
        const uint32_t e = ph - (DSP_OFFSET + 2);
        const uint16_t v = r(0x2000 + 4 * e);
        volpan(efs[e], (v >> 8) & 0xF, v & 0x1F, ml, mr);
    } else if (ph == (uint32_t)DSP_OFFSET + 18) {
        int32_t l = ml, rr = mr;
        const uint16_t c0 = r(0x2800);
        if (c0 & 0x8000) { l += rr; rr = l; }
        l = send_level(l, c0 & 0xF);
        rr = send_level(rr, c0 & 0xF);
        if (c0 & 0x100) { l >>= 2; rr >>= 2; }
        outL = (int16_t)clampi(l, -32768, 32767);
        outR = (int16_t)clampi(rr, -32768, 32767);
        outs++;
    }
    /* the one-sample interval edge: bit 10, and the timers (wren7 tests/hw/dspport: 40 clocks before DSP step 0) */
    if (ph == (uint32_t)EDGE_PH) {
        uint16_t set = 0x400;
        for (int t = 0; t < 3; t++)
            if (((MDEC_CT - tim_phase) & ((1u << irq.pre[t]) - 1)) == 0 && ++irq.cnt[t] == 0) set |= (uint16_t)(0x40 << t);
        irq.scipd |= set;
        irq.mcipd |= set;
    }
    /* (6) time */
    if (ph == (uint32_t)SAMPLE_CLKS - 1) {
        samples++;
        MDEC_CT = (MDEC_CT - 1) & 0xFFFF;
    }
    ph = (ph + 1) & 511;
    clocks++;
}

void AicaModel::step() {
    do clock(); while (ph != 0);
}

} // namespace caique
