/* ca_stop.c -- does CA hold or advance between the sample-fetch stop (a = 0x3C0) and "off" (a past 0x3FF)?
 * (tests/slot_tail found the two thresholds; the CA monitor reads 0 from off (tests/sgc_keys K4); in between is
 * invisible in captures -- the input is zero -- so poll the monitors.)  Slot 0: constant 0x7FFF, looped [0, 4096),
 * pitch 1.0, AR 31, D1R 31 to DL 30 (decay 1 lands exactly on a = 0x3C0 = the stop), then decay 2 at D2R 10 (R 20:
 * +1 every 64 clocks -> 0x3C0 to 0x400 in 8192 samples = 186 ms), RR 31.  Runs: S1 as described (LPCTL 1); S2 the
 * same with pitch 0.5 (OCT -1: CA advances every other sample); S3 one-shot (LPCTL 0, LEA 4096) so a held CA would
 * differ from a wrapped one.  Each run logs (t, EG, CA) at every CA change up to 400 lines, then every 5 ms.
 * Output: ca_stop.txt */
#include "aica_io.h"
static void run(const char *tag, int oct, int lpctl) {
    aica_quiet();
    for (int i = 0; i < 2048; i++) ram_w32(0x20000 + 4 * i, 0x7FFF7FFF);
    slot_cfg_t c;
    slot_cfg_default(&c, 0x20000, 4096);
    c.AR = 31; c.D1R = 31; c.DL = 30; c.D2R = 10; c.RR = 31; c.OCT = oct & 0xF; c.LPCTL = lpctl;
    slot_write(0, &c);
    aw(R_MSLC, 0);
    LOG("%s: AR 31 D1R 31 DL 30 D2R 10 RR 31 OCT %d LPCTL %d LEA 4096\n", tag, oct, lpctl);
    ch_keyon(0);
    uint64_t t0 = now_us(), tlog = 0;
    uint32_t lca = 0xFFFFFFFF, leg = 0xFFFFFFFF;
    int n = 0, nlog = 0;
    for (;;) {
        uint64_t t = now_us() - t0;
        uint32_t eg = ar(R_EGMON) & 0x7FFF, ca = ar(R_CAMON) & 0xFFFF;
        int st = (eg >> 13) & 3, a = eg & 0x1FFF;
        /* log: every EG state change, every CA change while a >= 0x3B8 (the stop region, a few ticks before), and
         * a heartbeat every 5 ms; stop 40 ms after "off" or after 1.5 s */
        int interesting = (eg >> 13) != (leg >> 13) || (a >= 0x3B8 && ca != lca && nlog < 400) || t - tlog >= 5000;
        if (interesting) { LOG("%s t %7lu us: EG %04lx (st %d a %03x) CA %04lx\n", tag, (unsigned long)t, (unsigned long)eg, st, a, (unsigned long)ca); tlog = t; if (a >= 0x3B8 && ca != lca) nlog++; }
        lca = ca; leg = eg;
        if (eg == 0x5FFF || eg == 0x7FFF) { if (++n > 4000) break; } /* ~40 ms of polling after off */
        if (t > 1500000) break;
    }
    LOG("%s end: EG %04lx CA %04lx\n", tag, (unsigned long)(ar(R_EGMON) & 0x7FFF), (unsigned long)(ar(R_CAMON) & 0xFFFF));
    ch_keyoff(0);
    spin_us(20000);
}
int test_main(void) {
    out_open("ca_stop.txt");
    run("S1", 0, 1);
    run("S2", -1, 1);
    run("S3", 0, 0);
    aica_quiet();
    out_close();
    return 0;
}
