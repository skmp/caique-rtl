/* filt_low.c -- slot filter at low cutoff exponents (e = 0..11), from a KNOWN start state.
 * Low cutoffs have huge deadbands, so an inherited rest state is unknown.  Each batch therefore first plays
 * zeros at FLV 0x1FFE (unity cutoff): the filter settles into the (0,-1,-1) cycle or the (0,0) fixed point,
 * i.e. low in {0,-1}, band in {-1,0,1}.  The slots are then keyed off (FRR 0 keeps the FEG value), re-configured
 * and keyed on together at the target cutoff: 512 zero samples, then a full-scale DC step (+32767, looped).
 * Streams 0..2 filter (VOFF=1), stream 3 is the unfiltered reference (LPOFF=1) that times the step.
 * Output: filt_low.txt, fl_<batch>.hdr/.bin */
#include "cap.h"
#define NS 4
#define MAXV (1u << 20)
#define SA_ZERO 0x40000u
#define SA_SIG 0x20000u
static int32_t capbuf[MAXV];
static int16_t sig[1024];
static const struct { uint16_t F[3]; uint8_t Q[3]; uint32_t us; } batch[] = {
    {{0x0000, 0x0200, 0x0400}, {4, 4, 4}, 4000000},
    {{0x0600, 0x0800, 0x0A00}, {4, 4, 4}, 2000000},
    {{0x0C00, 0x0E00, 0x1000}, {4, 4, 4}, 1000000},
    {{0x1200, 0x1400, 0x1600}, {4, 4, 4}, 1000000},
    {{0x01FE, 0x0955, 0x13FE}, {4, 4, 4}, 4000000},
    {{0x0A00, 0x0A00, 0x0400}, {0, 31, 31}, 4000000},
};
static void cfg(int k, uint32_t sa, uint16_t F, int Q, int lpoff) {
    slot_cfg_t c;
    slot_cfg_default(&c, sa, 1024);
    c.LSA = 512; c.LPCTL = 1; c.ISEL = k; c.VOFF = 1; c.LPOFF = lpoff; c.Q = Q;
    for (int j = 0; j < 5; j++) c.FLV[j] = F;
    c.FAR = c.FD1R = c.FD2R = c.FRR = 0;
    slot_write(k, &c);
}
int test_main(void) {
    out_open("filt_low.txt");
    memset(sig, 0, sizeof sig);
    ram_write(SA_ZERO, sig, sizeof sig);
    for (int i = 512; i < 1024; i++) sig[i] = 32767;
    ram_write(SA_SIG, sig, sizeof sig);
    for (unsigned b = 0; b < sizeof batch / sizeof batch[0]; b++) {
        aica_quiet();
        /* prelude: unity cutoff, zero input -> known small state */
        for (int k = 0; k < NS; k++) cfg(k, SA_ZERO, 0x1FFE, 4, k == 3);
        for (int k = 0; k < NS; k++) aw(CH(k, 0x00), (ar(CH(k, 0x00)) & 0x3FFF) | 0x4000);
        aw(CH(0, 0x00), (ar(CH(0, 0x00)) & 0x7FFF) | 0x8000);
        spin_us(5000);
        for (int k = 0; k < NS; k++) aw(CH(k, 0x00), ar(CH(k, 0x00)) & 0x3FFF);
        aw(CH(0, 0x00), (ar(CH(0, 0x00)) & 0x3FFF) | 0x8000);
        spin_us(2000);
        for (int k = 0; k < NS; k++) {
            cfg(k, SA_SIG, k < 3 ? batch[b].F[k] : 0x1FFE, k < 3 ? batch[b].Q[k] : 4, k == 3);
            LOG("fl_%u stream %d: F %04x Q %d lpoff %d\n", b, k, k < 3 ? batch[b].F[k] : 0, k < 3 ? batch[b].Q[k] : 0, k == 3);
        }
        static const int mixs[NS] = {0, 1, 2, 3};
        if (cap_start(NS, mixs, capbuf, MAXV)) { OUT("cap_start failed\n"); return 1; }
        cap_wait_us(3000);
        for (int k = 0; k < NS; k++) aw(CH(k, 0x00), (ar(CH(k, 0x00)) & 0x3FFF) | 0x4000);
        aw(CH(0, 0x00), (ar(CH(0, 0x00)) & 0x7FFF) | 0x8000);
        cap_mark(1);
        cap_wait_us(batch[b].us);
        uint32_t n = cap_stop();
        char nm[32];
        snprintf(nm, sizeof nm, "fl_%u", b);
        cap_save(nm, n);
        OUT("%s: %lu samples, errors %lu\n", nm, (unsigned long)n, (unsigned long)CAP.errors);
    }
    aica_quiet();
    out_close();
    return 0;
}
