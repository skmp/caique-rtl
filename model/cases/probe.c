/* probe.c -- first contact: register read/write masks, sample-rate timer, live MIXS/EG/CA monitors, and whether a
 * trivial DSP program can copy a channel's MIXS input into wave RAM, TEMP and EFREG.
 * Output: tests/probe/probe.txt
 */
#include "aica_io.h"

static void rw_mask(const char *name, uint32_t off, uint32_t wmask) {
    static const uint32_t pat[] = {0xFFFFFFFFu, 0x00000000u, 0x55555555u, 0xAAAAAAAAu};
    uint32_t old = ar(off);
    LOG("%-10s %04lx old %08lx:", name, (unsigned long)off, (unsigned long)old);
    for (int i = 0; i < 4; i++) {
        aw(off, pat[i] & wmask);
        LOG(" w%08lx->r%08lx", (unsigned long)(pat[i] & wmask), (unsigned long)ar(off));
    }
    aw(off, old);
    LOG("\n");
}

int test_main(void) {
    if (out_open("probe.txt")) return 1;
    aica_quiet();

    OUT("VER/MVOL 2800 = %08lx  RBPL 2804 = %08lx  ARMRST 2C00 = %08lx\n", (unsigned long)ar(0x2800),
        (unsigned long)ar(0x2804), (unsigned long)ar(0x2C00));

    /* --- common register dump (reads only) --- */
    LOG("common regs:\n");
    for (uint32_t o = 0x2800; o < 0x28C0; o += 4) LOG("  %04lx: %08lx\n", (unsigned long)o, (unsigned long)ar(o));
    LOG("  2C00: %08lx 2D00: %08lx 2D04: %08lx\n", (unsigned long)ar(0x2C00), (unsigned long)ar(0x2D00),
        (unsigned long)ar(0x2D04));

    /* --- read/write masks: channel 63 registers, DSP register files --- */
    LOG("rw masks (patterns FFFFFFFF 0 55555555 AAAAAAAA):\n");
    for (uint32_t r = 0; r < 0x80; r += 4) {
        char n[16];
        snprintf(n, sizeof n, "ch63+%02lx", (unsigned long)r);
        rw_mask(n, CH(63, r), r == 0 ? 0xFFFF7FFFu : 0xFFFFFFFFu); /* never KYONEX */
    }
    aw(CH(63, 0), 0);
    rw_mask("COEF0", R_COEF(0), ~0u);
    rw_mask("COEF127", R_COEF(127), ~0u);
    rw_mask("MADRS0", R_MADRS(0), ~0u);
    rw_mask("MADRS63", R_MADRS(63), ~0u);
    for (int k = 0; k < 4; k++) rw_mask("MPRO127", R_MPRO(127, k), ~0u);
    rw_mask("TEMP0.l", R_TEMP(0, 0), ~0u);
    rw_mask("TEMP0.h", R_TEMP(0, 1), ~0u);
    rw_mask("TEMP127.h", R_TEMP(127, 1), ~0u);
    rw_mask("MEMS0.l", R_MEMS(0, 0), ~0u);
    rw_mask("MEMS0.h", R_MEMS(0, 1), ~0u);
    rw_mask("MEMS31.h", R_MEMS(31, 1), ~0u);
    rw_mask("MIXS0.l", R_MIXS(0, 0), ~0u);
    rw_mask("MIXS0.h", R_MIXS(0, 1), ~0u);
    rw_mask("EFREG0", R_EFREG(0), ~0u);
    rw_mask("EFREG15", R_EFREG(15), ~0u);
    rw_mask("EXTS0", R_EXTS(0), ~0u);
    rw_mask("EXTS1", R_EXTS(1), ~0u);
    rw_mask("RBPL", R_RBPL, ~0u);
    dsp_clear_prog();

    /* --- timer A as a sample counter: TACTL=0 --- */
    aw(R_TIMA, 0);
    {
        uint32_t t0 = ar(R_TIMA) & 0xFF;
        uint64_t u0 = now_us();
        uint32_t cnt = 0, last = t0;
        while (now_us() - u0 < 100000) {
            uint32_t t = ar(R_TIMA) & 0xFF;
            cnt += (t - last) & 0xFF;
            last = t;
        }
        uint64_t u1 = now_us();
        OUT("TIMA: %lu ticks in %lu us = %lu Hz (reg %08lx)\n", (unsigned long)cnt, (unsigned long)(u1 - u0),
            (unsigned long)((uint64_t)cnt * 1000000ull / (u1 - u0)), (unsigned long)ar(R_TIMA));
    }
    {   /* cost of a G2 read */
        uint64_t u0 = now_us();
        for (int i = 0; i < 10000; i++) (void)ar(R_TIMA);
        OUT("10000 G2 register reads: %lu us\n", (unsigned long)(now_us() - u0));
        u0 = now_us();
        for (int i = 0; i < 10000; i++) (void)ram_r32(0x100000);
        OUT("10000 G2 RAM reads: %lu us\n", (unsigned long)(now_us() - u0));
    }

    /* --- channel 0: constant PCM16 0x4000, looped, to MIXS[0] only --- */
    for (int i = 0; i < 64; i++) ram_w16(0x10000 + 2 * i, 0x4000);
    aw(CH(0, 0x04), 0x0000);                      /* SA[15:0] */
    aw(CH(0, 0x08), 0);                           /* LSA */
    aw(CH(0, 0x0C), 32);                          /* LEA */
    aw(CH(0, 0x10), 0x001F);                      /* AR=31 D1R=0 D2R=0 */
    aw(CH(0, 0x14), (0xF << 10) | 0x1F);          /* KRS=F DL=0 RR=31 */
    aw(CH(0, 0x18), 0);                           /* OCT=0 FNS=0 */
    aw(CH(0, 0x1C), 0);                           /* no LFO */
    aw(CH(0, 0x20), 0x00F0);                      /* IMXL=15 ISEL=0 */
    aw(CH(0, 0x24), 0);                           /* DISDL=0 */
    aw(CH(0, 0x28), 0x0020);                      /* TL=0 LPOFF=1 Q=0 */
    for (int r = 0x2C; r <= 0x3C; r += 4) aw(CH(0, r), 0x1FF8);
    aw(CH(0, 0x40), 0x1F1F);
    aw(CH(0, 0x44), 0x1F1F);
    aw(CH(0, 0x00), 0x0201);                      /* LPCTL=1 PCMS=0 SA[22:16]=1 */
    aw(R_MSLC, 0);                                /* monitor slot 0, AEG */
    OUT("before keyon: EG %08lx CA %08lx MIXS0 %08lx/%08lx\n", (unsigned long)ar(R_EGMON), (unsigned long)ar(R_CAMON),
        (unsigned long)ar(R_MIXS(0, 1)), (unsigned long)ar(R_MIXS(0, 0)));
    ch_keyon(0);
    for (int i = 0; i < 12; i++) {
        uint64_t u = now_us();
        uint32_t eg = ar(R_EGMON), ca = ar(R_CAMON), mh = ar(R_MIXS(0, 1)), ml = ar(R_MIXS(0, 0));
        OUT("t+%3d: EG %08lx CA %08lx MIXS0 h %08lx l %08lx  (%lu us)\n", i, (unsigned long)eg, (unsigned long)ca,
            (unsigned long)mh, (unsigned long)ml, (unsigned long)(now_us() - u));
        spin_us(1000);
    }
    {   /* histogram of raw MIXS0 reads */
        uint32_t vals[16] = {0}, cnts[16] = {0}, nv = 0;
        for (int i = 0; i < 4000; i++) {
            uint32_t v = (ar(R_MIXS(0, 1)) << 4) | (ar(R_MIXS(0, 0)) & 0xF);
            int k;
            for (k = 0; k < (int)nv; k++) if (vals[k] == v) break;
            if (k == (int)nv && nv < 16) { vals[nv] = v; nv++; }
            if (k < 16) cnts[k]++;
        }
        OUT("MIXS0 read histogram (h<<4|l):");
        for (uint32_t k = 0; k < nv; k++) OUT(" %05lx x%lu", (unsigned long)vals[k], (unsigned long)cnts[k]);
        OUT("\n");
    }

    /* --- DSP: steps 0..7 ACC = MIXS0 * COEF(-1); step 3 MWT NOFL ring; step 5 MWT float ring; step 9 TWT; step 11 EWT --- */
    ram_fill(0x100000, 0xDEADBEEF, 0x4000);
    dsp_ring(0x100000 >> 11, 0);                  /* 8K words at 0x100000 */
    dsp_madrs(0, 0x0000);
    dsp_madrs(1, 0x1000);
    for (int s = 0; s < 16; s++) dsp_coef(s, -4096);
    for (int s = 0; s < 14; s++) {
        dsp_inst_t in = {0};
        in.XSEL = 1; in.YSEL = 1; in.IRA = IRA_MIXS(0);
        if (s == 3) { in.MWT = 1; in.NOFL = 1; in.MASA = 0; }
        if (s == 5) { in.MWT = 1; in.NOFL = 0; in.MASA = 1; }
        if (s == 9) { in.TWT = 1; in.TWA = 0; }
        if (s == 11) { in.EWT = 1; in.EWA = 0; }
        dsp_put(s, &in);
    }
    spin_us(200000);
    {
        uint32_t vals[16] = {0}, cnts[16] = {0}, nv = 0;
        for (uint32_t a = 0x100000; a < 0x104000; a += 2) {
            uint32_t v = ram_r16(a);
            int k;
            for (k = 0; k < (int)nv; k++) if (vals[k] == v) break;
            if (k == (int)nv && nv < 16) { vals[nv] = v; nv++; }
            if (k < 16) cnts[k]++;
        }
        OUT("ring 8K words histogram:");
        for (uint32_t k = 0; k < nv; k++) OUT(" %04lx x%lu", (unsigned long)vals[k], (unsigned long)cnts[k]);
        OUT("\n");
        LOG("ring words 0x100000..: ");
        for (uint32_t a = 0x100000; a < 0x100040; a += 2) LOG(" %04x", ram_r16(a));
        LOG("\nring words 0x102000..: ");
        for (uint32_t a = 0x102000; a < 0x102040; a += 2) LOG(" %04x", ram_r16(a));
        LOG("\n");
    }
    OUT("TEMP[0..3] h/l:");
    for (int i = 0; i < 4; i++) OUT(" %04lx/%02lx", (unsigned long)ar(R_TEMP(i, 1)), (unsigned long)ar(R_TEMP(i, 0)));
    OUT("\nTEMP[64..67] h/l:");
    for (int i = 64; i < 68; i++) OUT(" %04lx/%02lx", (unsigned long)ar(R_TEMP(i, 1)), (unsigned long)ar(R_TEMP(i, 0)));
    OUT("\nEFREG[0..1]: %08lx %08lx\n", (unsigned long)ar(R_EFREG(0)), (unsigned long)ar(R_EFREG(1)));
    OUT("MIXS0 h/l now: %08lx %08lx  EG %08lx\n", (unsigned long)ar(R_MIXS(0, 1)), (unsigned long)ar(R_MIXS(0, 0)),
        (unsigned long)ar(R_EGMON));

    ch_keyoff(0);
    spin_us(20000);
    OUT("after keyoff: EG %08lx CA %08lx MIXS0 %08lx\n", (unsigned long)ar(R_EGMON), (unsigned long)ar(R_CAMON),
        (unsigned long)ar(R_MIXS(0, 1)));
    aica_quiet();
    out_close();
    return 0;
}
