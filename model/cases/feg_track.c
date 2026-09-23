/* feg_track.c -- sample-exact filter envelope (FEG) through the slot filter.
 * The FEG value v drives the filter cutoff, and the filter arithmetic is known bit-exactly (NOTES.md "Slot filter"),
 * so a full-scale random input at a high cutoff lets tools/feg_track.cpp recover v >> 1 (bit 0 is unused by the
 * filter) at every sample.  Streams 0..2 run different FEG programs (VOFF=1, Q 4), stream 3 is the unfiltered
 * reference (LPOFF=1) giving the input.  All slots are keyed on together, and keyed off together after OFF_US.
 *   batch 0: segment transitions and overshoot (targets not multiples of the step), up and down, R rows 0..16
 *   batch 1: KRS on FEG rates: identical programs with KRS 15 (off), 0, 5 at OCT +3, FNS 0x200 (FNS[9] = 1)
 *   batch 2: key-off during the attack, KRS with a negative octave, D2 hold
 * Output: feg_track.txt, ft_<batch>.hdr/.bin, input.bin */
#include "cap.h"
#define NS 4
#define MAXV (1u << 20)
#define SA_SIG 0x20000u
#define NSIG 8192
static int32_t capbuf[MAXV];
static int16_t sig[NSIG];
typedef struct { uint16_t flv[5]; uint8_t far, fd1r, fd2r, frr, krs; } feg_t;
static const struct { int oct, fns; uint32_t off_us; feg_t s[3]; } batch[] = {
    {0, 0, 120000, {
        {{0x1800, 0x1C05, 0x1A03, 0x1B00, 0x1900}, 28, 26, 30, 27, 15},
        {{0x1FF0, 0x1802, 0x1F01, 0x1E00, 0x1FFD}, 30, 29, 24, 30, 15},
        {{0x1C00, 0x1C80, 0x1C00, 0x1C40, 0x1BF0}, 22, 23, 21, 25, 15}}},
    {3, 0x200, 150000, {
        {{0x1800, 0x1C00, 0x1800, 0x1A00, 0x1C00}, 16, 20, 24, 22, 15},
        {{0x1800, 0x1C00, 0x1800, 0x1A00, 0x1C00}, 16, 20, 24, 22, 0},
        {{0x1800, 0x1C00, 0x1800, 0x1A00, 0x1C00}, 16, 20, 24, 22, 5}}},
    {13, 0x155, 15000, {
        {{0x1800, 0x1FF0, 0x1800, 0x1800, 0x1A00}, 24, 26, 0, 28, 15},
        {{0x1800, 0x1C00, 0x1900, 0x1A00, 0x1C00}, 18, 20, 22, 24, 2},
        {{0x1C00, 0x1D00, 0x1D00, 0x1E00, 0x1B00}, 26, 26, 0, 26, 15}}},
};
int test_main(void) {
    out_open("feg_track.txt");
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
            c.RR = k < 3 ? 0 : 31; c.D1R = 0;   /* RR 0: a released FEG slot keeps playing (else it stops, input 0) */
            if (k < 3) {
                const feg_t *f = &batch[b].s[k];
                for (int j = 0; j < 5; j++) c.FLV[j] = f->flv[j];
                c.FAR = f->far; c.FD1R = f->fd1r; c.FD2R = f->fd2r; c.FRR = f->frr; c.KRS = f->krs;
            } else {
                for (int j = 0; j < 5; j++) c.FLV[j] = 0x1FFE;
                c.FAR = c.FD1R = c.FD2R = c.FRR = 0;
            }
            slot_write(k, &c);
            LOG("ft_%u stream %d: OCT %d FNS %03x KRS %d FLV %04x %04x %04x %04x %04x FAR %d FD1R %d FD2R %d FRR %d lpoff %d\n",
                b, k, c.OCT, c.FNS, c.KRS, c.FLV[0], c.FLV[1], c.FLV[2], c.FLV[3], c.FLV[4], c.FAR, c.FD1R, c.FD2R,
                c.FRR, c.LPOFF);
        }
        static const int mixs[NS] = {0, 1, 2, 3};
        if (cap_start(NS, mixs, capbuf, MAXV)) { OUT("cap_start failed\n"); return 1; }
        cap_wait_us(3000);
        for (int k = 0; k < NS; k++) aw(CH(k, 0x00), (ar(CH(k, 0x00)) & 0x3FFF) | 0x4000);
        aw(CH(0, 0x00), (ar(CH(0, 0x00)) & 0x7FFF) | 0x8000);
        cap_mark(1);
        cap_wait_us(batch[b].off_us);
        /* key off the three FEG slots only: the reference keeps playing (it times the input) */
        for (int k = 0; k < 3; k++) aw(CH(k, 0x00), ar(CH(k, 0x00)) & 0x3FFF);
        aw(CH(0, 0x00), (ar(CH(0, 0x00)) & 0x3FFF) | 0x8000);
        cap_mark(2);
        cap_wait_us(100000);
        uint32_t n = cap_stop();
        char nm[32];
        snprintf(nm, sizeof nm, "ft_%u", b);
        cap_save(nm, n);
        OUT("%s: %lu samples, errors %lu\n", nm, (unsigned long)n, (unsigned long)CAP.errors);
    }
    aica_quiet();
    out_close();
    return 0;
}
