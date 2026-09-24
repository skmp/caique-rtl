/* irq_bit9.c -- what sets pending bit 9 (TODO 4.1; minicast / the AICA manual: MIDI out).  tests/timer_irq and
 * timer_probe saw it after their register sweeps, the models never set it.  Each step: both pending registers cleared,
 * one write, 200 us, both read (bit 9 and the rest), cleared again.  Output: irq_bit9.txt */
#include "aica_io.h"

int test_main(void) {
    out_open("irq_bit9.txt");
    aica_quiet();
    static const struct { uint32_t off, v; } w[] = {
        {0x2890, 0x0000}, {0x2890, 0x00FF}, {0x2890, 0x0700}, {0x2890, 0xF800}, {0x2890, 0xFFFF},
        {0x2894, 0xF800}, {0x2898, 0xF800},
        {0x289C, 0x07FF}, {0x289C, 0x0000}, {0x28A0, 0x0200}, {0x28A0, 0xF800}, {0x28A4, 0x0000},
        {0x28A8, 0x00FF}, {0x28AC, 0x00FF}, {0x28B0, 0x00FF}, {0x28A8, 0xFFFF}, {0x28AC, 0xFFFF}, {0x28B0, 0xFFFF},
        {0x28B4, 0x07FF}, {0x28B4, 0x0000}, {0x28B8, 0x0200}, {0x28B8, 0xF800}, {0x28BC, 0x0000},
        {0x2808, 0x0000}, {0x2808, 0x00FF}, {0x2808, 0xFFFF},
    };
    const int n = sizeof w / sizeof w[0];
    for (int i = 0; i < n; i++) {
        aw(0x28A4, 0x7FF); aw(R_MCIRE, 0x7FF);
        spin_us(100);
        const uint32_t m0 = ar(R_MCIPD), s0 = ar(0x28A0);
        aw(w[i].off, w[i].v);
        spin_us(200);
        const uint32_t m1 = ar(R_MCIPD), s1 = ar(0x28A0);
        LOG("W %04lx = %04lx: before MCIPD %04lx SCIPD %04lx, after MCIPD %04lx SCIPD %04lx%s\n", (unsigned long)w[i].off,
            (unsigned long)w[i].v, (unsigned long)(m0 & 0xFFFF), (unsigned long)(s0 & 0xFFFF), (unsigned long)(m1 & 0xFFFF),
            (unsigned long)(s1 & 0xFFFF), ((m1 | s1) & 0x200) ? "  <-- bit 9" : "");
        aw(0x2890, 0); aw(0x2894, 0); aw(0x2898, 0);
        aw(0x289C, 0); aw(0x28B4, 0);
        for (int k = 0; k < 3; k++) aw(0x28A8 + 4 * k, 0);
    }
    /* the timers: every prescale and a few counts, each timer written 0 first */
    static const uint16_t cnt[] = {0x00, 0x01, 0x80, 0xFE, 0xFF};
    for (int x = 0; x < 3; x++)
        for (int P = 0; P < 8; P++)
            for (unsigned c = 0; c < sizeof cnt / 2; c++) {
                const uint32_t off = 0x2890 + 4 * x;
                aw(off, 0);
                aw(0x28A4, 0x7FF); aw(R_MCIRE, 0x7FF);
                spin_us(60);
                aw(off, ((uint32_t)P << 8) | cnt[c]);
                spin_us(200);
                const uint32_t m1 = ar(R_MCIPD), s1 = ar(0x28A0);
                LOG("T %c P %d count %02x: after MCIPD %04lx SCIPD %04lx%s\n", 'A' + x, P, cnt[c], (unsigned long)(m1 & 0xFFFF),
                    (unsigned long)(s1 & 0xFFFF), ((m1 | s1) & 0x200) ? "  <-- bit 9" : "");
            }
    aw(0x28A4, 0x7FF); aw(R_MCIRE, 0x7FF);
    aica_quiet();
    out_close();
    return 0;
}
