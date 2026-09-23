/* filt_top.c -- the FLV 0x1FFE / Q 4 near-passthrough: its error cycles with period 3 independent of the input.
 * Slots (VOFF=1): 0: 0x1FFE zeros; 1: 0x1FFE DC 1000; 2: 0x1FFE zeros after a 0x1400 DC 0x2000 prelude (stuck
 * residue); 3: 0x1FFF random.  The FEG monitor of slot 0 is polled.  Output: filt_top.txt, ft.hdr/.bin */
#include "cap.h"
#define NS 4
#define MAXV (1u << 20)
static int32_t capbuf[MAXV];
static int16_t rnd_sig[1024 + 64];
static uint16_t egm[4096]; static uint32_t negm;
static void hook(void) { uint16_t v = (uint16_t)ar(R_EGMON); if (negm < 4096 && (!negm || egm[negm - 1] != v)) egm[negm++] = v; }
int test_main(void) {
    out_open("filt_top.txt");
    uint32_t x = 3;
    for (int i = 0; i < 1024; i++) { x = x * 1103515245u + 12345u; rnd_sig[i] = (int16_t)(x >> 16); }
    for (int i = 0; i < 64; i++) rnd_sig[1024 + i] = rnd_sig[i];
    aica_quiet();
    ram_fill(0x40000, 0, 0x1000);                                  /* zeros */
    for (int i = 0; i < 512; i++) ram_w32(0x41000 + 4 * i, 0x03E803E8); /* DC 1000 */
    for (int i = 0; i < 512; i++) ram_w32(0x42000 + 4 * i, 0x20002000); /* DC 0x2000 */
    ram_write(0x20000, rnd_sig, sizeof rnd_sig);
    /* prelude for slot 2: 0x1400 on DC 0x2000 then zeros -> stuck state */
    {
        slot_cfg_t c;
        slot_cfg_default(&c, 0x42000, 1024);
        c.ISEL = 2; c.VOFF = 1; c.LPOFF = 0; c.Q = 0;
        for (int i = 0; i < 5; i++) c.FLV[i] = 0x1400;
        c.FAR = c.FD1R = c.FD2R = c.FRR = 0;
        slot_write(2, &c);
        ch_keyon(2);
        io_wait_us(100000);
        aw(CH(2, 0x04), 0x0000); aw(CH(2, 0x00), (ar(CH(2, 0x00)) & 0x4000) | 0x0204); /* SA -> zeros, looped */
        io_wait_us(50000);
        ch_keyoff(2);
        io_wait_us(5000);
    }
    static const struct { uint32_t sa; uint16_t F; } cfg[NS] = {{0x40000, 0x1FFE}, {0x41000, 0x1FFE}, {0x40000, 0x1FFE}, {0x20000, 0x1FFF}};
    for (int k = 0; k < NS; k++) {
        slot_cfg_t c;
        slot_cfg_default(&c, cfg[k].sa, 1024);
        c.ISEL = k; c.VOFF = 1; c.LPOFF = 0; c.Q = 4;
        for (int i = 0; i < 5; i++) c.FLV[i] = cfg[k].F;
        c.FAR = c.FD1R = c.FD2R = c.FRR = 0;
        slot_write(k, &c);
    }
    static const int mixs[NS] = {0, 1, 2, 3};
    if (cap_start(NS, mixs, capbuf, MAXV)) { OUT("cap_start failed\n"); return 1; }
    aw(R_MSLC, (1 << 14) | 0);
    negm = 0; CAP_HOOK = hook;
    cap_wait_us(3000);
    for (int k = 0; k < NS; k++) aw(CH(k, 0x00), (ar(CH(k, 0x00)) & 0x3FFF) | 0x4000);
    aw(CH(0, 0x00), (ar(CH(0, 0x00)) & 0x7FFF) | 0x8000);
    cap_mark(1);
    cap_wait_us(60000);
    CAP_HOOK = 0;
    uint32_t n = cap_stop();
    cap_save("ft", n);
    OUT("ft: %lu samples, errors %lu; FEG monitor values:", (unsigned long)n, (unsigned long)CAP.errors);
    for (uint32_t i = 0; i < negm && i < 20; i++) OUT(" %04x", egm[i]);
    OUT("\n");
    aica_quiet();
    out_close();
    return 0;
}
