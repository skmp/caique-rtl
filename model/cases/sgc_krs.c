/* sgc_krs.c -- key rate scaling: effective envelope rate vs KRS, OCT, FNS.  4 slots per run play a constant 0x7FFF
 * (1024 samples, looped over the first 256 so interpolation never leaves the DC; TL 0, filter off), AR 31, D1R 14 (R = 28 without scaling), DL 31; 600 ms of decay 1 is captured and reduced on
 * the console to its level changes (sample offset, MIXS) per stream.  tools/krs.py infers R from the tick spacings
 * and increments.  Output: sgc_krs.txt */
#include "cap.h"

#define NS 4
#define MAXV (1u << 20)
static int32_t capbuf[MAXV];

static const int krs_v[] = {0, 1, 2, 3, 4, 7, 8, 10, 14, 15};
static const int oct_v[] = {0, 1, 2, 4, 7, 8, 12, 15};
static const int fns_v[] = {0, 0x1FF, 0x200, 0x3FF};

int test_main(void) {
    out_open("sgc_krs.txt");
    int combos[320][3], nc = 0;
    for (unsigned a = 0; a < 10; a++)
        for (unsigned b = 0; b < 8; b++)
            for (unsigned c = 0; c < 4; c++) { combos[nc][0] = krs_v[a]; combos[nc][1] = oct_v[b]; combos[nc][2] = fns_v[c]; nc++; }
    for (int base = 0; base < nc; base += NS) {
        aica_quiet();
        for (int i = 0; i < 512; i++) ram_w32(0x10000 + 4 * i, 0x7FFF7FFF); /* 1024 samples: pad past LEA */
        for (int k = 0; k < NS; k++) {
            slot_cfg_t c;
            slot_cfg_default(&c, 0x10000, 256);
            c.ISEL = k;
            c.AR = 31; c.D1R = 14; c.DL = 31; c.KRS = combos[base + k][0];
            c.OCT = combos[base + k][1]; c.FNS = combos[base + k][2];
            slot_write(k, &c);
        }
        static const int mixs[NS] = {0, 1, 2, 3};
        if (cap_start(NS, mixs, capbuf, MAXV)) { OUT("cap_start failed\n"); return 1; }
        cap_wait_us(5000);
        for (int k = 0; k < NS; k++) aw(CH(k, 0x00), (ar(CH(k, 0x00)) & 0x3FFF) | 0x4000);
        aw(CH(0, 0x00), (ar(CH(0, 0x00)) & 0x7FFF) | 0x8000);
        cap_wait_us(600000);
        for (int k = 0; k < NS; k++) aw(CH(k, 0x00), ar(CH(k, 0x00)) & 0x3FFF);
        aw(CH(0, 0x00), (ar(CH(0, 0x00)) & 0x3FFF) | 0x8000);
        uint32_t n = cap_stop();
        for (int k = 0; k < NS; k++) {
            uint32_t on = 0;
            while (on < n && capbuf[on * NS + k] == 0) on++;
            LOG("KRS %d OCT %d FNS %03x err %lu:", combos[base + k][0], combos[base + k][1], combos[base + k][2],
                (unsigned long)CAP.errors);
            int32_t prev = capbuf[on * NS + k];
            int nt = 0;
            for (uint32_t i = on + 1; i < n && nt < 60; i++) {
                int32_t v = capbuf[i * NS + k];
                if (v != prev) { LOG(" %lu:%ld", (unsigned long)(i - on), (long)v); prev = v; nt++; }
            }
            LOG("\n");
        }
    }
    aica_quiet();
    out_close();
    return 0;
}
