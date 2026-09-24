/* adpcm_pitch.c -- ADPCM decoding between pitch 1.37 (tests/sgc_formats: exact) and OCT +2 FNS 3FF, where tests/adpcm_hi
 * shows the model diverging from the second sample (TODO 6.1).  Bypassed slots (VOFF 1, LPOFF 1), the random bytes of
 * adpcm_hi looped over [0, 4096) nibbles, 4 slots per capture (PCMS 2 in ap_0 / ap_1, PCMS 3 in ap_2 / ap_3):
 *   ap_0 / ap_2  OCT 1 FNS 000 (2 nibbles / sample), OCT 1 FNS 200 (3), OCT 1 FNS 3FF (~4), OCT 2 FNS 000 (4)
 *   ap_1 / ap_3  OCT 2 FNS 100 (5), OCT 2 FNS 200 (6), OCT 2 FNS 300 (7), OCT 2 FNS 3FF (~8)
 * Checker: tools/stream_replay.  Output: adpcm_pitch.txt, data.bin, ap_<n>.hdr/.bin */
#include "cap.h"
#define NS 4
#define MAXV (2u << 20)
static int32_t capbuf[MAXV];
static uint8_t data[8192];

int test_main(void) {
    out_open("adpcm_pitch.txt");
    uint32_t x = 4711;
    for (int i = 0; i < 8192; i++) { x = x * 1103515245u + 12345u; data[i] = (uint8_t)(x >> 16); }
    out_bin("data.bin", data, sizeof data);
    LOG("ramfile 020000 data.bin\n");
    static const int oct[2][NS] = {{1, 1, 1, 2}, {2, 2, 2, 2}};
    static const int fns[2][NS] = {{0x000, 0x200, 0x3FF, 0x000}, {0x100, 0x200, 0x300, 0x3FF}};
    for (int r = 0; r < 4; r++) {
        aica_quiet();
        ram_write(0x20000, data, sizeof data);
        char nm[16];
        snprintf(nm, sizeof nm, "ap_%d", r);
        for (int k = 0; k < NS; k++) {
            slot_cfg_t c;
            slot_cfg_default(&c, 0x20000, 4096);
            c.ISEL = k; c.VOFF = 1; c.PCMS = r < 2 ? 2 : 3; c.OCT = oct[r & 1][k]; c.FNS = fns[r & 1][k];
            slot_write(k, &c);
            slot_log(nm, k, k, &c);
        }
        static const int mixs[NS] = {0, 1, 2, 3};
        if (cap_start(NS, mixs, capbuf, MAXV)) { OUT("cap_start failed\n"); return 1; }
        cap_wait_us(5000);
        for (int k = 0; k < NS; k++) aw(CH(k, 0x00), (ar(CH(k, 0x00)) & 0x3FFF) | 0x4000);
        aw(CH(0, 0x00), (ar(CH(0, 0x00)) & 0x7FFF) | 0x8000);
        cap_mark(1);
        cap_wait_us(60000);
        uint32_t n = cap_stop();
        cap_save(nm, n);
    }
    aica_quiet();
    out_close();
    return 0;
}
