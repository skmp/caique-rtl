/* feg_koffatt.c -- does the FEG take "one more step of the old segment" on a key-off clock when the old segment is the
 * ATTACK?  tests/feg_koffdir (F4) established it for decay 2 (old increment AND old direction, hold check against the
 * release target); tests/aeg_koff koff_att (F2) found the AEG attack takes NO step on the key-off clock.  The FEG attack
 * is linear, so the question is open: old step / no step / old increment toward the release target (what
 * src/aica_model.cpp feg_clock did until F4) / release increment.
 *
 * Harness = feg_koffdir (full-scale random input at SA_SIG, Q 4, VOFF 1, LPOFF 0, RR 0 so a released slot keeps
 * playing; the FEG is recovered through the bit-exact filter), but the unfiltered reference of stream 3 is replaced by
 * an AEG WITNESS (the aeg_koff device): slot 3 plays the constant 0x7FFF (TL 0, VOFF 0, LPOFF 1, AR 31 with KRS 1 ->
 * R 63, D1R 31 DL 31 D2R 31 RR 31) and is keyed ON by the very KYONEX that keys the FEG slots OFF, so its level jumps
 * to 520176 (a = 0) on the key-off sample E, whatever its parity (S2 / S5).  The witness then decays to "off" by itself
 * (~256 samples).  Without a reference stream the checker takes the filter input from the known signal: pitch 1.0 and
 * CA restarting at the key-on give x(i) = sig[(i - onset) mod 8192].  KRS 15, OCT 0, FNS 0 on the FEG slots (R = 2 rate).
 *   slot 0: FLV 1C00 1800 1800 1800 1C00, FAR 26 FD1R 31 FD2R 31 FRR 28: attack DOWN at -2/clock (R 52: 512 clocks =
 *           23 ms to FLV1), release UP at +4 (R 56).  Even key-off clock E (v = the value before E): old step -> v-2 then
 *           +4/clock; no step -> v then +4; old inc toward the release target -> v+2 then +4; release inc -> v+4 then +4.
 *   slot 1: FLV 1800 1C00 1C00 1C00 1800, same rates: attack UP +2, release DOWN -4 (the mirror of slot 0).
 *   slot 2: FLV 1800 1C00 1C00 1C00 1C00, same rates: attack UP +2, release UP +4 (same direction: the S3-style witness,
 *           old step = old inc toward the target = +2, then +4; no step = hold then +4; release inc = +4 then +4).
 *           The task text had FAR 24 (+1) here; with u = v >> 1 recovered (bit 0 of v is unused by the filter) a +1 step
 *           followed by +4 steps is indistinguishable from no step whenever v is even at E, so +2 is used instead.
 *   Odd E: nothing at E, the release steps from E+1 under every reading (the batch is indifferent).
 * Batches b = 0..7: key-on (slots 0..2), key-off after 10000 + 977 * b us (both MDEC_CT parities over the 8 batches;
 * the attacks are 23 ms long, so every key-off lands inside them at v ~ 0x1A00..0x1B00), 100 ms more, then the witness
 * keyed off (it is off by itself long before).  Marks: 1 after the key-on write, 3 / 4 around the key-off KYONEX,
 * 5 / 6 around the witness key-off.  aica_quiet between batches.
 * Output: feg_koffatt.txt, ka_<b>.hdr/.bin, input.bin.  Analysis: build/work/koffatt_check <output dir> [-K kc]
 * (work/koffatt/koffatt_check.cpp). */
#include "cap.h"
#define NS 4
#define MAXV (1u << 20)
#define SA_SIG 0x20000u
#define NSIG 8192
#define SA_CONST 0x10000u
#define NBATCH 8
static int32_t capbuf[MAXV];
static int16_t sig[NSIG];
typedef struct { uint16_t flv[5]; uint8_t far, fd1r, fd2r, frr; } feg_t;
static const feg_t prog[3] = {
    {{0x1C00, 0x1800, 0x1800, 0x1800, 0x1C00}, 26, 31, 31, 28},
    {{0x1800, 0x1C00, 0x1C00, 0x1C00, 0x1800}, 26, 31, 31, 28},
    {{0x1800, 0x1C00, 0x1C00, 0x1C00, 0x1C00}, 26, 31, 31, 28},
};
/* KYONB of slot k = bit k of mask, then one KYONEX (through slot 0's register) */
static void keyx(int mask) {
    for (int k = 0; k < NS; k++) aw(CH(k, 0), (ar(CH(k, 0)) & 0x3FFF) | (((mask >> k) & 1) ? 0x4000 : 0));
    aw(CH(0, 0), (ar(CH(0, 0)) & 0x7FFF) | 0x8000);
}
int test_main(void) {
    out_open("feg_koffatt.txt");
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
    static const int mixs[NS] = {0, 1, 2, 3};
    for (unsigned b = 0; b < NBATCH; b++) {
        aica_quiet();
        for (int k = 0; k < NS; k++) {
            slot_cfg_t c;
            if (k < 3) {
                slot_cfg_default(&c, SA_SIG, NSIG);
                c.LPCTL = 1; c.ISEL = k; c.VOFF = 1; c.LPOFF = 0; c.Q = 4;
                c.OCT = 0; c.FNS = 0; c.KRS = 15;
                c.AR = 31; c.D1R = 0; c.RR = 0; /* the AEG sits at a = 0 and never releases: the slot plays on */
                for (int j = 0; j < 5; j++) c.FLV[j] = prog[k].flv[j];
                c.FAR = prog[k].far; c.FD1R = prog[k].fd1r; c.FD2R = prog[k].fd2r; c.FRR = prog[k].frr;
                slot_write(k, &c);
                LOG("ka_%u stream %d: slot %d role feg KRS %d FLV %04x %04x %04x %04x %04x FAR %d FD1R %d FD2R %d FRR %d Q %d lpoff %d voff %d\n",
                    b, k, k, c.KRS, c.FLV[0], c.FLV[1], c.FLV[2], c.FLV[3], c.FLV[4], c.FAR, c.FD1R, c.FD2R, c.FRR, c.Q, c.LPOFF, c.VOFF);
            } else {
                slot_cfg_default(&c, SA_CONST, 32);
                c.ISEL = k; c.TL = 0; c.VOFF = 0; c.LPOFF = 1;
                c.AR = 31; c.D1R = 31; c.DL = 31; c.D2R = 31; c.RR = 31;
                c.KRS = 1; c.OCT = 0; c.FNS = 0; /* k = 1, s = 2: R 63 everywhere at pitch 1.0 */
                slot_write(k, &c);
                LOG("ka_%u stream %d: slot %d role witness AR %d D1R %d DL %d D2R %d RR %d KRS %d OCT %d FNS %03x\n", b, k, k,
                    c.AR, c.D1R, c.DL, c.D2R, c.RR, c.KRS, c.OCT, c.FNS);
            }
        }
        if (cap_start(NS, mixs, capbuf, MAXV)) { OUT("cap_start failed\n"); return 1; }
        cap_wait_us(3000);
        keyx(0x7);            /* FEG slots on (the witness stays off) */
        cap_mark(1);
        cap_wait_us(10000 + 977 * b);
        cap_mark(3);
        keyx(0x8);            /* FEG slots off, witness on: its onset is the key-off sample */
        cap_mark(4);
        cap_wait_us(100000);
        cap_mark(5);
        keyx(0x0);            /* witness off (off by itself long before; the FEG slots, already released, are untouched) */
        cap_mark(6);
        cap_wait_us(2000);
        uint32_t n = cap_stop();
        char nm[32];
        snprintf(nm, sizeof nm, "ka_%u", b);
        cap_save(nm, n);
        OUT("%s: %lu samples, errors %lu, %lu marks\n", nm, (unsigned long)n, (unsigned long)CAP.errors, (unsigned long)CAP.nev);
    }
    aica_quiet();
    out_close();
    return 0;
}
