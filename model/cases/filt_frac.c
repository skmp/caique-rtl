/* filt_frac.c -- how the slot filter takes a FRACTIONAL (interpolated) input sample.
 * The interpolator keeps 4 fraction bits (s16, 1/16 sample; tests/sgc_pitch) while the filter works in 1/8 units.
 * All four slots play the same signed random PCM16 loop at the same fractional pitch, keyed on together: streams 0..2
 * filter it (VOFF=1), stream 3 bypasses the filter (LPOFF=1, VOFF=1) so MIXS = s16 exactly.
 * Batch 0: OCT 0, FNS 0x333; batch 1: OCT -1, FNS 0x155; batch 2: OCT 0, FNS 0x0AB.
 * Output: filt_frac.txt, ff_<batch>.hdr/.bin, input.bin (the PCM16 loop) */
#include "cap.h"
#define NS 4
#define MAXV (1u << 20)
#define SA_SIG 0x20000u
#define NSIG 4096
static int32_t capbuf[MAXV];
static int16_t sig[NSIG];
static const uint16_t fl[3] = {0x1FFE, 0x1E00, 0x1C00};
static const uint8_t fq[3] = {4, 4, 31};
static const struct { int oct, fns; } pitch[] = {{0, 0x333}, {15, 0x155}, {0, 0x0AB}};
int test_main(void) {
    out_open("filt_frac.txt");
    uint32_t seed = 12345;
    for (int i = 0; i < NSIG; i++) { seed = seed * 1103515245u + 12345u; sig[i] = (int16_t)((int16_t)(seed >> 16) >> 1); }
    for (int i = 0; i < 512; i++) sig[i] = 0;        /* quiet lead-in: the step into the random part times the onset */
    ram_write(SA_SIG, sig, sizeof sig);
    out_bin("input.bin", sig, sizeof sig);
    for (unsigned b = 0; b < sizeof pitch / sizeof pitch[0]; b++) {
        aica_quiet();
        for (int k = 0; k < NS; k++) {
            slot_cfg_t c;
            slot_cfg_default(&c, SA_SIG, NSIG);
            c.LSA = 512; c.LPCTL = 1; c.ISEL = k; c.VOFF = 1; c.LPOFF = k == 3; c.Q = k < 3 ? fq[k] : 4;
            c.OCT = pitch[b].oct; c.FNS = pitch[b].fns;
            for (int j = 0; j < 5; j++) c.FLV[j] = k < 3 ? fl[k] : 0x1FFE;
            c.FAR = c.FD1R = c.FD2R = c.FRR = 0;
            slot_write(k, &c);
            LOG("ff_%u stream %d: F %04x Q %d lpoff %d OCT %d FNS %03x\n", b, k, c.FLV[0], c.Q, c.LPOFF, c.OCT, c.FNS);
        }
        static const int mixs[NS] = {0, 1, 2, 3};
        if (cap_start(NS, mixs, capbuf, MAXV)) { OUT("cap_start failed\n"); return 1; }
        cap_wait_us(3000);
        for (int k = 0; k < NS; k++) aw(CH(k, 0x00), (ar(CH(k, 0x00)) & 0x3FFF) | 0x4000);
        aw(CH(0, 0x00), (ar(CH(0, 0x00)) & 0x7FFF) | 0x8000);
        cap_mark(1);
        cap_wait_us(150000);
        uint32_t n = cap_stop();
        char nm[32];
        snprintf(nm, sizeof nm, "ff_%u", b);
        cap_save(nm, n);
        OUT("%s: %lu samples, errors %lu\n", nm, (unsigned long)n, (unsigned long)CAP.errors);
    }
    aica_quiet();
    out_close();
    return 0;
}
