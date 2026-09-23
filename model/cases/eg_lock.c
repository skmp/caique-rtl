/* eg_lock.c -- envelope clock vs the DSP ring counter, increment rows at odd effective rates, key timing per slot,
 * and SH4-written MIXS persistence.  Hypothesis (tools/eg_phase.cpp, from tests/feg_track + tests/aeg_dl0): the
 * envelope clock ticks on every sample whose MDEC_CT is even (cap.h: ring address = MDEC_CT = c0 - n), with the
 * counter cnt = K - MDEC_CT/2 and ONE constant K for every run since power-on.
 * AEG runs (4 slots, constant 0x7FFF, TL 0, VOFF 0, IMXL 15: the level gives the attenuation of every sample):
 *   att_slow   AR 6/4/2/1, 11 s: pins K modulo 8192 (rows 0/2, R < 48 counter offset)
 *   att_mid    AR 22/20/18/16, 2 s: K modulo 64
 *   odd_att    KRS 0, OCT 0, FNS 0x200 (s = 1): AR 22/24/26/28 -> R 45/49/53/57, increment rows 1/5/9/13
 *   odd_att3   AR 23/25/27/29 -> R 47/51/55/59, rows 3/7/11/15
 *   odd_same   AR 24 (R 49) on slots 0..3, then on slots 5/17/40/63: is the counter the same for every slot?
 *   odd_dec    AR 31, D1R 24/26/28/22 (R 49/53/57/45) to DL 31: the odd rows in decay (a += inc)
 *   keys       AR 31 RR 31, 16 key-on/off cycles with pseudo-random timing on slots 0..3, then on 60..63: the onset
 *              and the first release step per slot show where a KYONEX write lands within the sample
 * FEG run (feg_track harness: full-scale random input, Q 4, VOFF 1, stream 3 = unfiltered reference):
 *   feg_odd    KRS 0 OCT 0 FNS 0x200 on slots 0..2: program A (R 49/53/57/45) on slots 0 and 2, program B
 *              (R 51/55/59/47) on slot 1; key-off after 150 ms
 * MIXS run:   mixs  SH4 writes MIXS3 (nothing sends there), then a slot sends 4096/sample to MIXS3, a second write
 *              while it plays, key-off, a third write; a write to the never-used MIXS2 at the end.
 * Output: eg_lock.txt, <run>.hdr/.bin, input.bin */
#include "cap.h"
#define NS 4
#define MAXV (2u << 20)
#define SA_CONST 0x10000u
#define SA_SIG 0x20000u
#define NSIG 8192
static int32_t capbuf[MAXV];
static int16_t sig[NSIG];
static const int mixs[NS] = {0, 1, 2, 3};

typedef struct { const char *name; int slot[4]; int AR[4], D1R[4], DL[4], D2R[4], RR[4]; int KRS, OCT, FNS; uint32_t on_us, off_us; int cycles; } aeg_run_t;

static void keys(const int *slot, int on) {
    for (int k = 0; k < NS; k++) aw(CH(slot[k], 0), (ar(CH(slot[k], 0)) & 0x3FFF) | (on ? 0x4000 : 0));
    aw(CH(slot[0], 0), (ar(CH(slot[0], 0)) & 0x7FFF) | 0x8000);
}
static void aeg_run(const aeg_run_t *r) {
    aica_quiet();
    for (int i = 0; i < 16; i++) ram_w32(SA_CONST + 4 * i, 0x7FFF7FFF);
    for (int k = 0; k < NS; k++) {
        slot_cfg_t c;
        slot_cfg_default(&c, SA_CONST, 32);
        c.ISEL = k; c.AR = r->AR[k]; c.D1R = r->D1R[k]; c.DL = r->DL[k]; c.D2R = r->D2R[k]; c.RR = r->RR[k];
        c.KRS = r->KRS; c.OCT = r->OCT; c.FNS = r->FNS;
        slot_write(r->slot[k], &c);
        LOG("%s stream %d: slot %d AR %d D1R %d DL %d D2R %d RR %d KRS %d OCT %d FNS %03x\n", r->name, k, r->slot[k],
            c.AR, c.D1R, c.DL, c.D2R, c.RR, c.KRS, c.OCT, c.FNS);
    }
    if (cap_start(NS, mixs, capbuf, MAXV)) { OUT("%s: cap_start failed\n", r->name); return; }
    cap_wait_us(10000);
    uint32_t seed = 777;
    for (int cyc = 0; cyc < r->cycles; cyc++) {
        uint32_t on = r->on_us, off = r->off_us;
        if (r->cycles > 1) {
            seed = seed * 1103515245u + 12345u; on = 700 + (seed >> 16) % 1800;
            seed = seed * 1103515245u + 12345u; off = 8000 + (seed >> 16) % 2000;
        }
        cap_mark(1); keys(r->slot, 1); cap_mark(2);
        cap_wait_us(on);
        cap_mark(3); keys(r->slot, 0); cap_mark(4);
        cap_wait_us(off);
    }
    uint32_t n = cap_stop();
    cap_save(r->name, n);
    OUT("%s: %lu samples, errors %lu, %lu marks\n", r->name, (unsigned long)n, (unsigned long)CAP.errors, (unsigned long)CAP.nev);
}

typedef struct { uint16_t flv[5]; uint8_t far, fd1r, fd2r, frr; } feg_t;
static void feg_run(void) {
    static const feg_t prog[3] = {
        {{0x1800, 0x1C00, 0x1800, 0x1A00, 0x1C00}, 24, 26, 28, 22},   /* R 49 53 57 45 with KRS 0 OCT 0 FNS 0x200 */
        {{0x1800, 0x1C00, 0x1800, 0x1A00, 0x1C00}, 25, 27, 29, 23},   /* R 51 55 59 47 */
        {{0x1800, 0x1C00, 0x1800, 0x1A00, 0x1C00}, 24, 26, 28, 22},   /* = program A on another slot */
    };
    aica_quiet();
    for (int k = 0; k < NS; k++) {
        slot_cfg_t c;
        slot_cfg_default(&c, SA_SIG, NSIG);
        c.LPCTL = 1; c.ISEL = k; c.VOFF = 1; c.LPOFF = k == 3; c.Q = 4;
        c.OCT = 0; c.FNS = 0x200;
        c.RR = k < 3 ? 0 : 31; c.D1R = 0;
        if (k < 3) {
            c.KRS = 0;
            for (int j = 0; j < 5; j++) c.FLV[j] = prog[k].flv[j];
            c.FAR = prog[k].far; c.FD1R = prog[k].fd1r; c.FD2R = prog[k].fd2r; c.FRR = prog[k].frr;
        } else {
            c.KRS = 15;
            for (int j = 0; j < 5; j++) c.FLV[j] = 0x1FFE;
            c.FAR = c.FD1R = c.FD2R = c.FRR = 0;
        }
        slot_write(k, &c);
        LOG("feg_odd stream %d: slot %d OCT %d FNS %03x KRS %d FLV %04x %04x %04x %04x %04x FAR %d FD1R %d FD2R %d FRR %d lpoff %d\n",
            k, k, c.OCT, c.FNS, c.KRS, c.FLV[0], c.FLV[1], c.FLV[2], c.FLV[3], c.FLV[4], c.FAR, c.FD1R, c.FD2R, c.FRR, c.LPOFF);
    }
    static const int slots[4] = {0, 1, 2, 3};
    if (cap_start(NS, mixs, capbuf, MAXV)) { OUT("feg_odd: cap_start failed\n"); return; }
    cap_wait_us(3000);
    cap_mark(1); keys(slots, 1); cap_mark(2);
    cap_wait_us(150000);
    cap_mark(3);
    for (int k = 0; k < 3; k++) aw(CH(k, 0x00), ar(CH(k, 0x00)) & 0x3FFF);
    aw(CH(0, 0x00), (ar(CH(0, 0x00)) & 0x3FFF) | 0x8000);
    cap_mark(4);
    cap_wait_us(100000);
    uint32_t n = cap_stop();
    cap_save("feg_odd", n);
    OUT("feg_odd: %lu samples, errors %lu\n", (unsigned long)n, (unsigned long)CAP.errors);
}

static void mixs_run(void) {
    aica_quiet();
    for (int i = 0; i < 16; i++) ram_w32(SA_CONST + 4 * i, 0x01000100);   /* 0x0100 -> 4096 per sample with VOFF */
    slot_cfg_t c;
    slot_cfg_default(&c, SA_CONST, 32);
    c.ISEL = 3; c.VOFF = 1;
    slot_write(3, &c);
    static const int slot3[4] = {3, 3, 3, 3};
    if (cap_start(NS, mixs, capbuf, MAXV)) { OUT("mixs: cap_start failed\n"); return; }
    cap_wait_us(10000);
    cap_mark(1); aw(R_MIXS(3, 1), 0x1234); aw(R_MIXS(3, 0), 0x5); cap_mark(2);
    LOG("mixs: MIXS3 readback right after the write %04lx/%lx\n", (unsigned long)ar(R_MIXS(3, 1)), (unsigned long)ar(R_MIXS(3, 0)));
    cap_wait_us(30000);
    LOG("mixs: MIXS3 readback after 30 ms %04lx/%lx\n", (unsigned long)ar(R_MIXS(3, 1)), (unsigned long)ar(R_MIXS(3, 0)));
    cap_mark(3); keys(slot3, 1); cap_mark(4);
    cap_wait_us(30000);
    cap_mark(5); aw(R_MIXS(3, 1), 0x0010); aw(R_MIXS(3, 0), 0x0); cap_mark(6);   /* 0x00100 while the slot plays */
    cap_wait_us(30000);
    cap_mark(7); keys(slot3, 0); cap_mark(8);
    cap_wait_us(30000);
    cap_mark(9); aw(R_MIXS(3, 1), 0x5432); aw(R_MIXS(3, 0), 0x1); cap_mark(10);
    cap_wait_us(30000);
    cap_mark(11); aw(R_MIXS(2, 1), 0x0ABC); aw(R_MIXS(2, 0), 0xD); cap_mark(12);  /* MIXS2: never used by a slot */
    cap_wait_us(30000);
    uint32_t n = cap_stop();
    cap_save("mixs", n);
    OUT("mixs: %lu samples, errors %lu\n", (unsigned long)n, (unsigned long)CAP.errors);
}

int test_main(void) {
    out_open("eg_lock.txt");
    uint32_t seed = 4242;
    for (int i = 0; i < NSIG; i++) {
        seed = seed * 1103515245u + 12345u;
        sig[i] = (int16_t)(seed >> 16);
        if (!sig[i]) sig[i] = 1;
    }
    ram_write(SA_SIG, sig, sizeof sig);
    out_bin("input.bin", sig, sizeof sig);
    static const aeg_run_t runs[] = {
        {"att_slow", {0, 1, 2, 3}, {6, 4, 2, 1}, {0}, {0}, {0}, {31, 31, 31, 31}, 15, 0, 0, 11000000, 50000, 1},
        {"att_mid", {0, 1, 2, 3}, {22, 20, 18, 16}, {0}, {0}, {0}, {31, 31, 31, 31}, 15, 0, 0, 2000000, 50000, 1},
        {"odd_att", {0, 1, 2, 3}, {22, 24, 26, 28}, {0}, {0}, {0}, {31, 31, 31, 31}, 0, 0, 0x200, 1500000, 50000, 1},
        {"odd_att3", {0, 1, 2, 3}, {23, 25, 27, 29}, {0}, {0}, {0}, {31, 31, 31, 31}, 0, 0, 0x200, 1500000, 50000, 1},
        {"odd_same", {0, 1, 2, 3}, {24, 24, 24, 24}, {0}, {0}, {0}, {31, 31, 31, 31}, 0, 0, 0x200, 1000000, 50000, 1},
        {"odd_same2", {5, 17, 40, 63}, {24, 24, 24, 24}, {0}, {0}, {0}, {31, 31, 31, 31}, 0, 0, 0x200, 1000000, 50000, 1},
        {"odd_dec", {0, 1, 2, 3}, {31, 31, 31, 31}, {24, 26, 28, 22}, {31, 31, 31, 31}, {0}, {31, 31, 31, 31}, 0, 0, 0x200, 1500000, 50000, 1},
        {"keys", {0, 1, 2, 3}, {31, 31, 31, 31}, {0}, {0}, {0}, {31, 31, 31, 31}, 15, 0, 0, 0, 0, 16},
        {"keys2", {60, 61, 62, 63}, {31, 31, 31, 31}, {0}, {0}, {0}, {31, 31, 31, 31}, 15, 0, 0, 0, 0, 16},
    };
    for (unsigned i = 0; i < sizeof runs / sizeof runs[0]; i++) aeg_run(&runs[i]);
    feg_run();
    mixs_run();
    aica_quiet();
    out_close();
    return 0;
}
