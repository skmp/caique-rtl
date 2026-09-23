/* feg_krs.c -- does KRS (the register, not the effective rate) change when a slot's FEG sees a key-off?
 * tests/feg_track batches 1 and 2 each had one slot whose release started one envelope clock before its siblings':
 * the slots with KRS 5 and KRS 2 (the others: KRS 15 / 0).  Same harness as feg_track (full-scale random input,
 * Q 4, VOFF 1, stream 3 = unfiltered reference), all slots keyed on together and off together:
 *   batch 0: the feg_track batch-1 programs with the slot numbers reversed (KRS 15 on slot 2, KRS 0 on 1, KRS 5 on 0)
 *   batch 1: one program at matched effective rates (R 48 / 52 / 56 / 48) with KRS 15 / 2 / 5 on slots 0 / 1 / 2
 *   batch 2: batch 1 on slots 5 / 17 / 40 (reference on 63)
 *   batch 3: batch 1 with KRS 2 / 5 / 15 on slots 0 / 1 / 2 (rotated)
 * Output: feg_krs.txt, fk_<batch>.hdr/.bin, input.bin */
#include "cap.h"
#define NS 4
#define MAXV (1u << 20)
#define SA_SIG 0x20000u
#define NSIG 8192
static int32_t capbuf[MAXV];
static int16_t sig[NSIG];
typedef struct { uint16_t flv[5]; uint8_t far, fd1r, fd2r, frr, krs; } feg_t;
static const struct { int oct, fns; int slot[4]; uint32_t off_us; feg_t s[3]; } batch[] = {
    {3, 0x200, {0, 1, 2, 3}, 150000, {
        {{0x1800, 0x1C00, 0x1800, 0x1A00, 0x1C00}, 16, 20, 24, 22, 5},
        {{0x1800, 0x1C00, 0x1800, 0x1A00, 0x1C00}, 16, 20, 24, 22, 0},
        {{0x1800, 0x1C00, 0x1800, 0x1A00, 0x1C00}, 16, 20, 24, 22, 15}}},
    {0, 0, {0, 1, 2, 3}, 100000, {
        {{0x1800, 0x1C00, 0x1800, 0x1A00, 0x1C00}, 24, 26, 28, 24, 15},   /* R 48 52 56 48 */
        {{0x1800, 0x1C00, 0x1800, 0x1A00, 0x1C00}, 22, 24, 26, 22, 2},    /* s = 4:  R 48 52 56 48 */
        {{0x1800, 0x1C00, 0x1800, 0x1A00, 0x1C00}, 19, 21, 23, 19, 5}}},  /* s = 10: R 48 52 56 48 */
    {0, 0, {5, 17, 40, 63}, 100000, {
        {{0x1800, 0x1C00, 0x1800, 0x1A00, 0x1C00}, 24, 26, 28, 24, 15},
        {{0x1800, 0x1C00, 0x1800, 0x1A00, 0x1C00}, 22, 24, 26, 22, 2},
        {{0x1800, 0x1C00, 0x1800, 0x1A00, 0x1C00}, 19, 21, 23, 19, 5}}},
    {0, 0, {0, 1, 2, 3}, 100000, {
        {{0x1800, 0x1C00, 0x1800, 0x1A00, 0x1C00}, 22, 24, 26, 22, 2},
        {{0x1800, 0x1C00, 0x1800, 0x1A00, 0x1C00}, 19, 21, 23, 19, 5},
        {{0x1800, 0x1C00, 0x1800, 0x1A00, 0x1C00}, 24, 26, 28, 24, 15}}},
};
int test_main(void) {
    out_open("feg_krs.txt");
    uint32_t seed = 4242;
    for (int i = 0; i < NSIG; i++) {
        seed = seed * 1103515245u + 12345u;
        sig[i] = (int16_t)(seed >> 16);
        if (!sig[i]) sig[i] = 1;
    }
    ram_write(SA_SIG, sig, sizeof sig);
    out_bin("input.bin", sig, sizeof sig);
    for (unsigned b = 0; b < sizeof batch / sizeof batch[0]; b++) {
        aica_quiet();
        for (int k = 0; k < NS; k++) {
            slot_cfg_t c;
            slot_cfg_default(&c, SA_SIG, NSIG);
            c.LPCTL = 1; c.ISEL = k; c.VOFF = 1; c.LPOFF = k == 3; c.Q = 4;
            c.OCT = batch[b].oct; c.FNS = batch[b].fns;
            c.RR = k < 3 ? 0 : 31; c.D1R = 0;
            if (k < 3) {
                const feg_t *f = &batch[b].s[k];
                for (int j = 0; j < 5; j++) c.FLV[j] = f->flv[j];
                c.FAR = f->far; c.FD1R = f->fd1r; c.FD2R = f->fd2r; c.FRR = f->frr; c.KRS = f->krs;
            } else {
                for (int j = 0; j < 5; j++) c.FLV[j] = 0x1FFE;
                c.FAR = c.FD1R = c.FD2R = c.FRR = 0;
            }
            slot_write(batch[b].slot[k], &c);
            LOG("fk_%u stream %d: slot %d OCT %d FNS %03x KRS %d FLV %04x %04x %04x %04x %04x FAR %d FD1R %d FD2R %d FRR %d lpoff %d\n",
                b, k, batch[b].slot[k], c.OCT, c.FNS, c.KRS, c.FLV[0], c.FLV[1], c.FLV[2], c.FLV[3], c.FLV[4], c.FAR, c.FD1R, c.FD2R,
                c.FRR, c.LPOFF);
        }
        static const int mixs[NS] = {0, 1, 2, 3};
        if (cap_start(NS, mixs, capbuf, MAXV)) { OUT("cap_start failed\n"); return 1; }
        cap_wait_us(3000);
        for (int k = 0; k < NS; k++) aw(CH(batch[b].slot[k], 0x00), (ar(CH(batch[b].slot[k], 0x00)) & 0x3FFF) | 0x4000);
        aw(CH(batch[b].slot[0], 0x00), (ar(CH(batch[b].slot[0], 0x00)) & 0x7FFF) | 0x8000);
        cap_mark(1);
        cap_wait_us(batch[b].off_us);
        cap_mark(3);
        for (int k = 0; k < 3; k++) aw(CH(batch[b].slot[k], 0x00), ar(CH(batch[b].slot[k], 0x00)) & 0x3FFF);
        aw(CH(batch[b].slot[0], 0x00), (ar(CH(batch[b].slot[0], 0x00)) & 0x3FFF) | 0x8000);
        cap_mark(4);
        cap_wait_us(100000);
        uint32_t n = cap_stop();
        char nm[32];
        snprintf(nm, sizeof nm, "fk_%u", b);
        cap_save(nm, n);
        OUT("%s: %lu samples, errors %lu\n", nm, (unsigned long)n, (unsigned long)CAP.errors);
    }
    aica_quiet();
    out_close();
    return 0;
}
