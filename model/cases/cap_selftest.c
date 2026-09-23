/* cap_selftest.c -- validates cap.h: 4 slots play 256-sample looped pseudo-random PCM16 at pitch 1.0 into MIXS 0..3
 * (slot 0,1 VOFF: MIXS = sample*16 exactly; slot 2 TL 0; slot 3 TL 0 IMXL 1), captured for 2 s.  Checks on the
 * console: counter continuity, and that stream 0/1 are exactly data*16 at some phase.
 * Output: cap_selftest.txt, cap.hdr/cap.bin */
#include "cap.h"

#define NS 4
#define MAXV (2u << 20)
static int32_t capbuf[MAXV];
static int16_t data[NS][256];

int test_main(void) {
    out_open("cap_selftest.txt");
    aica_quiet();
    uint32_t x = 12345;
    for (int k = 0; k < NS; k++)
        for (int i = 0; i < 256; i++) { x = x * 1103515245u + 12345u; data[k][i] = (int16_t)(x >> 16); }
    for (int k = 0; k < NS; k++) {
        ram_write(0x10000 + 0x1000 * k, data[k], 512);
        slot_cfg_t c;
        slot_cfg_default(&c, 0x10000 + 0x1000 * k, 256);
        c.ISEL = k;
        if (k < 2) c.VOFF = 1;
        if (k == 3) c.IMXL = 1;
        slot_write(k, &c);
    }
    static const int mixs[NS] = {0, 1, 2, 3};
    if (cap_start(NS, mixs, capbuf, MAXV)) { OUT("cap_start failed\n"); return 1; }
    cap_wait_us(20000);
    cap_mark(1);
    for (int k = 0; k < NS; k++) aw(CH(k, 0x00), (ar(CH(k, 0x00)) & 0x3FFF) | 0x4000);
    aw(CH(0, 0x00), (ar(CH(0, 0x00)) & 0x7FFF) | 0x8000);
    cap_wait_us(2000000);
    for (int k = 0; k < NS; k++) aw(CH(k, 0x00), ar(CH(k, 0x00)) & 0x3FFF);
    aw(CH(0, 0x00), (ar(CH(0, 0x00)) & 0x3FFF) | 0x8000);
    cap_mark(2);
    cap_wait_us(50000);
    uint32_t n = cap_stop();
    OUT("captured %lu samples x %d streams, counter errors %lu (first at %lu), events:", (unsigned long)n, NS,
        (unsigned long)CAP.errors, (unsigned long)CAP.first_err_n);
    for (uint32_t e = 0; e < CAP.nev; e++) OUT(" %lu@%lu", (unsigned long)CAP.ev[e].id, (unsigned long)(CAP.ev[e].n - CAP.n_first));
    OUT("\n");
    /* first non-zero sample of stream 0, then the phase that matches data[0] */
    for (int k = 0; k < 2; k++) {
        uint32_t on = 0;
        while (on < n && capbuf[on * NS + k] == 0) on++;
        int best = -1;
        for (int p = 0; p < 256 && best < 0; p++) {
            int ok = 1;
            for (uint32_t i = on; i < on + 512 && i < n; i++)
                if (capbuf[i * NS + k] != data[k][(i - on + p) & 255] * 16) { ok = 0; break; }
            if (ok) best = p;
        }
        uint32_t bad = 0, last = on;
        if (best >= 0)
            for (uint32_t i = on; i < n; i++) {
                int32_t v = capbuf[i * NS + k];
                if (v == 0 && i > on + 1000) break; /* keyed off */
                last = i;
                if (v != data[k][(i - on + best) & 255] * 16) bad++;
            }
        OUT("stream %d: first non-zero at %lu, phase %d, %lu samples checked to %lu, %lu mismatches\n", k,
            (unsigned long)on, best, (unsigned long)(last - on + 1), (unsigned long)last, (unsigned long)bad);
    }
    cap_save("cap", n);
    aica_quiet();
    out_close();
    return 0;
}
