/* flog.h -- frame logger: where in the sample an SH4 write lands, to one frame (8 clocks).
 *
 * The SH4 writes a marker into MEMS31 (bits 23:8) right before and right after the write under test; a DSP program reads
 * MEMS31 in every frame and logs it, every sample, into the wave RAM ring, together with a sample counter and up to
 * FLOG_MAXS MIXS buses (the effect of the write).  The two markers' landing frames bracket the write.
 *
 * DSP program (RBL 3: 64K-word ring at FLOG_RBP_BYTE; pair p = steps 2p (even) and 2p+1 (odd) writes region p at
 * MADRS[p] = 1024 p, the word of the DSP sample with counter m at region + m; a region holds the last 1024 samples):
 *   pair 0          counter: step 0 ACC = TEMP[1+m] - 0x100, step 1 TWT TEMP[0+m], MWT NOFL: word -(n + 1) for DSP
 *                   sample n (as cases/cap.h)
 *   pair 1..ns      MIXS stream: step 2p ACC = -INPUTS(MIXS[bus]); step 2p+1 MWT NOFL SHIFTED[23:8] (SHIFT 0): the word
 *                   is floor(-16 MIXS / 256) = -ceil(MIXS / 16), MIXS of the sweep before the DSP sample
 *   pair ns+1..63   marker: step 2p ACC = -MEMS31 (read at t1: ph 64 + 8p + 1 of the DSP sample, i.e. frame 8 + p, c1;
 *                   pairs 56..63 in frames 0..7 of the next sweep); step 2p+1 MWT NOFL SHIFTED[23:8] (SHIFT 3): the word
 *                   is -marker
 * Only odd steps write the wave RAM, so no playing channel's slot (DSP step 2K - 14, even) drops a log word.
 * A marker write whose X0 is at clock X shows first in the read after X; with the reads 8 clocks apart that brackets X
 * to one frame.  flog_start() clears the ring and loads the program; flog_stop() clears the odd steps' MWT (the ring
 * stops changing); flog_read() copies the whole ring for the checker.
 */
#ifndef CAIQUE_FLOG_H
#define CAIQUE_FLOG_H
#include "aica_io.h"

#define FLOG_RBP_BYTE 0x1E0000u
#define FLOG_WORDS 65536u
#define FLOG_MAXS 8

static int flog_ns;
static inline void flog_marker(uint16_t v) { aw(R_MEMS(31, 1), v); }

/* the program; ns MIXS streams (buses mixs[0..ns-1]) */
static inline void flog_start(int ns, const int *mixs) {
    flog_ns = ns;
    dsp_reset(FLOG_RBP_BYTE, 3);
    for (int r = 0; r < 64; r++) dsp_madrs(r, (uint16_t)(r * 1024));
    aw(R_MEMS(30, 1), 0x0001);   /* MEMS30 = 0x000100: the counter's X */
    prog_reset();
    dsp_coef(0, -4096); dsp_coef(1, 0);
    P[0].XSEL = 1; P[0].IRA = 30; P[0].YSEL = 1; P[0].TRA = 1;
    P[1].SHIFT = 3; P[1].TWT = 1; P[1].TWA = 0; P[1].MWT = 1; P[1].NOFL = 1; P[1].MASA = 0; P[1].ZERO = 1; P[1].YSEL = 1;
    for (int p = 1; p < 64; p++) {
        const int e = 2 * p, o = e + 1;
        dsp_coef(e, -4096); dsp_coef(o, 0);
        P[e].XSEL = 1; P[e].IRA = p <= ns ? IRA_MIXS(mixs[p - 1]) : 31; P[e].YSEL = 1; P[e].ZERO = 1;
        P[o].SHIFT = p <= ns ? 0 : 3; P[o].MWT = 1; P[o].NOFL = 1; P[o].MASA = p; P[o].ZERO = 1; P[o].YSEL = 1;
    }
    PN = 128;
    prog_load();
    io_wait_us(100);
}
static inline void flog_stop(void) {   /* no more ring writes (odd steps keep their other fields) */
    for (int p = 63; p >= 0; p--) { P[2 * p + 1].MWT = 0; dsp_put(2 * p + 1, &P[2 * p + 1]); }
    io_wait_us(50);
}
static inline void flog_resume(void) {
    for (int p = 63; p >= 0; p--) { P[2 * p + 1].MWT = 1; dsp_put(2 * p + 1, &P[2 * p + 1]); }
}
static inline void flog_read(uint16_t *ring) {   /* FLOG_WORDS words */
    ram_read(FLOG_RBP_BYTE, ring, FLOG_WORDS * 2);
}
#endif
