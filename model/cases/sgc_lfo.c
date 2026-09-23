/* sgc_lfo.c -- LFOs.  ALFO runs: constant 0x7FFF, TL 0, instant attack (AR 31, KRS 1), no decay: the captured level
 * gives the attenuation per sample = the ALFO contribution.  PLFO runs: PCM16 ramp s[i] = 8 i - 0x4000 over a 4096
 * loop, pitch 1.0, VOFF=1: the output gives the playback position to 1/64 sample.  4 slots per run.
 * LFORE is a held reset on hardware: set while configuring, released just before key-on.
 * Output: sgc_lfo.txt (run/stream -> settings), lf_<run>.hdr/.bin */
#include "cap.h"
#define NS 4
#define MAXV (2u << 20)
static int32_t capbuf[MAXV];
static int16_t ramp[4096 + 64];
typedef struct { int kind, ws, s, lfof; } L; /* kind 0 ALFO, 1 PLFO */
static const struct { uint32_t ms; L l[4]; } runs[] = {
    {1500, {{0, 0, 7, 20}, {0, 1, 7, 20}, {0, 2, 7, 20}, {0, 3, 7, 20}}},
    {1500, {{0, 2, 1, 20}, {0, 2, 3, 20}, {0, 2, 5, 20}, {0, 2, 6, 20}}},
    {600, {{0, 0, 7, 31}, {0, 0, 7, 29}, {0, 0, 7, 26}, {0, 0, 7, 23}}},
    {6000, {{0, 0, 7, 16}, {0, 0, 7, 12}, {0, 0, 7, 8}, {0, 0, 7, 4}}},
    {1500, {{1, 0, 7, 20}, {1, 1, 7, 20}, {1, 2, 7, 20}, {1, 3, 7, 20}}},
    {1500, {{1, 2, 1, 20}, {1, 2, 3, 20}, {1, 2, 5, 20}, {1, 2, 6, 20}}},
};

int test_main(void) {
    out_open("sgc_lfo.txt");
    for (int i = 0; i < 4096 + 64; i++) ramp[i] = (int16_t)(8 * (i & 4095) - 0x4000);
    int nr = sizeof runs / sizeof runs[0];
    for (int r = 0; r < nr; r++) {
        aica_quiet();
        for (int i = 0; i < 2048; i++) ram_w32(0x40000 + 4 * i, 0x7FFF7FFF);
        ram_write(0x20000, ramp, sizeof ramp);
        for (int k = 0; k < NS; k++) {
            const L *l = &runs[r].l[k];
            slot_cfg_t c;
            slot_cfg_default(&c, l->kind ? 0x20000 : 0x40000, 4096);
            c.ISEL = k; c.AR = 31; c.KRS = 1; c.LFOF = l->lfof;
            if (l->kind == 0) { c.ALFOWS = l->ws; c.ALFOS = l->s; }
            else { c.PLFOWS = l->ws; c.PLFOS = l->s; c.VOFF = 1; }
            c.LFORE = 1;
            slot_write(k, &c);
            LOG("lf_%d stream %d: %s WS %d S %d LFOF %d\n", r, k, l->kind ? "PLFO" : "ALFO", l->ws, l->s, l->lfof);
        }
        static const int mixs[NS] = {0, 1, 2, 3};
        if (cap_start(NS, mixs, capbuf, MAXV)) { OUT("cap_start failed\n"); return 1; }
        cap_wait_us(5000);
        for (int k = 0; k < NS; k++) aw(CH(k, 0x1C), ar(CH(k, 0x1C)) & 0x7FFF); /* release LFORE (held reset) */
        for (int k = 0; k < NS; k++) aw(CH(k, 0x00), (ar(CH(k, 0x00)) & 0x3FFF) | 0x4000);
        aw(CH(0, 0x00), (ar(CH(0, 0x00)) & 0x7FFF) | 0x8000);
        cap_mark(1);
        cap_wait_us(runs[r].ms * 1000);
        uint32_t n = cap_stop();
        char nm[32];
        snprintf(nm, sizeof nm, "lf_%d", r);
        cap_save(nm, n);
        OUT("%s: %lu samples, errors %lu\n", nm, (unsigned long)n, (unsigned long)CAP.errors);
    }
    aica_quiet();
    out_close();
    return 0;
}
