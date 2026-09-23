/* filt_reset.c -- what clears the slot filter state?  Slot at FLV 0x1400 (slow), Q 0, input DC 0x2000 for 150 ms
 * so the filter state is large, then the input switches to silence (SA change) and the slow decay is captured while
 * the CPU tries one action per stream: 0 nothing, 1 LPOFF=1 for 5 ms, 2 key-off + key-on, 3 FLV to 0x1FF8 for 5 ms
 * then back.  Output: filt_reset.txt, fr.hdr/.bin */
#include "cap.h"
#define NS 4
#define MAXV (2u << 20)
static int32_t capbuf[MAXV];

int test_main(void) {
    out_open("filt_reset.txt");
    aica_quiet();
    for (int i = 0; i < 512; i++) ram_w32(0x20000 + 4 * i, 0x20002000);
    ram_fill(0x40000, 0, 0x1000);
    for (int k = 0; k < NS; k++) {
        slot_cfg_t c;
        slot_cfg_default(&c, 0x20000, 256);
        c.ISEL = k; c.AR = 31; c.KRS = 1; c.RR = 31; c.LPOFF = 0; c.Q = 0; c.VOFF = 1;
        for (int i = 0; i < 5; i++) c.FLV[i] = 0x1400;
        c.FAR = c.FD1R = c.FD2R = c.FRR = 0;
        slot_write(k, &c);
    }
    static const int mixs[NS] = {0, 1, 2, 3};
    if (cap_start(NS, mixs, capbuf, MAXV)) { OUT("cap_start failed\n"); return 1; }
    for (int k = 0; k < NS; k++) aw(CH(k, 0x00), (ar(CH(k, 0x00)) & 0x3FFF) | 0x4000);
    aw(CH(0, 0x00), (ar(CH(0, 0x00)) & 0x7FFF) | 0x8000);
    cap_mark(1);
    cap_wait_us(150000);
    for (int k = 0; k < NS; k++) aw(CH(k, 0x00), (ar(CH(k, 0x00)) & 0x4000) | 0x0004 | (1 << 9)); /* SA -> 0x40000, loop */
    cap_mark(2);
    cap_wait_us(50000);
    cap_mark(3);
    aw(CH(1, 0x28), (1 << 6) | (1 << 5));                       /* LPOFF on */
    aw(CH(2, 0x00), ar(CH(2, 0x00)) & 0x3FFF);                  /* key off */
    aw(CH(0, 0x00), (ar(CH(0, 0x00)) & 0x3FFF) | 0x8000);
    for (int i = 0; i < 5; i++) aw(CH(3, 0x2C + 4 * i), 0x1FF8);
    cap_wait_us(5000);
    aw(CH(1, 0x28), (1 << 6));                                  /* LPOFF off */
    aw(CH(2, 0x00), (ar(CH(2, 0x00)) & 0x3FFF) | 0x4000);       /* key on again */
    aw(CH(0, 0x00), (ar(CH(0, 0x00)) & 0x7FFF) | 0x8000);
    for (int i = 0; i < 5; i++) aw(CH(3, 0x2C + 4 * i), 0x1400);
    cap_mark(4);
    cap_wait_us(100000);
    uint32_t n = cap_stop();
    cap_save("fr", n);
    OUT("fr: %lu samples, errors %lu\n", (unsigned long)n, (unsigned long)CAP.errors);
    aica_quiet();
    out_close();
    return 0;
}
