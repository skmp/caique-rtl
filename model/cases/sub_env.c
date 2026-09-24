/* sub_env.c -- sub-sample (TODO 1.6): where the envelope pass reads its registers (tests/sub_sched exp 1, four times the
 * events: that case placed it at T ~ 43..51 clocks after slot k's frame start, between frames k + 5 and k + 6).
 * Same program, events and output format as cases/sub_sched.c, experiment 1 only, four batches per slot.
 * Original header of sub_sched.c:
 *
 * cases/flog.h logs MEMS31 (markers) in every frame and buses 1 and 2 every sample; every write under test is queued
 * between two markers (io_wn), a read is followed by a marker, so each access is bracketed.  Slot k plays PCM16 blocks
 * at pitch 1 into bus 1; bus 2 has no slot pointing at it.  Experiments, NEV events per slot and experiment:
 *   exp 0  KYONB window: slot k one-shot (16 samples of 0x0800, VOFF), KYONB 0.  Burst 1 writes reg 0x00 with KYONEX
 *          (KYONB 0); d = 0..35 us later burst 2 writes KYONB 1.  The key-on shows on bus 1, or not at all.
 *   exp 1  envelope pass: slot k looped at full level (TL 0, VOFF 0), released with RR 0 (the level holds).  The burst
 *          writes reg 0x14 RR 30 (R 60: +8 on every envelope clock); the first sample below the held level shows where
 *          the envelope of the next sample was computed.
 *   exp 2  CPU MIXS write + read: the burst writes MIXS2 (hi) = v; the SH4 then reads MIXS2 hi and writes a third marker.
 *          Bus 2 (logged every sample) shows which DSP samples see v; the read shows which bank the CPU read.
 *   exp 3  CPU MIXS write to a bus with a writer: slot k plays (VOFF) into bus 1; the burst writes MIXS1 (hi) = v.
 * Output: sub_sched.txt (events: E k exp batch e m1 m2 m3 m4 value read), flog_<k>_<exp>_<batch>.bin (the ring after
 * each batch)
 * Checker: tools/sched_check (every event replayed through the cycle model at every clock the markers allow).
 */
#include "aica_io.h"
#include "flog.h"

#define NEV01 12   /* exp 0 / 1: 12 events per batch, two batches (a batch must stay inside the ring's 1024 samples) */
#define NEV23 32
#define NEXP 4
#define SA_A 0x010000u

static uint32_t rng = 777;
static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static uint16_t ring[FLOG_WORDS];

/* slot k for experiment x (tools/sched_check sched_setup mirrors this) */
static void setup(int k, int x) {
    slot_cfg_t c;
    slot_cfg_default(&c, SA_A, 64);
    c.LPOFF = 1; c.ISEL = 1; c.IMXL = 15; c.VOFF = 1;
    if (x == 0) { c.LPCTL = 0; c.LEA = 16; }   /* one-shot: 16 samples, then stopped */
    if (x == 1) { c.VOFF = 0; c.RR = 0; }      /* D1R 0, DL 0, D2R 0: the level holds at 0 attenuation */
    slot_write(k, &c);
}
static uint32_t r00_of(int x, int kyonb) { return (kyonb ? 0x4000u : 0u) | (x == 0 ? 0u : 1u << 9) | (SA_A >> 16); }

int test_main(void) {
    out_open("sub_env.txt");
    aica_quiet();
    for (int i = 0; i < 32; i++) ram_w32(SA_A + 4 * i, 0x08000800u);
    const int bus[2] = {1, 2};
    flog_start(2, bus);
    LOG("sub_sched: NEV %d / %d, NEXP %d, block %06x (0x0800), streams bus 1 and 2, markers in MEMS31, burst writes\n",
        NEV01, NEV23, NEXP, SA_A);
    uint16_t marker = 0;
    flog_marker(marker);
    uint16_t v2 = 0x0100;
    for (int k = 0; k < 64; k++) {
        for (int xb = 0; xb < 4; xb++) {
            const int x = 1, batch = xb, nev = NEV01;
            setup(k, x);
            if (x != 0) ch_keyon(k);
            spin_us(2000);
            if (x == 1) { ch_keyoff(k); spin_us(1000); }
            flog_resume();
            spin_us(500);
            for (int e = 0; e < nev; e++) {
                const uint16_t m1 = ++marker, m2 = ++marker, m3 = ++marker, m4 = ++marker;
                uint32_t value = 0, rd = 0;
                if (x == 0) {
                    const uint32_t o1[3] = {R_MEMS(31, 1), CH(k, 0x00), R_MEMS(31, 1)}, w1[3] = {m1, 0x8000u | r00_of(0, 0), m2};
                    io_wn(3, o1, w1);
                    const uint32_t d = rnd() % 36;
                    spin_us(d);
                    const uint32_t o2[3] = {R_MEMS(31, 1), CH(k, 0x00), R_MEMS(31, 1)}, w2[3] = {m3, r00_of(0, 1), m4};
                    io_wn(3, o2, w2);
                    value = d;
                    spin_us(1000);                            /* 16 samples of play, then stopped */
                } else if (x == 1) {
                    const uint32_t o[3] = {R_MEMS(31, 1), CH(k, 0x14), R_MEMS(31, 1)}, w[3] = {m1, (15u << 10) | 30u, m2};
                    io_wn(3, o, w);
                    flog_marker(m3); flog_marker(m4);
                    spin_us(300);
                    /* back to the held full level: key-on (attack R 62 to 0), RR 0, key-off */
                    aw(CH(k, 0x14), (15u << 10) | 0u);
                    ch_keyon(k);
                    spin_us(600);
                    ch_keyoff(k);
                    spin_us(300);
                } else {
                    const int b = x == 2 ? 2 : 1;
                    value = v2++;
                    const uint32_t o[3] = {R_MEMS(31, 1), R_MIXS(b, 1), R_MEMS(31, 1)}, w[3] = {m1, value, m2};
                    io_wn(3, o, w);
                    rd = ar(R_MIXS(b, 1));
                    flog_marker(m3);
                    flog_marker(m4);
                    spin_us(150 + rnd() % 200);
                }
                LOG("E %d %d %d %d %u %u %u %u %lu %lu\n", k, x, batch, e, m1, m2, m3, m4, (unsigned long)value, (unsigned long)rd);
                spin_us(100 + rnd() % 100);
            }
            spin_us(1000);
            flog_stop();
            flog_read(ring);
            char name[64];
            snprintf(name, sizeof name, "flog_%d_%d_%d.bin", k, x, batch);
            out_bin(name, ring, sizeof ring);
            aw(CH(k, 0x00), 0);
            ch_keyoff(k);
            aw(CH(k, 0x20), 0);
            spin_us(3000);
        }
    }
    flog_stop();
    prog_reset(); prog_load();
    aica_quiet();
    LOG("done, last marker %u\n", marker);
    out_close();
    return 0;
}
