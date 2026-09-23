/* aica_model.cpp -- caique AICA behavioural model.  See aica_model.h and NOTES.md. */
#include "aica_model.h"
#include "dsp_asm.h"
#include "dsp_float.h"
#include <stdlib.h>

namespace caique {

static inline int32_t sext(int32_t v, int bits) { return (int32_t)((uint32_t)v << (32 - bits)) >> (32 - bits); }
static inline int32_t clampi(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }

/* ------------------------------------------------------------------------------------------------------------------
 * Tables (minicast sgc_if.cpp)
 * ---------------------------------------------------------------------------------------------------------------- */
static const int32_t adpcm_qs[8] = {0x0e6, 0x0e6, 0x0e6, 0x0e6, 0x133, 0x199, 0x200, 0x266};

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

void AicaModel::reset() {
    memset(reg, 0, sizeof reg);
    memset(slot, 0, sizeof slot);
    memset(TEMP, 0, sizeof TEMP);
    memset(MEMS, 0, sizeof MEMS);
    memset(MIXS, 0, sizeof MIXS);
    memset(EXTS, 0, sizeof EXTS);
    memset(EFREG, 0, sizeof EFREG);
    MDEC_CT = 1;
    ACC = FRC_REG = Y_REG = 0;
    ADRS_REG = 0;
    memval = 0;
    mem_qn = 0;
    mrd_even_masa = -1;
    nofl_pipe[0] = nofl_pipe[1] = 0;
    outL = outR = 0;
    samples = 0;
    eg_cnt = 0;
    lfsr = 1;
    for (int ch = 0; ch < 64; ch++) {
        for (uint32_t o = 0; o < 0x48; o += 4) slot_regwrite(ch, o);
        slot[ch].enabled = false;
        slot[ch].AEG.state = EG_RELEASE;
        slot[ch].AEG.a = 0x3FF;
        slot[ch].AEG.off = true;
    }
}

/* ------------------------------------------------------------------------------------------------------------------
 * SGC (port of minicast ChannelEx)
 * ---------------------------------------------------------------------------------------------------------------- */
int AicaModel::fmt(int ch) const {
    uint16_t r0 = chr(ch, 0x00);
    return ((r0 >> 10) & 1) ? 4 : (r0 >> 7) & 3; /* SSCTL -> noise */
}
uint32_t AicaModel::sa_addr(int ch) const {
    uint16_t r0 = chr(ch, 0x00);
    uint32_t a = ((uint32_t)(r0 & 0x7F) << 16) | chr(ch, 0x04);
    if (((r0 >> 7) & 3) == 0) a &= ~1u;
    return a;
}

void AicaModel::set_aeg_state(int ch, EgState s) {
    slot[ch].AEG.state = s; /* KYONB is never cleared by the hardware (tests/sgc_keys; minicast cleared it) */
}

void AicaModel::key_on(int ch) {
    Slot &c = slot[ch];
    if (c.AEG.state != EG_RELEASE) return;
    c.enabled = true;
    set_aeg_state(ch, EG_ATTACK);
    c.AEG.a = 0x280; /* key-on level: -60 dB (tests/sgc_aeg) */
    c.AEG.off = false;
    if (eff_rate(ch, chr(ch, 0x10) & 0x1F) >= 63) { /* R 63: instant attack (tests/sgc_krs) */
        c.AEG.a = 0;
        if (!((chr(ch, 0x14) >> 14) & 1)) set_aeg_state(ch, EG_DECAY1);
    }
    c.FEG.state = EG_ATTACK;
    c.FEG.v = chr(ch, 0x2C) & 0x1FFF; /* FLV0 (tests/feg_probe) */
    c.CA = 0;
    c.step = 0;
    c.looped = false;
    c.in_loop = false;
    c.adpcm_quant = 127;
    c.s0 = 0;
    decode_initial(ch);
}

void AicaModel::key_off(int ch) {
    if (slot[ch].AEG.state != EG_RELEASE) set_aeg_state(ch, EG_RELEASE);
    slot[ch].FEG.state = EG_RELEASE;
}

/* effective EG rate R (0..63) from a 5-bit rate register (tests/sgc_krs, all 320 KRS x OCT x FNS combinations):
 *   k = KRS + OCT (signed); s = k < 0 ? 0 : 2 * min(k, 15) + FNS[9];  R = min(63, 2 * rate + s);  KRS = 15: s = 0.
 * rate 0 = no change. */
uint32_t AicaModel::eff_rate(int ch, uint32_t re) {
    if (re == 0) return 0;
    uint32_t KRS = (chr(ch, 0x14) >> 10) & 0xF, FNS = chr(ch, 0x18) & 0x3FF, OCT = (chr(ch, 0x18) >> 11) & 0xF;
    int32_t s = 0;
    if (KRS != 0xF) {
        int32_t k = (int32_t)KRS + ((OCT & 8) ? (int32_t)OCT - 16 : (int32_t)OCT);
        s = k < 0 ? 0 : 2 * (k > 15 ? 15 : k) + (int32_t)((FNS >> 9) & 1);
    }
    int32_t R = (int32_t)re * 2 + s;
    return (uint32_t)(R > 63 ? 63 : R);
}

void AicaModel::update_pitch(int ch) {
    uint32_t OCT = (chr(ch, 0x18) >> 11) & 0xF, FNS = chr(ch, 0x18) & 0x3FF;
    /* phase: 14 fraction bits.  increment = (1024 + FNS) << (OCT + 4), OCT signed -8..7, truncated for OCT < -4
     * (tests/sgc_pitch: all 16 pitches bit-exact only with 14 bits; minicast uses 10 and loses more) */
    uint32_t rate = 1024 | FNS;
    int oct = (OCT & 8) ? (int)OCT - 16 : (int)OCT;
    int sh = oct + 4;
    slot[ch].update_rate = sh >= 0 ? rate << sh : rate >> -sh;
}

void AicaModel::lfo_calc(int ch) {
    Slot &c = slot[ch];
    uint16_t r1c = chr(ch, 0x1C);
    uint32_t st = c.lfo.state, rv = 0;
    switch ((r1c >> 3) & 3) { /* ALFOWS (the noise waveform is taken per sample in slot_output) */
    case 0: rv = st; break;
    case 1: rv = st & 0x80 ? 255 : 0; break;
    case 2: rv = ((st & 0x7f) ^ (st & 0x80 ? 0x7F : 0)) << 1; break;
    case 3: rv = 0; break;
    }
    c.lfo.alfo_w = (uint8_t)rv;
    /* PLFO waveforms are signed 8-bit (tests/sgc_lfo): saw = (int8)state, square = +127 / -128, triangle starts at 0,
     * rises 2 per state to 126, falls to -128, rises back; noise is taken per sample in stream_step */
    int32_t pw = 0;
    switch ((r1c >> 8) & 3) { /* PLFOWS */
    case 0: pw = (int8_t)st; break;
    case 1: pw = st & 0x80 ? -128 : 127; break;
    case 2: pw = st < 64 ? 2 * (int32_t)st : st < 192 ? 256 - 2 * (int32_t)st : 2 * (int32_t)st - 512; break;
    case 3: pw = 0; break;
    }
    c.lfo.plfo = (uint8_t)(int8_t)pw;
}

void AicaModel::update_lfo(int ch, bool from_write) {
    Slot &c = slot[ch];
    uint16_t r1c = chr(ch, 0x1C);
    int N = (r1c >> 10) & 0x1F, S = N >> 2, M = (~N) & 3, G = 128 >> S, L = (G - 1) << 2, O = L + G * (M + 1);
    c.lfo.start_value = O;
    c.lfo.counter = O;
    c.lfo.plfo_shft = 8 - ((r1c >> 5) & 7);
    c.lfo.alfo_shft = 8 - (r1c & 7);
    if (from_write && (r1c & 0x8000)) c.lfo.state = 0; /* LFORE */
    lfo_calc(ch);
}

void AicaModel::slot_regwrite(int ch, uint32_t o) {
    Slot &c = slot[ch];
    switch (o) {
    case 0x00:
        if (reg[(0x80 * ch) >> 2] & 0x8000) { /* KYONEX */
            reg[(0x80 * ch) >> 2] &= 0x7FFF;
            /* key events take effect at the next envelope clock (tests/sgc_aeg: the key-on level always lasts
             * exactly one clock before the first attack step) */
            for (int i = 0; i < 64; i++) slot[i].key_pending = (chr(i, 0x00) & 0x4000) ? 1 : -1;
        }
        break;
    case 0x08: case 0x0C:
        c.LSA = chr(ch, 0x08);
        c.LEA = chr(ch, 0x0C);
        break;
    case 0x18: update_pitch(ch); break;
    case 0x1C: update_lfo(ch, true); break;
    default: break; /* FEG / filter registers: minicast has no FEG */
    }
}

static inline int16_t ram16(const uint8_t *ram, uint32_t byteaddr) {
    byteaddr &= AicaModel::RAM_SIZE - 1;
    return (int16_t)(ram[byteaddr & ~1u] | (ram[(byteaddr & ~1u) + 1] << 8));
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
    int f = fmt(ch);
    if (!last && (f < 2 || f == 4)) return;
    uint32_t sa = sa_addr(ch);
    int32_t s0 = 0, s1 = 0;
    switch (f) {
    case 4: /* noise: taken at output time from the global LFSR (slot_output), not decoded here */
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

void AicaModel::decode_initial(int ch) { decode_sample(ch, true, 0); }

void AicaModel::stream_step(int ch) {
    Slot &c = slot[ch];
    int f = fmt(ch);
    bool LPCTL = (chr(ch, 0x00) >> 9) & 1, LPSLNK = (chr(ch, 0x14) >> 14) & 1;
    /* pitch LFO (tests/sgc_lfo, sgc_lfo2): it scales the 11-bit mantissa before the octave shift:
     *   m = 1024 + FNS;  m' = m + floor(m * w' / 1024),  w' = (w & ~1) >> (7 - PLFOS);  inc = m' << (OCT + 4)
     * w the signed 8-bit waveform, PLFOS 0 = none (square at PLFOS 7, FNS 0: 1024 + 126 / 1024 - 128) */
    uint32_t r1c = chr(ch, 0x1C), plfos = (r1c >> 5) & 7, inc = c.update_rate;
    if (plfos) {
        int32_t w = ((r1c >> 8) & 3) == 3 ? (int32_t)(int8_t)(lfsr & 0xFF) : (int32_t)(int8_t)c.lfo.plfo;
        int32_t mod = (w & ~1) >> (7 - plfos);
        uint32_t OCT = (chr(ch, 0x18) >> 11) & 0xF, FNS = chr(ch, 0x18) & 0x3FF;
        int oct = (OCT & 8) ? (int)OCT - 16 : (int)OCT, sh = oct + 4;
        int32_t m0 = 1024 + (int32_t)FNS, m = m0 + ((m0 * mod) >> 10);
        inc = (uint32_t)(sh >= 0 ? m << sh : m >> -sh);
    }
    c.step += inc;
    uint32_t ip = c.step >> 14;
    c.step &= 0x3FFF;
    while (ip > 0) {
        ip--;
        uint32_t CA = (c.CA + 1) & 0xFFFF;
        uint32_t ca_t = CA;
        if (f == 3) ca_t &= ~3u;
        if (LPSLNK && c.AEG.state == EG_ATTACK && CA >= c.LSA) set_aeg_state(ch, EG_DECAY1);
        /* loop end (tests/sgc_loop): armed once CA has reached LSA since key-on (checked before this step arms it);
         * then CA >= LEA -> CA -= LEA - LSA (16-bit).  Same as CA = LSA for a normal loop; LSA == LEA plays straight
         * through; LEA < LSA jumps forward, and after CA wraps past 0xFFFF the armed loop triggers at LEA. */
        bool armed = c.in_loop;
        if (CA >= c.LSA) c.in_loop = true;
        if (ca_t >= c.LEA && armed) {
            c.looped = true;
            CA = (uint32_t)((int32_t)CA - ((int32_t)c.LEA - (int32_t)c.LSA)) & 0xFFFF;
            if (LPCTL) {
                if (f == 2) { c.adpcm_quant = 127; c.s0 = 0; }
            } else {
                c.enabled = false;
                set_aeg_state(ch, EG_RELEASE);
                c.AEG.a = 0x3FF;
                c.AEG.off = true;
            }
        }
        c.CA = CA;
        decode_sample(ch, ip == 0, CA);
    }
}

/* Envelope (tests/sgc_aeg): the OPN envelope generator clocked every 2 samples.  Rate R < 48: every
 * 2^(11 - R/4) clocks, increment from row R&3; R >= 48: every clock, rows 4.. (R 60-63 all +8).
 * Attack: a += (~a * inc) >> 4, to 0 -> decay 1 (unless LPSLNK).  Decay 1 -> decay 2 once a >= DL << 5 (checked
 * before the increment, the new state's increment applies at once).  Decay 2 / release: a += inc; past 0x3FF the
 * slot is "off" (monitor 0x1FFF), the state is kept. */
static const uint8_t eg_inc[17][8] = {
    {0, 1, 0, 1, 0, 1, 0, 1}, {0, 1, 0, 1, 1, 1, 0, 1}, {0, 1, 1, 1, 0, 1, 1, 1}, {0, 1, 1, 1, 1, 1, 1, 1},
    {1, 1, 1, 1, 1, 1, 1, 1}, {1, 1, 1, 2, 1, 1, 1, 2}, {1, 2, 1, 2, 1, 2, 1, 2}, {1, 2, 2, 2, 1, 2, 2, 2},
    {2, 2, 2, 2, 2, 2, 2, 2}, {2, 2, 2, 4, 2, 2, 2, 4}, {2, 4, 2, 4, 2, 4, 2, 4}, {2, 4, 4, 4, 2, 4, 4, 4},
    {4, 4, 4, 4, 4, 4, 4, 4}, {4, 4, 4, 8, 4, 4, 4, 8}, {4, 8, 4, 8, 4, 8, 4, 8}, {4, 8, 8, 8, 4, 8, 8, 8},
    {8, 8, 8, 8, 8, 8, 8, 8}};
static inline uint32_t eg_increment(uint32_t R, uint32_t cnt) {
    if (R == 0) return 0;
    if (R < 48) {
        uint32_t shift = 11 - (R >> 2);
        if (cnt & ((1u << shift) - 1)) return 0;
        return eg_inc[R & 3][(cnt >> shift) & 7];
    }
    return eg_inc[R >= 60 ? 16 : 4 + (R - 48)][cnt & 7];
}

void AicaModel::aeg_clock(int ch) {
    Slot &c = slot[ch];
    if (c.AEG.off) return;
    uint16_t r10 = chr(ch, 0x10), r14 = chr(ch, 0x14);
    if (c.AEG.state == EG_DECAY1 && c.AEG.a >= (((r14 >> 5) & 0x1F) << 5)) set_aeg_state(ch, EG_DECAY2);
    uint32_t rate = c.AEG.state == EG_ATTACK ? (r10 & 0x1F) : c.AEG.state == EG_DECAY1 ? ((r10 >> 6) & 0x1F)
                  : c.AEG.state == EG_DECAY2 ? ((r10 >> 11) & 0x1F) : (r14 & 0x1F);
    uint32_t inc = eg_increment(eff_rate(ch, rate), eg_cnt);
    if (!inc) return;
    int32_t a = c.AEG.a;
    if (c.AEG.state == EG_ATTACK) {
        a += ((~a) * (int32_t)inc) >> 4;
        if (a <= 0) {
            a = 0;
            if (!((r14 >> 14) & 1)) set_aeg_state(ch, EG_DECAY1);
        }
    } else {
        a += inc;
        if (a > 0x3FF) {
            a = 0x3FF;
            c.AEG.off = true;
            c.enabled = false; /* the slot stops, CA reads 0, in decay 2 as in release (tests/sgc_keys K4) */
            c.CA = 0;
        }
    }
    c.AEG.a = (uint16_t)a;
}

/* Filter envelope (tests/feg_probe): a 13-bit value loaded with FLV0 at key-on that moves linearly toward FLV1
 * (attack, FAR), then FLV2 (decay 1, FD1R), then FLV3 (decay 2, FD2R, then holds); key-off heads for FLV4 (FRR).
 * Steps come from the amplitude envelope's clock and increment tables at the rate's effective R (KRS applied as
 * for the AEG -- unmeasured).  Moving up or down; the target is never overshot; reaching it advances the state
 * (transition timing unmeasured). */
void AicaModel::feg_clock(int ch) {
    Slot &c = slot[ch];
    if (!c.enabled) return;
    uint16_t r40 = chr(ch, 0x40), r44 = chr(ch, 0x44);
    uint32_t rate;
    uint16_t target;
    switch (c.FEG.state) {
    case EG_ATTACK: rate = (r40 >> 8) & 0x1F; target = chr(ch, 0x30); break;
    case EG_DECAY1: rate = r40 & 0x1F; target = chr(ch, 0x34); break;
    case EG_DECAY2: rate = (r44 >> 8) & 0x1F; target = chr(ch, 0x38); break;
    default: rate = r44 & 0x1F; target = chr(ch, 0x3C); break;
    }
    target &= 0x1FFF;
    if (c.FEG.v == target) {
        if (c.FEG.state == EG_ATTACK) c.FEG.state = EG_DECAY1;
        else if (c.FEG.state == EG_DECAY1) c.FEG.state = EG_DECAY2;
        return;
    }
    uint32_t inc = eg_increment(eff_rate(ch, rate), eg_cnt);
    if (!inc) return;
    if (c.FEG.v < target) c.FEG.v = (uint16_t)(c.FEG.v + inc > target ? target : c.FEG.v + inc);
    else c.FEG.v = (uint16_t)(c.FEG.v < target + inc ? target : c.FEG.v - inc);
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

int32_t AicaModel::lpf_step(int ch, int32_t x8) {
    Slot &c = slot[ch];
    uint32_t v = c.FEG.v;
    int64_t k = v >= 0x1FFE ? 512 : 256 + ((v >> 1) & 0xFF);
    int sh = 24 - (int)(v >> 9);
    int64_t damping = 2 * ceil_shr((int64_t)lpf_q128[chr(ch, 0x28) & 0x1F] * c.lpf_band, 8);
    int64_t band = c.lpf_band + ((k * ((int64_t)x8 - c.lpf_low - damping)) >> sh);
    int64_t low = c.lpf_low + ceil_shr(k * band, sh);
    c.lpf_band = (int32_t)band;
    c.lpf_low = (int32_t)low;
    return (int32_t)-low;
}

/* Volume (tests/sgc_level): attenuation a in 0.09375 dB units (TL counts 4) -> 7-bit mantissa M = 127 - a[5:0],
 * exponent a >> 6:  V = floor(sample * M / 2^(7 + (a >> 6))).  VOFF bypasses it (V = sample).
 * The AEG and ALFO contributions to a are still minicast's (AEG10 + 4*ALFO); unmeasured. */
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

void AicaModel::slot_output(int ch, int32_t &l, int32_t &r, int32_t &d) {
    Slot &c = slot[ch];
    /* noise generator (tests/sgc_formats): one global 17-bit LFSR, s[n] = s[n-12] ^ s[n-17], stepped once per slot
     * (64 steps per sample); a noise slot outputs the signed byte of 8 consecutive sequence bits << 8 (bit j = the
     * j-th newer bit), independent of pitch, no interpolation.  The power-on state is unknown (model seed 1). */
    uint32_t nb = ((lfsr >> 0) ^ (lfsr >> 5)) & 1;
    lfsr = (lfsr >> 1) | (nb << 16);
    /* A stopped sample supplies zero, but its filter keeps evolving and VOFF
     * still exposes the result (filt_id2 tails). LPOFF alone freezes the filter. */
    if (c.enabled && ((chr(ch, 0x00) >> 10) & 1)) c.s0 = c.s1 = (int32_t)(int8_t)(lfsr & 0xFF) << 8;
    /* interpolation (tests/sgc_pitch): only the top 6 bits of the 14-bit phase fraction are used, and the result
     * keeps 4 fraction bits:  s16 = 16*s0 + floor((s1 - s0) * frac6 / 4)   (1/16 sample units) */
    int32_t frac6 = (int32_t)(c.step >> 8) & 63;
    int32_t s16 = c.enabled ? c.s0 * 16 + (((c.s1 - c.s0) * frac6) >> 2) : 0;
    int32_t sample = s16 >> 4;
    uint16_t r24 = chr(ch, 0x24), r28 = chr(ch, 0x28), r20 = chr(ch, 0x20);
    uint32_t TL = (r28 >> 8) & 0xFF;
    /* the filter works in 1/8 units (tests/filt_id); its input from a fractional sample: s16 >> 1 (unmeasured) */
    if (!((r28 >> 5) & 1)) s16 = clampi(lpf_step(ch, s16 >> 1) * 2, -524288, 524287);
    /* the volume stage outputs whole samples (tests/sgc_level); VOFF passes the fractional signal through
     * (tests/filt_id, sgc_pitch).  Filter on with VOFF=0: precision unmeasured. */
    int32_t V16;
    if ((r28 >> 6) & 1) V16 = s16; /* VOFF */
    else if (c.AEG.off) V16 = 0;
    else {
        /* amplitude LFO (tests/sgc_lfo): attenuation (w & 0xFE) >> (7 - ALFOS) with w the 8-bit waveform; the noise
         * waveform is the global LFSR byte at this slot, every sample (it ignores the LFO clock) */
        uint32_t r1c = chr(ch, 0x1C), ws = (r1c >> 3) & 3, alfos = r1c & 7;
        uint32_t w = ws == 3 ? (lfsr & 0xFF) : c.lfo.alfo_w;
        uint32_t alfo = alfos ? ((w & 0xFE) >> (7 - alfos)) : 0; /* ALFOS 7: 0..254, 6: 0..127, ..., 1: 0..3 */
        V16 = (att_apply(s16, TL * 4 + c.AEG.a + alfo) >> 4) * 16;
    }
    (void)sample;
    int32_t V = V16 >> 4;
    d = send_level(V16, (r20 >> 4) & 0xF);
    /* direct outputs (not observable digitally; 16-bit scale): DISDL level, DIPAN attenuates one side by
     * 3 dB per step (minicast law), 0x1F/0x0F = that side off */
    int32_t dir = send_level(V, (r24 >> 8) & 0xF);
    uint32_t pan = r24 & 0x1F;
    int32_t side = (pan & 0xF) == 0xF ? 0 : send_level(dir, 15 - (pan & 0xF));
    if (pan & 0x10) { l = dir; r = side; }
    else { l = side; r = dir; }
    if (!c.enabled) return;
    stream_step(ch);
    if ((chr(ch, 0x1C) >> 15) & 1) { /* LFORE: held reset while set (tests/sgc_lfo; minicast: one-shot) */
        c.lfo.state = 0;
        c.lfo.counter = c.lfo.start_value;
        lfo_calc(ch);
    } else if (--c.lfo.counter == 0) {
        c.lfo.state++;
        c.lfo.counter = c.lfo.start_value;
        lfo_calc(ch);
    }
}

/* ------------------------------------------------------------------------------------------------------------------
 * DSP (measured semantics: tests/dsp_basic; see NOTES.md "DSP")
 * ---------------------------------------------------------------------------------------------------------------- */
void AicaModel::dsp_step() {
    uint16_t rbpl = r(0x2804);
    uint32_t RBP = (rbpl & 0xFFF) * 2048u;
    uint32_t RBL = (8192u << ((rbpl >> 13) & 3)) - 1;
    for (int s = 0; s < 128; s++) {
        uint64_t gstep = samples * 128 + s;
        uint16_t w[4] = {r(0x3400 + 16 * s), r(0x3404 + 16 * s), r(0x3408 + 16 * s), r(0x340C + 16 * s)};
        dsp_inst_t in;
        dsp_decode(w, &in);

        for (int q = 0; q < mem_qn;) { /* reads landing at this step */
            if ((uint64_t)mem_q[q].land == gstep) {
                memval = mem_q[q].val;
                mem_q[q] = mem_q[--mem_qn];
            } else {
                q++;
            }
        }

        int32_t INPUTS;
        if (in.IRA <= 0x1F) INPUTS = MEMS[in.IRA];
        else if (in.IRA <= 0x2F) INPUTS = MIXS[in.IRA - 0x20] << 4;
        else if (in.IRA <= 0x31) INPUTS = EXTS[in.IRA - 0x30] << 8;
        else INPUTS = 0;
        INPUTS = sext(INPUTS, 24);

        if (in.IWT) MEMS[in.IWA] = nofl_pipe[1] ? sext((int32_t)memval << 8, 24) : dsp_unpack(memval);

        int32_t SHIFTED;
        switch (in.SHIFT) {
        case 0: SHIFTED = clampi(ACC, -0x800000, 0x7FFFFF); break;
        case 1: SHIFTED = clampi(ACC * 2, -0x800000, 0x7FFFFF); break;
        case 2: SHIFTED = sext(ACC * 2, 24); break;
        default: SHIFTED = sext(ACC, 24); break;
        }

        int32_t tempr = TEMP[(in.TRA + MDEC_CT) & 0x7F];
        int32_t B = in.ZERO ? 0 : in.BSEL ? ACC : tempr;
        if (in.NEGB) B = -B;
        int32_t X = in.XSEL ? INPUTS : tempr;
        int32_t Y;
        switch (in.YSEL) {
        case 0: Y = sext(FRC_REG, 13); break;
        case 1: Y = sext(r(0x3000 + 4 * s) >> 3, 13); break;
        case 2: Y = sext(Y_REG >> 11, 13); break;
        default: Y = (Y_REG >> 4) & 0xFFF; break;
        }
        if (in.YRL) Y_REG = INPUTS;
        ACC = sext((int32_t)(((int64_t)X * Y) >> 12) + B, 26);

        if (in.TWT) TEMP[(in.TWA + MDEC_CT) & 0x7F] = SHIFTED;
        if (in.FRCL) FRC_REG = in.SHIFT == 3 ? (SHIFTED & 0xFFF) : ((SHIFTED >> 11) & 0x1FFF);

        /* Memory (tests/dsp_mem, dsp_mem2): MWT writes at its own step, with its own SHIFTED/NOFL/address, even
         * or odd.  Reads use the odd-step memory slots: an odd-step MRD's word reaches IWT two steps later; an
         * even-step MRD waits for the next odd slot (landing three steps later) and is lost if that odd step reads
         * too.  An MRD in the same instruction as an MWT does not read (IWT then sees the memory read latch, which
         * holds the last word any master read -- tests/dsp_mem3).  ADRS_REG is 12 bits, sign-extended into the
         * 16-bit address sum. */
        const dsp_inst_t *rq = 0;
        if (in.MRD && !in.MWT && (s & 1)) rq = &in;
        else if (!in.MRD && (s & 1) && mrd_even_masa >= 0) rq = &mrd_even;
        if (s & 1) mrd_even_masa = -1;
        if (in.MRD && !in.MWT && !(s & 1)) { mrd_even = in; mrd_even_masa = in.MASA; }
        for (int pass = 0; pass < 2; pass++) {
            const dsp_inst_t *m = pass == 0 ? (in.MWT ? &in : 0) : rq;
            if (!m) continue;
            uint32_t ADDR = r(0x3200 + 4 * m->MASA);
            if (m->ADREB) ADDR += (uint32_t)sext((int32_t)ADRS_REG, 12);
            if (m->NXADR) ADDR++;
            if (!m->TABLE) ADDR = (ADDR + MDEC_CT) & RBL;
            else ADDR &= 0xFFFF;
            uint32_t ba = ((ADDR << 1) + RBP) & (RAM_SIZE - 1);
            if (pass == 0) {
                uint16_t v = in.NOFL ? (uint16_t)(SHIFTED >> 8) : dsp_pack(SHIFTED);
                ram[ba] = (uint8_t)v;
                ram[ba + 1] = (uint8_t)(v >> 8);
            } else if (mem_qn < 4) {
                mem_q[mem_qn].land = (int64_t)(gstep + 2);
                mem_q[mem_qn].val = (uint16_t)(ram[ba] | (ram[ba + 1] << 8));
                mem_qn++;
            }
        }
        if (in.ADRL) ADRS_REG = in.SHIFT == 3 ? ((SHIFTED >> 12) & 0xFFF) : ((uint32_t)(INPUTS >> 16) & 0xFFF);
        if (in.EWT) EFREG[in.EWA] = (int16_t)(SHIFTED >> 8);

        nofl_pipe[1] = nofl_pipe[0];
        nofl_pipe[0] = (uint8_t)in.NOFL;
    }
    if (--MDEC_CT == 0) MDEC_CT = RBL + 1;
}

/* ------------------------------------------------------------------------------------------------------------------
 * One sample
 * ---------------------------------------------------------------------------------------------------------------- */
/* DAC-side mixing (EFSDL/EFPAN for EFREG and EXTS, MVOL, MONO, DAC18B): not observable digitally, so unverified.
 * Levels use the measured send-level law (3 dB steps, send_level); pan attenuates the other side the same way. */
static void volpan(int32_t value, uint32_t vol, uint32_t pan, int32_t &outl, int32_t &outr) {
    int32_t temp = send_level(value, vol);
    int32_t Sc = (pan & 0xF) == 0xF ? 0 : send_level(temp, 15 - (pan & 0xF));
    if (pan & 0x10) { outl += temp; outr += Sc; }
    else { outl += Sc; outr += temp; }
}

int AicaModel::EG_PHASE = 0;

void AicaModel::step() {
    int32_t mixl = 0, mixr = 0;
    if ((samples & 1) == (uint64_t)EG_PHASE) {
        eg_cnt++;
        for (int ch = 0; ch < 64; ch++) {
            if (slot[ch].key_pending) {
                int k = slot[ch].key_pending;
                slot[ch].key_pending = 0;
                if (k > 0 && slot[ch].AEG.state == EG_RELEASE) { key_on(ch); continue; }
                if (k < 0) key_off(ch);
            }
            aeg_clock(ch);
            feg_clock(ch);
        }
    }
    memset(MIXS, 0, sizeof MIXS);
    for (int ch = 0; ch < 64; ch++) {
        int32_t l, rr, d;
        slot_output(ch, l, rr, d);
        mixl += l;
        mixr += rr;
        MIXS[chr(ch, 0x20) & 0xF] += d; /* MIXS is 20-bit (sample scale x16), DSP INPUTS = MIXS << 4 */
    }
    for (int i = 0; i < 16; i++) MIXS[i] = sext(MIXS[i], 20); /* the 20-bit sum wraps, no saturation (tests/sgc_mix) */
    for (int i = 0; i < 2; i++) {
        uint16_t v = r(0x2040 + 4 * i);
        volpan(EXTS[i], (v >> 8) & 0xF, v & 0x1F, mixl, mixr);
    }
    dsp_step();
    for (int i = 0; i < 16; i++) {
        uint16_t v = r(0x2000 + 4 * i);
        volpan(EFREG[i], (v >> 8) & 0xF, v & 0x1F, mixl, mixr);
    }
    uint16_t c0 = r(0x2800);
    if (c0 & 0x8000) { mixl += mixr; mixr = mixl; }
    mixl = send_level(mixl, c0 & 0xF);
    mixr = send_level(mixr, c0 & 0xF);
    if (c0 & 0x100) { mixl >>= 2; mixr >>= 2; }
    outL = (int16_t)clampi(mixl, -32768, 32767);
    outR = (int16_t)clampi(mixr, -32768, 32767);
    samples++;
}

/* ------------------------------------------------------------------------------------------------------------------
 * Register interface
 * ---------------------------------------------------------------------------------------------------------------- */
void AicaModel::write(uint32_t off, uint32_t val) {
    off &= 0x7FFC;
    uint16_t v = (uint16_t)val;
    if (off < 0x2000) {
        int ch = off >> 7;
        uint32_t o = off & 0x7F;
        uint16_t m = chan_mask(o);
        reg[off >> 2] = (uint16_t)((v & m) | (o == 0 ? (v & 0x8000) : 0));
        slot_regwrite(ch, o);
        return;
    }
    if (off >= 0x3000 && off < 0x3200) { reg[off >> 2] = v & 0xFFF8; return; }
    if (off >= 0x4000 && off < 0x4400) {
        int i = (off - 0x4000) >> 3;
        if (off & 4) TEMP[i] = sext((int32_t)((v << 8) | (TEMP[i] & 0xFF)), 24);
        else TEMP[i] = (TEMP[i] & ~0xFF) | (v & 0xFF);
        return;
    }
    if (off >= 0x4400 && off < 0x4500) {
        int i = (off - 0x4400) >> 3;
        if (off & 4) MEMS[i] = sext((int32_t)((v << 8) | (MEMS[i] & 0xFF)), 24); /* low byte not CPU-writable */
        return;
    }
    if (off >= 0x4500 && off < 0x4580) {
        int i = (off - 0x4500) >> 3;
        if (off & 4) MIXS[i] = sext((int32_t)((v << 4) | (MIXS[i] & 0xF)), 20);
        else MIXS[i] = (MIXS[i] & ~0xF) | (v & 0xF);
        return;
    }
    if (off >= 0x4580 && off < 0x45C0) { EFREG[(off - 0x4580) >> 2] = (int16_t)v; return; }
    if (off >= 0x45C0) return;
    reg[off >> 2] = v;
}

uint32_t AicaModel::ram_read32(uint32_t off) {
    off &= RAM_SIZE - 4;
    uint32_t v = ram[off] | (ram[off + 1] << 8) | (ram[off + 2] << 16) | ((uint32_t)ram[off + 3] << 24);
    memval = (uint16_t)(v >> 16); /* 16-bit memory bus: the upper half is read last and stays in the read latch */
    return v;
}

uint32_t AicaModel::read(uint32_t off) {
    off &= 0x7FFC;
    if (off < 0x2000) return reg[off >> 2] & chan_mask(off & 0x7F);
    switch (off) {
    case 0x2800: return 0x0010; /* VER = 1; MVOL/DAC18B/MEM8MB/MONO write-only */
    case 0x2804: return 0;
    case 0x2808: return 0x0900;
    case 0x2810: {
        int ch = (r(0x280C) >> 8) & 0x3F;
        Slot &c = slot[ch];
        uint32_t v;
        if ((r(0x280C) >> 14) & 1) /* AFSEL: filter envelope */
            v = ((c.looped ? 1u : 0u) << 15) | ((uint32_t)c.FEG.state << 13) | c.FEG.v;
        else
            v = ((c.looped ? 1u : 0u) << 15) | ((uint32_t)c.AEG.state << 13) | (c.AEG.off ? 0x1FFFu : c.AEG.a);
        c.looped = false;
        return v;
    }
    case 0x2814: return slot[(r(0x280C) >> 8) & 0x3F].CA & 0xFFFF;
    case 0x2890: case 0x2894: case 0x2898: return 0;
    }
    if (off >= 0x4000 && off < 0x4400) {
        int i = (off - 0x4000) >> 3;
        return (off & 4) ? ((TEMP[i] >> 8) & 0xFFFF) : (TEMP[i] & 0xFF);
    }
    if (off >= 0x4400 && off < 0x4500) {
        int i = (off - 0x4400) >> 3;
        return (off & 4) ? ((MEMS[i] >> 8) & 0xFFFF) : (MEMS[i] & 0xFF);
    }
    if (off >= 0x4500 && off < 0x4580) {
        int i = (off - 0x4500) >> 3;
        return (off & 4) ? ((MIXS[i] >> 4) & 0xFFFF) : (MIXS[i] & 0xF);
    }
    if (off >= 0x4580 && off < 0x45C0) return EFREG[(off - 0x4580) >> 2] & 0xFFFF;
    if (off >= 0x45C0 && off < 0x45C8) return 0;
    return reg[off >> 2];
}

} // namespace caique
