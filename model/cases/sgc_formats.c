/* sgc_formats.c -- sample formats through a bypassed slot (VOFF=1, LPOFF=1: MIXS = 16 x the interpolated sample):
 * PCM8 (PCMS 1), ADPCM (PCMS 2) and ADPCM long stream (PCMS 3), noise (SSCTL 1), each at pitch 1.0 and 1.37
 * (FNS 0x17B), looped over [0, 4096).  Output: sgc_formats.txt, data.bin (the 8 KB of random bytes), fm_<n>.hdr/.bin */
#include "cap.h"
#define NS 4
#define MAXV (2u << 20)
static int32_t capbuf[MAXV];
static uint8_t data[8192];

int test_main(void) {
    out_open("sgc_formats.txt");
    uint32_t x = 31337;
    for (int i = 0; i < 8192; i++) { x = x * 1103515245u + 12345u; data[i] = (uint8_t)(x >> 16); }
    out_bin("data.bin", data, sizeof data);
    LOG("ramfile 020000 data.bin\n");
    static const struct { int pcms, ssctl, fns; } cf[] = {
        {1, 0, 0}, {1, 0, 0x17B}, {2, 0, 0}, {2, 0, 0x17B}, {3, 0, 0}, {3, 0, 0x17B}, {0, 1, 0}, {0, 1, 0x17B}};
    for (int base = 0; base < 8; base += NS) {
        aica_quiet();
        ram_write(0x20000, data, sizeof data);
        for (int k = 0; k < NS; k++) {
            slot_cfg_t c;
            slot_cfg_default(&c, 0x20000, 4096);
            c.ISEL = k; c.VOFF = 1; c.PCMS = cf[base + k].pcms; c.SSCTL = cf[base + k].ssctl; c.FNS = cf[base + k].fns;
            slot_write(k, &c);
            LOG("fm_%d stream %d: PCMS %d SSCTL %d FNS %03x\n", base / NS, k, c.PCMS, c.SSCTL, c.FNS);
            char nm[32];
            snprintf(nm, sizeof nm, "fm_%d", base / NS);
            slot_log(nm, k, k, &c);
        }
        static const int mixs[NS] = {0, 1, 2, 3};
        if (cap_start(NS, mixs, capbuf, MAXV)) { OUT("cap_start failed\n"); return 1; }
        cap_wait_us(5000);
        for (int k = 0; k < NS; k++) aw(CH(k, 0x00), (ar(CH(k, 0x00)) & 0x3FFF) | 0x4000);
        aw(CH(0, 0x00), (ar(CH(0, 0x00)) & 0x7FFF) | 0x8000);
        cap_mark(1);
        cap_wait_us(300000);
        uint32_t n = cap_stop();
        char nm[32];
        snprintf(nm, sizeof nm, "fm_%d", base / NS);
        cap_save(nm, n);
        OUT("%s: %lu samples, errors %lu\n", nm, (unsigned long)n, (unsigned long)CAP.errors);
    }
    aica_quiet();
    out_close();
    return 0;
}
