/* dsp_mem2.c -- follow-ups to dsp_mem.
 *   A  ADRS_REG width/sign: ADRL from INPUTS (SHIFT!=3) and from SHIFTED (SHIFT=3), MADRS 0x1000, TABLE=1.
 *   B  ADRS_REG timing: ADRL at 0 (value a) and at 4 (value b), ADREB reads at 1,3,5,7,9.
 *   D  IRA==IWA ordering with a per-sample-changing memval (ring words alternate A/B).
 *   E  NOFL used by IWT: NOFL on exactly one of steps s-3..s.
 * RAM word w (from RBP) holds w for w < 0x2000.  Output: dsp_mem2.txt
 */
#include "aica_io.h"

#define RBP_BYTE 0x100000u
static uint32_t temp_rd(int i) { return ((ar(R_TEMP(i, 1)) & 0xFFFF) << 8) | (ar(R_TEMP(i, 0)) & 0xFF); }
static uint32_t mems_rd(int i) { return ((ar(R_MEMS(i, 1)) & 0xFFFF) << 8) | (ar(R_MEMS(i, 0)) & 0xFF); }
static void set_word(uint32_t w, uint16_t v) { ram_w16(RBP_BYTE + 2 * w, v); }
static void fill_index(void) {
    for (uint32_t w = 0; w < 0x2000; w += 2) ram_w32(RBP_BYTE + 2 * w, (w & 0xFFFF) | ((w + 1) << 16));
}
/* MRD (raw) at step s of word MADRS[masa] (+flags), IWT into MEMS[iwa] at s+2; NOFL set on s so the IWT is raw */
static void rd(int s, int masa, int iwa, int adreb, int nxadr, int table) {
    P[s].MRD = 1; P[s].TABLE = table; P[s].NOFL = 1; P[s].MASA = masa; P[s].ADREB = adreb; P[s].NXADR = nxadr;
    P[s + 2].IWT = 1; P[s + 2].IWA = iwa;
}

int test_main(void) {
    out_open("dsp_mem2.txt");
    aica_quiet();
    dsp_ring(RBP_BYTE >> 11, 0);
    for (int k = 0; k < 64; k++) dsp_madrs(k, 0);
    fill_index();

    /* ---- A ---- */
    {
        static const uint16_t src_in[] = {0x8000, 0x7F00, 0x80FF, 0xFF00, 0x0100, 0xFFFF, 0xF000, 0x0F00};
        static const uint16_t src_sh[] = {0x8000, 0x9000, 0xFFE0, 0x7000, 0x0FF0, 0x8010, 0xFFF0, 0x0010};
        dsp_madrs(1, 0x1000);
        for (int form = 0; form < 2; form++) {
            for (int i = 0; i < 8; i++) {
                aw(R_MEMS(0, 1), form ? src_sh[i] : src_in[i]);
                prog_reset();
                if (form == 0) {
                    P[0].IRA = 0; P[0].ADRL = 1;
                } else {
                    /* ACC(126) = -MEMS0, ACC(127) = +MEMS0, step 0 SHIFT 3 -> SHIFTED = MEMS0 << 8 */
                    for (int s = 124; s < 127; s++) { dsp_coef(s, -4096); P[s].XSEL = 1; P[s].IRA = 0; P[s].YSEL = 1; P[s].ZERO = 1; }
                    dsp_coef(127, 0);
                    P[127].BSEL = 1; P[127].NEGB = 1; P[127].YSEL = 1;
                    P[0].SHIFT = 3; P[0].ADRL = 1;
                }
                rd(1, 1, 1, 1, 0, 1);
                PN = form ? 128 : 4;
                prog_run(3000);
                LOG("A %s src %04x -> word %04lx\n", form ? "SHIFTED" : "INPUTS ", form ? src_sh[i] : src_in[i],
                    (unsigned long)(mems_rd(1) >> 8));
            }
        }
        for (int s = 124; s < 128; s++) dsp_coef(s, 0);
    }
    /* ---- B: ADRL at 0 (MEMS2 = 0x0100 -> 1), at 4 (MEMS3 = 0x0200 -> 2); ADREB MRD at 1,3,5,7,9 (MADRS 0x400) ---- */
    {
        aw(R_MEMS(2, 1), 0x0100); aw(R_MEMS(3, 1), 0x0200);
        dsp_madrs(2, 0x400);
        prog_reset();
        P[0].IRA = 2; P[0].ADRL = 1;
        P[4].IRA = 3; P[4].ADRL = 1;
        for (int j = 0; j < 5; j++) rd(1 + 2 * j, 2, 4 + j, 1, 0, 1);
        PN = 12;
        prog_run(3000);
        LOG("B ADRL@0 (+1) ADRL@4 (+2); ADREB reads of 0x400 at 1,3,5,7,9 ->");
        for (int j = 0; j < 5; j++) LOG(" %04lx", (unsigned long)(mems_rd(4 + j) >> 8));
        LOG("\n");
    }
    /* (C: MRD+MWT in one step moved to dsp_mem3, with a clean setup per sub-test) */
    /* ---- D: IRA==IWA.  Ring (TABLE=0) words alternate: even 0x1000, odd 0x2000 (RBL=0, whole 8K ring).
     *          MRD@1 ring MADRS 0 -> memval(c); step 3: IRA=5 IWT=5 X=INPUTS Y=-4096; step 4 TWT TEMP[0+c] ---- */
    {
        prog_reset(); prog_run(100);
        for (uint32_t w = 0; w < 8192; w += 2) ram_w32(RBP_BYTE + 2 * w, 0x20001000u);
        dsp_madrs(4, 0);
        prog_reset();
        dsp_coef(3, -4096);
        rd(1, 4, 5, 0, 0, 0);
        P[3].IRA = 5; P[3].XSEL = 1; P[3].YSEL = 1; P[3].ZERO = 1;
        P[4].TWT = 1; P[4].SHIFT = 3;
        PN = 5;
        prog_run(10000);
        LOG("D IRA=IWA: TEMP[0..7]:");
        for (int i = 0; i < 8; i++) LOG(" %06lx", (unsigned long)temp_rd(i));
        LOG("  (slot p holds -word(c=p)[new] or -word(c=p+1)[old]; even words 0x100000)\n");
        fill_index();
    }
    /* ---- E: NOFL for IWT: MRD@1 word 0x1111 (TABLE=1), IWT@5; NOFL set on exactly one of steps 1..5 ---- */
    {
        set_word(0x360, 0x1111);
        dsp_madrs(5, 0x360);
        for (int k = 0; k <= 5; k++) {
            prog_reset();
            P[1].MRD = 1; P[1].TABLE = 1; P[1].MASA = 5;
            P[5].IWT = 1; P[5].IWA = 7;
            if (k) P[k].NOFL = 1;
            PN = 6;
            prog_run(3000);
            LOG("E NOFL only on step %d: IWT@5 -> %06lx (raw 111100, float 122200)\n", k, (unsigned long)mems_rd(7));
        }
    }

    prog_reset(); prog_run(1000);
    aica_quiet();
    out_close();
    io_print("dsp_mem2 done\n");
    return 0;
}
