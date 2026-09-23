/* kon_first.c -- the first key-on of a freshly configured witness slot (AR 31 D1R 31 DL 31 D2R 31 RR 31 KRS 1,
 * constant 0x4000) sometimes lands in decay 2 at a = 0 (EG 0x4000) instead of running decay 1 (tests/kon_probe2 P1 ev0),
 * and in tests/eg_sched the witnesses did not re-key at all.  Trace the EG monitor every ~30 us for 3 ms after the key-on
 * for: F1 the first key-on after setup (slot released by aica_quiet); F2 the second (after key-off); F3 first key-on
 * after setup where the slot had gone off in decay 2 in a PREVIOUS setup; F4 F1 with 20 ms between the last register
 * write and the key-on; F5 F1 with the KYONEX carried by another slot's reg 0; F6..F9 = F1 repeated with the key-on
 * delayed by 0 / 6 / 11 / 17 us after a fixed sample-phase reference (a MIXS write... not available: instead spin
 * 0/6/11/17 us after a G2 read) to sample different phases.  Output: kon_first.txt */
#include "aica_io.h"
static uint16_t r0;
static void kb(int s, int on) { aw(CH(s, 0), r0 | (on ? 0x4000 : 0)); }
static void kx(int s) { aw(CH(s, 0), (ar(CH(s, 0)) & 0x7FFF) | 0x8000); }
static void trace(const char *tag) {
    uint32_t e[24]; uint64_t t0 = now_us(), t[24];
    for (int i = 0; i < 24; i++) { t[i] = now_us() - t0; e[i] = egmon(0, 0) & 0x7FFF; if (i < 12) spin_us(20); else spin_us(200); }
    LOG("%s:", tag); for (int i = 0; i < 24; i++) LOG(" %lu:%04lx", (unsigned long)t[i], (unsigned long)e[i]); LOG("\n");
}
static void setup(int also_off) {
    aica_quiet();
    slot_cfg_t c; slot_cfg_default(&c, 0x10000, 32); c.AR = 31; c.D1R = 31; c.DL = 31; c.D2R = 31; c.RR = 31; c.KRS = 1; c.ISEL = 0;
    slot_write(0, &c); c.ISEL = 1; slot_write(5, &c);
    r0 = ar(CH(0, 0)) & 0x3FFF;
    (void)also_off;
}
int test_main(void) {
    out_open("kon_first.txt");
    aica_quiet();
    for (int i = 0; i < 16; i++) ram_w32(0x10000 + 4 * i, 0x40004000);
    setup(0); spin_us(5000); kb(0, 1); kx(0); trace("F1 first key-on after setup");
    spin_us(8000); kb(0, 0); kx(0); spin_us(1500); kb(0, 1); kx(0); trace("F2 second key-on");
    spin_us(8000); /* slot off in decay 2 */
    setup(1); spin_us(5000); kb(0, 1); kx(0); trace("F3 first key-on after setup, previously off in decay 2");
    setup(0); spin_us(20000); kb(0, 1); kx(0); trace("F4 first key-on, 20 ms after the configuration");
    setup(0); spin_us(5000); kb(0, 1); kx(5); trace("F5 first key-on, KYONEX on slot 5");
    for (int rep = 0; rep < 6; rep++) {
        setup(0); spin_us(5000); ar(R_EGMON); spin_us(rep * 4); kb(0, 1); kx(0);
        char tag[32]; snprintf(tag, sizeof tag, "F6 rep %d (+%d us)", rep, rep * 4); trace(tag);
    }
    setup(0); spin_us(5000); kb(0, 1); kx(0); spin_us(3000); kb(0, 0); kx(0); spin_us(3000); kb(0, 1); kx(0); trace("F7 first key-on, then key-off at 3 ms and key-on again");
    aica_quiet();
    out_close();
    return 0;
}
