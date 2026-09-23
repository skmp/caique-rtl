/* feg_probe.c -- exploratory: FEG monitor (0x280C AFSEL=1) through key-on / key-off for a few rate settings, with
 * distinct FLV0..FLV4.  The CPU polls the monitor as fast as it can and logs changes with a timestamp in samples
 * (TMU-based: us * 0.0441).  Output: feg_probe.txt */
#include "aica_io.h"

static struct { uint32_t t; uint16_t v; } lg[20000];

static void run(const char *name, int far, int fd1r, int fd2r, int frr, const uint16_t flv[5], uint32_t on_ms, uint32_t off_ms) {
    aica_quiet();
    for (int i = 0; i < 512; i++) ram_w32(0x10000 + 4 * i, 0x10001000);
    slot_cfg_t c;
    slot_cfg_default(&c, 0x10000, 256);
    c.LPOFF = 0;
    for (int i = 0; i < 5; i++) c.FLV[i] = flv[i];
    c.FAR = far; c.FD1R = fd1r; c.FD2R = fd2r; c.FRR = frr;
    c.AR = 31; c.D1R = 0; c.RR = 10;
    slot_write(0, &c);
    aw(R_MSLC, (1 << 14) | (0 << 8));
    uint32_t n = 0;
    uint16_t last = 0xFFFF;
    uint64_t t0 = now_us();
    ch_keyon(0);
    uint64_t t_off = t0 + on_ms * 1000, t_end = t_off + off_ms * 1000;
    int offdone = 0;
    for (;;) {
        uint64_t t = now_us();
        if (!offdone && t >= t_off) { ch_keyoff(0); offdone = 1; if (n < 20000) { lg[n].t = (uint32_t)((t - t0) * 441 / 10000); lg[n].v = 0xEEEE; n++; } }
        if (t >= t_end) break;
        uint16_t v = (uint16_t)ar(R_EGMON);
        if (v != last && n < 20000) { lg[n].t = (uint32_t)((t - t0) * 441 / 10000); lg[n].v = v; n++; last = v; }
    }
    LOG("== %s FAR %d FD1R %d FD2R %d FRR %d FLV %04x %04x %04x %04x %04x: %lu changes\n", name, far, fd1r, fd2r, frr,
        flv[0], flv[1], flv[2], flv[3], flv[4], (unsigned long)n);
    for (uint32_t i = 0; i < n; i++) LOG("%lu %04x\n", (unsigned long)lg[i].t, lg[i].v);
}

int test_main(void) {
    out_open("feg_probe.txt");
    static const uint16_t f1[5] = {0x0100, 0x1000, 0x0800, 0x1800, 0x0200};
    run("slow", 12, 12, 12, 12, f1, 400, 400);
    run("fast", 28, 28, 28, 28, f1, 100, 100);
    static const uint16_t f2[5] = {0x1FF8, 0x0008, 0x1000, 0x0000, 0x1FFF};
    run("extremes", 20, 20, 20, 20, f2, 300, 300);
    run("rate0", 0, 0, 0, 0, f1, 100, 100);
    aica_quiet();
    out_close();
    return 0;
}
