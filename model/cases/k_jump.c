/* k_jump.c -- which register write moves MDEC_CT against the envelope counter (TODO 3; NOTES "Known start state and
 * replay parameters").  The hw9 console session: every run up to tests/probe's preamble fits K 0 with the envelope clock on
 * even MDEC_CT, every run after it K 10923 on ODD MDEC_CT -- one of probe's writes jumped one counter by an odd amount
 * (eg_kprobe's actions never did: RBP/RBL to 0 and back, timers, MVOL, ARM, a DSP program, every channel register).
 * probe's writes that eg_kprobe did not make: 0x2804 with its high bits (patterns FFFF / 0 / 5555 / AAAA), EXTS0/1,
 * MIXS0, EFREG0/15, TEMP / MEMS / COEF / MADRS / MPRO single words.  Each action below is followed by the replay
 * measurement (cases/common/replay.c replay_measure: K, the clock parity, MDEC_CT at an SH4 time); tools/replay_fit on
 * kj<N> gives K / par per step, and the MDEC_CT continuity between steps (from the SH4 times) says which counter moved.
 * Output: k_jump.txt (actions), kj<N>_log.txt / kj<N>.hdr / .bin. */
#include "aica_io.h"

static void rbpl(uint32_t v) { aw(R_RBPL, v); spin_us(2000); aw(R_RBPL, ((DSP_RING_RBP >> 11) & 0xFFF) | (3u << 13)); spin_us(2000); }
static void wr_restore(uint32_t off) {
    static const uint32_t pat[] = {0xFFFFFFFFu, 0x00000000u, 0x55555555u, 0xAAAAAAAAu};
    uint32_t old = ar(off);
    for (int i = 0; i < 4; i++) { aw(off, pat[i]); (void)ar(off); }
    aw(off, old);
    spin_us(2000);
}

int test_main(void) {
    out_open("k_jump.txt");
    static const char *act[] = {
        "none (baseline)",
        "0x2804 = 0x8000 (bit 15), then the default ring",
        "0x2804 = 0x1000 (bit 12), then the default ring",
        "0x2804 = 0x5555, then the default ring",
        "0x2804 = 0xAAAA, then the default ring",
        "0x2804 = 0xFFFF, then the default ring",
        "EXTS0 / EXTS1 written FFFF 0 5555 AAAA, restored",
        "MIXS0.l / MIXS0.h / EFREG0 / EFREG15 written FFFF 0 5555 AAAA, restored",
        "TEMP0 / MEMS0 / MEMS31 / COEF0 / MADRS0 / MPRO127 written FFFF 0 5555 AAAA, restored",
    };
    const int nact = sizeof act / sizeof act[0];
    for (int a = 0; a < nact; a++) {
        switch (a) {
        case 1: rbpl(0x8000); break;
        case 2: rbpl(0x1000); break;
        case 3: rbpl(0x5555); break;
        case 4: rbpl(0xAAAA); break;
        case 5: rbpl(0xFFFF); break;
        case 6: wr_restore(R_EXTS(0)); wr_restore(R_EXTS(1)); break;
        case 7: wr_restore(R_MIXS(0, 0)); wr_restore(R_MIXS(0, 1)); wr_restore(R_EFREG(0)); wr_restore(R_EFREG(15)); break;
        case 8:
            wr_restore(R_TEMP(0, 0)); wr_restore(R_TEMP(0, 1)); wr_restore(R_MEMS(0, 1)); wr_restore(R_MEMS(31, 1));
            wr_restore(R_COEF(0)); wr_restore(R_MADRS(0));
            for (int k = 0; k < 4; k++) wr_restore(R_MPRO(127, k));
            break;
        default: break;
        }
        char nm[16];
        snprintf(nm, sizeof nm, "kj%d", a);
        const uint32_t x = replay_measure(nm);
        const uint64_t t = now_us();
        LOG("kj%d action: %s; sync MDEC_CT %04lx, measured by t %lu%06lu\n", a, act[a], (unsigned long)x,
            (unsigned long)(t / 1000000u), (unsigned long)(t % 1000000u));
    }
    aica_quiet();
    out_close();
    return 0;
}
