/* sgc_loop.c -- loop control.  Sample data: PCM16 ramp s[i] = 8*i - 0x4000 (distinct, so the playback position is
 * readable from the output; VOFF=1, LPOFF=1).  Runs (4 slots each, 120 ms):
 *   A  LSA 100 LEA 200 at pitch 1.0 / 1.37 / 3.0 (OCT 1 FNS 0x200) / 0.25 (OCT -2)
 *   B  one-shot (LPCTL=0) LEA 300 at pitch 1.0 / 1.37; tiny loops LSA=LEA-1 and LSA=LEA
 *   C  LEA < LSA (LSA 200 LEA 100), LSA 0 LEA 0, LPSLNK with a slow attack (AR 8, LSA 2000), loop at 65535
 * The EG monitor (LP flag, state) and CA of slot 0 are polled during each run (changes only).
 * Output: sgc_loop.txt, lo_<n>.hdr/.bin, mon_<n>.txt */
#include "cap.h"
#define NS 4
#define MAXV (2u << 20)
static int32_t capbuf[MAXV];
static int16_t smp[4096];
static struct { uint32_t n; uint16_t eg, ca; } mon[100000];
static uint32_t nmon;
static void hook(void) {
    uint16_t eg = (uint16_t)ar(R_EGMON), ca = (uint16_t)ar(R_CAMON);
    if (nmon < 100000 && (nmon == 0 || mon[nmon - 1].eg != eg || mon[nmon - 1].ca != ca)) {
        mon[nmon].n = cap_head_now() - CAP.n_first; mon[nmon].eg = eg; mon[nmon].ca = ca; nmon++;
    }
}
typedef struct { uint16_t lsa, lea; int lpctl, oct, fns, ar, lpslnk; } lcfg;

int test_main(void) {
    out_open("sgc_loop.txt");
    for (int i = 0; i < 4096; i++) smp[i] = (int16_t)(8 * i - 0x4000);
    static const lcfg runs[3][4] = {
        {{100, 200, 1, 0, 0, 31, 0}, {100, 200, 1, 0, 0x17B, 31, 0}, {100, 200, 1, 1, 0x200, 31, 0}, {100, 200, 1, 14, 0, 31, 0}},
        {{0, 300, 0, 0, 0, 31, 0}, {0, 300, 0, 0, 0x17B, 31, 0}, {199, 200, 1, 0, 0, 31, 0}, {200, 200, 1, 0, 0, 31, 0}},
        {{200, 100, 1, 0, 0, 31, 0}, {0, 0, 1, 0, 0, 31, 0}, {2000, 3000, 1, 0, 0, 8, 1}, {1000, 3000, 1, 0, 0, 8, 0}},
    };
    for (int r = 0; r < 3; r++) {
        aica_quiet();
        ram_write(0x20000, smp, sizeof smp);
        for (int k = 0; k < NS; k++) {
            const lcfg *L = &runs[r][k];
            slot_cfg_t c;
            slot_cfg_default(&c, 0x20000, L->lea);
            c.LSA = L->lsa; c.LPCTL = L->lpctl; c.OCT = L->oct; c.FNS = L->fns; c.AR = L->ar; c.LPSLNK = L->lpslnk;
            c.ISEL = k; c.VOFF = (L->lpslnk || L->ar != 31) ? 0 : 1; c.D1R = L->lpslnk ? 20 : 0; c.DL = 0;
            slot_write(k, &c);
            LOG("lo_%d stream %d: LSA %d LEA %d LPCTL %d OCT %d FNS %03x AR %d LPSLNK %d VOFF %d\n", r, k, L->lsa,
                L->lea, L->lpctl, L->oct, L->fns, L->ar, L->lpslnk, c.VOFF);
        }
        static const int mixs[NS] = {0, 1, 2, 3};
        if (cap_start(NS, mixs, capbuf, MAXV)) { OUT("cap_start failed\n"); return 1; }
        aw(R_MSLC, 0 << 8);
        nmon = 0;
        CAP_HOOK = hook;
        cap_wait_us(5000);
        for (int k = 0; k < NS; k++) aw(CH(k, 0x00), (ar(CH(k, 0x00)) & 0x3FFF) | 0x4000);
        aw(CH(0, 0x00), (ar(CH(0, 0x00)) & 0x7FFF) | 0x8000);
        cap_mark(1);
        cap_wait_us(120000);
        CAP_HOOK = 0;
        LOG("lo_%d after run: slot regs +00:", r);
        for (int k = 0; k < NS; k++) LOG(" %04lx", (unsigned long)(ar(CH(k, 0x00)) & 0xFFFF));
        LOG("\n");
        uint32_t n = cap_stop();
        char nm[32];
        snprintf(nm, sizeof nm, "lo_%d", r);
        cap_save(nm, n);
        static char mb[1 << 20];
        uint32_t len = 0;
        for (uint32_t i = 0; i < nmon && len < sizeof mb - 64; i++)
            len += snprintf(mb + len, sizeof mb - len, "%lu %04x %04x\n", (unsigned long)mon[i].n, mon[i].eg, mon[i].ca);
        snprintf(nm, sizeof nm, "mon_%d.txt", r);
        io_write_file(nm, mb, len);
        OUT("%s: %lu samples, errors %lu, %lu monitor changes\n", "lo", (unsigned long)n, (unsigned long)CAP.errors,
            (unsigned long)nmon);
    }
    aica_quiet();
    out_close();
    return 0;
}
