/* filt_coef.c -- filter coefficient tables: q for all 32 Q (at FLV 0x1C00 and 0x1E00), f over FLV exponents and
 * mantissas (Q 4, q = 1).  Same method as filt_id2 (settle at 0x1E00, then the test key-on).  Per slot: settle (key-on at FLV 0x1E00 Q 0 --
 * f = 1/2, which decays to exactly 0 -- on a looped silence, 100 ms), key-off, then key-on at the test (F, Q) playing a one-shot [64 zeros, impulse 0x2000, 1023 zeros,
 * 2048 pseudo-random samples in +-0x2000, 64 zeros] at pitch 1.0.  FEG rates 0 so the cutoff stays at FLV0; VOFF=1
 * so MIXS carries the filter output (with its fractional bits).  4 slots per capture.
 * Output: filt_id.txt (stream -> F,Q table), fi_<n>.hdr/.bin, the input sequence in input.bin (int16). */
#include "cap.h"

#define NS 4
#define MAXV (1u << 20)
#define NIN 3200
static int32_t capbuf[MAXV];
static int16_t sig[NIN];

static uint16_t CF[200];
static int CQ[200];
static int build(void) {
    int n = 0;
    for (int q = 0; q < 32; q++) { CF[n] = 0x1C00; CQ[n++] = q; }
    for (int q = 0; q < 32; q++) { CF[n] = 0x1E00; CQ[n++] = q; }
    static const int ms[] = {0x000, 0x001, 0x002, 0x003, 0x055, 0x0AA, 0x0FF, 0x100, 0x155, 0x1FE, 0x1FF};
    for (int e = 11; e <= 15; e++) for (int i = 0; i < 11; i++) { CF[n] = (uint16_t)((e << 9) | ms[i]); CQ[n++] = 4; }
    return n;
}

int test_main(void) {
    out_open("filt_coef.txt");
    memset(sig, 0, sizeof sig);
    sig[64] = 0x2000;
    uint32_t x = 777;
    for (int i = 1088; i < 1088 + 2048; i++) { x = x * 1103515245u + 12345u; sig[i] = (int16_t)((int32_t)((x >> 16) & 0x3FFF) - 0x2000); }
    out_bin("input.bin", sig, sizeof sig);
    int nc = build();
    for (int base = 0; base < nc; base += NS) {
        aica_quiet();
        ram_fill(0x40000, 0, 0x1000); /* silence */
        ram_write(0x10000, sig, sizeof sig);
        /* settle */
        for (int k = 0; k < NS; k++) {
            slot_cfg_t c;
            slot_cfg_default(&c, 0x40000, 1024);
            c.ISEL = k; c.AR = 31; c.KRS = 1; c.RR = 31; c.LPOFF = 0; c.Q = 0; c.VOFF = 1;
            for (int i = 0; i < 5; i++) c.FLV[i] = 0x1E00;
            c.FAR = c.FD1R = c.FD2R = c.FRR = 0;
            slot_write(k, &c);
        }
        for (int k = 0; k < NS; k++) aw(CH(k, 0x00), (ar(CH(k, 0x00)) & 0x3FFF) | 0x4000);
        aw(CH(0, 0x00), (ar(CH(0, 0x00)) & 0x7FFF) | 0x8000);
        static const int mixs[NS] = {0, 1, 2, 3};
        if (cap_start(NS, mixs, capbuf, MAXV)) { OUT("cap_start failed\n"); return 1; }
        cap_wait_us(100000);
        for (int k = 0; k < NS; k++) aw(CH(k, 0x00), ar(CH(k, 0x00)) & 0x3FFF);
        aw(CH(0, 0x00), (ar(CH(0, 0x00)) & 0x3FFF) | 0x8000);
        cap_mark(1);
        cap_wait_us(2000);
        /* test */
        for (int k = 0; k < NS; k++) {
            int ci = base + k;
            if (ci >= nc) break;
            uint16_t F = CF[ci];
            int Q = CQ[ci];
            aw(CH(k, 0x04), 0x0000);
            aw(CH(k, 0x08), 0);
            aw(CH(k, 0x0C), NIN - 1);
            aw(CH(k, 0x28), (1 << 6) | Q); /* VOFF, filter on, Q */
            for (int i = 0; i < 5; i++) aw(CH(k, 0x2C + 4 * i), F);
            aw(CH(k, 0x00), (ar(CH(k, 0x00)) & 0x4000) | 0x0001); /* PCM16 one-shot, SA 0x10000 */
            LOG("fi_%d stream %d: F %04x Q %d\n", base / NS, k, F, Q);
        }
        for (int k = 0; k < NS && base + k < nc; k++) aw(CH(k, 0x00), (ar(CH(k, 0x00)) & 0x3FFF) | 0x4000);
        aw(CH(0, 0x00), (ar(CH(0, 0x00)) & 0x7FFF) | 0x8000);
        cap_mark(2);
        cap_wait_us(95000);
        uint32_t n = cap_stop();
        char nm[32];
        snprintf(nm, sizeof nm, "fi_%d", base / NS);
        cap_save(nm, n);
        OUT("%s: %lu samples, errors %lu\n", nm, (unsigned long)n, (unsigned long)CAP.errors);
    }
    aica_quiet();
    out_close();
    return 0;
}
