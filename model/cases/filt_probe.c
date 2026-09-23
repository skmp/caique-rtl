/* filt_probe.c -- exploratory filter captures: PCM16 one-shot [16 zeros, impulse A, 511 zeros, 512 x A (step),
 * 1008 zeros] at pitch 1.0 through the slot filter (LPOFF=0), FEG constant (all FLV = F, rates 0), for a few F x Q,
 * 4 slots per key-on.  Instant attack (AR 31, KRS 1 -> R 63), TL 0.  Streams: VOFF=1 and VOFF=0 variants.
 * Output: filt_probe.txt (combos), fp_<i>.hdr/.bin captures */
#include "cap.h"

#define NS 4
#define MAXV (1u << 20)
static int32_t capbuf[MAXV];
static int16_t smp[2048];

int test_main(void) {
    out_open("filt_probe.txt");
    static const struct { uint16_t F; int Q, voff, amp; } cmb[] = {
        {0x1FF8, 0, 1, 0x1000}, {0x1FF8, 0, 0, 0x1000}, {0x1FF8, 16, 1, 0x1000}, {0x1FF8, 31, 1, 0x1000},
        {0x1000, 0, 1, 0x1000}, {0x1000, 8, 1, 0x1000}, {0x1000, 16, 1, 0x1000}, {0x1000, 31, 1, 0x1000},
        {0x0800, 0, 1, 0x1000}, {0x0800, 16, 1, 0x1000}, {0x0400, 0, 1, 0x1000}, {0x0400, 16, 1, 0x1000},
        {0x0100, 0, 1, 0x1000}, {0x0100, 16, 1, 0x1000}, {0x0010, 0, 1, 0x1000}, {0x0000, 0, 1, 0x1000},
        {0x1FF8, 0, 1, 0x7FFF}, {0x1000, 0, 1, 0x7FFF}, {0x1C00, 4, 1, 0x4000}, {0x1FFF, 0, 1, 0x1000},
    };
    int nc = sizeof cmb / sizeof cmb[0];
    for (int base = 0; base < nc; base += NS) {
        aica_quiet();
        for (int k = 0; k < NS && base + k < nc; k++) {
            int A = cmb[base + k].amp;
            memset(smp, 0, sizeof smp);
            smp[16] = (int16_t)A;
            for (int i = 528; i < 1040; i++) smp[i] = (int16_t)A;
            ram_write(0x10000 + 0x1000 * k, smp, sizeof smp);
            slot_cfg_t c;
            slot_cfg_default(&c, 0x10000 + 0x1000 * k, 2047);
            c.LPCTL = 0; c.LSA = 0;
            c.ISEL = k; c.AR = 31; c.KRS = 1; c.D1R = 0; c.RR = 31;
            c.LPOFF = 0; c.Q = cmb[base + k].Q; c.VOFF = cmb[base + k].voff;
            for (int i = 0; i < 5; i++) c.FLV[i] = cmb[base + k].F;
            c.FAR = c.FD1R = c.FD2R = c.FRR = 0;
            slot_write(k, &c);
            LOG("stream %d of fp_%d: F %04x Q %d VOFF %d A %d\n", k, base / NS, cmb[base + k].F, cmb[base + k].Q,
                cmb[base + k].voff, A);
        }
        static const int mixs[NS] = {0, 1, 2, 3};
        if (cap_start(NS, mixs, capbuf, MAXV)) { OUT("cap_start failed\n"); return 1; }
        cap_wait_us(3000);
        for (int k = 0; k < NS && base + k < nc; k++) aw(CH(k, 0x00), (ar(CH(k, 0x00)) & 0x3FFF) | 0x4000);
        aw(CH(0, 0x00), (ar(CH(0, 0x00)) & 0x7FFF) | 0x8000);
        cap_mark(1);
        cap_wait_us(60000);
        uint32_t n = cap_stop();
        char nm[32];
        snprintf(nm, sizeof nm, "fp_%d", base / NS);
        cap_save(nm, n);
        OUT("%s: %lu samples, errors %lu\n", nm, (unsigned long)n, (unsigned long)CAP.errors);
    }
    aica_quiet();
    out_close();
    return 0;
}
