/* eg_latch.c -- which envelope registers reach the envelope generators one sample late?  tests/slot_tail tail_c
 * showed that RR rewritten on a clock sample takes effect on the NEXT clock while the SA rewrite of the same write
 * pair took effect on that sample: register writes reach the sample fetch on the sample after the write (like key
 * events) but the envelope generator one sample later still.  The model (src/aica_model.cpp Slot::egreg / eg_latch)
 * latches r10 (AR/D1R/D2R), r14 (RR/DL/KRS/LPSLNK), r18 (OCT/FNS), r40/r44 (FEG rates) one sample late and reads the
 * FLV targets (0x2C..0x3C) live.  Only RR is measured.  This case measures DL, KRS, AR, D2R, FD1R/FD2R and FLV3.
 *
 * Method: every register rewrite is paired with a WITNESS key-on (the aeg_koff device): the register write and the
 * witness's own reg 0x00 write with KYONB | KYONEX are two G2 writes ~2.4 us apart, almost always inside one 22.7 us
 * sample, so the witness onset E (its level jumps to 520176, a = 0, on the sample the key event takes effect) marks the
 * sample on which a key event from that write pair takes effect.  The question per rewrite: does its effect show at
 * clock E ("live", fetch-like timing) or at clock E + 2 ("latched", the model's egreg)?  Only an EVEN E (a clock
 * sample) is informative: on an odd E both readings step at E + 1.  The write ORDER alternates per cycle (even cycles:
 * register then KYONEX; odd cycles: KYONEX then register) because a sample boundary between the two writes (~10 % of
 * the bursts) makes a latched register look live in the first order and a live register look latched in the second:
 * the order that gives a 100 % consistent verdict tells the truth, the other order shows the ~10 % minority.
 *
 * Layout: slots 0..2 = test slots (buses 0..2), slots 3, 4, 5 = three witnesses all on bus 3 (AR 31 KRS 1 -> R 63,
 * D1R 31 DL 31 D2R 31 RR 31: each decays to "off" by itself within 5.8 ms; witness j serves event j of a cycle;
 * events are >= 2.6 ms apart so the previous witness's residual on bus 3 is < 4112 and the 20-bit sum does not wrap).
 * Every cycle: KYONEX keys the test slots on and all three witnesses off (a slot that reached off in decay 2 cannot be
 * keyed on again without a key-off, tests/sgc_keys K4); three rewrite events, each with its own witness; the test
 * slots are keyed off (KYONEX, every KYONB 0), the rewritten registers are restored, and the slots release to off
 * (RR 31) before the next cycle.  Test slots keep KYONB = 1 through the events: a KYONEX on a slot already keyed on
 * does nothing (the model; tests/sgc_keys K2).  16 cycles per run, event times from an LCG so E takes both parities.
 *
 * Runs (KRS 15, OCT 0, FNS 0 on the test slots: R = 2 * rate, constant increment rows except the R < 48 attacks of ar):
 *   dl        constant 0x7FFF (VOFF 0, LPOFF 1): AR 31, D1R 28 / 26 / 24 (+4 / +2 / +1 per clock in decay 1 from 0),
 *             DL 31, D2R 0 (decay 2 = hold).  Event = reg 0x14 with DL 13 (slot 2, 0), DL 17 (slot 1), KRS / RR kept,
 *             written while a[9:5] equals the new DL (the AEG monitor is polled, then a random spin of up to 200 /
 *             450 / 1000 us): the decay 1 -> decay 2 compare (after the clock's step) becomes true at clock E (live: the
 *             level holds from E) or E + 2 (latched: one more +inc step, then hold).  Events at ~5.1 / 12.7 / 19.3 ms.
 *   krs       AR 31, D1R 24 (+1), DL 31, D2R 0; reg 0x14 rewritten with KRS 2 / 4 / 6 (s = 4 / 8 / 12: R 52 / 56 / 60 =
 *             +2 / +4 / +8 per clock) on slots 2 / 0 / 1 at 4 / 10 / 16 ms (+ rand 2 ms).
 *   ar        AR 24 (R 48, inc 1) -> AR 28 (inc 4) on slot 0 at 0.6..1.5 ms; AR 20 (R 40: inc 1 every 4th clock) -> AR 30
 *             (inc 8) on slot 1 at 6..9 ms; AR 18 (R 36: inc 1 every 8th clock) -> AR 31 (R 62, inc 8) on slot 2 at
 *             13..17 ms (reg 0x10, D1R / D2R kept).  D1R 24 follows (+1 per clock from 0) so a one-clock offset of the
 *             attack's end stays visible.  Slots 1 / 2 use R < 48 rows: the checker needs the boot's K.
 *   d2r       AR 31, D1R 31, DL 2 (decay 2 from a = 0x40), D2R 26 / 24 / 24 on slots 2 / 0 / 1 rewritten (reg 0x10) to
 *             D2R 30 / 28 / 30 (+2 -> +8, +1 -> +4, +1 -> +8) at 4 / 10 / 16 ms (+ rand 2 ms).
 *   feg_rate  FEG harness (random PCM16 at SA_SIG looped [0, 8192), VOFF 1, LPOFF 0, Q 4; AEG AR 31 D1R 0: a = 0 while
 *             keyed on, RR 31): slots 0 / 1: FLV 1A00 1A00 1A00 0800 1FF8, FAR 31 FD1R 31 FD2R 24 (decay 2 down at
 *             -1 per clock after a 2-clock preamble) -> reg 0x44 with FD2R 28 (-4) / 30 (-8) at 4 / 11 ms (+ rand
 *             2.5 ms); slot 2: FLV 1A00 1A00 0800 0800 1FF8, FD1R 24 (decay 1 down -1) -> reg 0x40 with FD1R 28 (-4) at
 *             18 ms.  u = v >> 1 is recovered through the bit-exact filter; the two readings differ by 3 in v from E on.
 *   flv       FEG harness, FLV 1800 1800 1800 1C00 1FF8, FAR 31 FD1R 31 FRR 31, decay 2 UP toward FLV3 at FD2R 30 / 26 /
 *             28 (+8 / +2 / +4 per clock) on slots 0 / 1 / 2.  Event: the FEG monitor is polled until v >= a random
 *             threshold (0x1880.. / 0x1B00.. / 0x1A80..), then reg 0x38 (FLV3) := v_mon + 5 / 2 / 3 so that the next
 *             step would flip C = (v >= FLV3): with the target read live the hold begins at clock E (u constant);
 *             latched, the clock E steps past the new target and the segment then runs away upward (C stays true) to
 *             0x1FFF.  When a clock fell between the monitor read and E the target is at or below v (both readings run
 *             away: uninformative); the checker recovers v exactly and reports whether the target was in (v_E, v_E + inc].
 * Marks: 1 after the cycle's key-on write, 2 / 3 / 4 after event 0 / 1 / 2 (head estimates, search windows only:
 * 16 x 4 = CAP_MAXEV).  Text: per run the stream table, per event "<run> cyc c ev j: slot k reg xx old xxxx new xxxx
 * order o mon xxxx ..." (the checker parses these), the cap_start line (c0), "<run>: N samples, errors E, M marks".
 * Analysis: tools/latch_check.cpp (build/tools/latch_check tests/eg_latch/hw [-K 6491]).  Output: eg_latch.txt,
 * <run>.hdr/.bin, input.bin. */
#include "cap.h"
#define NS 4
#define NTEST 3
#define NWIT 3
#define NSLOT (NTEST + NWIT)
#define MAXV (2u << 20)
#define SA_CONST 0x10000u
#define SA_SIG 0x20000u
#define NSIG 8192
#define NCYC 16
#define TEST_MASK 0x7
static int32_t capbuf[MAXV];
static int16_t sig[NSIG];
static const int mixs[NS] = {0, 1, 2, 3};
static uint16_t r0[NSLOT]; /* reg 0x00 image of every slot without KYONB / KYONEX */

typedef struct { int AR, D1R, DL, D2R, RR, KRS; uint16_t flv[5]; int FAR, FD1R, FD2R, FRR; } tcfg_t;
enum { F_AR, F_D1R, F_D2R, F_DL, F_KRS, F_FD1R, F_FD2R, F_FLV3 };
enum { TR_TIME, TR_AEG, TR_FEG };
static const char *fname[] = {"AR", "D1R", "D2R", "DL", "KRS", "FD1R", "FD2R", "FLV3"};
static const char *tname[] = {"time", "aegmon", "fegmon"};
/* TR_TIME: burst at t_on + t_base + rand % t_rand us, the field := val.
 * TR_AEG: from t_on + t_base - 1500 us the slot's AEG monitor is polled until a[9:5] == val (the new DL), then a spin of
 *         rand % t_rand us; the field (DL) := val.  Missed when a[9:5] has passed val or 6 ms after t_base.
 * TR_FEG: the slot's FEG monitor is polled until v >= t_base + rand % t_rand (FEG values); FLV3 := v + val. */
typedef struct { int slot, field, trig; uint32_t t_base, t_rand, val; } ev_t;
typedef struct { const char *name; int feg; tcfg_t s[NTEST]; ev_t ev[NTEST]; uint32_t koff_us, cycle_us, seed; } run_t;

#define AEG_(AR, D1R, DL, D2R) {AR, D1R, DL, D2R, 31, 15, {0x1FF8, 0x1FF8, 0x1FF8, 0x1FF8, 0x1FF8}, 31, 31, 31, 31}
#define FEG_(f0, f1, f2, f3, FAR, FD1R, FD2R) {31, 0, 0, 0, 31, 15, {f0, f1, f2, f3, 0x1FF8}, FAR, FD1R, FD2R, 31}
static const run_t runs[] = {
    {"dl", 0, {AEG_(31, 24, 31, 0), AEG_(31, 26, 31, 0), AEG_(31, 28, 31, 0)},
     {{2, F_DL, TR_AEG, 5100, 200, 13}, {1, F_DL, TR_AEG, 12700, 450, 17}, {0, F_DL, TR_AEG, 19300, 1000, 13}}, 24000, 34000, 901},
    {"krs", 0, {AEG_(31, 24, 31, 0), AEG_(31, 24, 31, 0), AEG_(31, 24, 31, 0)},
     {{2, F_KRS, TR_TIME, 4000, 2000, 2}, {0, F_KRS, TR_TIME, 10000, 2000, 4}, {1, F_KRS, TR_TIME, 16000, 2000, 6}}, 22000, 32000, 902},
    {"ar", 0, {AEG_(24, 24, 31, 0), AEG_(20, 24, 31, 0), AEG_(18, 24, 31, 0)},
     {{0, F_AR, TR_TIME, 600, 900, 28}, {1, F_AR, TR_TIME, 6000, 3000, 30}, {2, F_AR, TR_TIME, 13000, 4000, 31}}, 22000, 32000, 903},
    {"d2r", 0, {AEG_(31, 31, 2, 24), AEG_(31, 31, 2, 24), AEG_(31, 31, 2, 26)},
     {{2, F_D2R, TR_TIME, 4000, 2000, 30}, {0, F_D2R, TR_TIME, 10000, 2000, 28}, {1, F_D2R, TR_TIME, 16000, 2000, 30}}, 22000, 32000, 904},
    {"feg_rate", 1, {FEG_(0x1A00, 0x1A00, 0x1A00, 0x0800, 31, 31, 24), FEG_(0x1A00, 0x1A00, 0x1A00, 0x0800, 31, 31, 24), FEG_(0x1A00, 0x1A00, 0x0800, 0x0800, 31, 24, 31)},
     {{0, F_FD2R, TR_TIME, 4000, 2500, 28}, {1, F_FD2R, TR_TIME, 11000, 2500, 30}, {2, F_FD1R, TR_TIME, 18000, 2500, 28}}, 25000, 40000, 905},
    {"flv", 1, {FEG_(0x1800, 0x1800, 0x1800, 0x1C00, 31, 31, 30), FEG_(0x1800, 0x1800, 0x1800, 0x1C00, 31, 31, 26), FEG_(0x1800, 0x1800, 0x1800, 0x1C00, 31, 31, 28)},
     {{0, F_FLV3, TR_FEG, 0x1880, 0x300, 5}, {2, F_FLV3, TR_FEG, 0x1A80, 0x100, 3}, {1, F_FLV3, TR_FEG, 0x1B00, 0xC0, 2}}, 26000, 40000, 906},
};

static uint32_t lcg(uint32_t *seed) { *seed = *seed * 1103515245u + 12345u; return *seed >> 16; }
static uint32_t field_get(const tcfg_t *c, int f) {
    switch (f) { case F_AR: return c->AR; case F_D1R: return c->D1R; case F_D2R: return c->D2R; case F_DL: return c->DL;
                 case F_KRS: return c->KRS; case F_FD1R: return c->FD1R; case F_FD2R: return c->FD2R; default: return c->flv[3]; }
}
/* the register word (and offset) of the slot's configuration with field f := val */
static uint16_t reg_of(const tcfg_t *c, int f, uint32_t val, int *off) {
    tcfg_t t = *c;
    switch (f) { case F_AR: t.AR = val; break; case F_D1R: t.D1R = val; break; case F_D2R: t.D2R = val; break; case F_DL: t.DL = val; break;
                 case F_KRS: t.KRS = val; break; case F_FD1R: t.FD1R = val; break; case F_FD2R: t.FD2R = val; break; default: t.flv[3] = val; break; }
    switch (f) {
    case F_AR: case F_D1R: case F_D2R: *off = 0x10; return (t.D2R << 11) | (t.D1R << 6) | t.AR;
    case F_DL: case F_KRS: *off = 0x14; return (t.KRS << 10) | (t.DL << 5) | t.RR;
    case F_FD1R: *off = 0x40; return (t.FAR << 8) | t.FD1R;
    case F_FD2R: *off = 0x44; return (t.FD2R << 8) | t.FRR;
    default: *off = 0x38; return t.flv[3] & 0x1FFF;
    }
}
/* KYONB of slot k = bit k of mask (test slots and witnesses), then one KYONEX through slot 0's register */
static void keyset(int mask) {
    for (int k = 0; k < NSLOT; k++) aw(CH(k, 0), r0[k] | (((mask >> k) & 1) ? 0x4000 : 0));
    aw(CH(0, 0), r0[0] | ((mask & 1) ? 0x4000 : 0) | 0x8000);
}
/* the event burst: the register write and the witness's KYONB | KYONEX write (one write: KYONEX applies every KYONB) */
static void burst(int slot, int off, uint16_t val, int w, int order) {
    if (order == 0) { aw(CH(slot, off), val); aw(CH(w, 0), r0[w] | 0xC000); }
    else { aw(CH(w, 0), r0[w] | 0xC000); aw(CH(slot, off), val); }
}
static uint32_t mon(int ch, int afsel) { aw(R_MSLC, (afsel << 14) | (ch << 8)); return ar(R_EGMON) & 0x1FFF; }
static void wait_until(uint64_t t) { while (now_us() < t) cap_poll(); }

static void run(const run_t *r) {
    aica_quiet();
    for (int k = 0; k < NSLOT; k++) {
        slot_cfg_t c;
        if (k < NTEST) {
            const tcfg_t *s = &r->s[k];
            slot_cfg_default(&c, r->feg ? SA_SIG : SA_CONST, r->feg ? NSIG : 32);
            c.ISEL = k; c.AR = s->AR; c.D1R = s->D1R; c.DL = s->DL; c.D2R = s->D2R; c.RR = s->RR; c.KRS = s->KRS;
            if (r->feg) { c.VOFF = 1; c.LPOFF = 0; c.Q = 4; }
            for (int j = 0; j < 5; j++) c.FLV[j] = s->flv[j];
            c.FAR = s->FAR; c.FD1R = s->FD1R; c.FD2R = s->FD2R; c.FRR = s->FRR;
            slot_write(k, &c);
            LOG("%s stream %d: slot %d role test AR %d D1R %d DL %d D2R %d RR %d KRS %d OCT %d FNS %03x VOFF %d LPOFF %d Q %d FLV %04x %04x %04x %04x %04x FAR %d FD1R %d FD2R %d FRR %d\n",
                r->name, k, k, c.AR, c.D1R, c.DL, c.D2R, c.RR, c.KRS, c.OCT, c.FNS, c.VOFF, c.LPOFF, c.Q, c.FLV[0], c.FLV[1], c.FLV[2], c.FLV[3], c.FLV[4], c.FAR, c.FD1R, c.FD2R, c.FRR);
        } else {
            slot_cfg_default(&c, SA_CONST, 32);
            c.ISEL = 3; c.AR = 31; c.D1R = 31; c.DL = 31; c.D2R = 31; c.RR = 31;
            c.KRS = 1; c.OCT = 0; c.FNS = 0; /* k = 1, s = 2: R 63 everywhere at pitch 1.0 */
            slot_write(k, &c);
            if (k == NTEST) LOG("%s stream 3: slots 3 4 5 role witness AR %d D1R %d DL %d D2R %d RR %d KRS %d OCT %d FNS %03x (witness j keys on with event j)\n",
                                r->name, c.AR, c.D1R, c.DL, c.D2R, c.RR, c.KRS, c.OCT, c.FNS);
        }
        r0[k] = ar(CH(k, 0)) & 0x3FFF;
    }
    LOG("%s run: mode %s cycles %d koff_us %lu cycle_us %lu", r->name, r->feg ? "feg" : "aeg", NCYC, (unsigned long)r->koff_us, (unsigned long)r->cycle_us);
    for (int j = 0; j < NTEST; j++) {
        const ev_t *e = &r->ev[j];
        LOG(" | ev%d slot %d %s trig %s %lu+%lu val %lu", j, e->slot, fname[e->field], tname[e->trig], (unsigned long)e->t_base, (unsigned long)e->t_rand, (unsigned long)e->val);
    }
    LOG("\n");
    if (cap_start(NS, mixs, capbuf, MAXV)) { OUT("%s: cap_start failed\n", r->name); return; }
    cap_wait_us(10000);
    uint32_t seed = r->seed;
    for (int cyc = 0; cyc < NCYC; cyc++) {
        int order = cyc & 1;
        keyset(TEST_MASK);        /* test slots on, every witness off (fresh for its next key-on) */
        uint64_t t_on = now_us();
        cap_mark(1);
        int offs[NTEST]; uint16_t olds[NTEST];
        for (int j = 0; j < NTEST; j++) {
            const ev_t *e = &r->ev[j];
            const tcfg_t *c = &r->s[e->slot];
            int w = NTEST + j, missed = 0, off;
            uint32_t rnd = lcg(&seed), monv = 0, newval = e->val, spin = 0, a = 0;
            uint64_t tgt = 0;
            if (e->trig == TR_TIME) {
                tgt = t_on + e->t_base + rnd % e->t_rand;
                wait_until(tgt);
                spin = lcg(&seed) % 46; /* the wait ends phase-locked to a ring block read: decorrelate from the sample clock */
                spin_us(spin);
            } else if (e->trig == TR_AEG) {
                tgt = t_on + e->t_base;
                wait_until(tgt - 1500);
                for (;;) {
                    a = mon(e->slot, 0);
                    if ((a >> 5) == e->val) break;
                    if ((a >> 5) > e->val || now_us() > tgt + 6000) { missed = 1; break; }
                    if ((a >> 5) + 1 < e->val) cap_poll(); /* far from the window: keep the ring reader going */
                }
                monv = a;
                spin = rnd % e->t_rand;
                if (!missed) spin_us(spin);
            } else {
                uint32_t vtrig = e->t_base + rnd % e->t_rand;
                wait_until(t_on + 200); /* the key-on (FLV0 load) takes effect on the next sample: do not read the previous cycle's release value */
                for (;;) {
                    a = mon(e->slot, 1);
                    if (a >= vtrig) break;
                    if (now_us() > t_on + r->koff_us - 2000) { missed = 1; break; }
                    if (a + 0x100 < vtrig) cap_poll();
                }
                /* the threshold is crossed right after a clock step: a random spin of up to two samples spreads the burst
                 * over the clock phase, then v is read again so the target sits just above the value the EG holds */
                spin = lcg(&seed) % 46;
                spin_us(spin);
                a = mon(e->slot, 1);
                monv = a;
                newval = a + e->val;
                if (newval > 0x1FFF) newval = 0x1FFF;
                tgt = t_on;
            }
            olds[j] = reg_of(c, e->field, field_get(c, e->field), &offs[j]);
            uint16_t newreg = reg_of(c, e->field, newval, &off);
            uint64_t tb = now_us();
            if (!missed) burst(e->slot, off, newreg, w, order);
            cap_mark(2 + j);
            LOG("%s cyc %d ev %d: slot %d reg %02x old %04x new %04x order %d mon %04x field %s %lu->%lu rnd %lu spin %lu t_us %lu witness %d%s\n",
                r->name, cyc, j, e->slot, off, olds[j], newreg, order, (unsigned)monv, fname[e->field], (unsigned long)field_get(c, e->field), (unsigned long)newval,
                (unsigned long)rnd, (unsigned long)spin, (unsigned long)(tb - t_on), w, missed ? " MISSED (no burst)" : "");
        }
        wait_until(t_on + r->koff_us);
        keyset(0);                /* test slots off (release to off at RR 31), witnesses off */
        for (int j = 0; j < NTEST; j++) aw(CH(r->ev[j].slot, offs[j]), olds[j]); /* restore during the release (RR kept) */
        wait_until(t_on + r->cycle_us);
    }
    uint32_t n = cap_stop();
    cap_save(r->name, n);
    OUT("%s: %lu samples, errors %lu, %lu marks\n", r->name, (unsigned long)n, (unsigned long)CAP.errors, (unsigned long)CAP.nev);
}

int test_main(void) {
    out_open("eg_latch.txt");
    aica_quiet(); /* ARM7 in reset before the RAM is written */
    uint32_t seed = 4242;
    for (int i = 0; i < NSIG; i++) {
        seed = seed * 1103515245u + 12345u;
        sig[i] = (int16_t)(seed >> 16);
        if (!sig[i]) sig[i] = 1;
    }
    ram_write(SA_SIG, sig, sizeof sig);
    out_bin("input.bin", sig, sizeof sig);
    for (int i = 0; i < 24; i++) ram_w32(SA_CONST + 4 * i, 0x7FFF7FFF); /* the constant, loop [0,32) */
    for (unsigned i = 0; i < sizeof runs / sizeof runs[0]; i++) run(&runs[i]);
    aica_quiet();
    out_close();
    return 0;
}
