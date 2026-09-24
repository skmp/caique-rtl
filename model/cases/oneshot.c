/* oneshot.c -- what a one-shot end (LPCTL 0, CA reaching LEA) does to the envelope (tests/sub_sched exp 0: a key-on
 * after a one-shot end was ignored until a key-off; the model had minicast's rule, "release, a = 0x3FF, off").
 * Slot 0 plays a constant block one-shot (LEA 64: 64 samples, 1.45 ms); the EG and CA monitors are polled (~5 us per
 * read) through the end and 3 ms after it, logging every change.  Then: a KYONEX with KYONB still 1, a key-off
 * (KYONB 0 + KYONEX), a key-on (KYONB 1 + KYONEX), each followed by a poll of the monitors.  Runs (envelope at the end):
 *   H  held: AR 31, D1R 0, DL 0 (decay 2 at D2R 0: a = 0)
 *   D  moving: AR 31, D1R 12 (decay 1 stepping), DL 31
 *   A  attack: AR 6 (still attacking after 64 samples)
 * Output: oneshot.txt */
#include "aica_io.h"

/* every change of the two monitors is buffered (a fast loop on the console too: each value lasts a sample or more, so
 * both platforms see the same sequence); the run's four windows are logged after the run (LOG costs ~35 us a line on
 * the console, nothing in the model) */
#define NPOLL 3000
static uint32_t pt[NPOLL], peg[NPOLL], pca[NPOLL];
static uint8_t pw[NPOLL];
static int np;
static const char *wname[4] = {"play_end", "kyonex_kyonb1", "key_off", "key_on"};
static void poll(int w, uint32_t us) {
    uint64_t t0 = now_us();
    uint32_t leg = 0xFFFFFFFF, lca = 0xFFFFFFFF;
    while (now_us() - t0 < us) {
        uint32_t eg = ar(R_EGMON) & 0xFFFF, ca = ar(R_CAMON) & 0xFFFF;
        if ((eg != leg || ca != lca) && np < NPOLL) {
            pt[np] = (uint32_t)(now_us() - t0); peg[np] = eg; pca[np] = ca; pw[np] = (uint8_t)w; np++;
        }
        leg = eg; lca = ca;
    }
}
static void flush(const char *tag) {
    int cnt[4] = {0, 0, 0, 0};
    for (int i = 0; i < np; i++) {
        LOG("%s %s t %6lu: EG %04lx (LP %lu st %lu a %04lx) CA %04lx\n", tag, wname[pw[i]], (unsigned long)pt[i],
            (unsigned long)peg[i], (unsigned long)(peg[i] >> 15), (unsigned long)((peg[i] >> 13) & 3),
            (unsigned long)(peg[i] & 0x1FFF), (unsigned long)pca[i]);
        cnt[pw[i]]++;
    }
    for (int w = 0; w < 4; w++) LOG("%s %s: %d changes\n", tag, wname[w], cnt[w]);
    if (np >= NPOLL) LOG("%s: buffer full\n", tag);
    np = 0;
}

static void run(const char *tag, int ar_, int d1r, int dl) {
    aica_quiet();
    for (int i = 0; i < 64; i++) ram_w32(0x20000 + 4 * i, 0x40004000);
    slot_cfg_t c;
    slot_cfg_default(&c, 0x20000, 64);
    c.LPCTL = 0; c.AR = ar_; c.D1R = d1r; c.DL = dl; c.D2R = 0; c.RR = 31; c.ISEL = 1; c.VOFF = 1;
    slot_write(0, &c);
    aw(R_MSLC, 0);
    LOG("%s: one-shot LEA 64, AR %d D1R %d DL %d D2R 0 RR 31\n", tag, ar_, d1r, dl);
    ch_keyon(0);
    poll(0, 5000);
    aw(CH(0, 0x00), (ar(CH(0, 0x00)) & 0x7FFF) | 0x8000);   /* KYONEX, KYONB still 1 */
    poll(1, 3000);
    ch_keyoff(0);
    poll(2, 3000);
    ch_keyon(0);
    poll(3, 3000);
    flush(tag);
    ch_keyoff(0);
    spin_us(20000);
}

int test_main(void) {
    out_open("oneshot.txt");
    run("H", 31, 0, 0);
    run("D", 31, 12, 31);
    run("A", 6, 0, 0);
    aica_quiet();
    out_close();
    return 0;
}
