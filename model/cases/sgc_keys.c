/* sgc_keys.c -- key events, observed through the monitors (EG with state, CA) of the slot under test, polled with a
 * sample timestamp, plus reg +00 readback (KYONB).  Constant 0x7FFF PCM16 looped [0, 4096), pitch 1.0.
 *   K1 LPSLNK: AR 8 (slow), LSA 1000: attack -> decay 1 when CA reaches LSA; K1b: same without LPSLNK
 *   K2 key-on while in decay 2 (sustain): ignored or restart?
 *   K3 key-off, then key-on 2 ms later (release not finished)
 *   K4 decay 2 runs to "off" (D2R 31), then key-on without key-off
 *   K5 KYONB after release end / after decay-2 off
 * Output: sgc_keys.txt (event log per test) */
#include "aica_io.h"

static void setup(int ar, int d1r, int dl, int d2r, int rr, int lpslnk, uint16_t lsa) {
    aica_quiet();
    for (int i = 0; i < 2048; i++) ram_w32(0x20000 + 4 * i, 0x7FFF7FFF);
    slot_cfg_t c;
    slot_cfg_default(&c, 0x20000, 4096);
    c.AR = ar; c.D1R = d1r; c.DL = dl; c.D2R = d2r; c.RR = rr; c.LPSLNK = lpslnk; c.LSA = lsa;
    slot_write(0, &c);
    aw(R_MSLC, 0);
}
/* poll the monitors for us microseconds, log changes: t(us) EG CA reg0 */
static void watch(const char *tag, uint32_t us) {
    uint64_t t0 = now_us(), t;
    uint32_t le = 0xFFFFFFFF, lc = 0xFFFFFFFF, lr = 0xFFFFFFFF;
    int n = 0;
    while ((t = now_us()) - t0 < us) {
        uint32_t eg = ar(R_EGMON) & 0x7FFF, ca = ar(R_CAMON), r0 = ar(CH(0, 0)) & 0xFFFF;
        /* log state/KYONB changes and CA resets always; EG value changes are too many: only state */
        int logit = ((eg >> 13) != (le >> 13)) || (r0 != lr) || (ca < lc && lc != 0xFFFFFFFF && lc - ca > 64) || n == 0;
        if (logit && n < 200) { LOG("%s t %6lu us: EG %04lx CA %04lx reg0 %04lx\n", tag, (unsigned long)(t - t0), (unsigned long)eg, (unsigned long)ca, (unsigned long)r0); n++; }
        le = eg; lc = ca; lr = r0;
    }
    LOG("%s end: EG %04lx CA %04lx reg0 %04lx\n", tag, (unsigned long)(ar(R_EGMON) & 0x7FFF), (unsigned long)ar(R_CAMON), (unsigned long)(ar(CH(0, 0)) & 0xFFFF));
}

int test_main(void) {
    out_open("sgc_keys.txt");
    setup(8, 20, 31, 0, 31, 1, 1000);
    ch_keyon(0); watch("K1 lpslnk", 60000);
    setup(8, 20, 31, 0, 31, 0, 1000);
    ch_keyon(0); watch("K1b nolink", 60000);
    setup(31, 31, 4, 0, 31, 0, 0);
    ch_keyon(0); watch("K2 sustain", 20000);
    ch_keyon(0); watch("K2 keyon-again", 20000);
    setup(31, 31, 4, 0, 10, 0, 0);
    ch_keyon(0); watch("K3 on", 20000);
    ch_keyoff(0); watch("K3 off", 2000);
    ch_keyon(0); watch("K3 on-again", 20000);
    setup(31, 31, 2, 31, 31, 0, 0);
    ch_keyon(0); watch("K4 d2off", 40000);
    ch_keyon(0); watch("K4 keyon-after-off", 20000);
    ch_keyoff(0); watch("K5 release", 40000);
    aica_quiet();
    out_close();
    return 0;
}
