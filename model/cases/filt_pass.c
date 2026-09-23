/* filt_pass.c -- does FLV 0x1FFE (f = 1.0 by the least-squares fits) with Q 4 (q = 1) make the filter a pure
 * passthrough (b0 = 1, a1 = a2 = 0 -> y = x, output inverted)?  If so it resets the filter state exactly, whatever the
 * rounding.  Slots: F 0x1FFE Q 4, F 0x1FFF Q 4, F 0x1FFC Q 4, F 0x1FF0 Q 4; input: looped pseudo-random PCM16,
 * VOFF=1, pitch 1.0.  Output: filt_pass.txt, fp.hdr/.bin, input.bin */
#include "cap.h"
#define NS 4
#define MAXV (1u << 20)
static int32_t capbuf[MAXV];
static int16_t sig[1024 + 64];
int test_main(void) {
    out_open("filt_pass.txt");
    uint32_t x = 17;
    for (int i = 0; i < 1024; i++) { x = x * 1103515245u + 12345u; sig[i] = (int16_t)(x >> 16); }
    for (int i = 0; i < 64; i++) sig[1024 + i] = sig[i];
    out_bin("input.bin", sig, sizeof sig);
    static const uint16_t Fv[NS] = {0x1FFE, 0x1FFF, 0x1FFC, 0x1FF0};
    aica_quiet();
    ram_write(0x20000, sig, sizeof sig);
    for (int k = 0; k < NS; k++) {
        slot_cfg_t c;
        slot_cfg_default(&c, 0x20000, 1024);
        c.ISEL = k; c.VOFF = 1; c.LPOFF = 0; c.Q = 4;
        for (int i = 0; i < 5; i++) c.FLV[i] = Fv[k];
        c.FAR = c.FD1R = c.FD2R = c.FRR = 0;
        slot_write(k, &c);
    }
    static const int mixs[NS] = {0, 1, 2, 3};
    if (cap_start(NS, mixs, capbuf, MAXV)) { OUT("cap_start failed\n"); return 1; }
    cap_wait_us(3000);
    for (int k = 0; k < NS; k++) aw(CH(k, 0x00), (ar(CH(k, 0x00)) & 0x3FFF) | 0x4000);
    aw(CH(0, 0x00), (ar(CH(0, 0x00)) & 0x7FFF) | 0x8000);
    cap_mark(1);
    cap_wait_us(80000);
    uint32_t n = cap_stop();
    cap_save("fp", n);
    OUT("fp: %lu samples, errors %lu\n", (unsigned long)n, (unsigned long)CAP.errors);
    aica_quiet();
    out_close();
    return 0;
}
