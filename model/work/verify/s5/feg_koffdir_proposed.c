/* feg_koffdir.c -- PROPOSED console case (analysis_S3alt, work/verify/s5/S3alt.md): the two readings of claim S3 that
 * the existing captures cannot separate, plus the KYONB-driven-target alternative.
 *   (a) Direction on the key-off clock.  S3 as modelled steps with the OLD segment's increment in the RELEASE direction
 *       (toward FLV4).  The alternative "oldDir" applies the old segment's full step (old increment, old direction),
 *       only checked against the release target.  Every capture so far had the old segment and the release moving the
 *       same way (or increment 0 / an odd key-off), so both fit.  Needs a decay 2 holding on one side of FLV4 while
 *       the release goes the other way.
 *   (b) KYONB-driven target.  Does clearing KYONB alone (no KYONEX) already switch the FEG target to FLV4?  If it did,
 *       every "+old increment toward FLV4" event could be a decay-2-rate step taken between the KYONB clear and the
 *       KYONEX write, and S3 would be an artefact of the write sequence.
 * Harness = feg_krs / feg_track: full-scale random input at SA_SIG, Q 4, VOFF 1, LPOFF 0 on streams 0..2, stream 3 =
 * unfiltered reference; RR 0 so a released slot keeps playing (the FEG is recovered through the bit-exact filter,
 * tools/feg_track.cpp algorithm, in work/verify/s5/koffdir_check.cpp).  KRS 15, OCT 0, FNS 0 everywhere (R = 2 rate).
 *   slot 0: FLV 1C00 1800 1C00 1A02 1C00, FAR 28 FD1R 26 FD2R 28 FRR 24 (R 56/52/56/48): decay 2 goes DOWN and holds at
 *           0x1A04 (u d02); release UP at +1.  Even key-off clock: S3 -> 0x1A08 (u d04), oldDir -> 0x1A00 (u d00);
 *           odd key-off: 0x1A05 (u d02) on the next clock, then +1 per clock either way.
 *   slot 1: FLV 1800 1C00 1800 1A00 1C00, FAR 24 FD1R 26 FD2R 28 FRR 24 (= feg_krs fk_1 stream 0): decay 2 UP holds at
 *           0x19FE (u cff), release UP: the S3 witness (even: 0x1A02, u d01; odd: 0x19FF, u cff).
 *   slot 2: FLV 1800 1C00 1800 1A00 1800, FAR 28 FD1R 26 FD2R 28 FRR 24: decay 2 UP holds at 0x19FE, release DOWN at +1.
 *           Even: S3 -> 0x19FA (u cfd), oldDir -> 0x1A02 (u d01); odd: 0x19FD (u cfe).
 * Batches 0..5: key-off = 3 KYONB clears + KYONEX after 100 ms + 977 us * b (pseudo-random key-off parity; ~3 of 6
 *   land on a clock).  Marks: 1 key-on, 3 before / 4 after the key-off writes.
 * Batches 6, 7: KYONB cleared on slots 0..2 WITHOUT KYONEX after 100 ms (marks 5 / 6), KYONEX alone 100 ms later
 *   (marks 7 / 8).  KYONB-driven target: the held values move toward FLV4 at the decay-2 rate (+4 per clock) right after
 *   the clears (slot 0 0x1A04 -> 0x1BFC in 126 clocks, slot 1 0x19FE -> 0x1BFE, slot 2 0x19FE -> 0x1802); state-driven
 *   target: they hold until the KYONEX, then release as in batches 0..5.
 * Output: feg_koffdir.txt, kd_<batch>.hdr/.bin, input.bin.  Analysis: build/work/koffdir_check <output dir>. */
#include "cap.h"
#define NS 4
#define MAXV (1u << 20)
#define SA_SIG 0x20000u
#define NSIG 8192
#define NBATCH 8
static int32_t capbuf[MAXV];
static int16_t sig[NSIG];
typedef struct { uint16_t flv[5]; uint8_t far, fd1r, fd2r, frr; } feg_t;
static const feg_t prog[3] = {
    {{0x1C00, 0x1800, 0x1C00, 0x1A02, 0x1C00}, 28, 26, 28, 24},
    {{0x1800, 0x1C00, 0x1800, 0x1A00, 0x1C00}, 24, 26, 28, 24},
    {{0x1800, 0x1C00, 0x1800, 0x1A00, 0x1800}, 28, 26, 28, 24},
};
int test_main(void) {
    out_open("feg_koffdir.txt");
    uint32_t seed = 4242;
    for (int i = 0; i < NSIG; i++) {
        seed = seed * 1103515245u + 12345u;
        sig[i] = (int16_t)(seed >> 16);
        if (!sig[i]) sig[i] = 1;
    }
    ram_write(SA_SIG, sig, sizeof sig);
    out_bin("input.bin", sig, sizeof sig);
    static const int mixs[NS] = {0, 1, 2, 3};
    for (unsigned b = 0; b < NBATCH; b++) {
        int kyonb_only = b >= 6;
        aica_quiet();
        for (int k = 0; k < NS; k++) {
            slot_cfg_t c;
            slot_cfg_default(&c, SA_SIG, NSIG);
            c.LPCTL = 1; c.ISEL = k; c.VOFF = 1; c.LPOFF = k == 3; c.Q = 4;
            c.OCT = 0; c.FNS = 0; c.KRS = 15;
            c.RR = k < 3 ? 0 : 31; c.D1R = 0;
            if (k < 3) {
                for (int j = 0; j < 5; j++) c.FLV[j] = prog[k].flv[j];
                c.FAR = prog[k].far; c.FD1R = prog[k].fd1r; c.FD2R = prog[k].fd2r; c.FRR = prog[k].frr;
            } else {
                for (int j = 0; j < 5; j++) c.FLV[j] = 0x1FFE;
                c.FAR = c.FD1R = c.FD2R = c.FRR = 0;
            }
            slot_write(k, &c);
            LOG("kd_%u stream %d: slot %d KRS %d FLV %04x %04x %04x %04x %04x FAR %d FD1R %d FD2R %d FRR %d lpoff %d%s\n", b, k, k,
                c.KRS, c.FLV[0], c.FLV[1], c.FLV[2], c.FLV[3], c.FLV[4], c.FAR, c.FD1R, c.FD2R, c.FRR, c.LPOFF,
                kyonb_only ? " (KYONB-only clear, KYONEX 100 ms later)" : "");
        }
        if (cap_start(NS, mixs, capbuf, MAXV)) { OUT("cap_start failed\n"); return 1; }
        cap_wait_us(3000);
        for (int k = 0; k < NS; k++) aw(CH(k, 0x00), (ar(CH(k, 0x00)) & 0x3FFF) | 0x4000);
        aw(CH(0, 0x00), (ar(CH(0, 0x00)) & 0x7FFF) | 0x8000);
        cap_mark(1);
        cap_wait_us(100000 + 977 * b);
        if (!kyonb_only) {
            cap_mark(3);
            for (int k = 0; k < 3; k++) aw(CH(k, 0x00), ar(CH(k, 0x00)) & 0x3FFF);
            aw(CH(0, 0x00), (ar(CH(0, 0x00)) & 0x3FFF) | 0x8000);
            cap_mark(4);
            cap_wait_us(100000);
        } else {
            cap_mark(5);
            for (int k = 0; k < 3; k++) aw(CH(k, 0x00), ar(CH(k, 0x00)) & 0x3FFF);   /* KYONB off, no KYONEX */
            cap_mark(6);
            cap_wait_us(100000);
            cap_mark(7);
            aw(CH(0, 0x00), (ar(CH(0, 0x00)) & 0x3FFF) | 0x8000);                      /* KYONEX alone */
            cap_mark(8);
            cap_wait_us(100000);
        }
        uint32_t n = cap_stop();
        char nm[32];
        snprintf(nm, sizeof nm, "kd_%u", b);
        cap_save(nm, n);
        OUT("%s: %lu samples, errors %lu\n", nm, (unsigned long)n, (unsigned long)CAP.errors);
    }
    aica_quiet();
    out_close();
    return 0;
}
