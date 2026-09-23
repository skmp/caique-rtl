/* filt_cyc.c -- tiny-signal dynamics of the slot filter: each slot is keyed on at (F, Q) playing a short burst of
 * input (DC or noise) followed by silence (one-shot [256 x A, then zeros]), and the zero-input tail is captured long
 * (limit cycles / stuck states show the rounding of tiny products).  Several amplitudes per (F, Q).  VOFF=1.
 * Output: filt_cyc.txt (stream -> F, Q, A), fc_<n>.hdr/.bin */
#include "cap.h"
#define NS 4
#define MAXV (1u << 20)
static int32_t capbuf[MAXV];
static int16_t sig[4096];
static const struct { uint16_t F; int Q; int16_t A; } cf[] = {
    {0x1FFE, 4, 1000}, {0x1FFE, 4, -1000}, {0x1FFE, 4, 7}, {0x1FFE, 0, 1000},
    {0x1FF0, 4, 1000}, {0x1FF0, 4, -1000}, {0x1FF0, 0, 1000}, {0x1FFC, 4, 1000},
    {0x1E00, 4, 1000}, {0x1E00, 4, -1000}, {0x1E00, 0, 1000}, {0x1E00, 0, -1000},
    {0x1C00, 4, 1000}, {0x1C00, 4, -1000}, {0x1C00, 0, 1000}, {0x1C00, 0, -1000},
    {0x1A00, 4, 1000}, {0x1A00, 4, -1000}, {0x1800, 4, 1000}, {0x1800, 4, -1000},
    {0x1F55, 4, 1000}, {0x1F55, 4, -1000}, {0x1D55, 4, 1000}, {0x1D55, 4, -1000},
};
int test_main(void) {
    out_open("filt_cyc.txt");
    int nc = sizeof cf / sizeof cf[0];
    for (int base = 0; base < nc; base += NS) {
        aica_quiet();
        for (int k = 0; k < NS; k++) {
            memset(sig, 0, sizeof sig);
            for (int i = 0; i < 256; i++) sig[i] = cf[base + k].A;
            ram_write(0x20000 + 0x2000 * k, sig, sizeof sig);
            slot_cfg_t c;
            slot_cfg_default(&c, 0x20000 + 0x2000 * k, 4095);
            c.LPCTL = 0; c.ISEL = k; c.VOFF = 1; c.LPOFF = 0; c.Q = cf[base + k].Q;
            for (int i = 0; i < 5; i++) c.FLV[i] = cf[base + k].F;
            c.FAR = c.FD1R = c.FD2R = c.FRR = 0;
            slot_write(k, &c);
            LOG("fc_%d stream %d: F %04x Q %d A %d\n", base / NS, k, cf[base + k].F, cf[base + k].Q, cf[base + k].A);
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
        snprintf(nm, sizeof nm, "fc_%d", base / NS);
        cap_save(nm, n);
        OUT("%s: %lu samples, errors %lu\n", nm, (unsigned long)n, (unsigned long)CAP.errors);
    }
    aica_quiet();
    out_close();
    return 0;
}
