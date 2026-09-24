/* timer_phase.c -- where the timers tick (TODO 4.1): the prescaler's phase against MDEC_CT and the tick's position in
 * the sample, measured with the DSP frame logger (cases/flog.h: MEMS31 read in every frame, each DSP sample's MDEC_CT
 * from its ring address).  tests/timer_irq showed an overflow (256 - start) ticks after the write, ticks every 2^P
 * samples, a count that wraps to 0 (no reload) and a prescaler that no write restarts.
 * Event (one per line of timer_phase.txt, in batches of NEV, the ring saved after each batch):
 *   kind 0  the one-sample interval: MCIRE bit 10, poll MCIPD bit 10, marker m3 at once (m1 / m2 bracket the MCIRE write)
 *   kind 1  timer x (A/B/C) at prescale P: MCIRE its bit; burst {m1, TIMx = P << 8 | 0xFF, m2}: the next tick overflows;
 *           poll MCIPD until the bit sets, marker m3 at once
 * The tick lies between the TIMx write's X0 (m1..m2) and m3's X0 minus the poll and write latency.  Checker:
 * tools/timer_check (prints each event's marker frames and MDEC_CT; the tick frame and the prescaler phase follow).
 * Output: timer_phase.txt ("E batch e kind x P m1 m2 m3 poll_us"), tp_<batch>.bin (the ring). */
#include "aica_io.h"
#include "flog.h"

#define NEV 16
#define NBATCH 12
static uint32_t rng = 2024;
static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static uint16_t ring[FLOG_WORDS];
static const uint32_t TIM[3] = {R_TIMA, R_TIMB, R_TIMC};

int test_main(void) {
    out_open("timer_phase.txt");
    aica_quiet();
    for (int t = 0; t < 3; t++) aw(TIM[t], 7u << 8);
    flog_start(0, 0);
    flog_stop();
    LOG("timer_phase: %d batches of %d events, markers in MEMS31\n", NBATCH, NEV);
    uint16_t marker = 0;
    static const uint8_t pre[] = {0, 0, 1, 2, 3, 5};
    for (int b = 0; b < NBATCH; b++) {
        flog_resume();
        spin_us(300);
        for (int e = 0; e < NEV; e++) {
            const int kind = (e % 4 == 0) ? 0 : 1;
            const int x = kind ? (int)(rnd() % 3) : 0;
            const uint32_t P = kind ? pre[rnd() % sizeof pre] : 0;
            const uint32_t bit = kind ? 0x40u << x : 0x400u;
            const uint16_t m1 = ++marker, m2 = ++marker, m3 = ++marker;
            spin_us(rnd() % 40);
            if (kind == 0) {
                const uint32_t o[3] = {R_MEMS(31, 1), R_MCIRE, R_MEMS(31, 1)}, w[3] = {m1, bit, m2};
                io_wn(3, o, w);
            } else {
                aw(R_MCIRE, bit);
                const uint32_t o[3] = {R_MEMS(31, 1), TIM[x], R_MEMS(31, 1)}, w[3] = {m1, (P << 8) | 0xFF, m2};
                io_wn(3, o, w);
            }
            const uint64_t t0 = now_us();
            uint32_t polls = 0, got = 0;
            while (now_us() - t0 < (uint64_t)(40u << P) + 200u) { polls++; if (ar(R_MCIPD) & bit) { got = 1; break; } }
            flog_marker(m3);
            LOG("E %d %d %d %d %lu %u %u %u %lu %lu\n", b, e, kind, x, (unsigned long)P, m1, m2, m3,
                (unsigned long)(now_us() - t0), (unsigned long)(got ? polls : 0));
            if (kind) aw(TIM[x], 7u << 8);   /* park */
            spin_us(100 + rnd() % 100);
        }
        spin_us(500);
        flog_stop();
        flog_read(ring);
        char name[32];
        snprintf(name, sizeof name, "tp_%d.bin", b);
        out_bin(name, ring, sizeof ring);
    }
    aw(R_MCIRE, 0x7FF);
    aica_quiet();
    out_close();
    return 0;
}
