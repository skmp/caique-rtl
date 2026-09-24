/* dsp_basic.c -- first DSP semantics map with static inputs (SH4-written MEMS.h, COEF), one small program per vector.
 * Results per run: TEMP (every slot converges to the one TWT result; 4 slots read), EFREG[0..15], RAM words written
 * by TABLE=1 MWTs at MADRS[k] (word 0x10+k from RBP=0x100000).
 * Output: tests/dsp_basic/dsp_basic.txt (host-side analysis in tools/).
 */
#include "aica_io.h"

#define RBP_BYTE 0x100000u

static uint32_t temp_rd(int i) { return ((ar(R_TEMP(i, 1)) & 0xFFFF) << 8) | (ar(R_TEMP(i, 0)) & 0xFF); }
static void log_temp(void) {
    uint32_t t0 = temp_rd(0), t1 = temp_rd(37), t2 = temp_rd(64), t3 = temp_rd(101);
    if (t0 == t1 && t0 == t2 && t0 == t3) LOG(" T=%06lx", (unsigned long)t0);
    else LOG(" T=%06lx/%06lx/%06lx/%06lx", (unsigned long)t0, (unsigned long)t1, (unsigned long)t2, (unsigned long)t3);
}
static uint16_t res_word(int k) { return ram_r16(RBP_BYTE + 2 * (0x10 + k)); }

int test_main(void) {
    out_open("dsp_basic.txt");
    aica_reset(RBP_BYTE, 0);
    for (int k = 0; k < 64; k++) dsp_madrs(k, 0x10 + k);
    ram_fill(RBP_BYTE, 0, 0x1000);
    for (int i = 0; i < 128; i++) { aw(R_TEMP(i, 0), 0); aw(R_TEMP(i, 1), 0); }

    /* ---- A: X=MEMS0 (h=m) * Y=COEF c, B=0; steps 0..5 identical; TWT/EWT/MWT(NOFL) at 3, MWT(float) at 5 ---- */
    static const uint16_t mv[] = {0x0001, 0x0080, 0x7FFF, 0x8000, 0xFFFF, 0x4000, 0xC000, 0x1234, 0xEDCC};
    static const int cv[] = {-4096, -1, 1, 4095, 2048, -2048, 1024};
    LOG("# A m c shift : T(TEMP) E(EFREG0) N(RAM NOFL @3) F(RAM float @5)\n");
    for (unsigned mi = 0; mi < sizeof mv / sizeof mv[0]; mi++) {
        aw(R_MEMS(0, 1), mv[mi]);
        for (unsigned ci = 0; ci < sizeof cv / sizeof cv[0]; ci++) {
            for (int s = 0; s < 6; s++) dsp_coef(s, cv[ci]);
            for (int sh = 0; sh < 4; sh++) {
                prog_reset();
                for (int s = 0; s < 6; s++) {
                    P[s].XSEL = 1; P[s].IRA = IRA_MEMS(0); P[s].YSEL = 1; P[s].ZERO = 1; P[s].SHIFT = sh;
                }
                P[3].TWT = 1; P[3].TWA = 0; P[3].EWT = 1; P[3].EWA = 0;
                P[3].MWT = 1; P[3].TABLE = 1; P[3].NOFL = 1; P[3].MASA = 0;
                P[5].MWT = 1; P[5].TABLE = 1; P[5].NOFL = 0; P[5].MASA = 1;
                PN = 6;
                prog_run(4000);
                LOG("A %04x %5d %d :", mv[mi], cv[ci], sh);
                log_temp();
                LOG(" E=%04lx N=%04x F=%04x\n", (unsigned long)(ar(R_EFREG(0)) & 0xFFFF), res_word(0), res_word(1));
            }
        }
    }

    /* ---- B: pipeline: COEF[s] = 16*(s+1), X = MEMS0 (0x4000), SHIFT=3; MWT NOFL at every odd step 1..15 (MADRS[s]),
     *          EWT at even steps 0..14 (EWA s/2); separate runs with TWT at step 4,5,6,7 ---- */
    aw(R_MEMS(0, 1), 0x4000);
    for (int s = 0; s < 20; s++) dsp_coef(s, 16 * (s + 1));
    for (int tw = 3; tw <= 8; tw++) {
        prog_reset();
        for (int s = 0; s < 18; s++) {
            P[s].XSEL = 1; P[s].IRA = IRA_MEMS(0); P[s].YSEL = 1; P[s].ZERO = 1; P[s].SHIFT = 3;
            if (s & 1) { P[s].MWT = 1; P[s].TABLE = 1; P[s].NOFL = 1; P[s].MASA = s; }
            else if (s <= 14) { P[s].EWT = 1; P[s].EWA = s / 2; }
        }
        P[tw].TWT = 1; P[tw].TWA = 0;
        PN = 18;
        prog_run(4000);
        LOG("B twt@%d:", tw);
        log_temp();
        LOG(" RAM(odd s):");
        for (int s = 1; s < 18; s += 2) LOG(" %d=%04x", s, res_word(s));
        LOG(" EFREG(even s):");
        for (int s = 0; s <= 14; s += 2) LOG(" %d=%04lx", s, (unsigned long)(ar(R_EFREG(s / 2)) & 0xFFFF));
        LOG("\n");
    }
    /* B2: B=ACC chain: steps 0..3 ACC=X*c (c=16*(s+1)); step 4..9: Y=COEF 0 (X*Y=0), BSEL=1 (B=ACC) with NEGB on 6 */
    for (int neg = 0; neg < 2; neg++) {
        prog_reset();
        for (int s = 0; s < 4; s++) { P[s].XSEL = 1; P[s].IRA = 0; P[s].YSEL = 1; P[s].ZERO = 1; P[s].SHIFT = 3; }
        for (int s = 4; s < 16; s++) {
            dsp_coef(s, 0);
            P[s].XSEL = 1; P[s].IRA = 0; P[s].YSEL = 1; P[s].BSEL = 1; P[s].SHIFT = 3;
            if (s & 1) { P[s].MWT = 1; P[s].TABLE = 1; P[s].NOFL = 1; P[s].MASA = s; }
        }
        if (neg) P[6].NEGB = 1;
        PN = 16;
        prog_run(4000);
        LOG("B2 negb@6=%d RAM(odd s):", neg);
        for (int s = 1; s < 16; s += 2) LOG(" %d=%04x", s, res_word(s));
        LOG("\n");
    }
    for (int s = 0; s < 20; s++) dsp_coef(s, 16 * (s + 1));

    /* ---- C: MRD -> MEMVAL -> IWT latency. RAM words at MADRS[32+j] = 0x1111*(j+1); MRD NOFL TABLE at odd steps
     *          1,3,5,7,9,11 (and one even-step MRD at 14); IWT at steps 2..19 -> MEMS[step] ---- */
    for (int j = 0; j < 8; j++) ram_w16(RBP_BYTE + 2 * (0x10 + 32 + j), (uint16_t)(0x1111 * (j + 1)));
    for (int i = 2; i < 20; i++) aw(R_MEMS(i, 1), 0xBAD0 + i);
    prog_reset();
    for (int s = 0; s < 20; s++) {
        if ((s & 1) && s <= 11) { P[s].MRD = 1; P[s].TABLE = 1; P[s].NOFL = 1; P[s].MASA = 32 + (s - 1) / 2; }
        if (s >= 2) { P[s].IWT = 1; P[s].IWA = s; }
    }
    P[14].MRD = 1; P[14].TABLE = 1; P[14].NOFL = 1; P[14].MASA = 38;
    PN = 20;
    prog_run(4000);
    LOG("C MEMS[2..19].h/l:");
    for (int i = 2; i < 20; i++) LOG(" %d=%04lx/%02lx", i, (unsigned long)(ar(R_MEMS(i, 1)) & 0xFFFF),
                                    (unsigned long)(ar(R_MEMS(i, 0)) & 0xFF));
    LOG("\n");
    /* C2: float-format reads (NOFL=0) of a few words into MEMS, to see MEMS low byte */
    {
        static const uint16_t fw[] = {0x0001, 0x07FF, 0x0800, 0x7FFF, 0x8000, 0xFFFF, 0x5A5A, 0x3FFF};
        for (int j = 0; j < 8; j++) ram_w16(RBP_BYTE + 2 * (0x10 + 40 + j), fw[j]);
        prog_reset();
        for (int j = 0; j < 8; j++) {
            int s = 1 + 4 * j;
            P[s].MRD = 1; P[s].TABLE = 1; P[s].NOFL = 0; P[s].MASA = 40 + j;
            P[s + 2].IWT = 1; P[s + 2].IWA = 20 + j;
        }
        PN = 36;
        prog_run(4000);
        LOG("C2 float MRD -> MEMS[20..27] h/l:");
        for (int j = 0; j < 8; j++) LOG(" %04x->%04lx/%02lx", fw[j], (unsigned long)(ar(R_MEMS(20 + j, 1)) & 0xFFFF),
                                        (unsigned long)(ar(R_MEMS(20 + j, 0)) & 0xFF));
        LOG("\n");
    }

    /* ---- D: Y_REG / FRC_REG.  MEMS0 = y source, MEMS1 = 0x4000 x.  step0: IRA=0 YRL; steps 1..5 X=MEMS1 Y=YSEL,
     *          TWT at 4.  FRC: steps 0..3 ACC = MEMS0*COEF(-4096)... FRCL at 3 with SHIFT sh; steps 6..9 X=MEMS1 Y=FRC ---- */
    {
        static const uint16_t yv[] = {0x1234, 0x9876, 0x98F6, 0x7FFF, 0x8000, 0xFFFF, 0x0FF0};
        aw(R_MEMS(1, 1), 0x4000);
        for (unsigned yi = 0; yi < sizeof yv / sizeof yv[0]; yi++) {
            aw(R_MEMS(0, 1), yv[yi]);
            for (int ys = 2; ys <= 3; ys++) {
                prog_reset();
                P[0].IRA = 0; P[0].YRL = 1;
                for (int s = 1; s < 6; s++) { P[s].XSEL = 1; P[s].IRA = 1; P[s].YSEL = ys; P[s].ZERO = 1; P[s].SHIFT = 3; }
                P[4].TWT = 1;
                PN = 6;
                prog_run(4000);
                LOG("D yreg %04x ysel %d:", yv[yi], ys);
                log_temp();
                LOG("\n");
            }
            for (int sh = 0; sh < 4; sh++) {
                prog_reset();
                for (int s = 0; s < 6; s++) {
                    dsp_coef(s, -4096);
                    P[s].XSEL = 1; P[s].IRA = 0; P[s].YSEL = 1; P[s].ZERO = 1; P[s].SHIFT = sh;
                }
                P[3].FRCL = 1;
                for (int s = 6; s < 12; s++) { P[s].XSEL = 1; P[s].IRA = 1; P[s].YSEL = 0; P[s].ZERO = 1; P[s].SHIFT = 3; }
                P[9].TWT = 1;
                PN = 12;
                prog_run(4000);
                LOG("D frc src -%04x shift %d:", yv[yi], sh);
                log_temp();
                LOG("\n");
            }
        }
        for (int s = 0; s < 20; s++) dsp_coef(s, 16 * (s + 1));
    }

    /* ---- F: MIXS written by SH4: does it persist?  and EXTS as DSP input ---- */
    aw(R_MIXS(5, 1), 0x1234); aw(R_MIXS(5, 0), 0x5);
    LOG("F MIXS5 right after write %04lx/%lx", (unsigned long)ar(R_MIXS(5, 1)), (unsigned long)ar(R_MIXS(5, 0)));
    spin_us(2000);
    LOG(", after 2ms %04lx/%lx\n", (unsigned long)ar(R_MIXS(5, 1)), (unsigned long)ar(R_MIXS(5, 0)));
    for (int src = 0; src < 3; src++) {
        static const int ira[] = {IRA_MIXS(5), IRA_EXTS(0), IRA_EXTS(1)};
        prog_reset();
        for (int s = 0; s < 6; s++) {
            dsp_coef(s, -4096);
            P[s].XSEL = 1; P[s].IRA = ira[src]; P[s].YSEL = 1; P[s].ZERO = 1; P[s].SHIFT = 3;
        }
        P[3].TWT = 1;
        PN = 6;
        aw(R_MIXS(5, 1), 0x1234); aw(R_MIXS(5, 0), 0x5);
        prog_run(4000);
        LOG("F ira %02x * -4096 shift3:", ira[src]);
        log_temp();
        LOG("\n");
    }

    prog_reset();
    prog_run(1000);
    aica_quiet();
    out_close();
    io_print("dsp_basic done\n");
    return 0;
}
