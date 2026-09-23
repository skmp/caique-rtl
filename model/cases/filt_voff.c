/* filt_voff.c -- slot filter with VOFF = 0: where the level (TL/AEG) multiply sits relative to the filter, and at
 * what precision.  Pitch 1.0, signed random PCM16 loop after a 512-sample quiet lead-in (instant attack settles the
 * AEG at 0 during it).  Streams 0, 1: filter on, VOFF = 0 at two TL values; stream 2: the same filter with VOFF = 1
 * (low itself); stream 3: unfiltered reference (LPOFF = 1, VOFF = 1) = the input.
 * Output: filt_voff.txt, fv_<batch>.hdr/.bin */
#include "cap.h"
#define NS 4
#define MAXV (1u << 20)
#define SA_SIG 0x20000u
#define NSIG 4096
static int32_t capbuf[MAXV];
static int16_t sig[NSIG];
static const struct { uint16_t F; uint8_t Q; uint8_t tl[2]; } batch[] = {
    {0x1C00, 4, {0x00, 0x13}},
    {0x1E00, 4, {0x2A, 0x55}},
    {0x1C00, 31, {0x07, 0x40}},
    {0x1A00, 16, {0x01, 0xA3}},
};
int test_main(void) {
    out_open("filt_voff.txt");
    uint32_t seed = 777;
    for (int i = 0; i < NSIG; i++) { seed = seed * 1103515245u + 12345u; sig[i] = (int16_t)((int16_t)(seed >> 16) >> 2); }
    for (int i = 0; i < 512; i++) sig[i] = 0;
    ram_write(SA_SIG, sig, sizeof sig);
    out_bin("input.bin", sig, sizeof sig);
    for (unsigned b = 0; b < sizeof batch / sizeof batch[0]; b++) {
        aica_quiet();
        for (int k = 0; k < NS; k++) {
            slot_cfg_t c;
            slot_cfg_default(&c, SA_SIG, NSIG);
            c.LSA = 512; c.LPCTL = 1; c.ISEL = k; c.Q = batch[b].Q;
            c.VOFF = k >= 2; c.LPOFF = k == 3; c.TL = k < 2 ? batch[b].tl[k] : 0;
            for (int j = 0; j < 5; j++) c.FLV[j] = batch[b].F;
            c.FAR = c.FD1R = c.FD2R = c.FRR = 0;
            slot_write(k, &c);
            LOG("fv_%u stream %d: F %04x Q %d TL %02x VOFF %d LPOFF %d\n", b, k, c.FLV[0], c.Q, c.TL, c.VOFF, c.LPOFF);
        }
        static const int mixs[NS] = {0, 1, 2, 3};
        if (cap_start(NS, mixs, capbuf, MAXV)) { OUT("cap_start failed\n"); return 1; }
        cap_wait_us(3000);
        for (int k = 0; k < NS; k++) aw(CH(k, 0x00), (ar(CH(k, 0x00)) & 0x3FFF) | 0x4000);
        aw(CH(0, 0x00), (ar(CH(0, 0x00)) & 0x7FFF) | 0x8000);
        cap_mark(1);
        cap_wait_us(150000);
        uint32_t n = cap_stop();
        char nm[32];
        snprintf(nm, sizeof nm, "fv_%u", b);
        cap_save(nm, n);
        OUT("%s: %lu samples, errors %lu\n", nm, (unsigned long)n, (unsigned long)CAP.errors);
    }
    aica_quiet();
    out_close();
    return 0;
}
