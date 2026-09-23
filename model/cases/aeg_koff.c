/* aeg_koff.c -- AEG key events with the key sample PINNED: does a key-off on an envelope clock step the AEG with
 * the previous segment's increment (S3, measured on the FEG only), what happens when the key-off lands inside an
 * attack, and does a key-on during a release load 0x280 on the key-on sample (with a step or not) or on the next
 * clock, and when does CA restart.
 *
 * A key-off is not locatable from the AEG level alone: "S3 with the key-off on an even sample n" and "any rule with
 * the key-off on the odd sample n+1" give the same level sequence (decay 2 and release both raise a).  So every
 * key-off write here also keys ON a WITNESS slot (slot 3, stream 3: KYONEX applies the KYONB of every slot at
 * once): AR 31 with KRS 1 (OCT 0, FNS 0: s = 2, pitch 1.0) -> R 63, whose level jumps to full (a = 0) on the key-on
 * sample (S2, S5; KRS 0 / FNS 0x200 would also give R 63 but at pitch 1.5, whose loop-end interpolation halves one
 * sample per loop, eg_lock odd_* runs).  The witness onset is the key-off sample of the test slots.  The witness
 * then decays to "off" on its own (D1R 31 to DL 31, D2R 31: about 256 samples), so it is off long before the next
 * write and every one of its key-ons is a fresh one (the established S2 / S5 case, not the key-on-during-release
 * case this test asks about).  Assumption: one KYONEX write applies the key-on and the key-off of different slots
 * on the same sample (eg_lock keys: identical onsets on 4 slots; feg_track: one key-off sample for 3 slots).
 *
 * Runs (constant 0x7FFF PCM16 looped [0,32) at 0x10000, TL 0, VOFF 0, LPOFF 1, IMXL 15, ISEL k -> MIXS k; KRS 15 on
 * the test slots; on/off durations from an LCG so the KYONEX writes land on both MDEC_CT parities):
 *   koff_d2   AR 31 D1R 31 DL 2 (decay 2 from a = 0x40), (D2R, RR) = (26,24) (24,28) (28,0): decay-2 increments
 *             +2/+1/+4 vs release +1/+4/hold.  Key-on 2000 + rand%4500 us (the key-off lands in decay 2, a < 0x240),
 *             key-off wait 55000 + rand%3000 us.  Slot 2 (RR 0) never releases: every key-on after the first is a
 *             key-on during a held release (Q3).
 *   koff_d2b  (D2R, RR) = (30,24) (26,30) (0,26): +8/+2/0 vs +1/+8/+2 (slot 2: the rate-0 case, decay 2 holding at
 *             0x40); key-on 1000 + rand%2500 us.
 *   koff_d1   AR 31 D1R 26/24/28 DL 31 (decay 1 runs from a = 0 all the way up: +2/+1/+4 per clock, R 52/48/56), D2R 0,
 *             RR 24/28/26 (release +1/+4/+2): the key-off lands inside DECAY 1 (the segment F1/F2 left unmeasured).
 *             Key-on 1000 + rand%5000 us (a < 0x260 at the key-off: the +4 slot reaches the 0x3C0 fetch stop only after
 *             240 clocks = 10.9 ms), key-off wait 55000 + rand%3000 us.
 *   koff_att  attack R 48/52/56 (AR 24/26/28, increments 1/2/4) with RR 28/24/26 (release 4/1/2): the key-off lands
 *             inside the attack (key-on 200 + rand%300 us = 9..22 samples; cap_poll adds up to 0.7 ms to a wait
 *             when a ring block is due, so a few key-offs land after the R 56 attack: rate-0 cases).
 *   kon_rel   AR 24 (R 48) constant, AR 24 ramp, AR 31 (R 62) ramp; RR 24 (+1/clock).  Ramp: int16 8*i - 0x4000,
 *             i in [0,4096) at 0x20000 looped [0,4096) (CA becomes visible through the level law once a is small).
 *             Cycle: key-on, 3000 us, key-off, 5000 + rand%8000 us (mid-release), key-on again WITH the witness
 *             (pins the second key-on sample), 3000 us, key-off, 60000 us; 16 cycles.
 * Marks: koff runs 1/2 around the key-on write, 3/4 around the key-off write (16 cycles x 4 = CAP_MAXEV);
 * kon_rel 1, 3, 5, 7 before each of the four writes (16 x 4 = 64).  Marks are head estimates (search windows only).
 * Analysis: work/koff/koff_fit.cpp.  Output: aeg_koff.txt, <run>.hdr/.bin */
#include "cap.h"
#define NS 4
#define MAXV (2u << 20)
#define SA_CONST 0x10000u
#define SA_RAMP 0x20000u
#define NRAMP 4096
#define TEST 0x7 /* KYONB mask: test slots 0..2 */
#define WIT 0x8  /* witness slot 3 */
static int32_t capbuf[MAXV];
static int16_t ramp[NRAMP];
static const int mixs[NS] = {0, 1, 2, 3};

typedef struct { int AR, D1R, DL, D2R, RR, KRS, OCT, FNS, ramp; } stream_t;
typedef struct {
    const char *name;
    int kind; /* 0: key-on / key-off cycles; 1: kon_rel (on, off, on during release, off) */
    stream_t s[NS];
    uint32_t on_base, on_rand, off_base, off_rand;
    int cycles;
    uint32_t seed;
} run_t;

#define WITNESS {31, 31, 31, 31, 31, 1, 0, 0, 0} /* KRS 1 (k 1, s 2): R 63 everywhere at pitch 1.0; decays to "off" by itself (DL 31) within 256 samples of its key-on, so every witness key-on is a fresh one */

/* KYONB of slot k = bit k of mask, then one KYONEX (through slot 0's register) */
static void keyx(int mask) {
    for (int k = 0; k < NS; k++) aw(CH(k, 0), (ar(CH(k, 0)) & 0x3FFF) | (((mask >> k) & 1) ? 0x4000 : 0));
    aw(CH(0, 0), (ar(CH(0, 0)) & 0x7FFF) | 0x8000);
}
static uint32_t lcg(uint32_t *seed) {
    *seed = *seed * 1103515245u + 12345u;
    return *seed >> 16;
}

static void run(const run_t *r) {
    aica_quiet();
    for (int i = 0; i < 16; i++) ram_w32(SA_CONST + 4 * i, 0x7FFF7FFF);
    for (int k = 0; k < NS; k++) {
        const stream_t *s = &r->s[k];
        slot_cfg_t c;
        slot_cfg_default(&c, s->ramp ? SA_RAMP : SA_CONST, s->ramp ? NRAMP : 32);
        c.ISEL = k; c.AR = s->AR; c.D1R = s->D1R; c.DL = s->DL; c.D2R = s->D2R; c.RR = s->RR;
        c.KRS = s->KRS; c.OCT = s->OCT; c.FNS = s->FNS;
        slot_write(k, &c);
        LOG("%s stream %d: slot %d AR %d D1R %d DL %d D2R %d RR %d KRS %d OCT %d FNS %03x ramp %d role %s\n", r->name, k, k,
            c.AR, c.D1R, c.DL, c.D2R, c.RR, c.KRS, c.OCT, c.FNS, s->ramp, k == 3 ? "witness" : "test");
    }
    if (cap_start(NS, mixs, capbuf, MAXV)) { OUT("%s: cap_start failed\n", r->name); return; }
    cap_wait_us(10000);
    uint32_t seed = r->seed;
    for (int cyc = 0; cyc < r->cycles; cyc++) {
        if (r->kind == 0) {
            uint32_t on = r->on_base + lcg(&seed) % r->on_rand, off = r->off_base + lcg(&seed) % r->off_rand;
            cap_mark(1); keyx(TEST); cap_mark(2); /* A: test slots on, witness off (from its a = 0 sustain) */
            cap_wait_us(on);
            cap_mark(3); keyx(WIT); cap_mark(4);  /* B: test slots off, witness on: its onset = the key-off sample */
            cap_wait_us(off);
        } else {
            uint32_t gap = r->on_base + lcg(&seed) % r->on_rand;
            cap_mark(1); keyx(TEST);       /* A: fresh key-on */
            cap_wait_us(3000);
            cap_mark(3); keyx(0);          /* B: key-off */
            cap_wait_us(gap);
            cap_mark(5); keyx(TEST | WIT); /* C: key-on during the release; the witness (off since D) pins the sample */
            cap_wait_us(3000);
            cap_mark(7); keyx(0);          /* D: key-off */
            cap_wait_us(r->off_base);
        }
    }
    uint32_t n = cap_stop();
    cap_save(r->name, n);
    OUT("%s: %lu samples, errors %lu, %lu marks\n", r->name, (unsigned long)n, (unsigned long)CAP.errors, (unsigned long)CAP.nev);
}

int test_main(void) {
    out_open("aeg_koff.txt");
    for (int i = 0; i < NRAMP; i++) ramp[i] = (int16_t)(8 * i - 0x4000);
    ram_write(SA_RAMP, ramp, sizeof ramp);
    static const run_t runs[] = {
        {"koff_d2", 0, {{31, 31, 2, 26, 24, 15, 0, 0, 0}, {31, 31, 2, 24, 28, 15, 0, 0, 0}, {31, 31, 2, 28, 0, 15, 0, 0, 0}, WITNESS},
         2000, 4500, 55000, 3000, 16, 777},
        {"koff_d2b", 0, {{31, 31, 2, 30, 24, 15, 0, 0, 0}, {31, 31, 2, 26, 30, 15, 0, 0, 0}, {31, 31, 2, 0, 26, 15, 0, 0, 0}, WITNESS},
         1000, 2500, 55000, 3000, 16, 778},
        {"koff_d1", 0, {{31, 26, 31, 0, 24, 15, 0, 0, 0}, {31, 24, 31, 0, 28, 15, 0, 0, 0}, {31, 28, 31, 0, 26, 15, 0, 0, 0}, WITNESS},
         1000, 5000, 55000, 3000, 16, 781},
        {"koff_att", 0, {{24, 0, 0, 0, 28, 15, 0, 0, 0}, {26, 0, 0, 0, 24, 15, 0, 0, 0}, {28, 0, 0, 0, 26, 15, 0, 0, 0}, WITNESS},
         200, 300, 55000, 3000, 16, 779},
        {"kon_rel", 1, {{24, 0, 0, 0, 24, 15, 0, 0, 0}, {24, 0, 0, 0, 24, 15, 0, 0, 1}, {31, 0, 0, 0, 24, 15, 0, 0, 1}, WITNESS},
         5000, 8000, 60000, 0, 16, 780},
    };
    for (unsigned i = 0; i < sizeof runs / sizeof runs[0]; i++) run(&runs[i]);
    aica_quiet();
    out_close();
    return 0;
}
