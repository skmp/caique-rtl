/* eg_sched.c -- where in the sample does the SGC process slot k?  Hypothesis (bus 22.5792 MHz, 512 cycles per sample,
 * one 8-cycle frame per slot in slot order): a register write for slot k acts in the CURRENT sample iff it lands before
 * frame k, so the effect-sample offset of a write depends on the slot number by up to a whole sample; key events may be
 * processed per frame too (then two slots keyed by ONE KYONEX can key on in different samples) or at the boundary.
 * Every write group has two test slots A, B (buses 0, 1) and two witnesses wl (low slot number) / wh (high) on buses
 * 2 / 3 keyed ON by the group's KYONEX (AR 31 KRS 1: R 63, level 520176 on their key-on sample).  Runs:
 *   f_<A>_<B>  fetch probe: A, B play a constant with VOFF 1 (output = 16 x sample, no envelope), the group rewrites
 *              reg 0x00 with the SA[22:16] field toggled between two blocks holding 0x4000 and 0x2000 (KYONB kept 1;
 *              CA runs on: tests/eg_latch3): the effect sample is the output toggle.  4 events per cycle (8 ms apart,
 *              witnesses keyed off in between), 16 cycles.
 *   e_<A>_<B>  envelope probe: A, B use the eg_latch2 rr0 skeleton (AR 31 D1R 24 DL 4, key-off with RR 0 at 10 ms:
 *              release held at a 0x80, level 130032); the group writes reg 0x14 RR 0 -> 30 (+8 per clock): the effect
 *              sample is the first level drop (quantised to the envelope clock).  1 event per cycle, 24 cycles.
 * Write order alternates per event (order 0: A, B, KYONEX; order 1: KYONEX, A, B).  Marks after every event.
 * Slot sets: (A, B, wl, wh) = (0, 32, 1, 62), (8, 40, 2, 61), (16, 48, 3, 60), (24, 56, 4, 63).
 * Text: "<run> run: kind <f|e> A a B b wl l wh h cycles n" and per event "<run> cyc c ev j order o t_us t".
 * Analysis: build/tools/sched_check tests/eg_sched/hw.   Output: eg_sched.txt, <run>.hdr/.bin. */
#include "cap.h"
#define NS 4
#define MAXV (2u << 20)
#define SA_A 0x10000u
#define SA_B 0x20000u
#define NCONST 4224
#define LEA0 4096
static int32_t capbuf[MAXV];
static const int mixs[NS] = {0, 1, 2, 3};
typedef struct { int A, B, wl, wh; } set_t;
static const set_t sets[4] = {{0, 32, 1, 62}, {8, 40, 2, 61}, {16, 48, 3, 60}, {24, 56, 4, 63}};
static uint16_t r0[64];
static uint32_t seed = 4711;
static uint32_t lcg(void) { seed = seed * 1103515245u + 12345u; return seed >> 16; }
static void wait_until(uint64_t t) { while (now_us() < t) cap_poll(); }
static void kyonex(int slot) { aw(CH(slot, 0), (ar(CH(slot, 0)) & 0x7FFF) | 0x8000); }
static void kyonb(int slot, int on) { aw(CH(slot, 0), (r0[slot] & 0x3FFF) | (on ? 0x4000 : 0)); }

static void setup(const set_t *s, int kind, const char *name) {
    aica_quiet();
    int test[2] = {s->A, s->B}, wit[2] = {s->wl, s->wh};
    for (int i = 0; i < 2; i++) {
        slot_cfg_t c;
        slot_cfg_default(&c, SA_A, LEA0);
        c.ISEL = i; c.KRS = 15;
        if (kind == 'f') { c.VOFF = 1; c.AR = 31; c.D1R = 0; c.RR = 0; }
        else { c.AR = 31; c.D1R = 24; c.DL = 4; c.D2R = 0; c.RR = 0; }
        slot_write(test[i], &c);
        r0[test[i]] = ar(CH(test[i], 0)) & 0x3FFF;
        LOG("%s stream %d: slot %d role test AR %d D1R %d DL %d D2R %d RR %d KRS %d VOFF %d\n", name, i, test[i], c.AR, c.D1R, c.DL, c.D2R, c.RR, c.KRS, c.VOFF);
    }
    for (int i = 0; i < 2; i++) {
        slot_cfg_t c;
        slot_cfg_default(&c, SA_A, 32);   /* 0x4000 constant: witness level 16*floor(16384*127/128) = 260080 at a 0 */
        c.ISEL = 2 + i; c.AR = 31; c.D1R = 31; c.DL = 31; c.D2R = 31; c.RR = 31; c.KRS = 1;
        slot_write(wit[i], &c);
        r0[wit[i]] = ar(CH(wit[i], 0)) & 0x3FFF;
        LOG("%s stream %d: slot %d role witness AR 31 D1R 31 DL 31 D2R 31 RR 31 KRS 1\n", name, 2 + i, wit[i]);
    }
    LOG("%s run: kind %c A %d B %d wl %d wh %d cycles 24\n", name, kind, s->A, s->B, s->wl, s->wh);
}
/* the group: two register writes on A and B + the KYONEX (witness KYONB set on both just before, test slots unchanged) */
static void group(const set_t *s, int order, int reg, uint16_t va, uint16_t vb) {
    if (order == 0) { aw(CH(s->A, reg), va); aw(CH(s->B, reg), vb); aw(CH(s->wl, 0), r0[s->wl] | 0xC000); }
    else { aw(CH(s->wl, 0), r0[s->wl] | 0xC000); aw(CH(s->A, reg), va); aw(CH(s->B, reg), vb); }
}
static void run_f(const set_t *s, const char *name) {
    setup(s, 'f', name);
    if (cap_start(NS, mixs, capbuf, MAXV)) { OUT("%s: cap_start failed\n", name); return; }
    cap_wait_us(5000);
    kyonb(s->A, 1); kyonb(s->B, 1); kyonb(s->wl, 0); kyonb(s->wh, 0); kyonex(s->A);   /* test slots on */
    cap_wait_us(5000);
    kyonb(s->wl, 1); kyonb(s->wh, 1); kyonex(s->wl); cap_wait_us(8000); kyonb(s->wl, 0); kyonb(s->wh, 0); kyonex(s->wl); cap_wait_us(3000);   /* warm-up witness cycle */
    int ev = 0, base = 0;
    for (int cyc = 0; cyc < 24; cyc++) {
        uint64_t t0 = now_us();
        for (int j = 0; j < 4; j++, ev++) {
            int order = ev & 1;
            base ^= 1;
            uint16_t va = (r0[s->A] & ~0x7F) | 0x4000 | (base ? (SA_B >> 16) : (SA_A >> 16));
            uint16_t vb = (r0[s->B] & ~0x7F) | 0x4000 | (base ? (SA_B >> 16) : (SA_A >> 16));
            wait_until(t0 + 8000 * j + 1800);
            kyonb(s->wh, 1);                                     /* wh's KYONB armed >= 1 ms after the last KYONEX (KYONB is sampled when a KYONEX is processed, tests/kon_defer) */
            wait_until(t0 + 8000 * j + 2000 + lcg() % 1500);
            spin_us(lcg() % 46);
            uint64_t tb = now_us();
            group(s, order, 0x00, va, vb);
            cap_mark(1 + j);
            LOG("%s cyc %d ev %d order %d base %d t_us %lu\n", name, cyc, j, order, base, (unsigned long)(tb - t0));
            wait_until(tb + 5600);
            kyonb(s->wl, 0); kyonb(s->wh, 0); kyonex(s->wl);     /* witnesses off (off by themselves at 5.4 ms); the next arming comes >= 0.6 ms later */
        }
        wait_until(t0 + 32000);
    }
    uint32_t n = cap_stop();
    cap_save(name, n);
    OUT("%s: %lu samples, errors %lu, %lu marks\n", name, (unsigned long)n, (unsigned long)CAP.errors, (unsigned long)CAP.nev);
}
static void run_e(const set_t *s, const char *name) {
    setup(s, 'e', name);
    if (cap_start(NS, mixs, capbuf, MAXV)) { OUT("%s: cap_start failed\n", name); return; }
    cap_wait_us(5000);
    kyonb(s->wl, 1); kyonb(s->wh, 1); kyonex(s->wl); cap_wait_us(8000); kyonb(s->wl, 0); kyonb(s->wh, 0); kyonex(s->wl); cap_wait_us(3000);   /* warm-up witness cycle */
    const uint16_t rr_old = (15 << 10) | (4 << 5) | 0, rr_new = (15 << 10) | (4 << 5) | 30;
    for (int cyc = 0; cyc < 24; cyc++) {
        int order = cyc & 1;
        uint64_t t0 = now_us();
        kyonb(s->A, 1); kyonb(s->B, 1); kyonb(s->wl, 0); kyonb(s->wh, 0); kyonex(s->A);   /* test on, witnesses off */
        wait_until(t0 + 10000);
        kyonb(s->A, 0); kyonb(s->B, 0); kyonex(s->A);                                    /* key-off: the release holds at 0x80 */
        wait_until(t0 + 11000);
        kyonb(s->wh, 1);                                                                 /* armed 1 ms after that KYONEX */
        wait_until(t0 + 12000 + lcg() % 1500);
        spin_us(lcg() % 46);
        uint64_t tb = now_us();
        group(s, order, 0x14, rr_new, rr_new);
        cap_mark(1);
        LOG("%s cyc %d ev 0 order %d base 0 t_us %lu\n", name, cyc, order, (unsigned long)(tb - t0));
        wait_until(t0 + 20000);
        aw(CH(s->A, 0x14), rr_old); aw(CH(s->B, 0x14), rr_old);
        wait_until(t0 + 24000);
    }
    uint32_t n = cap_stop();
    cap_save(name, n);
    OUT("%s: %lu samples, errors %lu, %lu marks\n", name, (unsigned long)n, (unsigned long)CAP.errors, (unsigned long)CAP.nev);
}
int test_main(void) {
    out_open("eg_sched.txt");
    aica_quiet();
    for (int i = 0; i < NCONST / 2; i++) { ram_w32(SA_A + 4 * i, 0x40004000); ram_w32(SA_B + 4 * i, 0x20002000); }
    char name[32];
    for (int i = 0; i < 4; i++) { snprintf(name, sizeof name, "f_%d_%d", sets[i].A, sets[i].B); run_f(&sets[i], name); }
    for (int i = 0; i < 4; i++) { snprintf(name, sizeof name, "e_%d_%d", sets[i].A, sets[i].B); run_e(&sets[i], name); }
    aica_quiet();
    out_close();
    return 0;
}
