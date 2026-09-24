/* cap.h -- sample-exact capture of up to 4 MIXS buses (20 bits each) through a DSP program writing to a wave RAM
 * ring, read back continuously by the CPU.  Shared by the SGC test cases (both platforms).
 *
 * DSP program (RBL=3: 64K-word ring at CAP_RBP_BYTE; region r at MADRS[r] = r * CAP_REGION):
 *   step 0      ACC = TEMP[1+c] - 0x100            (X = MEMS30 = 0x000100, Y = COEF -4096, B = TEMP)
 *   step 1      TWT TEMP[0+c]; MWT NOFL region 0     -> counter word = -(n+1) for sample n, persists via TEMP
 *   stream k, b = 2 + 4k:
 *   b     even  ACC = -INPUTS(MIXS[m]); YRL (Y_REG = MIXS << 4)
 *   b+1   odd   MWT NOFL clamp(-INPUTS)[23:8] -> region 1+2k (hi);  ACC = MEMS31 (-2^23) * Y_REG[15:4]
 *   b+2   even  ACC kept (B = ACC)
 *   b+3   odd   MWT NOFL (-MIXS[11:0] * 2048)[23:8] -> region 2+2k (lo)
 * hi gives MIXS[19:4] (as -MIXS/16 floored), lo gives MIXS[11:0] exactly; together the exact 20-bit value.
 * The ring address of sample n is (region base + c0 - n) mod 64K (MDEC_CT counts down), c0 found at sync from the
 * counter words (the ring is cleared first, so only counter words are non-zero until a channel plays).
 * Words are overwritten CAP_REGION samples after they are written; the reader stays CAP_MARGIN samples behind.
 */
#ifndef CAIQUE_CAP_H
#define CAIQUE_CAP_H
#include "aica_io.h"

#define CAP_RBP_BYTE 0x1E0000u
#define CAP_REGION 7281u /* 65536 / 9 */
#define CAP_MARGIN 96u
#define CAP_MAXEV 64

typedef struct {
    int ns, mixs[4];
    uint32_t c0;              /* ring address of sample 0 (mod 64K) */
    uint32_t n_first, n_next; /* first captured sample, next sample to read */
    uint64_t t_head;          /* time of the head measurement */
    uint32_t n_head;
    int32_t *buf;
    uint32_t max;
    uint32_t errors, first_err_n;
    uint32_t nev;
    struct { uint32_t id, n; } ev[CAP_MAXEV];
} cap_t;
static cap_t CAP;

static inline uint16_t cap_word(uint32_t a) { return ram_r16(CAP_RBP_BYTE + 2 * (a & 0xFFFF)); }
static inline uint32_t cap_addr(int region, uint32_t n) { return (region * CAP_REGION + CAP.c0 - n) & 0xFFFF; }
static inline int cap_cnt_ok(uint32_t n) { return cap_word(cap_addr(0, n)) == ((uint16_t)(-(int32_t)(n + 1))); }

/* read words for samples n..n+len-1 of one region into w[0..len-1] (addresses descend with n) */
static inline void cap_read_region(int region, uint32_t n, uint32_t len, uint16_t *w) {
    uint32_t a_hi = cap_addr(region, n), a_lo = (a_hi - (len - 1)) & 0xFFFF;
    for (uint32_t i = 0; i < len;) {
        uint32_t a = (a_lo + i) & 0xFFFF;
        if ((a & 1) == 0 && i + 1 < len && a != 0xFFFF) {
            uint32_t v = ram_r32(CAP_RBP_BYTE + 2 * a);
            w[len - 1 - i] = (uint16_t)v;
            w[len - 2 - i] = (uint16_t)(v >> 16);
            i += 2;
        } else {
            w[len - 1 - i] = cap_word(a);
            i++;
        }
    }
}

static inline int32_t cap_decode(uint16_t hi, uint16_t lo) {
    int32_t Y = (int32_t)(((uint32_t)(0x10000 - lo) & 0xFFFF) >> 3); /* MIXS[11:0] */
    int32_t top = -(int32_t)(int16_t)hi * 16;                        /* MIXS in [top-15, top] */
    int32_t m = (top - 15) + (int32_t)((uint32_t)(Y - (top - 15)) & 0xFFF);
    if (m > top) m = -0x80000; /* only the clamped -2^19 case lands here */
    return m;
}

static inline uint32_t cap_head_now(void) {
    return CAP.n_head + (uint32_t)(((now_us() - CAP.t_head) * 441) / 10000);
}

static inline void cap_poll(void) {
    if (!CAP.buf) return;
    uint64_t t = now_us();
    uint32_t head = CAP.n_head + (uint32_t)(((t - CAP.t_head) * 441) / 10000);
    while (CAP.n_next + 64 + CAP_MARGIN <= head) {
        uint32_t n = CAP.n_next, len = 64;
        if (!cap_cnt_ok(n + len)) { /* the estimate ran ahead of the DSP (timer glitch): resync and wait */
            CAP.n_head = n + len - CAP_MARGIN;
            CAP.t_head = now_us();
            break;
        }
        uint16_t cw[64], hw[64], lw[64];
        cap_read_region(0, n, len, cw);
        for (int k = 0; k < CAP.ns; k++) {
            cap_read_region(1 + 2 * k, n, len, hw);
            cap_read_region(2 + 2 * k, n, len, lw);
            for (uint32_t i = 0; i < len; i++) {
                uint32_t idx = n + i - CAP.n_first;
                if (idx < CAP.max / CAP.ns) CAP.buf[idx * CAP.ns + k] = cap_decode(hw[i], lw[i]);
            }
        }
        for (uint32_t i = 0; i < len; i++)
            if (cw[i] != (uint16_t)(-(int32_t)(n + i + 1))) {
                if (!CAP.errors) CAP.first_err_n = n + i;
                CAP.errors++;
            }
        CAP.n_next += len;
        if ((CAP.n_next - CAP.n_first) * CAP.ns >= CAP.max) break;
    }
}

/* optional hook run on every wait-loop iteration (e.g. EG monitor sampling) */
static void (*CAP_HOOK)(void);
static inline void cap_wait_us(uint32_t us) {
    uint64_t t_end = now_us() + us;
    while (now_us() < t_end) { cap_poll(); if (CAP_HOOK) CAP_HOOK(); }
}

static inline void cap_mark(uint32_t id) {
    if (CAP.nev < CAP_MAXEV) { CAP.ev[CAP.nev].id = id; CAP.ev[CAP.nev].n = cap_head_now(); CAP.nev++; }
}

/* start: clear the ring, load the program, find c0 and the head.  Returns 0 on success. */
static inline int cap_start(int nstreams, const int *mixs, int32_t *buf, uint32_t max_values) {
    memset(&CAP, 0, sizeof CAP);
    CAP.ns = nstreams;
    for (int k = 0; k < nstreams; k++) CAP.mixs[k] = mixs[k];
    dsp_reset(CAP_RBP_BYTE, 3);   /* known DSP state: every step a NOP, the ring and every buffer cleared (TODO 2.1) */
    for (int r = 0; r < 9; r++) dsp_madrs(r, (uint16_t)(r * CAP_REGION));
    aw(R_MEMS(30, 1), 0x0001);
    aw(R_MEMS(31, 1), 0x8000);
    prog_reset();
    dsp_coef(0, -4096); dsp_coef(1, 0);
    P[0].XSEL = 1; P[0].IRA = 30; P[0].YSEL = 1; P[0].TRA = 1;
    P[1].SHIFT = 3; P[1].TWT = 1; P[1].TWA = 0; P[1].MWT = 1; P[1].NOFL = 1; P[1].MASA = 0; P[1].ZERO = 1; P[1].YSEL = 1;
    for (int k = 0; k < nstreams; k++) {
        int b = 2 + 4 * k;
        dsp_coef(b, -4096); dsp_coef(b + 1, 0); dsp_coef(b + 2, 0); dsp_coef(b + 3, 0);
        P[b].XSEL = 1; P[b].IRA = IRA_MIXS(mixs[k]); P[b].YSEL = 1; P[b].ZERO = 1; P[b].YRL = 1;
        P[b + 1].SHIFT = 0; P[b + 1].MWT = 1; P[b + 1].NOFL = 1; P[b + 1].MASA = 1 + 2 * k;
        P[b + 1].XSEL = 1; P[b + 1].IRA = 31; P[b + 1].YSEL = 3; P[b + 1].ZERO = 1;
        P[b + 2].YSEL = 1; P[b + 2].BSEL = 1;
        P[b + 3].SHIFT = 3; P[b + 3].MWT = 1; P[b + 3].NOFL = 1; P[b + 3].MASA = 2 + 2 * k; P[b + 3].ZERO = 1; P[b + 3].YSEL = 1;
    }
    PN = 2 + 4 * nstreams;
    prog_load();
    io_wait_us(12000);
    /* sync: a non-zero word followed by 4 words counting up by one (older samples sit at higher addresses) is a
     * counter word; data words can be non-zero already (VOFF slots play without a key-on) */
    uint32_t a = 0xFFFFFFFF;
    uint16_t v = 0;
    for (uint32_t s = 0; s < 65536 && a == 0xFFFFFFFF; s += 128) {
        for (uint32_t d = 0; d < 128; d += 32) {
            v = cap_word(s + d);
            if (!v) continue;
            int ok = 1;
            for (uint32_t i = 1; i <= 4 && ok; i++) ok = cap_word(s + d + i) == (uint16_t)(v + i);
            if (ok) { a = s + d; break; }
        }
    }
    if (a == 0xFFFFFFFF) return -1;
    uint32_t n = (uint16_t)(-(int32_t)v - 1);
    CAP.c0 = (a + n) & 0xFFFF;
    /* head: the latest n with a valid counter word (exponential then binary search) */
    uint32_t lo = n, step = 64;
    while (cap_cnt_ok(lo + step)) { lo += step; step *= 2; }
    uint32_t hi = lo + step;
    while (hi - lo > 1) { uint32_t mid = (lo + hi) / 2; if (cap_cnt_ok(mid)) lo = mid; else hi = mid; }
    CAP.n_head = lo;
    CAP.t_head = now_us();
    LOG("cap_start: counter word %04x at %04lx (n %lu), next %04x %04x %04x, c0 %04lx, head n %lu\n", v,
        (unsigned long)a, (unsigned long)n, cap_word(a + 1), cap_word(a + 2), cap_word(a + 3), (unsigned long)CAP.c0,
        (unsigned long)lo);
    CAP.n_first = CAP.n_next = lo + 1;
    CAP.buf = buf;
    CAP.max = max_values;
    return 0;
}

/* stop: read up to the head, unload the program.  Returns the number of samples captured. */
static inline uint32_t cap_stop(void) {
    cap_wait_us((CAP_MARGIN + 128) * 23);
    cap_poll();
    prog_reset(); prog_load();
    uint32_t n = CAP.n_next - CAP.n_first;
    if (n * CAP.ns > CAP.max) n = CAP.max / CAP.ns;
    return n;
}

/* <name>.hdr: "CAP1", ns, nsamples, n_first, errors, first_err_n, nev, mixs[4], ev[nev]{id,n} (u32 LE)
 * <name>.bin: int32 data[nsamples][ns] (MIXS values) */
static inline int cap_save(const char *name, uint32_t nsamples) {
    static uint32_t hdr[16 + 2 * CAP_MAXEV];
    /* the ring alignment and SH4 time of this capture, for the replay tools (tools/stream_replay) */
    LOG("capture %s c0 %04lx first %lu samples %lu head n %lu t_head_us %lu%06lu\n", name, (unsigned long)CAP.c0,
        (unsigned long)CAP.n_first, (unsigned long)nsamples, (unsigned long)CAP.n_head,
        (unsigned long)(CAP.t_head / 1000000u), (unsigned long)(CAP.t_head % 1000000u));
    uint32_t h = 0;
    hdr[h++] = 0x31504143; hdr[h++] = CAP.ns; hdr[h++] = nsamples; hdr[h++] = CAP.n_first; hdr[h++] = CAP.errors;
    hdr[h++] = CAP.first_err_n; hdr[h++] = CAP.nev;
    for (int k = 0; k < 4; k++) hdr[h++] = k < CAP.ns ? (uint32_t)CAP.mixs[k] : 0;
    for (uint32_t e = 0; e < CAP.nev; e++) { hdr[h++] = CAP.ev[e].id; hdr[h++] = CAP.ev[e].n; }
    char n1[128];
    snprintf(n1, sizeof n1, "%s.hdr", name);
    io_write_file(n1, hdr, h * 4);
    snprintf(n1, sizeof n1, "%s.bin", name);
    return io_write_file(n1, CAP.buf, nsamples * CAP.ns * 4);
}
#endif
