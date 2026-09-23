/* eg_sched2.c -- eg_sched with an ABSOLUTE anchor: every write group also writes MIXS bus 3 (no slot points at it), and
 * the CPU-written value appears in the capture on the first sample after the next sample boundary (tests/mixs_rd:
 * the CPU writes the bank the DSP reads next).  So per group: M = that sample; E = the witness (slot wl, bus 2) key-on
 * sample; S_A / S_B = the register effect samples of the test slots A / B (buses 0 / 1).  Offsets E - M, S - M are
 * absolute.  Group order 0: [A][B][MIXS3 hi][MIXS3 lo][KYONEX on wl]; order 1: [KYONEX][A][B][MIXS3].  Otherwise as
 * eg_sched (f: SA toggle with VOFF 1, 4 events per cycle, 24 cycles; e: RR 0 -> 30 on a held release, 1 event per
 * cycle, 24 cycles).  Slot sets (A, B, wl): (0, 32, 1), (8, 40, 2), (16, 48, 3), (24, 56, 4).
 * Text: "<run> run: kind <f|e> A a B b wl l cycles 24" and per event "<run> cyc c ev j order o mixs v t_us t".
 * Analysis: build/tools/sched2_check tests/eg_sched2/hw.   Output: eg_sched2.txt, <run>.hdr/.bin. */
#include "cap.h"
#define NS 4
#define MAXV (2u << 20)
#define SA_A 0x10000u
#define SA_B 0x20000u
#define NCONST 4224
#define LEA0 4096
static int32_t capbuf[MAXV];
static const int mixs[NS] = {0, 1, 2, 3};
typedef struct { int A, B, wl; } set_t;
static const set_t sets[4] = {{0, 32, 1}, {8, 40, 2}, {16, 48, 3}, {24, 56, 4}};
static uint16_t r0[64];
static uint32_t seed = 4713;
static uint32_t lcg(void) { seed = seed * 1103515245u + 12345u; return seed >> 16; }
static void wait_until(uint64_t t) { while (now_us() < t) cap_poll(); }
static void kyonex(int slot) { aw(CH(slot, 0), (ar(CH(slot, 0)) & 0x7FFF) | 0x8000); }
static void kyonb(int slot, int on) { aw(CH(slot, 0), (r0[slot] & 0x3FFF) | (on ? 0x4000 : 0)); }
static void setup(const set_t *s, int kind, const char *name) {
    aica_quiet();
    int test[2] = {s->A, s->B};
    for (int i = 0; i < 2; i++) {
        slot_cfg_t c; slot_cfg_default(&c, SA_A, LEA0); c.ISEL = i; c.KRS = 15;
        if (kind == 'f') { c.VOFF = 1; c.AR = 31; c.D1R = 0; c.RR = 0; } else { c.AR = 31; c.D1R = 24; c.DL = 4; c.D2R = 0; c.RR = 0; }
        slot_write(test[i], &c); r0[test[i]] = ar(CH(test[i], 0)) & 0x3FFF;
        LOG("%s stream %d: slot %d role test AR %d D1R %d DL %d D2R %d RR %d KRS %d VOFF %d\n", name, i, test[i], c.AR, c.D1R, c.DL, c.D2R, c.RR, c.KRS, c.VOFF);
    }
    slot_cfg_t c; slot_cfg_default(&c, SA_A, 32); c.ISEL = 2; c.AR = 31; c.D1R = 31; c.DL = 31; c.D2R = 31; c.RR = 31; c.KRS = 1;
    slot_write(s->wl, &c); r0[s->wl] = ar(CH(s->wl, 0)) & 0x3FFF;
    LOG("%s stream 2: slot %d role witness AR 31 D1R 31 DL 31 D2R 31 RR 31 KRS 1\n", name, s->wl);
    LOG("%s stream 3: MIXS3 written by the CPU in every group (anchor)\n", name);
    LOG("%s run: kind %c A %d B %d wl %d cycles 24\n", name, kind, s->A, s->B, s->wl);
}
static void group(const set_t *s, int order, int reg, uint16_t va, uint16_t vb, uint16_t mv) {
    if (order == 0) { aw(CH(s->A, reg), va); aw(CH(s->B, reg), vb); aw(R_MIXS(3, 1), mv); aw(R_MIXS(3, 0), 0); aw(CH(s->wl, 0), r0[s->wl] | 0xC000); }
    else { aw(CH(s->wl, 0), r0[s->wl] | 0xC000); aw(CH(s->A, reg), va); aw(CH(s->B, reg), vb); aw(R_MIXS(3, 1), mv); aw(R_MIXS(3, 0), 0); }
}
static void run_f(const set_t *s, const char *name) {
    setup(s, 'f', name);
    if (cap_start(NS, mixs, capbuf, MAXV)) { OUT("%s: cap_start failed\n", name); return; }
    cap_wait_us(5000);
    kyonb(s->A, 1); kyonb(s->B, 1); kyonb(s->wl, 0); kyonex(s->A);
    cap_wait_us(5000);
    kyonb(s->wl, 1); kyonex(s->wl); cap_wait_us(8000); kyonb(s->wl, 0); kyonex(s->wl); cap_wait_us(3000);
    int ev = 0, base = 0;
    for (int cyc = 0; cyc < 24; cyc++) {
        uint64_t t0 = now_us();
        for (int j = 0; j < 4; j++, ev++) {
            int order = ev & 1; base ^= 1;
            uint16_t va = (r0[s->A] & ~0x7F) | 0x4000 | (base ? (SA_B >> 16) : (SA_A >> 16));
            uint16_t vb = (r0[s->B] & ~0x7F) | 0x4000 | (base ? (SA_B >> 16) : (SA_A >> 16));
            uint16_t mv = 0x100 + ev;
            wait_until(t0 + 8000 * j + 2000 + lcg() % 1500);
            spin_us(lcg() % 46);
            uint64_t tb = now_us();
            group(s, order, 0x00, va, vb, mv);
            cap_mark(1 + j);
            LOG("%s cyc %d ev %d order %d mixs %03x t_us %lu\n", name, cyc, j, order, mv, (unsigned long)(tb - t0));
            wait_until(tb + 5600);
            kyonb(s->wl, 0); kyonex(s->wl);
        }
        wait_until(t0 + 32000);
    }
    uint32_t n = cap_stop(); cap_save(name, n);
    OUT("%s: %lu samples, errors %lu, %lu marks\n", name, (unsigned long)n, (unsigned long)CAP.errors, (unsigned long)CAP.nev);
}
static void run_e(const set_t *s, const char *name) {
    setup(s, 'e', name);
    if (cap_start(NS, mixs, capbuf, MAXV)) { OUT("%s: cap_start failed\n", name); return; }
    cap_wait_us(5000);
    kyonb(s->wl, 1); kyonex(s->wl); cap_wait_us(8000); kyonb(s->wl, 0); kyonex(s->wl); cap_wait_us(3000);
    const uint16_t rr_old = (15 << 10) | (4 << 5) | 0, rr_new = (15 << 10) | (4 << 5) | 30;
    for (int cyc = 0; cyc < 24; cyc++) {
        int order = cyc & 1;
        uint64_t t0 = now_us();
        kyonb(s->A, 1); kyonb(s->B, 1); kyonb(s->wl, 0); kyonex(s->A);
        wait_until(t0 + 10000);
        kyonb(s->A, 0); kyonb(s->B, 0); kyonex(s->A);
        wait_until(t0 + 12000 + lcg() % 1500);
        spin_us(lcg() % 46);
        uint64_t tb = now_us();
        uint16_t mv = 0x200 + cyc;
        group(s, order, 0x14, rr_new, rr_new, mv);
        cap_mark(1);
        LOG("%s cyc %d ev 0 order %d mixs %03x t_us %lu\n", name, cyc, order, mv, (unsigned long)(tb - t0));
        wait_until(t0 + 20000);
        aw(CH(s->A, 0x14), rr_old); aw(CH(s->B, 0x14), rr_old);
        wait_until(t0 + 24000);
    }
    uint32_t n = cap_stop(); cap_save(name, n);
    OUT("%s: %lu samples, errors %lu, %lu marks\n", name, (unsigned long)n, (unsigned long)CAP.errors, (unsigned long)CAP.nev);
}
int test_main(void) {
    out_open("eg_sched2.txt");
    aica_quiet();
    for (int i = 0; i < NCONST / 2; i++) { ram_w32(SA_A + 4 * i, 0x40004000); ram_w32(SA_B + 4 * i, 0x20002000); }
    char name[32];
    for (int i = 0; i < 4; i++) { snprintf(name, sizeof name, "f_%d_%d", sets[i].A, sets[i].B); run_f(&sets[i], name); }
    for (int i = 0; i < 4; i++) { snprintf(name, sizeof name, "e_%d_%d", sets[i].A, sets[i].B); run_e(&sets[i], name); }
    aica_quiet(); out_close(); return 0;
}
