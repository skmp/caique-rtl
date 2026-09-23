/* feg_koffpass.c -- the FEG key-off clock when the OLD segment has already PASSED its target (NOTES "Open items"):
 * an attack / decay 1 that crossed its target on clock N sets `passed`; on clock N+1 the FEG normally switches to the
 * next segment BEFORE stepping (its rate and direction, no idle clock).  What happens when the key-off lands exactly on
 * clock N+1?  Readings (v = the value at the crossing, before E):
 *   A  one more step of the segment that just passed: its increment and direction, hold check against FLV4
 *      (src/aica_model.cpp feg_clock: key_off saves feg_prev / feg_prev_dir of the passed segment and clears `passed`,
 *      so the `passed && state < DECAY2` advance never runs -- the model's answer);
 *   B  a step of the NEXT segment (its rate, its direction toward its target: what a normal clock N+1 does);
 *   C  no step (like the AEG attack), release from the next clock;
 *   D  the release step itself (release increment toward FLV4).
 * Harness = feg_koffatt (full-scale random input at SA_SIG, Q 4, VOFF 1, LPOFF 0, KRS 15, AEG AR 31 / D1R 0 / RR 0 so a
 * released slot keeps playing; the FEG is recovered through the bit-exact filter, u = v >> 1), the key-off sample E
 * pinned by the AEG WITNESS slot 3 (constant 0x7FFF, R 63) keyed ON by the KYONEX that keys the FEG slots OFF.
 * Every slot crosses on clock N = 8 (kp_a) / 12 (kp_b) after the key-on, so one key-off sample is "N+1" for all three:
 *   slot 0: attack FLV0 1800 -> FLV1 1810 at +2 (FAR 26, R 52; C = v >= 1810 flips at 1810: N = 8), decay 1 toward FLV2
 *           1000 at -4 (FD1R 28, R 56), release UP to FLV4 1C00 at +1 (FRR 24, R 48).  At N+1: A 1812, B 180C, C 1810,
 *           D 1811 (u C09 / C06 / C08 / C08; E+2: C09 / C06 / C08 / C09): every pair differs at E or E+2.
 *   slot 1: the mirror: attack 1C00 -> 1BF2 at -2 (down: ends strictly below the target, 1BF0 on clock 8), decay 1 UP toward
 *           1FF0 at +4, release DOWN to 1400 at -1.  At N+1: A 1BEE, B 1BF4, C 1BF0, D 1BEF.
 *   slot 2: the decay 1 -> decay 2 boundary: attack 1800 -> 1808 at +8 (FAR 30, R 60: passed on clock 1), decay 1 1808 ->
 *           1816 at +2 (FD1R 26; passed on clock 8), decay 2 toward 1000 at -4 (FD2R 28), release UP to 1C00 at +1.
 *           At N+1: A 1818, B 1812, C 1816, D 1817.
 * kp_b: the same with the crossing on clock 12 (FLV1 1818 / 1BEA, slot 2 FLV2 181E) and the on-times shifted.
 * Cycle (NCYC per run, no aica_quiet between them: a key-on during the FEG slots' release is a fresh key-on, FLV0
 * reloaded, CA restarted; the witness is keyed off after it went off by itself): cap_poll (catch the reader up, so no
 * ring read lands in the timed window), key-on 0..2, spin on_us WITHOUT polling (the key-on -> key-off spacing is then
 * timer-exact: keyx + on_us), key-off 0..2 + witness on (one KYONEX), mark 3, 8 ms (witness off by itself: 128 clocks),
 * witness key-off, 3 ms.  on_us = on_base + 8 * (cyc % 16) + 2 * (cyc / 16): 128 us = 5.6 samples of spread around the
 * N+1 key-off (kp_a: on_base 310, the model's keyx costs 24 us, so the spacing is 334..462 us = 14.7..20.4 samples: clock
 * N at 15/16 samples after the key-on, N+1 at 17/18, N+2 at 19/20; kp_b: on_base 490, spacing 22.7..28.2 samples, N+1 at
 * 25/26).  Half the key-offs land on odd samples (indifferent); of the even ones a third are N+1, a third N (the crossing
 * clock itself: re-tests the plain "old step"), a third N+2 (keyed off from the NEXT segment: decay 1 / decay 2 old step).
 * Marks: 3 after each key-off KYONEX (NCYC = CAP_MAXEV).  One capture per run (4 buses, ~0.8 s).
 * Output: feg_koffpass.txt, kp_a.hdr/.bin, kp_b.hdr/.bin, input.bin.  Analysis: build/tools/koffpass_check <output dir> [-K kc]. */
#include "cap.h"
#define NS 4
#define MAXV (2u << 20)
#define SA_SIG 0x20000u
#define NSIG 8192
#define SA_CONST 0x10000u
#define NCYC 64
#define TEST 0x7 /* KYONB mask: FEG slots 0..2 */
#define WIT 0x8  /* witness slot 3 */
static int32_t capbuf[MAXV];
static int16_t sig[NSIG];
typedef struct { uint16_t flv[5]; uint8_t far, fd1r, fd2r, frr; } feg_t;
typedef struct { const char *name; feg_t prog[3]; uint32_t on_base; int ncross; } run_t;
static const run_t runs[2] = {
    {"kp_a", {{{0x1800, 0x1810, 0x1000, 0x1000, 0x1C00}, 26, 28, 28, 24},
              {{0x1C00, 0x1BF2, 0x1FF0, 0x1FF0, 0x1400}, 26, 28, 28, 24},
              {{0x1800, 0x1808, 0x1816, 0x1000, 0x1C00}, 30, 26, 28, 24}}, 310, 8},
    {"kp_b", {{{0x1800, 0x1818, 0x1000, 0x1000, 0x1C00}, 26, 28, 28, 24},
              {{0x1C00, 0x1BEA, 0x1FF0, 0x1FF0, 0x1400}, 26, 28, 28, 24},
              {{0x1800, 0x1808, 0x181E, 0x1000, 0x1C00}, 30, 26, 28, 24}}, 490, 12},
};
/* KYONB of slot k = bit k of mask, then one KYONEX (through slot 0's register) */
static void keyx(int mask) {
    for (int k = 0; k < NS; k++) aw(CH(k, 0), (ar(CH(k, 0)) & 0x3FFF) | (((mask >> k) & 1) ? 0x4000 : 0));
    aw(CH(0, 0), (ar(CH(0, 0)) & 0x7FFF) | 0x8000);
}
static int run(const run_t *r) {
    static const int mixs[NS] = {0, 1, 2, 3};
    aica_quiet();
    for (int k = 0; k < NS; k++) {
        slot_cfg_t c;
        if (k < 3) {
            slot_cfg_default(&c, SA_SIG, NSIG);
            c.LPCTL = 1; c.ISEL = k; c.VOFF = 1; c.LPOFF = 0; c.Q = 4;
            c.OCT = 0; c.FNS = 0; c.KRS = 15;
            c.AR = 31; c.D1R = 0; c.RR = 0; /* the AEG sits at a = 0 and never releases: the slot plays on */
            for (int j = 0; j < 5; j++) c.FLV[j] = r->prog[k].flv[j];
            c.FAR = r->prog[k].far; c.FD1R = r->prog[k].fd1r; c.FD2R = r->prog[k].fd2r; c.FRR = r->prog[k].frr;
            slot_write(k, &c);
            LOG("%s stream %d: slot %d role feg KRS %d FLV %04x %04x %04x %04x %04x FAR %d FD1R %d FD2R %d FRR %d Q %d lpoff %d voff %d\n",
                r->name, k, k, c.KRS, c.FLV[0], c.FLV[1], c.FLV[2], c.FLV[3], c.FLV[4], c.FAR, c.FD1R, c.FD2R, c.FRR, c.Q, c.LPOFF, c.VOFF);
        } else {
            slot_cfg_default(&c, SA_CONST, 32);
            c.ISEL = k; c.TL = 0; c.VOFF = 0; c.LPOFF = 1;
            c.AR = 31; c.D1R = 31; c.DL = 31; c.D2R = 31; c.RR = 31;
            c.KRS = 1; c.OCT = 0; c.FNS = 0; /* k = 1, s = 2: R 63 everywhere at pitch 1.0 */
            slot_write(k, &c);
            LOG("%s stream %d: slot %d role witness AR %d D1R %d DL %d D2R %d RR %d KRS %d OCT %d FNS %03x\n", r->name, k, k,
                c.AR, c.D1R, c.DL, c.D2R, c.RR, c.KRS, c.OCT, c.FNS);
        }
    }
    LOG("%s: crossing clock N %d, on_base %lu us, %d cycles\n", r->name, r->ncross, (unsigned long)r->on_base, NCYC);
    if (cap_start(NS, mixs, capbuf, MAXV)) { OUT("%s: cap_start failed\n", r->name); return 1; }
    cap_wait_us(3000);
    for (int cyc = 0; cyc < NCYC; cyc++) {
        uint32_t on_us = r->on_base + 8 * (cyc % 16) + 2 * (cyc / 16);
        cap_poll();           /* reader caught up: the next ring read is >= 64 samples away, past the timed window */
        keyx(TEST);           /* FEG slots on (fresh key-on during their held release; the witness stays off) */
        spin_us(on_us);       /* no polling here: key-on -> key-off spacing = keyx + on_us exactly */
        keyx(WIT);            /* FEG slots off, witness on: its onset is the key-off sample E */
        cap_mark(3);
        LOG("%s cycle %d: on_us %lu, mark 3 at head estimate %lu\n", r->name, cyc, (unsigned long)on_us, (unsigned long)cap_head_now());
        cap_wait_us(8000);    /* the witness decays to off by itself (128 clocks); the FEG slots release toward FLV4 */
        keyx(0);              /* witness off (RELEASE, so its next key-on is fresh); the released FEG slots are untouched */
        cap_wait_us(3000);
    }
    uint32_t n = cap_stop();
    cap_save(r->name, n);
    OUT("%s: %lu samples, errors %lu, %lu marks\n", r->name, (unsigned long)n, (unsigned long)CAP.errors, (unsigned long)CAP.nev);
    return 0;
}
int test_main(void) {
    out_open("feg_koffpass.txt");
    aica_quiet(); /* ARM7 in reset before the RAM is written */
    uint32_t seed = 4242;
    for (int i = 0; i < NSIG; i++) {
        seed = seed * 1103515245u + 12345u;
        sig[i] = (int16_t)(seed >> 16);
        if (!sig[i]) sig[i] = 1;
    }
    ram_write(SA_SIG, sig, sizeof sig);
    out_bin("input.bin", sig, sizeof sig);
    for (int i = 0; i < 24; i++) ram_w32(SA_CONST + 4 * i, 0x7FFF7FFF); /* the witness constant, loop [0,32) */
    int rc = 0;
    for (unsigned i = 0; i < sizeof runs / sizeof runs[0] && !rc; i++) rc = run(&runs[i]);
    aica_quiet();
    out_close();
    return rc;
}
