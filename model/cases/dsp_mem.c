/* dsp_mem.c -- DSP memory addressing and the remaining pipeline questions.
 *   M1  MDEC_CT: a TEMP-held sample counter written to the ring (TABLE=0) every sample; ring sizes RBL 0..3.
 *   M2  TABLE=1 masking, NXADR, MADRS wrap.
 *   M3  ADRS_REG: ADRL from INPUTS (SHIFT!=3) / SHIFTED (SHIFT=3), ADREB, and when a new ADRS_REG takes effect.
 *   M4  even-step MWT; M5 even+odd MRD pairs (MRD+MWT in one step: dsp_mem3).
 *   M7  EFREG written twice in a sample; IRA==IWA in one step; YRL + YSEL=2 in one step; ACC width.
 * Output: dsp_mem.txt
 */
#include "aica_io.h"

#define RBP_BYTE 0x100000u
static uint32_t temp_rd(int i) { return ((ar(R_TEMP(i, 1)) & 0xFFFF) << 8) | (ar(R_TEMP(i, 0)) & 0xFF); }
static uint32_t mems_rd(int i) { return ((ar(R_MEMS(i, 1)) & 0xFFFF) << 8) | (ar(R_MEMS(i, 0)) & 0xFF); }
static uint16_t wword(uint32_t w) { return ram_r16(RBP_BYTE + 2 * w); }
static void set_word(uint32_t w, uint16_t v) { ram_w16(RBP_BYTE + 2 * w, v); }

/* counter program: MEMS31 = 0x0001 (INPUTS 0x100); steps 0,1: ACC = TEMP[1+MDEC] - 0x100; step 2 (SHIFT 3)
 * TWT TEMP[0+MDEC], ACC kept (B=ACC, X*Y=0); step 3: MWT NOFL of the same value (odd step) */
static void counter_prog(int masa, int table) {
    prog_reset();
    dsp_coef(0, -4096); dsp_coef(1, -4096);
    for (int s = 0; s < 2; s++) { P[s].XSEL = 1; P[s].IRA = 31; P[s].YSEL = 1; P[s].TRA = 1; P[s].SHIFT = 3; }
    dsp_coef(2, 0);
    P[2].SHIFT = 3; P[2].TWT = 1; P[2].TWA = 0; P[2].BSEL = 1; P[2].YSEL = 1;
    P[3].SHIFT = 3; P[3].MWT = 1; P[3].NOFL = 1; P[3].MASA = masa; P[3].TABLE = table; P[3].ZERO = 1;
    PN = 4;
}

int test_main(void) {
    out_open("dsp_mem.txt");
    aica_reset(RBP_BYTE, 3);
    for (int i = 0; i < 128; i++) { aw(R_TEMP(i, 0), 0); aw(R_TEMP(i, 1), 0); }
    for (int k = 0; k < 64; k++) dsp_madrs(k, 0);
    aw(R_MEMS(31, 1), 0x0001);

    /* ---- M1: ring direction/step with RBL=0 (8K words): fill 64K words with 0x5555 sentinel, count 20 ms ---- */
    for (int rbl = 0; rbl < 4; rbl++) {
        uint32_t ringw = 8192u << rbl;
        prog_reset(); prog_run(100);
        ram_fill(RBP_BYTE, 0x55555555u, 2 * 65536 + 64);
        for (int i = 0; i < 128; i++) { aw(R_TEMP(i, 0), 0); aw(R_TEMP(i, 1), 0); }
        dsp_ring(RBP_BYTE >> 11, rbl);
        dsp_madrs(0, 0);
        counter_prog(0, 0);
        uint64_t t0 = now_us();
        prog_load();
        spin_us(20000);
        prog_reset(); prog_load(); /* stop writing */
        uint64_t t1 = now_us();
        /* scan 64K+32 words: written runs */
        uint32_t nw = 0, first = 0xFFFFFFFF, last = 0, minv = 0xFFFF, maxv = 0, minv_at = 0, maxv_at = 0;
        int disc = 0;
        uint16_t prev = 0x5555;
        for (uint32_t w = 0; w < 65536 + 32; w++) {
            uint16_t v = wword(w);
            if (v != 0x5555) {
                nw++;
                if (first == 0xFFFFFFFF) first = w;
                last = w;
                if (v < minv) { minv = v; minv_at = w; }
                if (v > maxv) { maxv = v; maxv_at = w; }
                if (prev != 0x5555 && (uint16_t)(prev - v) != 0xFFFF) disc++; /* expect v(w) = v(w-1)+1 */
            }
            prev = v;
        }
        /* phase-invariant summary: MDEC_CT's starting value differs between runs and platforms */
        uint32_t span = (last - first + 1);
        int wrapped = span != nw; /* the run wrapped around the ring end */
        int outside = 0;
        for (uint32_t w = ringw; w < 65536 + 32; w++) if (wword(w) != 0x5555) outside++;
        LOG("M1 rbl %d (ring %lu words): ~20 ms written %s (>= 850 words), one descending run %s, words outside "
            "the ring %d\n", rbl, (unsigned long)ringw, nw >= 850 && nw < 1000 ? "yes" : "NO",
            disc <= (wrapped ? 2 : 1) ? "yes" : "NO", outside);
        (void)minv_at; (void)maxv_at; (void)t0; (void)t1;
    }
    /* M1b: long run (> 8K samples) at RBL=0 with MADRS = 0x1FF0 and MADRS = 0x3000 (> ring) */
    for (int mi = 0; mi < 2; mi++) {
        static const uint16_t madrs[] = {0x1FF0, 0x3000};
        prog_reset(); prog_run(100);
        ram_fill(RBP_BYTE, 0x55555555u, 2 * 16384);
        dsp_ring(RBP_BYTE >> 11, 0);
        dsp_madrs(0, madrs[mi]);
        counter_prog(0, 0);
        prog_load();
        spin_us(250000);
        prog_reset(); prog_load();
        uint32_t nw = 0, n_hi = 0;
        for (uint32_t w = 0; w < 16384; w++) {
            if (wword(w) != 0x5555) { nw++; if (w >= 8192) n_hi++; }
        }
        LOG("M1b madrs %04x 250ms: written %lu words (%lu above 8K); word(1)-word(0) %d, word(0)-word(8191) %d, "
            "8192..8193: %04x %04x\n", madrs[mi], (unsigned long)nw, (unsigned long)n_hi,
            (int16_t)(wword(1) - wword(0)), (int16_t)(wword(0) - wword(8191)), wword(8192), wword(8193));
    }
    /* M1c: RBP: base 0x100800 (RBP reg 0x201) */
    {
        prog_reset(); prog_run(100);
        ram_fill(RBP_BYTE, 0x55555555u, 2 * 8192 + 4096);
        dsp_ring((RBP_BYTE + 0x800) >> 11, 0);
        dsp_madrs(0, 0);
        counter_prog(0, 1); /* TABLE=1: fixed word 0 */
        prog_run(5000);
        prog_reset(); prog_load();
        LOG("M1c RBP 0x201 TABLE=1 MADRS 0: word@0x100000 %04x, word@0x100800 %s\n", wword(0),
            wword(0x400) != 0x5555 ? "written" : "5555");
        dsp_ring(RBP_BYTE >> 11, 0);
    }

    /* ---- M2/M3: RAM word w holds w (TABLE=1 reads reveal the address); MRD at odd steps, IWT two later ---- */
    for (uint32_t w = 0; w < 0x2000; w += 2) ram_w32(RBP_BYTE + 2 * w, (w & 0xFFFF) | ((w + 1) << 16));
    {
        struct { uint16_t madrs; int nxadr; } v[] = {{0x0100, 0}, {0x0100, 1}, {0xFFFF, 1}, {0x0FFF, 1}};
        prog_reset();
        for (int j = 0; j < 4; j++) {
            dsp_madrs(8 + j, v[j].madrs);
            int s = 1 + 2 * j;
            P[s].MRD = 1; P[s].TABLE = 1; P[s].NOFL = 1; P[s].MASA = 8 + j; P[s].NXADR = v[j].nxadr;
            P[s + 2].IWT = 1; P[s + 2].IWA = 8 + j; P[s + 2].NOFL = 0;
            P[s + 1].NOFL = 1; /* raw conversion: NOFL two steps before the IWT = MRD step (odd) */
        }
        PN = 12;
        prog_run(3000);
        LOG("M2 TABLE=1:");
        for (int j = 0; j < 4; j++) LOG(" madrs %04x nx %d -> %06lx;", v[j].madrs, v[j].nxadr, (unsigned long)mems_rd(8 + j));
        LOG("\n");
    }
    for (int form = 0; form < 2; form++) {
        /* ADRL at step 0 from MEMS0 (INPUTS) or, with SHIFT=3, from SHIFTED = ACC(prev) where ACC = MEMS0*-1*-1...
         * MRDs with ADREB at steps 1,3,5,7 (MADRS 0x200) -> IWT at 3,5,7,9 */
        static const uint16_t src[] = {0x1234, 0x00F0, 0xFFFF, 0x7FFF};
        for (int si = 0; si < 4; si++) {
            aw(R_MEMS(0, 1), src[si]);
            prog_reset();
            dsp_madrs(16, 0x200);
            if (form == 0) {
                P[0].IRA = 0; P[0].ADRL = 1;
            } else {
                /* ACC(step 126 of prev sample.. ) : steps 124..126 ACC = MEMS0 * -1 ; step 127 ACC = -ACC via NEGB;
                 * step 0: SHIFT=3 ADRL -> SHIFTED = ACC(127) = MEMS0 */
                for (int s = 124; s < 127; s++) { dsp_coef(s, -4096); P[s].XSEL = 1; P[s].IRA = 0; P[s].YSEL = 1; P[s].ZERO = 1; }
                dsp_coef(127, 0);
                P[127].BSEL = 1; P[127].NEGB = 1; P[127].YSEL = 1;
                P[0].SHIFT = 3; P[0].ADRL = 1;
            }
            for (int j = 0; j < 4; j++) {
                int s = 1 + 2 * j;
                P[s].MRD = 1; P[s].TABLE = 1; P[s].NOFL = 1; P[s].MASA = 16; P[s].ADREB = 1;
                P[s + 2].IWT = 1; P[s + 2].IWA = 16 + j;
            }
            PN = form ? 128 : 10;
            prog_run(3000);
            LOG("M3 %s src %04x: MRD(ADREB)@1,3,5,7 ->", form ? "SHIFTED(sh3)" : "INPUTS", src[si]);
            for (int j = 0; j < 4; j++) LOG(" %06lx", (unsigned long)mems_rd(16 + j));
            LOG("\n");
        }
    }
    prog_reset(); prog_run(1000);
    for (int s = 0; s < 128; s++) dsp_coef(s, 0);

    /* ---- M4: even-step MWT.  X = MEMS1(0x4000) * COEF[s] = 16*(s+1) -> SHIFTED(s) = 0x4000*s (SHIFT 3).
     *          MWT NOFL TABLE=1 at step 4 (word 0x300); also at step 7 (word 0x301) for reference ---- */
    aw(R_MEMS(1, 1), 0x4000);
    for (int s = 0; s < 16; s++) dsp_coef(s, 16 * (s + 1));
    for (int variant = 0; variant < 3; variant++) {
        set_word(0x300, 0xAAAA); set_word(0x301, 0xAAAA); set_word(0x302, 0xAAAA);
        dsp_madrs(20, 0x300); dsp_madrs(21, 0x301); dsp_madrs(22, 0x302);
        prog_reset();
        for (int s = 0; s < 16; s++) { P[s].XSEL = 1; P[s].IRA = 1; P[s].YSEL = 1; P[s].ZERO = 1; P[s].SHIFT = 3; }
        if (variant == 0) { P[4].MWT = 1; P[4].TABLE = 1; P[4].NOFL = 1; P[4].MASA = 20; }
        if (variant == 1) { P[4].MWT = 1; P[4].TABLE = 1; P[4].NOFL = 1; P[4].MASA = 20;
                            P[5].MWT = 1; P[5].TABLE = 1; P[5].NOFL = 1; P[5].MASA = 21; }
        if (variant == 2) { P[4].MWT = 1; P[4].TABLE = 1; P[4].NOFL = 1; P[4].MASA = 20;
                            P[5].MRD = 1; P[5].TABLE = 1; P[5].NOFL = 1; P[5].MASA = 22; }
        P[7].MWT = 1; P[7].TABLE = 1; P[7].NOFL = 1; P[7].MASA = 21 + (variant == 1);
        PN = 16;
        prog_run(3000);
        LOG("M4 variant %d: word300 %04x word301 %04x word302 %04x (step s value = %04x*s)\n", variant,
            wword(0x300), wword(0x301), wword(0x302), 0x40);
    }
    /* ---- M5: MRD at even step 4 (word A=0x0400..) and odd step 5 (word B) -> IWT at 5..10 ---- */
    for (int variant = 0; variant < 2; variant++) {
        set_word(0x310, 0x1111); set_word(0x311, 0x2222); set_word(0x312, 0x3333);
        dsp_madrs(23, 0x310); dsp_madrs(24, 0x311); dsp_madrs(25, 0x312);
        prog_reset();
        P[3].MRD = 1; P[3].TABLE = 1; P[3].NOFL = 1; P[3].MASA = 25;
        P[4].MRD = 1; P[4].TABLE = 1; P[4].NOFL = 1; P[4].MASA = 23;
        if (variant) { P[5].MRD = 1; P[5].TABLE = 1; P[5].NOFL = 1; P[5].MASA = 24; }
        for (int s = 3; s < 12; s++) { P[s].NOFL = 1; if (s >= 4) { P[s].IWT = 1; P[s].IWA = s; } }
        PN = 12;
        prog_run(3000);
        LOG("M5 MRD@3(3333) MRD@4(1111)%s: IWT@4..11 ->", variant ? " MRD@5(2222)" : "");
        for (int s = 4; s < 12; s++) LOG(" %d=%06lx", s, (unsigned long)mems_rd(s));
        LOG("\n");
    }
    /* (MRD+MWT in one step: see dsp_mem3) */

    /* ---- M7a: EFREG written at steps 3 and 5 (values 0x4000*3>>8.. ) ---- */
    {
        prog_reset();
        for (int s = 0; s < 8; s++) { P[s].XSEL = 1; P[s].IRA = 1; P[s].YSEL = 1; P[s].ZERO = 1; P[s].SHIFT = 3; }
        P[3].EWT = 1; P[3].EWA = 2;
        P[5].EWT = 1; P[5].EWA = 2;
        PN = 8;
        prog_run(3000);
        LOG("M7a EFREG2 written @3 (%04x) and @5 (%04x): %04lx\n", 0x40 * 3, 0x40 * 5, (unsigned long)ar(R_EFREG(2)));
    }
    /* ---- M7b: IRA==IWA in one step.  MEMS5 = 0x0100 (CPU), MRD@1 word 0x7000; step 3: IRA=5 IWT IWA=5, X=INPUTS
     *           Y=COEF(-4096): TWT @4 of -INPUTS ---- */
    {
        set_word(0x330, 0x7000);
        dsp_madrs(27, 0x330);
        aw(R_MEMS(5, 1), 0x0100);
        prog_reset();
        dsp_coef(3, -4096);
        P[1].MRD = 1; P[1].TABLE = 1; P[1].NOFL = 1; P[1].MASA = 27;
        P[3].IRA = 5; P[3].XSEL = 1; P[3].YSEL = 1; P[3].ZERO = 1; P[3].IWT = 1; P[3].IWA = 5;
        P[4].TWT = 1; P[4].SHIFT = 3;
        PN = 5;
        prog_run(3000);
        LOG("M7b IRA=IWA=5 (old 010000, new 700000): TEMP=%06lx MEMS5=%06lx\n", (unsigned long)temp_rd(0),
            (unsigned long)mems_rd(5));
    }
    /* ---- M7c: YRL and YSEL=2 in the same step. Y_REG old = MEMS6 (0x2000 -> Y 0x400), new = MEMS7 (0x4000 -> 0x800) */
    {
        aw(R_MEMS(6, 1), 0x2000); aw(R_MEMS(7, 1), 0x4000); aw(R_MEMS(8, 1), 0x4000);
        prog_reset();
        P[0].IRA = 6; P[0].YRL = 1;
        P[2].IRA = 7; P[2].YRL = 1; P[2].XSEL = 1; P[2].YSEL = 2; P[2].ZERO = 1; /* X = INPUTS = MEMS7 */
        P[3].TWT = 1; P[3].SHIFT = 3;
        PN = 4;
        for (int i = 0; i < 128; i++) { aw(R_TEMP(i, 0), 0); aw(R_TEMP(i, 1), 0x4000); } /* X = 0x400000 */
        prog_run(3000);
        LOG("M7c YRL+YSEL2 same step: TEMP=%06lx (old Y_REG -> 100000, new -> 200000)\n", (unsigned long)temp_rd(0));
    }
    /* ---- M7d: ACC width: ACC += 0x7FF700 (X=MEMS 0x7FFF, Y=4095, B=ACC) at steps 0..9; MWT NOFL of SHIFT 0 and 3
     *           variants at odd steps ---- */
    for (int sh = 0; sh < 4; sh += 3) {
        aw(R_MEMS(9, 1), 0x7FFF);
        prog_reset();
        for (int s = 0; s < 12; s++) {
            dsp_coef(s, 4095);
            dsp_madrs(32 + s, 0x340 + s);
            P[s].XSEL = 1; P[s].IRA = 9; P[s].YSEL = 1; P[s].BSEL = 1; P[s].SHIFT = sh;
            if (s & 1) { P[s].MWT = 1; P[s].TABLE = 1; P[s].NOFL = 1; P[s].MASA = 32 + s; }
        }
        P[0].BSEL = 0; P[0].ZERO = 1;
        P[12].TWT = 1; P[12].SHIFT = sh;
        PN = 13;
        prog_run(3000);
        LOG("M7d ACC accumulate 0x7ff700/step shift %d: odd-step SHIFTED[23:8]:", sh);
        for (int s = 1; s < 12; s += 2) LOG(" %d=%04x", s, wword(0x340 + s));
        LOG(" step12 TEMP=%06lx\n", (unsigned long)temp_rd(0));
    }

    prog_reset(); prog_run(1000);
    aica_quiet();
    out_close();
    io_print("dsp_mem done\n");
    return 0;
}
