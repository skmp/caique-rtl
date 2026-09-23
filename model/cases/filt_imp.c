/* filt_imp.c -- the filter's first-sample response to isolated impulses: one-shot PCM16 with impulses of amplitude
 * A_i at 64 + 128 i (511 per key-on, zeros between; Q 0 decays to 0 within 128 samples at these cutoffs), 4 key-ons
 * covering A = +-1..+-256, powers of two +-1, and pseudo-random amplitudes.  Slots: F 0x1C00 (f = 1/4, control),
 * 0x1D55, 0x1E80, 0x1F55; zero initial state (settle at 0x1E00).  VOFF=1.
 * Output: filt_imp.txt, amps_<r>.bin (int16 A_i), fm_<r>.hdr/.bin captures */
#include "cap.h"

#define NS 4
#define NIMP 511
#define MAXV (2u << 20)
static int32_t capbuf[MAXV];
static int16_t sig[64 + 128 * NIMP + 64];
static int16_t amps[4][NIMP];
static const uint16_t Fv[NS] = {0x1C00, 0x1D55, 0x1E80, 0x1F55};

int test_main(void) {
    out_open("filt_imp.txt");
    /* amplitude sets */
    int na = 0;
    static int16_t all[4 * NIMP];
    for (int a = 1; a <= 256; a++) { all[na++] = (int16_t)a; all[na++] = (int16_t)-a; }
    for (int k = 9; k < 15; k++) for (int d = -1; d <= 1; d++) { all[na++] = (int16_t)((1 << k) + d); all[na++] = (int16_t)-((1 << k) + d); }
    all[na++] = 32767; all[na++] = -32768;
    uint32_t x = 4242;
    while (na < 4 * NIMP) { x = x * 1103515245u + 12345u; all[na++] = (int16_t)(x >> 16); }
    for (int r = 0; r < 4; r++) {
        for (int i = 0; i < NIMP; i++) amps[r][i] = all[r * NIMP + i];
        amps[r][0] = 16384; /* marker for alignment */
    }
    for (int r = 0; r < 4; r++) {
        memset(sig, 0, sizeof sig);
        for (int i = 0; i < NIMP; i++) sig[64 + 128 * i] = amps[r][i];
        char nm[32];
        snprintf(nm, sizeof nm, "amps_%d.bin", r);
        out_bin(nm, amps[r], sizeof amps[r]);
        aica_quiet();
        ram_fill(0x40000, 0, 0x1000);
        ram_write(0x100000 - 0x100000 + 0x20000, sig, sizeof sig); /* sample at 0x20000 */
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
        cap_wait_us(50000);
        for (int k = 0; k < NS; k++) aw(CH(k, 0x00), ar(CH(k, 0x00)) & 0x3FFF);
        aw(CH(0, 0x00), (ar(CH(0, 0x00)) & 0x3FFF) | 0x8000);
        cap_wait_us(2000);
        for (int k = 0; k < NS; k++) {
            aw(CH(k, 0x04), 0x0000);
            aw(CH(k, 0x08), 0);
            aw(CH(k, 0x0C), (uint16_t)(sizeof sig / 2 - 1));
            aw(CH(k, 0x28), (1 << 6) | 0);
            for (int i = 0; i < 5; i++) aw(CH(k, 0x2C + 4 * i), Fv[k]);
            aw(CH(k, 0x00), (ar(CH(k, 0x00)) & 0x4000) | 0x0002); /* PCM16 one-shot, SA 0x20000 */
        }
        for (int k = 0; k < NS; k++) aw(CH(k, 0x00), (ar(CH(k, 0x00)) & 0x3FFF) | 0x4000);
        aw(CH(0, 0x00), (ar(CH(0, 0x00)) & 0x7FFF) | 0x8000);
        cap_mark(2);
        cap_wait_us(1510000);
        uint32_t n = cap_stop();
        snprintf(nm, sizeof nm, "fm_%d", r);
        cap_save(nm, n);
        OUT("%s: %lu samples, errors %lu\n", nm, (unsigned long)n, (unsigned long)CAP.errors);
    }
    aica_quiet();
    out_close();
    return 0;
}
