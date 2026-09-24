/* sgc_pitch.c -- phase accumulator and interpolation: a 4096-sample pseudo-random PCM16 loop played at 16 pitches
 * (OCT, FNS), VOFF=1 and LPOFF=1 so MIXS carries the interpolated sample (with any fraction bits).  4 slots per key-on,
 * 150 ms each.  Output: sgc_pitch.txt (stream -> OCT/FNS), sample.bin (int16 data), pi_<n>.hdr/.bin */
#include "cap.h"
#define NS 4
#define MAXV (2u << 20)
static int32_t capbuf[MAXV];
static int16_t smp[4096 + 64];

int test_main(void) {
    out_open("sgc_pitch.txt");
    uint32_t x = 99;
    for (int i = 0; i < 4096 + 64; i++) { x = x * 1103515245u + 12345u; smp[i] = (int16_t)(x >> 16); }
    for (int i = 0; i < 64; i++) smp[4096 + i] = smp[i]; /* loop continuation: s1 past the end = the loop start */
    out_bin("sample.bin", smp, sizeof smp);
    LOG("ramfile 020000 sample.bin\n");
    static const struct { int oct, fns; } pv[] = {
        {0, 0}, {0, 0x200}, {0, 0x001}, {0, 0x3FF}, {0, 0x155}, {1, 0}, {1, 0x2AB}, {2, 0x0FF},
        {7, 0x3FF}, {15, 0}, {15, 0x200}, {14, 0x3FF}, {12, 0x123}, {8, 0x3FF}, {9, 0x001}, {3, 0x1FF}};
    for (int base = 0; base < 16; base += NS) {
        aica_quiet();
        ram_write(0x20000, smp, sizeof smp);
        for (int k = 0; k < NS; k++) {
            slot_cfg_t c;
            slot_cfg_default(&c, 0x20000, 4096);
            c.ISEL = k; c.VOFF = 1; c.OCT = pv[base + k].oct; c.FNS = pv[base + k].fns;
            slot_write(k, &c);
            LOG("pi_%d stream %d: OCT %d FNS %03x\n", base / NS, k, pv[base + k].oct, pv[base + k].fns);
            char nm[32];
            snprintf(nm, sizeof nm, "pi_%d", base / NS);
            slot_log(nm, k, k, &c);
        }
        static const int mixs[NS] = {0, 1, 2, 3};
        if (cap_start(NS, mixs, capbuf, MAXV)) { OUT("cap_start failed\n"); return 1; }
        cap_wait_us(5000);
        for (int k = 0; k < NS; k++) aw(CH(k, 0x00), (ar(CH(k, 0x00)) & 0x3FFF) | 0x4000);
        aw(CH(0, 0x00), (ar(CH(0, 0x00)) & 0x7FFF) | 0x8000);
        cap_mark(1);
        cap_wait_us(150000);
        uint32_t n = cap_stop();
        char nm[32];
        snprintf(nm, sizeof nm, "pi_%d", base / NS);
        cap_save(nm, n);
        OUT("%s: %lu samples, errors %lu\n", nm, (unsigned long)n, (unsigned long)CAP.errors);
    }
    aica_quiet();
    out_close();
    return 0;
}
