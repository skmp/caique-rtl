/* sub_frame.c -- sub-sample (TODO 1.6): the sample in which a register write acts, against the slot's frame.
 *
 * Slot k plays a constant PCM16 block (0x0800) at pitch 1, filter off, into MIXS bus 1.  The write under test toggles:
 *   exp 0: reg 0x00 SA[22:16] between two blocks (0x0800 at 0x010000, 0x1000 at 0x020000), VOFF 1 -- the fetch
 *   exp 1: reg 0x20 IMXL 15 <-> 13 (the send halves), VOFF 1                                          -- the send
 *   exp 2: reg 0x28 TL 0 <-> 32, VOFF 0                                                               -- the level
 *   exp 3: reg 0x28 VOFF 1 <-> 0 at TL 32                                                             -- the level bypass
 *   exp 4: reg 0x28 LPOFF 0 <-> 1, VOFF 1 (a settled filter at DC outputs -input, the bypass +input)    -- the filter
 * Each write is queued between two MEMS31 markers in one G2 burst (io_wn: they reach the AICA a few steps apart);
 * cases/flog.h logs MEMS31 in every frame and bus 1 every sample, so each write is known to a few clocks.  Per slot and
 * experiment NEV events, a pseudo-random 120..280 us apart; the ring is saved after each batch for the checker
 * (tools/sub_check: every event replayed through the cycle model at every clock the markers allow).
 * Output: sub_frame.txt (the event list), flog_<k>_<exp>.bin (the ring, 64K words, after each batch)
 */
#include "aica_io.h"
#include "flog.h"

#define NEV 40
#define NEXP 5
#define SA_A 0x010000u
#define SA_B 0x020000u

static uint32_t rng = 12345;
static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static uint16_t ring[FLOG_WORDS];

/* the slot of experiment x in its first state (tools/sub_check sub_setup mirrors this) */
static void setup(int k, int x) {
    slot_cfg_t c;
    slot_cfg_default(&c, SA_A, 64);
    c.LPOFF = x == 4 ? 0 : 1; c.ISEL = 1; c.IMXL = 15;
    c.VOFF = x == 2 ? 0 : 1;   /* exp 4: the filter at FLV 0x1FF8 (the default), Q 0 */
    c.TL = x == 3 ? 32 : 0;
    slot_write(k, &c);
}
/* the register and the value of toggle number e (e odd: back to the first state) */
static void toggle(int x, int e, uint32_t *reg, uint32_t *val) {
    const int on = !(e & 1);
    switch (x) {
    case 0: *reg = 0x00; *val = 0x4000 | (1 << 9) | ((on ? SA_B : SA_A) >> 16); break;
    case 1: *reg = 0x20; *val = ((on ? 13 : 15) << 4) | 1; break;
    case 2: *reg = 0x28; *val = ((on ? 32 : 0) << 8) | (1 << 5); break;
    case 3: *reg = 0x28; *val = (32 << 8) | ((on ? 0 : 1) << 6) | (1 << 5); break;
    default: *reg = 0x28; *val = (1 << 6) | ((on ? 1 : 0) << 5); break;
    }
}

int test_main(void) {
    out_open("sub_frame.txt");
    aica_quiet();
    for (int i = 0; i < 32; i++) { ram_w32(SA_A + 4 * i, 0x08000800u); ram_w32(SA_B + 4 * i, 0x10001000u); }
    const int bus[1] = {1};
    flog_start(1, bus);
    LOG("sub_frame: NEV %d, NEXP %d, blocks A %06x (0x0800) B %06x (0x1000), stream bus 1, markers in MEMS31, burst writes\n",
        NEV, NEXP, SA_A, SA_B);
    uint16_t marker = 0;
    flog_marker(marker);
    for (int k = 0; k < 64; k++) {
        for (int x = 0; x < NEXP; x++) {
            setup(k, x);
            ch_keyon(k);
            spin_us(2000);
            flog_resume();
            spin_us(500);
            for (int e = 0; e < NEV; e++) {
                const uint16_t m1 = ++marker, m2 = ++marker;
                uint32_t reg, val;
                toggle(x, e, &reg, &val);
                const uint32_t off[3] = {R_MEMS(31, 1), CH(k, reg), R_MEMS(31, 1)}, v[3] = {m1, val, m2};
                io_wn(3, off, v);
                LOG("E %d %d %d %u %u %lx\n", k, x, e, m1, m2, (unsigned long)val);
                spin_us(120 + rnd() % 160);
            }
            spin_us(1000);
            flog_stop();
            flog_read(ring);
            char name[64];
            snprintf(name, sizeof name, "flog_%d_%d.bin", k, x);
            out_bin(name, ring, sizeof ring);
            ch_keyoff(k);
            aw(CH(k, 0x20), 0);
            spin_us(1000);
        }
    }
    flog_stop();
    prog_reset(); prog_load();
    aica_quiet();
    LOG("done, last marker %u\n", marker);
    out_close();
    return 0;
}
