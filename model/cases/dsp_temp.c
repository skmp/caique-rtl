/* dsp_temp.c -- which TEMP slots a DSP program rewrites, and when (the two known dsp_basic TEMP differences).
 * TEMP is a 128-word ring addressed (TWA + MDEC_CT) & 127 with MDEC_CT counting every sample, so a TWT step should
 * rewrite every slot within 128 samples.  dsp_basic read 4 slots (0, 37, 64, 101) after 4 ms and found one stale slot
 * ("A ffff -4096 1") and an even/odd split ("F ira 25": SH4-written MIXS5).  Here all 128 slots are dumped:
 *   T1: vector "A ffff -4096 0" run for 4 ms, then "A ffff -4096 1": dumps at several times after its load
 *   T2: vector "F ira 25" (SH4 writes MIXS5 = 0x12345 just before the run), dump after 4 ms
 * Every dump records the SH4 time (us since the load) at its start and end.  Output: dsp_temp.txt */
#include "aica_io.h"

#define RBP_BYTE 0x100000u
static uint32_t temp_rd(int i) { return ((ar(R_TEMP(i, 1)) & 0xFFFF) << 8) | (ar(R_TEMP(i, 0)) & 0xFF); }
static uint32_t tv[128];
static void dump(const char *tag, uint64_t t0) {
    uint64_t a = now_us();
    for (int i = 0; i < 128; i++) tv[i] = temp_rd(i);
    uint64_t b = now_us();
    LOG("%s t %lu..%lu us:", tag, (unsigned long)(a - t0), (unsigned long)(b - t0));
    for (int i = 0; i < 128; i++) LOG("%s%06lx", (i & 15) ? " " : "\n  ", (unsigned long)tv[i]);
    LOG("\n");
}
static void prog_A(int sh) {
    prog_reset();
    for (int s = 0; s < 6; s++) { P[s].XSEL = 1; P[s].IRA = IRA_MEMS(0); P[s].YSEL = 1; P[s].ZERO = 1; P[s].SHIFT = sh; }
    P[3].TWT = 1; P[3].TWA = 0; P[3].EWT = 1; P[3].EWA = 0;
    P[3].MWT = 1; P[3].TABLE = 1; P[3].NOFL = 1; P[3].MASA = 0;
    P[5].MWT = 1; P[5].TABLE = 1; P[5].NOFL = 0; P[5].MASA = 1;
    PN = 6;
}
int test_main(void) {
    out_open("dsp_temp.txt");
    aica_reset(RBP_BYTE, 0);
    for (int k = 0; k < 64; k++) dsp_madrs(k, 0x10 + k);
    ram_fill(RBP_BYTE, 0, 0x1000);
    for (int i = 0; i < 128; i++) { aw(R_TEMP(i, 0), 0); aw(R_TEMP(i, 1), 0); }
    /* T1 */
    aw(R_MEMS(0, 1), 0xFFFF);
    for (int s = 0; s < 6; s++) dsp_coef(s, -4096);
    prog_A(0);
    prog_run(4000);
    dump("T1 after A shift 0 (4 ms)", now_us() - 4000);
    prog_A(1);
    uint64_t t0 = now_us();
    prog_load();
    dump("T1 A shift 1 dump 1", t0);
    dump("T1 A shift 1 dump 2", t0);
    spin_us(2000);
    dump("T1 A shift 1 dump 3", t0);
    spin_us(4000);
    dump("T1 A shift 1 dump 4", t0);
    /* T2 */
    prog_reset();
    for (int s = 0; s < 6; s++) {
        dsp_coef(s, -4096);
        P[s].XSEL = 1; P[s].IRA = IRA_MIXS(5); P[s].YSEL = 1; P[s].ZERO = 1; P[s].SHIFT = 3;
    }
    P[3].TWT = 1;
    PN = 6;
    aw(R_MIXS(5, 1), 0x1234); aw(R_MIXS(5, 0), 0x5);
    t0 = now_us();
    prog_run(4000);
    dump("T2 F ira 25 (4 ms)", t0);
    aw(R_MIXS(5, 1), 0x1234); aw(R_MIXS(5, 0), 0x5);
    LOG("T2 MIXS5 readback right after write %04lx/%lx\n", (unsigned long)ar(R_MIXS(5, 1)), (unsigned long)ar(R_MIXS(5, 0)));
    dump("T2 F ira 25 after a second write (no reload)", now_us());
    prog_reset();
    prog_run(1000);
    aica_quiet();
    out_close();
    return 0;
}
