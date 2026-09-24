/* timer_irq.c -- the timers and the interrupt registers from the SH4 (TODO 4.1).
 *
 * Registers (minicast / wren7 map): TIMA/B/C 0x2890/94/98 (count 7:0, prescale 10:8; write-only: tests/timer_probe),
 * SCIEB/SCIPD/SCIRE 0x289C/A0/A4 (the ARM's enable / pending / reset), SCILV0-2 0x28A8/AC/B0, MCIEB/MCIPD/MCIRE
 * 0x28B4/B8/BC (the SH4's).  Pending bits: 6/7/8 timer A/B/C overflow, 9 MIDI out, 10 one-sample interval, 5 SCPU.
 * The AICA's SH4 interrupt line is SB_ISTEXT bit 1 (io_sh4_irq).  Every poll is bounded.  Parts:
 *   R  storage: each register written 0x7FF / 0xFFFF / 0x0000 / 0x5555, read back (the ARM stays in reset)
 *   C  pending: timers parked (prescale 7), then MCIRE 0x7FF and SCIRE 0x7FF in turn, both pending registers read
 *      right after each (shared or separate?), then writes of 0x7FF to MCIPD / SCIPD (which bits a write sets)
 *   L  the interrupt line: MCIEB = each single bit with its pending bit set / cleared, SB_ISTEXT bit 1 read
 *   S  the one-sample interval: MCIRE bit 10, poll MCIPD until bit 10 is set, 48 times back to back (the edge spacing)
 *   T  timers: per (timer, prescale, start): MCIRE bit, TIMx write, poll MCIPD until the bit sets (interval 1), clear,
 *      poll again (interval 2: reload or wrap?); with 0..N us of random delay before the write (the prescaler phase)
 * Times are SH4 us (io_now_us) from the write that starts the interval to the poll that saw the bit (the poll period
 * is ~2.5 us).  Output: timer_irq.txt; checker tools/timer_check. */
#include "aica_io.h"

#define R_SCIEB 0x289C
#define R_SCIPD 0x28A0
#define R_SCIRE 0x28A4
#define R_SCILV(i) (0x28A8 + 4 * (i))
#define R_MCIEB 0x28B4
static const uint32_t TIM[3] = {R_TIMA, R_TIMB, R_TIMC};

static uint32_t rng = 12345;
static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

/* poll MCIPD until (bit) is set; returns the us from t0, or 0xFFFFFFFF after limit_us.  g_gap: the time between the
 * last two polls -- the bound on how late the detection is (the SH4 is held now and then: ~60 us often, ~2.5 ms
 * sometimes) */
static uint32_t g_gap, g_max;   /* the last poll gap; the longest (a jump of ~2.5 ms: SH4 time base or a stall) */
static uint32_t wait_bit(uint32_t bit, uint64_t t0, uint32_t limit_us) {
    uint64_t prev = now_us();
    g_gap = 0; g_max = 0;
    for (;;) {
        uint32_t v = ar(R_MCIPD);
        uint64_t t = now_us();
        g_gap = (uint32_t)(t - prev);
        if (g_gap > g_max) g_max = g_gap;
        prev = t;
        if (v & bit) return (uint32_t)(t - t0);
        if (t - t0 > limit_us) return 0xFFFFFFFFu;
    }
}

static void part_r(void) {
    static const uint32_t regs[] = {R_TIMA, R_TIMB, R_TIMC, R_SCIEB, R_SCIPD, R_SCIRE, R_SCILV(0), R_SCILV(1), R_SCILV(2),
                                    R_MCIEB, R_MCIPD, R_MCIRE};
    static const uint32_t vals[] = {0x7FF, 0xFFFF, 0x0000, 0x5555};
    LOG("R initial:");
    for (unsigned i = 0; i < sizeof regs / 4; i++) LOG(" %04lx=%04lx", (unsigned long)regs[i], (unsigned long)(ar(regs[i]) & 0xFFFF));
    LOG("\n");
    for (unsigned i = 0; i < sizeof regs / 4; i++) {
        LOG("R %04lx", (unsigned long)regs[i]);
        for (unsigned v = 0; v < 4; v++) {
            aw(regs[i], vals[v]);
            LOG(" w %04lx r %04lx", (unsigned long)vals[v], (unsigned long)(ar(regs[i]) & 0xFFFF));
        }
        LOG("\n");
        aw(regs[i], 0);
    }
}

static void part_c(void) {
    for (int t = 0; t < 3; t++) aw(TIM[t], 7u << 8);   /* 32768 samples to the next overflow */
    aw(R_MCIEB, 0); aw(R_SCIEB, 0);
    spin_us(1000);
    for (int k = 0; k < 4; k++) {
        const uint32_t re = k & 1 ? R_SCIRE : R_MCIRE;
        const uint32_t before_m = ar(R_MCIPD), before_s = ar(R_SCIPD);
        aw(re, 0x7FF);
        const uint32_t m = ar(R_MCIPD), s = ar(R_SCIPD);
        LOG("C %s 7ff: before MCIPD %04lx SCIPD %04lx, after MCIPD %04lx SCIPD %04lx\n", k & 1 ? "SCIRE" : "MCIRE",
            (unsigned long)(before_m & 0xFFFF), (unsigned long)(before_s & 0xFFFF), (unsigned long)(m & 0xFFFF),
            (unsigned long)(s & 0xFFFF));
        spin_us(500);
    }
    for (int k = 0; k < 2; k++) {
        const uint32_t pd = k ? R_SCIPD : R_MCIPD;
        aw(R_MCIRE, 0x7FF); aw(R_SCIRE, 0x7FF);
        aw(pd, 0x7FF);
        const uint32_t m = ar(R_MCIPD), s = ar(R_SCIPD);
        LOG("C write %s 7ff: MCIPD %04lx SCIPD %04lx\n", k ? "SCIPD" : "MCIPD", (unsigned long)(m & 0xFFFF), (unsigned long)(s & 0xFFFF));
    }
    aw(R_MCIRE, 0x7FF); aw(R_SCIRE, 0x7FF);
}

static void part_l(void) {
    aw(R_MCIEB, 0);
    aw(R_MCIRE, 0x7FF);
    LOG("L all off: line %lu\n", (unsigned long)io_sh4_irq());
    for (int b = 0; b <= 10; b++) {
        aw(R_MCIRE, 0x7FF);
        aw(R_MCIEB, 1u << b);
        const uint32_t pd = ar(R_MCIPD), l1 = io_sh4_irq();
        aw(R_MCIPD, 0x20);                         /* SCPU: the settable bit */
        const uint32_t pd2 = ar(R_MCIPD), l2 = io_sh4_irq();
        aw(R_MCIRE, 0x7FF);
        const uint32_t l3 = io_sh4_irq();
        LOG("L MCIEB bit %2d: MCIPD %04lx line %lu; after MCIPD write 20: MCIPD %04lx line %lu; after MCIRE 7ff: line %lu\n", b,
            (unsigned long)(pd & 0xFFFF), (unsigned long)l1, (unsigned long)(pd2 & 0xFFFF), (unsigned long)l2, (unsigned long)l3);
        aw(R_MCIEB, 0);
    }
    aw(R_MCIRE, 0x7FF);
}

static void part_s(void) {
    aw(R_MCIRE, 0x400);
    (void)wait_bit(0x400, now_us(), 200);
    LOG("S sample interval (us from the previous edge seen / the last poll gap):");
    uint64_t prev = now_us();
    for (int i = 0; i < 48; i++) {
        aw(R_MCIRE, 0x400);
        const uint32_t d = wait_bit(0x400, prev, 200);
        prev += d;
        LOG(" %lu/%lu/%lu", (unsigned long)d, (unsigned long)g_gap, (unsigned long)g_max);
    }
    LOG("\n");
}

static void part_t(void) {
    static const uint8_t pre[] = {0, 1, 2, 3, 5};
    static const uint16_t start[] = {0xFF, 0xFE, 0xF0, 0xC0, 0x00};
    for (int x = 0; x < 3; x++) {
        const uint32_t bit = 0x40u << x;
        for (unsigned p = 0; p < sizeof pre; p++) {
            for (unsigned s = 0; s < sizeof start / 2; s++) {
                const uint32_t P = pre[p], S = start[s];
                const uint32_t samples = (256 - S) << P;
                if (samples > 8192) continue;
                const uint32_t lim = samples * 23 + 2000;
                for (int rep = 0; rep < 3; rep++) {
                    aw(TIM[x], 7u << 8);   /* park with prescale 7 (a prescale change: the next write changes it again) */
                    spin_us(rnd() % 90);
                    aw(R_MCIRE, bit);
                    const uint64_t t0 = now_us();
                    aw(TIM[x], (P << 8) | S);
                    const uint32_t d1 = wait_bit(bit, t0, lim), g1 = g_gap, x1 = g_max;
                    const uint64_t t1 = t0 + (d1 == 0xFFFFFFFFu ? lim : d1);
                    aw(R_MCIRE, bit);
                    const uint32_t d2 = wait_bit(bit, t1, 256u * (23u << P) + 2000), g2 = g_gap, x2 = g_max;
                    /* the same prescale written again after a random delay: does the write restart the prescaler? */
                    spin_us(rnd() % 90);
                    aw(R_MCIRE, bit);
                    const uint64_t t3 = now_us();
                    aw(TIM[x], (P << 8) | S);
                    const uint32_t d3 = wait_bit(bit, t3, lim), g3 = g_gap, x3 = g_max;
                    LOG("T %c P %lu S %02lx: first %lu second %lu same-P rewrite %lu (us; %lu samples nominal) gaps %lu %lu %lu max %lu %lu %lu\n",
                        'A' + x, (unsigned long)P, (unsigned long)S, (unsigned long)d1, (unsigned long)d2, (unsigned long)d3,
                        (unsigned long)samples, (unsigned long)g1, (unsigned long)g2, (unsigned long)g3, (unsigned long)x1,
                        (unsigned long)x2, (unsigned long)x3);
                }
            }
        }
        aw(TIM[x], 7u << 8);
    }
    aw(R_MCIRE, 0x7FF);
}

int test_main(void) {
    out_open("timer_irq.txt");
    aica_quiet();
    part_r();
    part_c();
    part_l();
    part_s();
    part_t();
    aw(R_MCIEB, 0); aw(R_SCIEB, 0);
    aw(R_MCIRE, 0x7FF); aw(R_SCIRE, 0x7FF);
    aica_quiet();
    out_close();
    return 0;
}
