/* adpcm_hi.c -- ADPCM at OCT +3 and above (TODO 6.1): the console fetches nothing there (wren7 tests/hw/sgcadp: no
 * wave RAM slot at OCT 3..7), and the models step at most 8 nibbles per sample (OCT +2).  What does the slot output?
 * Bypassed slots (VOFF 1, LPOFF 1: MIXS = 16 x the interpolated sample), random bytes looped over [0, 4096) nibbles,
 * 4 slots per run, keyed together:
 *   a_0  PCMS 2 at OCT 2 FNS 3FF (the fastest fetching pitch), OCT 3 FNS 0, OCT 5 FNS 0, OCT 7 FNS 3FF
 *   a_1  PCMS 3 (long stream) at the same four pitches
 *   a_2  PCMS 2, SA odd (the start nibble in the high half of a byte... SA is a byte address: SA + 1) at OCT 3 / 4 / 6 / 2
 *   a_3  switching: all four keyed at OCT 2 FNS 0 (PCMS 2, 2, 3, 3), then 20 ms later OCT 3 (slots 0, 2) / OCT 4 (1, 3),
 *        then 20 ms later back to OCT 2: does the slot resume, restart, or stay stopped?
 * Output: adpcm_hi.txt, data.bin (the 8 KB of random bytes at 0x20000), ah_<n>.hdr/.bin, with marks for every write. */
#include "cap.h"
#define NS 4
#define MAXV (2u << 20)
static int32_t capbuf[MAXV];
static uint8_t data[8192];

static void keys(void) {
    for (int k = 0; k < NS; k++) aw(CH(k, 0x00), (ar(CH(k, 0x00)) & 0x3FFF) | 0x4000);
    aw(CH(0, 0x00), (ar(CH(0, 0x00)) & 0x7FFF) | 0x8000);
}

int test_main(void) {
    out_open("adpcm_hi.txt");
    uint32_t x = 4711;
    for (int i = 0; i < 8192; i++) { x = x * 1103515245u + 12345u; data[i] = (uint8_t)(x >> 16); }
    out_bin("data.bin", data, sizeof data);
    LOG("ramfile 020000 data.bin\n");
    static const struct { int pcms[NS], oct[NS], fns[NS], sa_odd; } runs[3] = {
        {{2, 2, 2, 2}, {2, 3, 5, 7}, {0x3FF, 0, 0, 0x3FF}, 0},
        {{3, 3, 3, 3}, {2, 3, 5, 7}, {0x3FF, 0, 0, 0x3FF}, 0},
        {{2, 2, 2, 2}, {3, 4, 6, 2}, {0, 0, 0, 0}, 1},
    };
    for (int r = 0; r < 4; r++) {
        aica_quiet();
        ram_write(0x20000, data, sizeof data);
        for (int k = 0; k < NS; k++) {
            slot_cfg_t c;
            slot_cfg_default(&c, 0x20000, 4096);
            c.ISEL = k; c.VOFF = 1;
            if (r < 3) { c.PCMS = runs[r].pcms[k]; c.OCT = runs[r].oct[k]; c.FNS = runs[r].fns[k]; c.SA += runs[r].sa_odd; }
            else { c.PCMS = k < 2 ? 2 : 3; c.OCT = 2; c.FNS = 0; }
            slot_write(k, &c);
            LOG("ah_%d stream %d: PCMS %d OCT %d FNS %03x SA %06lx\n", r, k, c.PCMS, c.OCT, c.FNS, (unsigned long)c.SA);
            char nm[16];
            snprintf(nm, sizeof nm, "ah_%d", r);
            slot_log(nm, k, k, &c);
        }
        static const int mixs[NS] = {0, 1, 2, 3};
        if (cap_start(NS, mixs, capbuf, MAXV)) { OUT("cap_start failed\n"); return 1; }
        cap_wait_us(5000);
        cap_mark(1); keys(); cap_mark(2);
        if (r == 3) {
            cap_wait_us(20000);
            cap_mark(3);
            for (int k = 0; k < NS; k++) aw(CH(k, 0x18), (uint32_t)((k & 1 ? 4 : 3) << 11));
            cap_mark(4);
            cap_wait_us(20000);
            cap_mark(5);
            for (int k = 0; k < NS; k++) aw(CH(k, 0x18), 2u << 11);
            cap_mark(6);
        }
        cap_wait_us(r == 3 ? 20000 : 60000);
        uint32_t n = cap_stop();
        char nm[32];
        snprintf(nm, sizeof nm, "ah_%d", r);
        cap_save(nm, n);
        OUT("%s: %lu samples, errors %lu\n", nm, (unsigned long)n, (unsigned long)CAP.errors);
    }
    aica_quiet();
    out_close();
    return 0;
}
