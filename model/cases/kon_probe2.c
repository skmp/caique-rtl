/* kon_probe2.c -- why did the eg_sched witnesses not re-key on the console?  The exact eg_sched write sequences with
 * the EG monitor read on both witnesses (wl = slot 1, wh = slot 62) 1 ms after each key-on; test slots A = 0, B = 32
 * configured as in eg_sched f_ (VOFF 1 constant) -- they take the same writes as there.  Sequences:
 *   P1  f_ cycle: keyset (A B on, wl wh off, KYONEX on A); +5 ms group0 [A r0][B r0][wl KYONB|KYONEX] (wh armed before);
 *       +6.5 ms key-off [wl 0][wh 0][KYONEX on wl]; +1.5 ms arm wh, group1 [wl KYONB|KYONEX][A][B]; +6.5 key-off; +1.5 group0
 *   P2  P1 with the key-off KYONEX written on A instead of wl
 *   P3  P1 with every group in order 0
 *   P4  e_ cycle: keyset(A B on; wl wh off; KYONEX on A); +10 ms [A 0][B 0][KYONEX on A]; arm wh; +2 ms [wl KYONB|KYONEX]
 *   P5  P4 with the wl key-on as two writes (KYONB then KYONEX on wl)
 *   P6  P4 without the 10 ms key-off
 *   P7  P4 with the group's KYONEX on wh's reg (wl armed before)
 *   P8  only the witnesses: arm wl, arm wh, KYONEX on wl (two separate writes each)
 * Output: kon_probe2.txt */
#include "aica_io.h"
#define A 0
#define B 32
#define WL 1
#define WH 62
static uint16_t r0[64];
static void kb(int s, int on) { aw(CH(s, 0), r0[s] | (on ? 0x4000 : 0)); }
static void kx(int s) { aw(CH(s, 0), (ar(CH(s, 0)) & 0x7FFF) | 0x8000); }
static uint32_t eg(int s) { return egmon(s, 0) & 0x7FFF; }
static void rep(const char *tag) { spin_us(1000); uint32_t l = eg(WL), h = eg(WH); LOG("%s: wl %04lx %s, wh %04lx %s\n", tag, (unsigned long)l, l < 0x4000 ? "on" : "OFF", (unsigned long)h, h < 0x4000 ? "on" : "OFF"); }
static void keyset_start(void) { kb(A, 1); kb(B, 1); kb(WL, 0); kb(WH, 0); kx(A); }
static void group(int order, int reg, uint16_t va, uint16_t vb, int kslot) {
    if (order == 0) { aw(CH(A, reg), va); aw(CH(B, reg), vb); aw(CH(kslot, 0), r0[kslot] | 0xC000); }
    else { aw(CH(kslot, 0), r0[kslot] | 0xC000); aw(CH(A, reg), va); aw(CH(B, reg), vb); }
}
static void koff_wit(int kslot) { kb(WL, 0); kb(WH, 0); kx(kslot); }
static void setup(void) {
    aica_quiet();
    slot_cfg_t c;
    slot_cfg_default(&c, 0x10000, 4096); c.KRS = 15; c.VOFF = 1; c.AR = 31; c.D1R = 0; c.RR = 0; c.ISEL = 0; slot_write(A, &c); c.ISEL = 1; slot_write(B, &c);
    slot_cfg_default(&c, 0x10000, 32); c.AR = 31; c.D1R = 31; c.DL = 31; c.D2R = 31; c.RR = 31; c.KRS = 1; c.ISEL = 2; slot_write(WL, &c); c.ISEL = 3; slot_write(WH, &c);
    for (int s = 0; s < 64; s++) r0[s] = ar(CH(s, 0)) & 0x3FFF;
}
int test_main(void) {
    out_open("kon_probe2.txt");
    aica_quiet();
    for (int i = 0; i < 2112; i++) ram_w32(0x10000 + 4 * i, 0x40004000);
    uint16_t va1 = 0x4202, vb1 = 0x4202, va0 = 0x4201, vb0 = 0x4201;
    for (int p = 1; p <= 3; p++) {
        setup(); keyset_start(); spin_us(5000);
        int koff_slot = p == 2 ? A : WL;
        char tag[32];
        kb(WH, 1); group(0, 0x00, va1, vb1, WL); snprintf(tag, sizeof tag, "P%d ev0 group0", p); rep(tag);
        spin_us(5500); koff_wit(koff_slot); spin_us(1500);
        kb(WH, 1); group(p == 3 ? 0 : 1, 0x00, va0, vb0, WL); snprintf(tag, sizeof tag, "P%d ev1 group%d", p, p == 3 ? 0 : 1); rep(tag);
        spin_us(5500); koff_wit(koff_slot); spin_us(1500);
        kb(WH, 1); group(0, 0x00, va1, vb1, WL); snprintf(tag, sizeof tag, "P%d ev2 group0", p); rep(tag);
        spin_us(5500); koff_wit(koff_slot); spin_us(1500);
        kb(WH, 1); group(p == 3 ? 0 : 1, 0x00, va0, vb0, WL); snprintf(tag, sizeof tag, "P%d ev3 group%d", p, p == 3 ? 0 : 1); rep(tag);
    }
    const uint16_t rr_new = (15 << 10) | (4 << 5) | 30;
    for (int p = 4; p <= 7; p++) {
        setup(); keyset_start(); spin_us(10000);
        if (p != 6) { kb(A, 0); kb(B, 0); kx(A); }
        if (p == 7) kb(WL, 1); else kb(WH, 1);
        spin_us(2000);
        char tag[32];
        if (p == 5) { kb(WL, 1); kx(WL); } else group(0, 0x14, rr_new, rr_new, p == 7 ? WH : WL);
        snprintf(tag, sizeof tag, "P%d", p); rep(tag);
        spin_us(8000); koff_wit(A); spin_us(1500);
        if (p == 7) kb(WL, 1); else kb(WH, 1);
        if (p == 5) { kb(WL, 1); kx(WL); } else group(1, 0x14, rr_new, rr_new, p == 7 ? WH : WL);
        snprintf(tag, sizeof tag, "P%d again", p); rep(tag);
    }
    setup(); spin_us(5000); kb(WL, 1); kb(WH, 1); kx(WL); rep("P8 witnesses only");
    spin_us(8000); koff_wit(WL); spin_us(1500); kb(WL, 1); kb(WH, 1); kx(WL); rep("P8 again");
    aica_quiet();
    out_close();
    return 0;
}
