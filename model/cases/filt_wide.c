/* filt_wide.c -- integrator width: drive the slot filter's states as large as any setting can make them.
 * No FLV/Q setting is unstable, so the largest states come from resonance: Q 31 (q = 13/128, gain ~10) driven by a
 * full-scale square wave (+-32767) whose period matches the resonance (about 2 pi / f samples).  low and band then
 * swing to ~ +-3.3M (1/8 units, ~2^22) while the output rails at +-2^19; if the integrators clipped or wrapped below
 * that, the exact recurrence diverges when the signal comes back.  Each batch: one cutoff, three Q settings, and
 * an unfiltered reference (stream 3).  After 2048 samples of square wave the input stops (zeros) so the ring-down
 * from the large state is captured too.  Output: filt_wide.txt, fw_<batch>.hdr/.bin */
#include "cap.h"
#define NS 4
#define MAXV (1u << 20)
#define SA_SIG 0x20000u
#define NSIG 8192
static int32_t capbuf[MAXV];
static int16_t sig[NSIG];
static const struct { uint16_t F; int half; uint8_t Q[3]; } batch[] = {
    {0x1800, 50, {31, 28, 24}},   /* f = 1/16: period ~100.5 */
    {0x1A00, 25, {31, 30, 20}},   /* f = 1/8:  period ~50 */
    {0x1600, 100, {31, 29, 16}},  /* f = 1/32: period ~201 */
    {0x1400, 201, {31, 31, 31}},  /* f = 1/64: period ~402, all Q31 */
};
int test_main(void) {
    out_open("filt_wide.txt");
    for (unsigned b = 0; b < sizeof batch / sizeof batch[0]; b++) {
        memset(sig, 0, sizeof sig);
        for (int i = 512; i < 512 + 2048; i++) sig[i] = ((i - 512) / batch[b].half) & 1 ? -32767 : 32767;
        ram_write(SA_SIG, sig, sizeof sig);
        aica_quiet();
        for (int k = 0; k < NS; k++) {
            slot_cfg_t c;
            slot_cfg_default(&c, SA_SIG, NSIG - 1);
            c.LPCTL = 0; c.ISEL = k; c.VOFF = 1; c.LPOFF = k == 3; c.Q = k < 3 ? batch[b].Q[k] : 4;
            for (int j = 0; j < 5; j++) c.FLV[j] = batch[b].F;
            c.FAR = c.FD1R = c.FD2R = c.FRR = 0;
            slot_write(k, &c);
            LOG("fw_%u stream %d: F %04x Q %d lpoff %d half-period %d\n", b, k, c.FLV[0], c.Q, c.LPOFF, batch[b].half);
        }
        static const int mixs[NS] = {0, 1, 2, 3};
        if (cap_start(NS, mixs, capbuf, MAXV)) { OUT("cap_start failed\n"); return 1; }
        cap_wait_us(3000);
        for (int k = 0; k < NS; k++) aw(CH(k, 0x00), (ar(CH(k, 0x00)) & 0x3FFF) | 0x4000);
        aw(CH(0, 0x00), (ar(CH(0, 0x00)) & 0x7FFF) | 0x8000);
        cap_mark(1);
        cap_wait_us(250000);
        uint32_t n = cap_stop();
        char nm[32];
        snprintf(nm, sizeof nm, "fw_%u", b);
        cap_save(nm, n);
        OUT("%s: %lu samples, errors %lu\n", nm, (unsigned long)n, (unsigned long)CAP.errors);
    }
    aica_quiet();
    out_close();
    return 0;
}
