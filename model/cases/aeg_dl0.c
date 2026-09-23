/* aeg_dl0.c -- amplitude envelope with DL = 0 (copy of the sgc_aeg harness): 4 slots (MIXS 0..3) play a looped constant 0x7FFF, TL 0, filter off, KRS 15, pitch
 * 1.0; keyed on together, captured, keyed off, captured.  The attenuation per sample follows from the level law
 * (tests/sgc_level).  The EG monitor of slot 0 (13 bits + state) is polled during the capture with a sample
 * timestamp (approximate: CPU-side head estimate).
 * Run: DL 0 with D1R 31/20/10 (D2R 0), and AR 20 + D1R 31 + DL 0 + D2R 10: does decay 1 ever act when DL = 0?
 * Output: aeg_dl0.txt, <run>.hdr/.bin captures, <run>_eg.txt monitor samples */
#include "cap.h"

#define NS 4
#define MAXV (2u << 20)
static int32_t capbuf[MAXV];
static struct { uint32_t n; uint16_t eg; } egs[200000];
static uint32_t negs;
static uint32_t nreads;
static void eg_hook(void) { /* keep changes only */
    uint16_t v = (uint16_t)ar(R_EGMON);
    nreads++;
    if (negs < 200000 && (negs == 0 || egs[negs - 1].eg != v)) { egs[negs].n = cap_head_now() - CAP.n_first; egs[negs].eg = v; negs++; }
}

typedef struct { const char *name; int AR[4], D1R[4], DL[4], D2R[4], RR[4]; uint32_t on_ms, off_ms; } run_t;

static void do_run(const run_t *r) {
    aica_quiet();
    uint32_t w = 0x7FFF7FFF;
    for (int i = 0; i < 16; i++) ram_w32(0x10000 + 4 * i, w);
    for (int k = 0; k < NS; k++) {
        slot_cfg_t c;
        slot_cfg_default(&c, 0x10000, 32);
        c.ISEL = k;
        c.AR = r->AR[k]; c.D1R = r->D1R[k]; c.DL = r->DL[k]; c.D2R = r->D2R[k]; c.RR = r->RR[k];
        slot_write(k, &c);
    }
    static const int mixs[NS] = {0, 1, 2, 3};
    if (cap_start(NS, mixs, capbuf, MAXV)) { OUT("%s: cap_start failed\n", r->name); return; }
    aw(R_MSLC, 0 << 8);
    negs = 0; nreads = 0;
    CAP_HOOK = eg_hook;
    cap_wait_us(10000);
    cap_mark(1);
    for (int k = 0; k < NS; k++) aw(CH(k, 0x00), (ar(CH(k, 0x00)) & 0x3FFF) | 0x4000);
    aw(CH(0, 0x00), (ar(CH(0, 0x00)) & 0x7FFF) | 0x8000);
    cap_mark(2);
    cap_wait_us(r->on_ms * 1000);
    cap_mark(3);
    for (int k = 0; k < NS; k++) aw(CH(k, 0x00), ar(CH(k, 0x00)) & 0x3FFF);
    aw(CH(0, 0x00), (ar(CH(0, 0x00)) & 0x3FFF) | 0x8000);
    cap_mark(4);
    cap_wait_us(r->off_ms * 1000);
    CAP_HOOK = 0;
    uint32_t n = cap_stop();
    OUT("%s: %lu samples, counter errors %lu, %lu EG monitor reads, %lu changes\n", r->name, (unsigned long)n,
        (unsigned long)CAP.errors, (unsigned long)nreads, (unsigned long)negs);
    cap_save(r->name, n);
    /* EG monitor samples: only changes */
    static char eb[1 << 20];
    uint32_t len = 0;
    for (uint32_t i = 0; i < negs && len < sizeof eb - 64; i++)
        len += snprintf(eb + len, sizeof eb - len, "%lu %04x\n", (unsigned long)egs[i].n, egs[i].eg);
    char nm[64];
    snprintf(nm, sizeof nm, "%s_eg.txt", r->name);
    io_write_file(nm, eb, len);
}

int test_main(void) {
    out_open("aeg_dl0.txt");
    static const run_t runs[] = {
        {"dl0", {31, 31, 31, 20}, {31, 20, 10, 31}, {0, 0, 0, 0}, {0, 0, 0, 10}, {31, 31, 31, 31}, 400, 50},
    };
    for (unsigned i = 0; i < sizeof runs / sizeof runs[0]; i++) do_run(&runs[i]);
    aica_quiet();
    out_close();
    return 0;
}
