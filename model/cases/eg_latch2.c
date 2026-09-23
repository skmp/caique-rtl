/* eg_latch2.c -- follow-up of eg_latch (console: DL, KRS, AR, D2R, FD1R/FD2R and FLV3 all act at the clock of the sample
 * the write pair's key event takes effect on, i.e. LIVE; no register latch).  What then delayed the RR rewrite of
 * tests/slot_tail tail_c by one clock (RR 0 -> 31 written in the same group as reg 0x00 := 0 (SA 0, KYONB 0) + KYONEX on
 * clock sample 11300: SA switched at 11300, the first +8 step only at 11302)?  Two candidates:
 *   H_rate0   a rate register going from 0 ("no change") to nonzero needs one extra clock before it steps (eg_latch only
 *             rewrote nonzero -> nonzero);
 *   H_rekoff  the KYONEX of that group delivered a (redundant) key-off to the already released slot and the key-off
 *             clock rule ("the step this clock would have taken without the key event") used the register value from
 *             before the write: a key event on the slot itself makes that clock use the previous sample's registers.
 * Same harness as eg_latch (constant 0x7FFF test slots 0..2 on buses 0..2, three witnesses 3, 4, 5 on bus 3, the write
 * pair register + witness KYONB|KYONEX, alternating write order per cycle, 16 cycles per run, time-triggered events with
 * a random spin), KRS 15 everywhere.  Every test slot: AR 31, D1R 24 (+1 per clock from a = 0), DL 4 -> decay 2 holds at
 * a = 0x80 (level 130032) with D2R 0.
 *   rr0       key-off at 10 ms (KYONEX, every KYONB 0): the release holds at 0x80 with RR 0.  Event j (slot j, 12 / 16 /
 *             20 ms + rand 1.5 ms): reg 0x14 := RR 30 (+8 per clock; DL 4, KRS 15 kept) + witness KYONEX, NO write to
 *             the slot's reg 0x00.  Live: first +8 at clock E; one extra clock (H_rate0): at E + 2.
 *   rr24      the same with RR 24 (release running at +1 from 0x80) rewritten to RR 30: nonzero -> nonzero, the control
 *             (eg_latch's krs / d2r / ar runs predict LIVE here).
 *   rr0_koff  rr0 with the tail_c group: the slot's own reg 0x00 rewritten with its current value (KYONB 0, SA / LPCTL
 *             kept: a redundant key-off) before the KYONEX that keys the witness on.  Order 0: reg 0x14, reg 0x00, KYONEX;
 *             order 1: reg 0x00, KYONEX, reg 0x14.  Does the redundant key-off delay the RR effect to E + 2 (H_rekoff)?
 *   rekoff24  release running at RR 24 (+1), reg 0x00 (KYONB 0) + KYONEX together with RR 24 -> 30: the redundant key-off
 *             alone (no rate-0 transition) -- H_rekoff predicts the +8 from E + 2, H_rate0 from E.
 *   d2r0      no key-off: decay 2 holding at 0x80 with D2R 0 while keyed on; reg 0x10 := D2R 28 (+4; AR / D1R kept) +
 *             witness KYONEX (the test slots' KYONB stays 1): rate 0 -> nonzero without any key event (H_rate0 alone).
 *             Events at 10 / 14 / 18 ms + rand 1.5 ms, key-off at 30 ms.
 * The rewritten registers are restored at restore_us (every rewritten slot is off by then: +8 from 0x80 reaches 0x3C0 in
 * 104 clocks = 4.7 ms; +4 in 9.4 ms), the next cycle keys on from the release.  Marks: 1 key-on, 2 / 3 / 4 after the
 * events.  Text: "<run> run: mode aeg cycles 16 koff_us N cycle_us M koff_before B restore_us R | ev..." then the
 * stream lines (eg_latch format) and per event "<run> cyc c ev j: slot k reg xx old xxxx new xxxx order o mon 0000 field F
 * a->b reg00 xxxx|none rnd .. spin .. t_us .. witness w".  Analysis: build/tools/latch_check tests/eg_latch2/hw [-K].
 * Model: the latch (egreg) makes every run read "one clock late" (Vc = E + 1); the model has no rate-0 special case and
 * ignores the redundant key-off.  Output: eg_latch2.txt, <run>.hdr/.bin. */
#include "cap.h"
#define NS 4
#define NTEST 3
#define NWIT 3
#define NSLOT (NTEST + NWIT)
#define MAXV (2u << 20)
#define SA_CONST 0x10000u
#define NCYC 16
#define TEST_MASK 0x7
static int32_t capbuf[MAXV];
static const int mixs[NS] = {0, 1, 2, 3};
static uint16_t r0[NSLOT]; /* reg 0x00 image of every slot without KYONB / KYONEX */

typedef struct { int AR, D1R, DL, D2R, RR; } tcfg_t;
enum { F_D2R, F_RR };
static const char *fname[] = {"D2R", "RR"};
/* burst at t_on + t_base + rand % t_rand us (+ a spin of rand % 46 us); field := val; reg00: the slot's reg 0x00 is
 * rewritten (KYONB 0, SA / LPCTL kept) before the KYONEX */
typedef struct { int slot, field; uint32_t t_base, t_rand, val; int reg00; } ev_t;
typedef struct { const char *name; tcfg_t s[NTEST]; ev_t ev[NTEST]; int koff_before; uint32_t koff_us, restore_us, cycle_us, seed; } run_t;

#define HOLD(RR) {31, 24, 4, 0, RR}
static const run_t runs[] = {
    {"rr0", {HOLD(0), HOLD(0), HOLD(0)}, {{0, F_RR, 12000, 1500, 30, 0}, {1, F_RR, 16000, 1500, 30, 0}, {2, F_RR, 20000, 1500, 30, 0}}, 1, 10000, 28000, 30000, 911},
    {"rr24", {HOLD(24), HOLD(24), HOLD(24)}, {{0, F_RR, 12000, 1500, 30, 0}, {1, F_RR, 16000, 1500, 30, 0}, {2, F_RR, 20000, 1500, 30, 0}}, 1, 10000, 28000, 30000, 912},
    {"rr0_koff", {HOLD(0), HOLD(0), HOLD(0)}, {{0, F_RR, 12000, 1500, 30, 1}, {1, F_RR, 16000, 1500, 30, 1}, {2, F_RR, 20000, 1500, 30, 1}}, 1, 10000, 28000, 30000, 913},
    {"rekoff24", {HOLD(24), HOLD(24), HOLD(24)}, {{0, F_RR, 12000, 1500, 30, 1}, {1, F_RR, 16000, 1500, 30, 1}, {2, F_RR, 20000, 1500, 30, 1}}, 1, 10000, 28000, 30000, 914},
    {"d2r0", {HOLD(31), HOLD(31), HOLD(31)}, {{0, F_D2R, 10000, 1500, 28, 0}, {1, F_D2R, 14000, 1500, 28, 0}, {2, F_D2R, 18000, 1500, 28, 0}}, 0, 30000, 30000, 36000, 915},
};

static uint32_t lcg(uint32_t *seed) { *seed = *seed * 1103515245u + 12345u; return *seed >> 16; }
static uint16_t reg_of(const tcfg_t *c, int f, uint32_t val, int *off) {
    tcfg_t t = *c;
    if (f == F_D2R) t.D2R = val; else t.RR = val;
    if (f == F_D2R) { *off = 0x10; return (t.D2R << 11) | (t.D1R << 6) | t.AR; }
    *off = 0x14; return (15 << 10) | (t.DL << 5) | t.RR;
}
static void keyset(int mask) {
    for (int k = 0; k < NSLOT; k++) aw(CH(k, 0), r0[k] | (((mask >> k) & 1) ? 0x4000 : 0));
    aw(CH(0, 0), r0[0] | ((mask & 1) ? 0x4000 : 0) | 0x8000);
}
/* the event burst; reg00: the slot's reg 0x00 (KYONB 0) is written before the KYONEX in both orders (the tail_c group) */
static void burst(int slot, int off, uint16_t val, int w, int order, int reg00) {
    if (order == 0) { aw(CH(slot, off), val); if (reg00) aw(CH(slot, 0), r0[slot]); aw(CH(w, 0), r0[w] | 0xC000); }
    else { if (reg00) aw(CH(slot, 0), r0[slot]); aw(CH(w, 0), r0[w] | 0xC000); aw(CH(slot, off), val); }
}
static void wait_until(uint64_t t) { while (now_us() < t) cap_poll(); }

static void run(const run_t *r) {
    aica_quiet();
    for (int k = 0; k < NSLOT; k++) {
        slot_cfg_t c;
        slot_cfg_default(&c, SA_CONST, 32);
        if (k < NTEST) {
            const tcfg_t *s = &r->s[k];
            c.ISEL = k; c.AR = s->AR; c.D1R = s->D1R; c.DL = s->DL; c.D2R = s->D2R; c.RR = s->RR; c.KRS = 15;
            slot_write(k, &c);
            LOG("%s stream %d: slot %d role test AR %d D1R %d DL %d D2R %d RR %d KRS %d OCT %d FNS %03x VOFF %d LPOFF %d Q %d FLV %04x %04x %04x %04x %04x FAR %d FD1R %d FD2R %d FRR %d\n",
                r->name, k, k, c.AR, c.D1R, c.DL, c.D2R, c.RR, c.KRS, c.OCT, c.FNS, c.VOFF, c.LPOFF, c.Q, c.FLV[0], c.FLV[1], c.FLV[2], c.FLV[3], c.FLV[4], c.FAR, c.FD1R, c.FD2R, c.FRR);
        } else {
            c.ISEL = 3; c.AR = 31; c.D1R = 31; c.DL = 31; c.D2R = 31; c.RR = 31;
            c.KRS = 1; c.OCT = 0; c.FNS = 0; /* k = 1, s = 2: R 63 everywhere at pitch 1.0 */
            slot_write(k, &c);
            if (k == NTEST) LOG("%s stream 3: slots 3 4 5 role witness AR %d D1R %d DL %d D2R %d RR %d KRS %d OCT %d FNS %03x (witness j keys on with event j)\n",
                                r->name, c.AR, c.D1R, c.DL, c.D2R, c.RR, c.KRS, c.OCT, c.FNS);
        }
        r0[k] = ar(CH(k, 0)) & 0x3FFF;
    }
    LOG("%s run: mode aeg cycles %d koff_us %lu cycle_us %lu koff_before %d restore_us %lu", r->name, NCYC, (unsigned long)r->koff_us, (unsigned long)r->cycle_us, r->koff_before, (unsigned long)r->restore_us);
    for (int j = 0; j < NTEST; j++) {
        const ev_t *e = &r->ev[j];
        LOG(" | ev%d slot %d %s trig time %lu+%lu val %lu reg00 %d", j, e->slot, fname[e->field], (unsigned long)e->t_base, (unsigned long)e->t_rand, (unsigned long)e->val, e->reg00);
    }
    LOG("\n");
    if (cap_start(NS, mixs, capbuf, MAXV)) { OUT("%s: cap_start failed\n", r->name); return; }
    cap_wait_us(10000);
    uint32_t seed = r->seed;
    for (int cyc = 0; cyc < NCYC; cyc++) {
        int order = cyc & 1;
        keyset(TEST_MASK);        /* test slots on (from the release), every witness off */
        uint64_t t_on = now_us();
        cap_mark(1);
        if (r->koff_before) { wait_until(t_on + r->koff_us); keyset(0); } /* key-off from the decay-2 hold: the release holds (RR 0) or runs (RR 24) */
        int offs[NTEST]; uint16_t olds[NTEST];
        for (int j = 0; j < NTEST; j++) {
            const ev_t *e = &r->ev[j];
            const tcfg_t *c = &r->s[e->slot];
            int w = NTEST + j, off;
            uint32_t rnd = lcg(&seed), spin;
            wait_until(t_on + e->t_base + rnd % e->t_rand);
            spin = lcg(&seed) % 46; /* the wait ends phase-locked to a ring block read: decorrelate from the sample clock */
            spin_us(spin);
            olds[j] = reg_of(c, e->field, e->field == F_D2R ? (uint32_t)c->D2R : (uint32_t)c->RR, &offs[j]);
            uint16_t newreg = reg_of(c, e->field, e->val, &off);
            uint64_t tb = now_us();
            burst(e->slot, off, newreg, w, order, e->reg00);
            cap_mark(2 + j);
            LOG("%s cyc %d ev %d: slot %d reg %02x old %04x new %04x order %d mon 0000 field %s %lu->%lu reg00 %s rnd %lu spin %lu t_us %lu witness %d\n",
                r->name, cyc, j, e->slot, off, olds[j], newreg, order, fname[e->field], (unsigned long)(e->field == F_D2R ? c->D2R : c->RR), (unsigned long)e->val,
                e->reg00 ? "written" : "none", (unsigned long)rnd, (unsigned long)spin, (unsigned long)(tb - t_on), w);
        }
        wait_until(t_on + r->restore_us);
        if (!r->koff_before) { wait_until(t_on + r->koff_us); keyset(0); }
        for (int j = 0; j < NTEST; j++) aw(CH(r->ev[j].slot, offs[j]), olds[j]); /* restore (the slots are off) */
        wait_until(t_on + r->cycle_us);
    }
    uint32_t n = cap_stop();
    cap_save(r->name, n);
    OUT("%s: %lu samples, errors %lu, %lu marks\n", r->name, (unsigned long)n, (unsigned long)CAP.errors, (unsigned long)CAP.nev);
}

int test_main(void) {
    out_open("eg_latch2.txt");
    aica_quiet(); /* ARM7 in reset before the RAM is written */
    for (int i = 0; i < 24; i++) ram_w32(SA_CONST + 4 * i, 0x7FFF7FFF); /* the constant, loop [0,32) */
    for (unsigned i = 0; i < sizeof runs / sizeof runs[0]; i++) run(&runs[i]);
    aica_quiet();
    out_close();
    return 0;
}
