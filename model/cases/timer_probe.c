/* timer_probe.c -- are the AICA timers (TIMA/B/C, 0x2890..0x2898) readable counters at the sample rate?
 * Writes each timer's prescale (bits 10:8) and start value (7:0), then reads them back repeatedly with the SH4 clock
 * (us) alongside.  Output: timer_probe.txt */
#include "aica_io.h"

int test_main(void) {
    out_open("timer_probe.txt");
    aica_quiet();
    static const uint16_t cfg[] = {0x0000, 0x0100, 0x0700};
    for (unsigned i = 0; i < 3; i++) {
        aw(R_TIMA, cfg[i] | 0x10);
        aw(R_TIMB, cfg[i] | 0x20);
        aw(R_TIMC, cfg[i] | 0x30);
        LOG("prescale %u (TIMA start 0x10, TIMB 0x20, TIMC 0x30):\n", cfg[i] >> 8);
        uint64_t t0 = now_us();
        for (int k = 0; k < 24; k++) {
            uint32_t a = ar(R_TIMA), b = ar(R_TIMB), c = ar(R_TIMC);
            LOG("  t %6lu us  TIMA %04lx TIMB %04lx TIMC %04lx\n", (unsigned long)(now_us() - t0), (unsigned long)a,
                (unsigned long)b, (unsigned long)c);
            spin_us(k < 12 ? 5 : 400);
        }
    }
    LOG("SCIPD %04lx SCIEB %04lx\n", (unsigned long)ar(0x289C), (unsigned long)ar(0x28A0));
    aica_quiet();
    out_close();
    return 0;
}
