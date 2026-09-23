/* kon_probe.c -- which key-on / key-off sequences does the hardware accept?  (tests/eg_sched: a witness slot keyed on,
 * decayed to "off" in decay 2, keyed off 1.1 ms later and keyed on again 1.5 ms after that did NOT restart on the
 * console, while the model restarts it; a KYONB|KYONEX written in one access to the slot's own reg 0x00 did not key it
 * on either.)  Slot 0: constant 0x4000, AR 31 D1R 31 DL 31 D2R 31 RR 31, KRS 1 (R 63: off 5.4 ms after key-on).
 * The EG monitor (0x2810) is polled after every key-on: 0x0xxx/0x2xxx = running, 0x5FFF = off in decay 2, 0x7FFF =
 * released/off.  Sequences (each starts from a slot that has been released for >= 20 ms):
 *   S1 W  key-on; 8 ms (off); key-off (KYONB 0 write, then KYONEX write); wait W us; key-on (KYONB 1 write, then KYONEX)
 *   S2    as S1 W=1500 but the key-on is ONE write (KYONB|KYONEX to slot 0's reg 0x00)
 *   S2b   as S2 but the KYONEX carrying the key-on is written to slot 5's reg 0x00 (KYONB 1 written to slot 0 first)
 *   S3    key-on; 3 ms (still decaying); key-off; 1.5 ms; key-on
 *   S4    key-on; 8 ms (off); key-on without a key-off (expected ignored: tests/sgc_keys K4)
 *   S5    key-on; 8 ms; key-off; 1.5 ms; key-off again; 1.5 ms; key-on
 *   S6    key-on; 8 ms; key-off written as ONE write (KYONB 0 | KYONEX to slot 0); 1.5 ms; key-on (two writes)
 *   S7    key-on; 8 ms; key-off (two writes); wait W; key-on as ONE write (KYONB|KYONEX), for W = 200, 1500, 6000, 20000
 * Output: kon_probe.txt */
#include "aica_io.h"
static uint16_t r0;
static void kb(int slot, int on) { aw(CH(slot, 0), (ar(CH(slot, 0)) & 0x3FFF) | (on ? 0x4000 : 0)); }
static void kx(int slot) { aw(CH(slot, 0), (ar(CH(slot, 0)) & 0x7FFF) | 0x8000); }
static uint32_t eg(void) { return egmon(0, 0) & 0x7FFF; }
static void report(const char *tag) {
    uint32_t e0 = eg(); spin_us(100); uint32_t e1 = eg(); spin_us(400); uint32_t e2 = eg(); spin_us(1500); uint32_t e3 = eg();
    LOG("%s: EG after the key-on +0 %04lx +100us %04lx +500us %04lx +2ms %04lx -> %s\n", tag, (unsigned long)e0, (unsigned long)e1, (unsigned long)e2, (unsigned long)e3,
        (e1 < 0x4000 || e2 < 0x4000 || e3 < 0x4000) ? "STARTED" : "ignored");
}
static void rest(void) { kb(0, 0); kx(0); spin_us(25000); }
int test_main(void) {
    out_open("kon_probe.txt");
    aica_quiet();
    for (int i = 0; i < 16; i++) ram_w32(0x10000 + 4 * i, 0x40004000);
    slot_cfg_t c; slot_cfg_default(&c, 0x10000, 32);
    c.ISEL = 0; c.AR = 31; c.D1R = 31; c.DL = 31; c.D2R = 31; c.RR = 31; c.KRS = 1;
    slot_write(0, &c); slot_write(5, &c); aw(CH(5, 0x20), 0x00F1);
    r0 = ar(CH(0, 0)) & 0x3FFF;
    static const uint32_t W[] = {200, 1500, 3000, 6000, 12000, 20000};
    char tag[64];
    for (unsigned i = 0; i < 6; i++) {
        rest(); kb(0, 1); kx(0); spin_us(8000); LOG("S1 W %lu: EG at 8 ms %04lx;", (unsigned long)W[i], (unsigned long)eg()); kb(0, 0); kx(0); spin_us(W[i]); kb(0, 1); kx(0);
        snprintf(tag, sizeof tag, " S1 W %lu", (unsigned long)W[i]); report(tag);
    }
    rest(); kb(0, 1); kx(0); spin_us(8000); kb(0, 0); kx(0); spin_us(1500); aw(CH(0, 0), r0 | 0xC000); report("S2 one-write key-on");
    rest(); kb(0, 1); kx(0); spin_us(8000); kb(0, 0); kx(0); spin_us(1500); kb(0, 1); kx(5); report("S2b KYONB then KYONEX via slot 5");
    rest(); kb(0, 1); kx(0); spin_us(3000); LOG("S3: EG at 3 ms %04lx;", (unsigned long)eg()); kb(0, 0); kx(0); spin_us(1500); kb(0, 1); kx(0); report(" S3 key-off while decaying");
    rest(); kb(0, 1); kx(0); spin_us(8000); kb(0, 1); kx(0); report("S4 key-on after off, no key-off");
    rest(); kb(0, 1); kx(0); spin_us(8000); kb(0, 0); kx(0); spin_us(1500); kb(0, 0); kx(0); spin_us(1500); kb(0, 1); kx(0); report("S5 two key-offs");
    rest(); kb(0, 1); kx(0); spin_us(8000); aw(CH(0, 0), r0 | 0x8000); spin_us(1500); kb(0, 1); kx(0); report("S6 one-write key-off");
    for (unsigned i = 0; i < 6; i++) {
        rest(); kb(0, 1); kx(0); spin_us(8000); kb(0, 0); kx(0); spin_us(W[i]); aw(CH(0, 0), r0 | 0xC000);
        snprintf(tag, sizeof tag, "S7 W %lu one-write key-on", (unsigned long)W[i]); report(tag);
    }
    rest(); kb(0, 1); kx(0); report("S8 plain key-on from a long release");
    rest(); kb(0, 1); kx(0); spin_us(8000); kb(0, 0); kx(0); spin_us(1500); kb(0, 1); kx(0); spin_us(200); kb(0, 1); kx(0); report("S9 key-on twice 200 us apart");
    aica_quiet();
    out_close();
    return 0;
}
