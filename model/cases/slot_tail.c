/* slot_tail.c -- what a slot sends after its amplitude envelope reaches "off" (release past 0x3FF: the slot stops,
 * CA reads 0), through every output path, and the value a bus retains once the slot stops sending (IMXL 0).
 * Open item (NOTES "Open items"): the stopped slot 2 of tests/eg_lock mixs left -8 on MIXS2 (the model leaves 0);
 * the residual a slot sends after its release -- the filter tail through the VOFF path -- was never compared.
 * Harness = eg_lock feg_run(): 8192-word pseudo-random PCM16 (LCG seed 4242, s = (int16)(seed >> 16), 0 -> 1) at
 * 0x20000 looped [0, 8192), 4 slots ISEL k -> MIXS k, slot 3 = unfiltered reference (VOFF 1, LPOFF 1, FLV 0x1FFE,
 * FEG rates 0, AR 31, RR 0: never off, keeps playing, gives the key-on sample).  Slots 0..2 are keyed off together.
 * A second known signal (seed 4243) sits at wave RAM 0 (zeros to 0x8000): tail_c rewrites reg 0x00 to 0, which
 * moves SA to 0 and clears LPCTL, so what the slot then plays must be known on both platforms.
 *   tail_a  VOFF 1: slot 0 LPOFF 0 Q 4 FLV 0x1B00 (FEG rates 0: constant cutoff) RR 31 (R 62: +8/clock, off 128
 *           clocks after the key-off); slot 1 the same with RR 24 (R 48: +1/clock, off after ~2046 samples);
 *           slot 2 LPOFF 1 (no filter) RR 31: is the output exactly 0 from "off" on?
 *   tail_b  slot 0 VOFF 0 LPOFF 0 Q 4 FLV 0x1B00 RR 31 (level applied after the filter); slot 1 VOFF 0 LPOFF 1
 *           RR 31; slot 2 VOFF 1 LPOFF 0 Q 0 FLV 0x1FFE (unity cutoff: the zero-input rest is a limit cycle) RR 31.
 *   tail_c  the eg_lock sequence that left -8: slots 0..2 = feg_odd program A (VOFF 1, LPOFF 0, Q 4,
 *           FLV 1800 1C00 1800 1A00 1C00, FAR 24 FD1R 26 FD2R 28 FRR 22, KRS 0, OCT 0, FNS 0x200, AR 31) with
 *           RR 0 / 31 / 24 (R -- / 63 / 49 releases; slot 0's AEG never releases until the RR rewrite).  Timeline:
 *           key-on all; 150 ms; mark 3; key-off 0..2; mark 4; 100 ms; mark 5; reg 0x14 = 0x001F (RR 31 KRS 0 DL 0)
 *           and reg 0x00 = 0 (KYONB 0, SA 0, LPCTL 0) on slots 2, 1, 0, then KYONEX (as aica_quiet does); mark 6;
 *           20 ms; mark 7; reg 0x20 = 0 (IMXL 0) on slots 0..2: the buses retain their last value; mark 8; 50 ms.
 *           The MIXS0..3 register readbacks after the IMXL write and at the end are logged.
 * Every run: aica_quiet; configure; cap_start; 5 ms; mark 1; key-on all 4; mark 2; wait; mark 3; key-off 0..2;
 * mark 4; wait; cap_stop; save.  Output: slot_tail.txt, tail_<x>.hdr/.bin, input.bin, input2.bin.
 * Compare with work/tail/tail_cmp.cpp (ring-locked replay through the model, events searched in the mark windows). */
#include "cap.h"
#define NS 4
#define MAXV (1u << 20)
#define SA_SIG 0x20000u
#define SA_SIG2 0x0u
#define NSIG 8192
static int32_t capbuf[MAXV];
static int16_t sig[NSIG], sig2[NSIG];
static const int mixs[NS] = {0, 1, 2, 3};

typedef struct { int VOFF, LPOFF, Q; uint16_t flv[5]; int FAR, FD1R, FD2R, FRR; int KRS, OCT, FNS, AR, RR; } tcfg_t;
typedef struct { const char *name; int extra; tcfg_t s[NS]; } run_t;

#define FLV5(v) {v, v, v, v, v}
#define PROG_A {0x1800, 0x1C00, 0x1800, 0x1A00, 0x1C00}
/* reference slot: unfiltered, VOFF, never released (RR 0), FEG frozen at unity */
#define REF(fns) {1, 1, 4, FLV5(0x1FFE), 0, 0, 0, 0, 15, 0, fns, 31, 0}
static const run_t runs[] = {
    {"tail_a", 0, {
        {1, 0, 4, FLV5(0x1B00), 0, 0, 0, 0, 15, 0, 0, 31, 31},
        {1, 0, 4, FLV5(0x1B00), 0, 0, 0, 0, 15, 0, 0, 31, 24},
        {1, 1, 4, FLV5(0x1B00), 0, 0, 0, 0, 15, 0, 0, 31, 31},
        REF(0)}},
    {"tail_b", 0, {
        {0, 0, 4, FLV5(0x1B00), 0, 0, 0, 0, 15, 0, 0, 31, 31},
        {0, 1, 4, FLV5(0x1B00), 0, 0, 0, 0, 15, 0, 0, 31, 31},
        {1, 0, 0, FLV5(0x1FFE), 0, 0, 0, 0, 15, 0, 0, 31, 31},
        REF(0)}},
    {"tail_c", 1, {
        {1, 0, 4, PROG_A, 24, 26, 28, 22, 0, 0, 0x200, 31, 0},
        {1, 0, 4, PROG_A, 24, 26, 28, 22, 0, 0, 0x200, 31, 31},
        {1, 0, 4, PROG_A, 24, 26, 28, 22, 0, 0, 0x200, 31, 24},
        REF(0x200)}},
};

static void keys(int nslots, int on) { /* KYONB on slots 0..nslots-1, one KYONEX */
    for (int k = 0; k < nslots; k++) aw(CH(k, 0), (ar(CH(k, 0)) & 0x3FFF) | (on ? 0x4000 : 0));
    aw(CH(0, 0), (ar(CH(0, 0)) & 0x7FFF) | 0x8000);
}
static void log_mixs(const char *run, const char *when) {
    LOG("%s: MIXS readback %s: %ld %ld %ld %ld\n", run, when, (long)mixs_rd(0), (long)mixs_rd(1), (long)mixs_rd(2), (long)mixs_rd(3));
}

static void run(const run_t *r) {
    aica_quiet();
    for (int k = 0; k < NS; k++) {
        const tcfg_t *t = &r->s[k];
        slot_cfg_t c;
        slot_cfg_default(&c, SA_SIG, NSIG);
        c.LPCTL = 1; c.ISEL = k; c.IMXL = 15; c.TL = 0;
        c.VOFF = t->VOFF; c.LPOFF = t->LPOFF; c.Q = t->Q;
        for (int j = 0; j < 5; j++) c.FLV[j] = t->flv[j];
        c.FAR = t->FAR; c.FD1R = t->FD1R; c.FD2R = t->FD2R; c.FRR = t->FRR;
        c.KRS = t->KRS; c.OCT = t->OCT; c.FNS = t->FNS;
        c.AR = t->AR; c.D1R = 0; c.DL = 0; c.D2R = 0; c.RR = t->RR;
        slot_write(k, &c);
        LOG("%s stream %d: slot %d SA %05lx LSA %u LEA %u LPCTL %d OCT %d FNS %03x KRS %d AR %d D1R %d DL %d D2R %d RR %d "
            "TL %d VOFF %d LPOFF %d Q %d FLV %04x %04x %04x %04x %04x FAR %d FD1R %d FD2R %d FRR %d IMXL %d ISEL %d\n",
            r->name, k, k, (unsigned long)c.SA, c.LSA, c.LEA, c.LPCTL, c.OCT, c.FNS, c.KRS, c.AR, c.D1R, c.DL, c.D2R, c.RR,
            c.TL, c.VOFF, c.LPOFF, c.Q, c.FLV[0], c.FLV[1], c.FLV[2], c.FLV[3], c.FLV[4], c.FAR, c.FD1R, c.FD2R, c.FRR, c.IMXL, c.ISEL);
    }
    if (cap_start(NS, mixs, capbuf, MAXV)) { OUT("%s: cap_start failed\n", r->name); return; }
    cap_wait_us(5000);
    cap_mark(1); keys(NS, 1); cap_mark(2);
    cap_wait_us(r->extra ? 150000 : 100000);
    cap_mark(3); keys(3, 0); cap_mark(4);
    if (!r->extra) {
        cap_wait_us(200000);
    } else {
        cap_wait_us(100000);
        cap_mark(5);
        /* aica_quiet's per-channel writes on slots 0..2 (slot 0 last so its two writes are adjacent), then KYONEX */
        for (int k = 2; k >= 0; k--) { aw(CH(k, 0x14), 0x001F); aw(CH(k, 0x00), 0); }
        aw(CH(0, 0x00), 0x8000);
        cap_mark(6);
        cap_wait_us(20000);
        cap_mark(7);
        for (int k = 0; k < 3; k++) aw(CH(k, 0x20), 0);   /* IMXL 0: slots 0..2 stop sending */
        cap_mark(8);
        log_mixs(r->name, "after the IMXL 0 write");
        cap_wait_us(50000);
        log_mixs(r->name, "at the end");
    }
    uint32_t n = cap_stop();
    cap_save(r->name, n);
    OUT("%s: %lu samples, errors %lu, %lu marks\n", r->name, (unsigned long)n, (unsigned long)CAP.errors, (unsigned long)CAP.nev);
}

int test_main(void) {
    out_open("slot_tail.txt");
    uint32_t seed = 4242;
    for (int i = 0; i < NSIG; i++) {
        seed = seed * 1103515245u + 12345u;
        sig[i] = (int16_t)(seed >> 16);
        if (!sig[i]) sig[i] = 1;
    }
    seed = 4243;
    for (int i = 0; i < NSIG; i++) {
        seed = seed * 1103515245u + 12345u;
        sig2[i] = (int16_t)(seed >> 16);
        if (!sig2[i]) sig2[i] = 1;
    }
    aica_quiet();   /* ARM7 in reset before wave RAM 0 is written */
    ram_write(SA_SIG, sig, sizeof sig);
    ram_write(SA_SIG2, sig2, sizeof sig2);
    ram_fill(SA_SIG2 + sizeof sig2, 0, 0x8000 - sizeof sig2);
    out_bin("input.bin", sig, sizeof sig);
    out_bin("input2.bin", sig2, sizeof sig2);
    LOG("signals: input.bin (seed 4242) at %05lx, input2.bin (seed 4243) at %05lx, zeros to 08000\n", (unsigned long)SA_SIG, (unsigned long)SA_SIG2);
    for (unsigned i = 0; i < sizeof runs / sizeof runs[0]; i++) run(&runs[i]);
    aica_quiet();
    out_close();
    return 0;
}
