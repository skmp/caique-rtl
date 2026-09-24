/* sgc_lfo2.c -- pitch LFO at other base pitches (square, PLFOS 7, LFOF 20 on a ramp sample, VOFF=1):
 * OCT 0 FNS 0x3F0 (FNS + 126 overflows 0x3FF), OCT 2 FNS 0x100, OCT -3 FNS 0x200, OCT 0 FNS 0x050 (FNS - 128 < 0).
 * And the FEG driving the filter: FLV0 0x1A00 -> FLV1 0x1F00 (FAR 24), FLV2 0x1C00 (FD1R 26), FLV3 0x1800 (FD2R 20),
 * FLV4 0x1E00 (FRR 28), Q 4, input: a looped pseudo-random PCM16 (VOFF=1).  Output: sgc_lfo2.txt, l2_<n>.hdr/.bin */
#include "cap.h"
#define NS 4
#define MAXV (2u << 20)
static int32_t capbuf[MAXV];
static int16_t ramp[4096 + 64], noise[4096 + 64];

int test_main(void) {
    out_open("sgc_lfo2.txt");
    for (int i = 0; i < 4096 + 64; i++) ramp[i] = (int16_t)(8 * (i & 4095) - 0x4000);
    uint32_t x = 5;
    for (int i = 0; i < 4096; i++) { x = x * 1103515245u + 12345u; noise[i] = (int16_t)((int32_t)((x >> 16) & 0x1FFF) - 0x1000); }
    for (int i = 0; i < 64; i++) noise[4096 + i] = noise[i];
    static const struct { int oct, fns; } pv[4] = {{0, 0x3F0}, {2, 0x100}, {13, 0x200}, {0, 0x050}};
    out_bin("ramp.bin", ramp, sizeof ramp);
    out_bin("noise.bin", noise, sizeof noise);
    LOG("ramfile 020000 ramp.bin\nramfile 030000 noise.bin\n");   /* tools/stream_replay */
    for (int r = 0; r < 2; r++) {
        aica_quiet();
        ram_write(0x20000, ramp, sizeof ramp);
        ram_write(0x30000, noise, sizeof noise);
        for (int k = 0; k < NS; k++) {
            slot_cfg_t c;
            if (r == 0) {
                slot_cfg_default(&c, 0x20000, 4096);
                c.OCT = pv[k].oct; c.FNS = pv[k].fns; c.PLFOWS = 1; c.PLFOS = 7; c.LFOF = 20; c.LFORE = 1;
                LOG("l2_0 stream %d: square PLFOS 7 OCT %d FNS %03x\n", k, pv[k].oct, pv[k].fns);
            } else {
                slot_cfg_default(&c, 0x30000, 4096);
                c.LPOFF = 0; c.Q = 4;
                c.FLV[0] = 0x1A00; c.FLV[1] = 0x1F00; c.FLV[2] = 0x1C00; c.FLV[3] = 0x1800; c.FLV[4] = 0x1E00;
                c.FAR = 24 - 2 * k; c.FD1R = 26 - 2 * k; c.FD2R = 20; c.FRR = 28;
                LOG("l2_1 stream %d: FEG FAR %d FD1R %d FD2R 20 FRR 28 Q 4\n", k, c.FAR, c.FD1R);
            }
            c.ISEL = k; c.VOFF = 1;
            slot_write(k, &c);
            char nm[32];
            snprintf(nm, sizeof nm, "l2_%d", r);
            slot_log(nm, k, k, &c);
        }
        static const int mixs[NS] = {0, 1, 2, 3};
        if (cap_start(NS, mixs, capbuf, MAXV)) { OUT("cap_start failed\n"); return 1; }
        cap_wait_us(5000);
        for (int k = 0; k < NS; k++) aw(CH(k, 0x1C), ar(CH(k, 0x1C)) & 0x7FFF);
        for (int k = 0; k < NS; k++) aw(CH(k, 0x00), (ar(CH(k, 0x00)) & 0x3FFF) | 0x4000);
        aw(CH(0, 0x00), (ar(CH(0, 0x00)) & 0x7FFF) | 0x8000);
        cap_mark(1);
        cap_wait_us(r ? 400000 : 700000);
        if (r) {
            for (int k = 0; k < NS; k++) aw(CH(k, 0x00), ar(CH(k, 0x00)) & 0x3FFF);
            aw(CH(0, 0x00), (ar(CH(0, 0x00)) & 0x3FFF) | 0x8000);
            cap_mark(2);
            cap_wait_us(20000);
        }
        uint32_t n = cap_stop();
        char nm[32];
        snprintf(nm, sizeof nm, "l2_%d", r);
        cap_save(nm, n);
        OUT("%s: %lu samples, errors %lu\n", nm, (unsigned long)n, (unsigned long)CAP.errors);
    }
    aica_quiet();
    out_close();
    return 0;
}
