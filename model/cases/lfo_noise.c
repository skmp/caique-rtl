/* lfo_noise.c -- the noise LFO waveforms on slots other than 3 (TODO 5.1): tests/sgc_lfo found the ALFO noise byte two
 * LFSR steps after the slot's own step and the PLFO noise byte 67 steps before its stage-A point, XOR 0x80 -- on slot 3
 * only (its stream 3).  Slot-independent or tied to fixed frames?  Two captures of 4 slots each (ISEL = stream):
 *   ln_0  ALFO noise (ALFOWS 3, ALFOS 7) on a constant 0x7FFF, slots 0, 21, 42, 63
 *   ln_1  PLFO noise (PLFOWS 3, PLFOS 7) on a PCM16 ramp (VOFF), slots 5, 26, 47, 60
 * Checker: tools/stream_replay (-knob alfo_noff / plfo_noff / plfo_nxor).  Output: lfo_noise.txt, ln_<n>.hdr/.bin,
 * const.bin, ramp.bin. */
#include "cap.h"
#define NS 4
#define MAXV (2u << 20)
static int32_t capbuf[MAXV];
static int16_t ramp[4096 + 64];

int test_main(void) {
    out_open("lfo_noise.txt");
    for (int i = 0; i < 4096 + 64; i++) ramp[i] = (int16_t)(8 * (i & 4095) - 0x4000);
    static uint32_t cst[2048];
    for (int i = 0; i < 2048; i++) cst[i] = 0x7FFF7FFF;
    out_bin("const.bin", cst, sizeof cst);
    out_bin("ramp.bin", ramp, sizeof ramp);
    LOG("ramfile 040000 const.bin\nramfile 020000 ramp.bin\n");
    static const int slots[2][NS] = {{0, 21, 42, 63}, {5, 26, 47, 60}};
    for (int r = 0; r < 2; r++) {
        aica_quiet();
        for (int i = 0; i < 2048; i++) ram_w32(0x40000 + 4 * i, 0x7FFF7FFF);
        ram_write(0x20000, ramp, sizeof ramp);
        char nm[16];
        snprintf(nm, sizeof nm, "ln_%d", r);
        for (int k = 0; k < NS; k++) {
            slot_cfg_t c;
            slot_cfg_default(&c, r ? 0x20000 : 0x40000, 4096);
            c.ISEL = k; c.AR = 31; c.KRS = 1; c.LFOF = 20;
            if (r == 0) { c.ALFOWS = 3; c.ALFOS = 7; }
            else { c.PLFOWS = 3; c.PLFOS = 7; c.VOFF = 1; }
            slot_write(slots[r][k], &c);
            slot_log(nm, k, slots[r][k], &c);
        }
        static const int mixs[NS] = {0, 1, 2, 3};
        if (cap_start(NS, mixs, capbuf, MAXV)) { OUT("cap_start failed\n"); return 1; }
        cap_wait_us(5000);
        for (int k = 0; k < NS; k++) aw(CH(slots[r][k], 0x00), (ar(CH(slots[r][k], 0x00)) & 0x3FFF) | 0x4000);
        aw(CH(0, 0x00), (ar(CH(0, 0x00)) & 0x7FFF) | 0x8000);
        cap_mark(1);
        cap_wait_us(300000);
        uint32_t n = cap_stop();
        cap_save(nm, n);
        OUT("%s: %lu samples, errors %lu\n", nm, (unsigned long)n, (unsigned long)CAP.errors);
    }
    aica_quiet();
    out_close();
    return 0;
}
