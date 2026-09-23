/* kon_defer.c -- KYONB is sampled when the KYONEX is PROCESSED, not when it is written (tests/eg_sched: a KYONB write
 * 2.4 us after a KYONEX was honoured by that KYONEX).  Measure the window: slot S released; write KYONEX (KYONB 0,
 * carried by slot 5's reg 0x00), then after d us write KYONB 1 to S (no KYONEX); 1 ms later read S's EG monitor:
 * keyed on or not.  d = 0 (back to back), 3, 6, ... 33 us, 8 trials each (the KYONEX phase within the sample is random),
 * for S = 1 and S = 62 (a per-frame key processing would give the two slots different windows).  Also the reverse:
 * KYONB 1 written, KYONEX, KYONB 0 after d us: keyed on or not.  Slot config = the witness (AR 31 D1R 31 DL 31 D2R 31
 * RR 31 KRS 1).  Output: kon_defer.txt */
#include "aica_io.h"
static uint16_t r0[64];
static void kb(int s, int on) { aw(CH(s, 0), r0[s] | (on ? 0x4000 : 0)); }
static void kx(int s) { aw(CH(s, 0), (ar(CH(s, 0)) & 0x7FFF) | 0x8000); }
static int on(int s) { spin_us(1000); return (egmon(s, 0) & 0x7FFF) < 0x4000; }
static void reset_slot(int s) { kb(s, 0); kx(5); spin_us(12000); }   /* key-off (the slot is off after 5.4 ms anyway) */
int test_main(void) {
    out_open("kon_defer.txt");
    aica_quiet();
    for (int i = 0; i < 16; i++) ram_w32(0x10000 + 4 * i, 0x40004000);
    slot_cfg_t c; slot_cfg_default(&c, 0x10000, 32); c.AR = 31; c.D1R = 31; c.DL = 31; c.D2R = 31; c.RR = 31; c.KRS = 1;
    static const int S[2] = {1, 62};
    c.ISEL = 0; slot_write(1, &c); c.ISEL = 1; slot_write(62, &c); c.ISEL = 2; slot_write(5, &c);
    for (int s = 0; s < 64; s++) r0[s] = ar(CH(s, 0)) & 0x3FFF;
    kb(1, 1); kb(62, 1); kx(5); spin_us(8000); kb(1, 0); kb(62, 0); kx(5); spin_us(12000);   /* warm-up cycle */
    for (int si = 0; si < 2; si++) {
        int s = S[si];
        LOG("slot %d: KYONEX then KYONB 1 after d us -> keyed on (of 8):", s);
        for (int d = 0; d <= 33; d += 3) {
            int n = 0;
            for (int t = 0; t < 8; t++) { reset_slot(s); spin_us(7 * t); kx(5); if (d) spin_us(d); kb(s, 1); n += on(s); }
            LOG(" d%d:%d", d, n);
        }
        LOG("\n");
        LOG("slot %d: KYONB 1, KYONEX, KYONB 0 after d us -> keyed on (of 8):", s);
        for (int d = 0; d <= 33; d += 3) {
            int n = 0;
            for (int t = 0; t < 8; t++) { reset_slot(s); spin_us(7 * t); kb(s, 1); kx(5); if (d) spin_us(d); kb(s, 0); n += on(s); }
            LOG(" d%d:%d", d, n);
        }
        LOG("\n");
    }
    aica_quiet();
    out_close();
    return 0;
}
