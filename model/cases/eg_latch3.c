/* eg_latch3.c -- what delayed tests/slot_tail tail_c's RR rewrite by one clock?  eg_latch / eg_latch2 (console) showed:
 * every envelope register acts LIVE (at the clock of the sample the write pair's key event takes effect on), a rate going
 * 0 -> nonzero does not arm (rr0, d2r0 live), a redundant key-off in the write group changes nothing (rr0_koff, rekoff24
 * live).  What tail_c's group changed and rr0_koff did not: reg 0x00 := 0 moved SA[22:16] 0x20000 -> 0 and cleared LPCTL
 * (KYONB already 0, PCMS / SSCTL unchanged); the fetch switched to the new SA on the effect sample 11300 and the RR 31
 * release stepped only from 11302.  Hypothesis: a write that changes the stream address (or LPCTL / the start registers)
 * restarts the fetch path and, like a key-on sample, blocks the envelope step on its effect sample.
 *
 * Same harness as eg_latch2 rr0: constant 0x7FFF test slots 0..2 (buses 0..2), three witnesses 3, 4, 5 on bus 3, 16
 * cycles, AR 31 D1R 24 DL 4 -> decay 2 holds at a = 0x80 (level 130032), key-off at 10 ms with RR 0 (the release holds
 * at 0x80), events at 12 / 16 / 20 ms (+ rand 1.5 ms, spin 0..45 us) = reg 0x14 RR 0 -> 30 (+8 per clock) + the PROBE
 * write + the witness's KYONB|KYONEX; write order alternates per cycle (order 0: RR, probe, KYONEX; order 1: probe,
 * KYONEX, RR).  The SAME constant fills 4224 words at 0x10000 and at 0x20000, LEA 4096 (loop [0, 4096): never reached
 * inside a 31 ms cycle, so LPCTL 0 has no loop-end effect either), so every probe keeps the level law valid.  Runs:
 *   none    no probe (the eg_latch2 rr0 control)
 *   sa_hi   reg 0x00 with SA[22:16] 1 -> 2 (0x10000 -> 0x20000), KYONB 0, LPCTL / PCMS / SSCTL kept
 *   sa_lo   reg 0x04 SA[15:0] 0x0000 -> 0x0040 (32 samples into the block; LSA / LEA unchanged)
 *   lpctl   reg 0x00 with LPCTL 1 -> 0 only
 *   lea     reg 0x0C LEA 4096 -> 4160
 * (the optional sa_hi + KYONB 1 variant is the known key-on-during-release case and is not run.)  Live (no skip): the
 * first +8 at clock E; a blocked step on the effect sample: at E + 2 (the checker's "latched" pattern).
 * CA restart evidence: the CA monitor (0x2814, MSLC = the slot) is read just before the group, right after it (~10 us,
 * before the effect sample) and 250 us later (~11 samples after it): with the fetch running since the key-on CA is in
 * the hundreds (12..21 ms = 530..950 samples) and grows by ~11; a restart shows CA < 20 in the third read.  The reads are
 * logged per event ("ca a b c").  The rewritten registers are restored at 29 ms (every slot is off by then: +8 from 0x80
 * reaches 0x3C0 in 104 clocks = 4.7 ms).  Marks: 1 key-on, 2 / 3 / 4 after the events.
 * Text: "<run> run: mode aeg cycles 16 koff_us 10000 cycle_us 31000 koff_before 1 restore_us 29000 probe <kind> ..." then the
 * eg_latch stream lines and per event "<run> cyc c ev j: slot k reg 14 old xxxx new xxxx order o mon 0000 field RR 0->30
 * probe <reg|none> <val> ca a b c rnd .. spin .. t_us .. witness w".  Analysis: build/tools/latch_check tests/eg_latch3/hw.
 * Output: eg_latch3.txt, <run>.hdr/.bin. */
#include "cap.h"
#define NS 4
#define NTEST 3
#define NWIT 3
#define NSLOT (NTEST + NWIT)
#define MAXV (2u << 20)
#define SA_A 0x10000u
#define SA_B 0x20000u
#define NCONST 4224
#define LEA0 4096
#define NCYC 16
#define TEST_MASK 0x7
static int32_t capbuf[MAXV];
static const int mixs[NS] = {0, 1, 2, 3};
static uint16_t r0[NSLOT]; /* reg 0x00 image of every slot without KYONB / KYONEX */

enum { PK_NONE, PK_SAHI, PK_SALO, PK_LPCTL, PK_LEA };
static const char *pkname[] = {"none", "sa_hi", "sa_lo", "lpctl", "lea"};
typedef struct { const char *name; int probe; uint32_t seed; } run_t;
static const run_t runs[] = {
    {"none", PK_NONE, 921}, {"sa_hi", PK_SAHI, 922}, {"sa_lo", PK_SALO, 923}, {"lpctl", PK_LPCTL, 924}, {"lea", PK_LEA, 925},
};
static const uint32_t EV_T[NTEST] = {12000, 16000, 20000};
#define KOFF_US 10000
#define RESTORE_US 29000
#define CYCLE_US 31000

static uint32_t lcg(uint32_t *seed) { *seed = *seed * 1103515245u + 12345u; return *seed >> 16; }
static void keyset(int mask) {
    for (int k = 0; k < NSLOT; k++) aw(CH(k, 0), r0[k] | (((mask >> k) & 1) ? 0x4000 : 0));
    aw(CH(0, 0), r0[0] | ((mask & 1) ? 0x4000 : 0) | 0x8000);
}
/* the probe write of run kind pk on slot: register offset and value (and the value that restores it) */
static int probe_of(int pk, int slot, uint16_t *val, uint16_t *restore) {
    switch (pk) {
    case PK_SAHI: *val = (r0[slot] & ~0x7F) | ((SA_B >> 16) & 0x7F); *restore = r0[slot]; return 0x00;
    case PK_SALO: *val = 0x0040; *restore = SA_A & 0xFFFF; return 0x04;
    case PK_LPCTL: *val = r0[slot] & ~0x0200; *restore = r0[slot]; return 0x00;
    case PK_LEA: *val = LEA0 + 64; *restore = LEA0; return 0x0C;
    default: *val = 0; *restore = 0; return -1;
    }
}
/* the write group: RR, [probe], witness KYONB|KYONEX (order 0) / [probe], KYONEX, RR (order 1) */
static void burst(int slot, uint16_t rrval, int preg, uint16_t pval, int w, int order) {
    if (order == 0) { aw(CH(slot, 0x14), rrval); if (preg >= 0) aw(CH(slot, preg), pval); aw(CH(w, 0), r0[w] | 0xC000); }
    else { if (preg >= 0) aw(CH(slot, preg), pval); aw(CH(w, 0), r0[w] | 0xC000); aw(CH(slot, 0x14), rrval); }
}
static void wait_until(uint64_t t) { while (now_us() < t) cap_poll(); }

static void run(const run_t *r) {
    aica_quiet();
    for (int k = 0; k < NSLOT; k++) {
        slot_cfg_t c;
        slot_cfg_default(&c, SA_A, k < NTEST ? LEA0 : 32);
        if (k < NTEST) {
            c.ISEL = k; c.AR = 31; c.D1R = 24; c.DL = 4; c.D2R = 0; c.RR = 0; c.KRS = 15;
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
    LOG("%s run: mode aeg cycles %d koff_us %u cycle_us %u koff_before 1 restore_us %u probe %s SA %05lx/%05lx LEA %d", r->name, NCYC, KOFF_US, CYCLE_US, RESTORE_US, pkname[r->probe],
        (unsigned long)SA_A, (unsigned long)SA_B, LEA0);
    for (int j = 0; j < NTEST; j++) LOG(" | ev%d slot %d RR trig time %lu+1500 val 30 probe %s", j, j, (unsigned long)EV_T[j], pkname[r->probe]);
    LOG("\n");
    if (cap_start(NS, mixs, capbuf, MAXV)) { OUT("%s: cap_start failed\n", r->name); return; }
    cap_wait_us(10000);
    uint32_t seed = r->seed;
    const uint16_t rr_old = (15 << 10) | (4 << 5) | 0, rr_new = (15 << 10) | (4 << 5) | 30;
    for (int cyc = 0; cyc < NCYC; cyc++) {
        int order = cyc & 1;
        keyset(TEST_MASK);        /* test slots on (from the release), every witness off */
        uint64_t t_on = now_us();
        cap_mark(1);
        wait_until(t_on + KOFF_US);
        keyset(0);                /* key-off from the decay-2 hold: the release holds at 0x80 (RR 0) */
        int pregs[NTEST]; uint16_t prest[NTEST];
        for (int j = 0; j < NTEST; j++) {
            int slot = j, w = NTEST + j;
            uint16_t pval;
            pregs[j] = probe_of(r->probe, slot, &pval, &prest[j]);
            uint32_t rnd = lcg(&seed);
            wait_until(t_on + EV_T[j] + rnd % 1500);
            uint32_t spin = lcg(&seed) % 46;
            spin_us(spin);
            aw(R_MSLC, slot << 8);
            uint32_t ca0 = ar(R_CAMON) & 0xFFFF;
            uint64_t tb = now_us();
            burst(slot, rr_new, pregs[j], pval, w, order);
            uint32_t ca1 = ar(R_CAMON) & 0xFFFF;
            cap_mark(2 + j);
            spin_us(250);
            uint32_t ca2 = ar(R_CAMON) & 0xFFFF;
            if (pregs[j] >= 0)
                LOG("%s cyc %d ev %d: slot %d reg 14 old %04x new %04x order %d mon 0000 field RR 0->30 probe %02x %04x ca %lu %lu %lu rnd %lu spin %lu t_us %lu witness %d\n",
                    r->name, cyc, j, slot, rr_old, rr_new, order, pregs[j], pval, (unsigned long)ca0, (unsigned long)ca1, (unsigned long)ca2, (unsigned long)rnd, (unsigned long)spin, (unsigned long)(tb - t_on), w);
            else
                LOG("%s cyc %d ev %d: slot %d reg 14 old %04x new %04x order %d mon 0000 field RR 0->30 probe none 0000 ca %lu %lu %lu rnd %lu spin %lu t_us %lu witness %d\n",
                    r->name, cyc, j, slot, rr_old, rr_new, order, (unsigned long)ca0, (unsigned long)ca1, (unsigned long)ca2, (unsigned long)rnd, (unsigned long)spin, (unsigned long)(tb - t_on), w);
        }
        wait_until(t_on + RESTORE_US);
        for (int j = 0; j < NTEST; j++) { aw(CH(j, 0x14), rr_old); if (pregs[j] >= 0) aw(CH(j, pregs[j]), prest[j]); } /* restore (the slots are off) */
        wait_until(t_on + CYCLE_US);
    }
    uint32_t n = cap_stop();
    cap_save(r->name, n);
    OUT("%s: %lu samples, errors %lu, %lu marks\n", r->name, (unsigned long)n, (unsigned long)CAP.errors, (unsigned long)CAP.nev);
}

int test_main(void) {
    out_open("eg_latch3.txt");
    aica_quiet(); /* ARM7 in reset before the RAM is written */
    for (int i = 0; i < NCONST / 2; i++) { ram_w32(SA_A + 4 * i, 0x7FFF7FFF); ram_w32(SA_B + 4 * i, 0x7FFF7FFF); } /* the same constant at both bases */
    for (unsigned i = 0; i < sizeof runs / sizeof runs[0]; i++) run(&runs[i]);
    aica_quiet();
    out_close();
    return 0;
}
