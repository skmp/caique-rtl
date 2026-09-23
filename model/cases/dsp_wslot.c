/* dsp_wslot.c -- does a DSP MWT have its own memory slot (posted write) or share the DSP's single read slot per
 * 8-cycle frame (= 2 steps)?  Measured so far: MRD at an odd step s returns at s+2, an even-step MRD waits for the next
 * odd slot (s+3) and is lost if that odd step reads itself; MWT works at any step.  TABLE=1 word addressing at RBP
 * 0x100000: word w holds 0x1000 + w (w < 64), MADRS[i] = i.  A constant is written by computing ACC = -MEMS[16+j]
 * (X = MEMS, Y = COEF -4096, ZERO) at step s-1 and MWT NOFL at step s (writes SHIFTED[23:8] = -(MEMS>>8)).  A read is
 * MRD NOFL at step s, IWT at s+2 (NOFL also set at s+1 so the raw word lands), result = MEMS[j] (word << 8).
 *   W1  MRD 3 -> IWT 5 (control)                          W2  MWT at even step 2, MRD at odd step 3 (same frame)
 *   W3  MWT at odd 3, MRD at odd 5                        W4  MWT at odd 3, MRD at even 4 (lands 7)
 *   W5  MWT at every even step 2..30 + MRD at every odd step 3..31 (15 + 15 in 15 frames)
 *   W6  MRD 3 + MRD 4 (both land: 5 and 7; dsp_mem M5 control)
 * Each program runs 10 ms, then the written words and MEMS are read twice 1 ms apart.  Output: dsp_wslot.txt */
#include "aica_io.h"
#define RBP_BYTE 0x100000u
static uint32_t mems_rd(int i) { return ((ar(R_MEMS(i, 1)) & 0xFFFF) << 8) | (ar(R_MEMS(i, 0)) & 0xFF); }
static uint16_t wword(uint32_t w) { return ram_r16(RBP_BYTE + 2 * w); }
static int fails;
static void chk(const char *tag, const char *what, uint32_t exp, uint32_t got) {
    LOG("%s %s: expected %06lx got %06lx %s\n", tag, what, (unsigned long)exp, (unsigned long)got, exp == got ? "OK" : "FAIL");
    if (exp != got) fails++;
}
static void fill(void) { for (uint32_t w = 0; w < 64; w += 2) ram_w32(RBP_BYTE + 2 * w, (0x1000 + w) | ((0x1000 + w + 1) << 16)); }
/* program pieces */
static void rd(int s, int word, int iwa) { P[s].MRD = 1; P[s].TABLE = 1; P[s].NOFL = 1; P[s].MASA = word; P[s + 1].NOFL = 1; P[s + 2].IWT = 1; P[s + 2].IWA = iwa; }
static void wr(int s, int word, int cj) { dsp_coef(s - 1, -4096); P[s - 1].XSEL = 1; P[s - 1].IRA = 16 + cj; P[s - 1].YSEL = 1; P[s - 1].ZERO = 1; P[s].SHIFT = 0; P[s].MWT = 1; P[s].NOFL = 1; P[s].TABLE = 1; P[s].MASA = word; }
static uint16_t wexp(int cj) { return (uint16_t)(0x10000u - (0x1000u + 0x100u * cj)); }   /* -(MEMS[16+cj] >> 8) */
static void run(const char *tag, int nsteps) {
    PN = nsteps; prog_load(); spin_us(10000); prog_reset(); prog_load();
}
int test_main(void) {
    out_open("dsp_wslot.txt");
    aica_quiet();
    dsp_ring(RBP_BYTE >> 11, 0);
    for (int i = 0; i < 64; i++) dsp_madrs(i, i);
    for (int i = 0; i < 32; i++) { aw(R_MEMS(i, 1), 0); }
    for (int j = 0; j < 15; j++) aw(R_MEMS(16 + j, 1), 0x1000 + 0x100 * j);   /* constants: MEMS[16+j] = (0x1000 + 0x100 j) << 8 */
    /* W1 */
    fill(); prog_reset(); rd(3, 40, 0); run("W1", 8);
    chk("W1", "MRD 3 -> MEMS0 (word 40)", (0x1000 + 40) << 8, mems_rd(0)); spin_us(1000); chk("W1", "MEMS0 again", (0x1000 + 40) << 8, mems_rd(0));
    /* W2 */
    fill(); prog_reset(); wr(2, 50, 0); rd(3, 41, 1); run("W2", 8);
    chk("W2", "MWT even 2 -> word 50", wexp(0), wword(50)); chk("W2", "MRD odd 3 -> MEMS1 (word 41)", (0x1000 + 41) << 8, mems_rd(1));
    /* W3 */
    fill(); prog_reset(); wr(3, 51, 1); rd(5, 42, 2); run("W3", 10);
    chk("W3", "MWT odd 3 -> word 51", wexp(1), wword(51)); chk("W3", "MRD odd 5 -> MEMS2 (word 42)", (0x1000 + 42) << 8, mems_rd(2));
    /* W4 */
    fill(); prog_reset(); wr(3, 52, 2); P[4].MRD = 1; P[4].TABLE = 1; P[4].NOFL = 1; P[4].MASA = 43; P[5].NOFL = 1; P[6].NOFL = 1; P[7].IWT = 1; P[7].IWA = 3; run("W4", 10);
    chk("W4", "MWT odd 3 -> word 52", wexp(2), wword(52)); chk("W4", "MRD even 4 -> IWT 7 MEMS3 (word 43)", (0x1000 + 43) << 8, mems_rd(3));
    /* W5 dense */
    fill(); prog_reset();
    for (int j = 0; j < 15; j++) { wr(2 * j + 2, 32 + j, j); rd(2 * j + 3, 48 + j, j); }
    run("W5", 36);
    { int wok = 0, rok = 0; for (int j = 0; j < 15; j++) { wok += wword(32 + j) == wexp(j); rok += mems_rd(j) == (uint32_t)((0x1000 + 48 + j) << 8); }
      LOG("W5 dense: writes completed %d/15, reads completed %d/15\n", wok, rok);
      LOG("W5 words:"); for (int j = 0; j < 15; j++) LOG(" %04x", wword(32 + j)); LOG("\nW5 mems:"); for (int j = 0; j < 15; j++) LOG(" %06lx", (unsigned long)mems_rd(j)); LOG("\n");
      if (wok != 15 || rok != 15) fails++; }
    /* W6 */
    fill(); prog_reset(); rd(3, 44, 4); P[4].MRD = 1; P[4].TABLE = 1; P[4].NOFL = 1; P[4].MASA = 45; P[5].NOFL = 1; P[6].NOFL = 1; P[7].IWT = 1; P[7].IWA = 5; run("W6", 10);   /* NOFL at 5: the IWT at 7 converts with the NOFL of step 5 (first run lacked it: both platforms returned the float decode 0x105a00 of word 45) */
    chk("W6", "MRD 3 -> MEMS4 (word 44)", (0x1000 + 44) << 8, mems_rd(4)); chk("W6", "MRD 4 -> IWT 7 MEMS5 (word 45)", (0x1000 + 45) << 8, mems_rd(5));
    OUT("dsp_wslot: %d failing checks\n", fails);
    aica_quiet();
    out_close();
    return 0;
}
