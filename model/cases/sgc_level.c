/* sgc_level.c -- static channel levels at the DSP input: a looped constant PCM16 sample, read back from MIXS[0]
 * (20-bit) by the CPU once the slot has settled.  Filter off (LPOFF=1), AEG at 0 (AR=31, D1R=D2R=0).
 *   L1  TL 0..255 at IMXL 15, sample 0x7FFF / 0x4000 / -0x8000 / 0x0101
 *   L2  IMXL 0..15 at TL 0, sample 0x7FFF
 *   L3  sample sweep at TL 0 / 1 / 16 / 100, IMXL 15
 *   L4  TL + IMXL combinations
 *   L5  VOFF, LPOFF=0 (filter on at FLV 0x1FF8, Q 0..31), DISDL/DIPAN (should not affect MIXS)
 * Output: sgc_level.txt */
#include "aica_io.h"

#define SA 0x10000u
static slot_cfg_t C;

static void set_sample(int16_t v) {
    uint32_t w = (uint16_t)v | ((uint32_t)(uint16_t)v << 16);
    for (int i = 0; i < 32; i += 2) ram_w32(SA + 2 * i, w);
}
static int32_t settle_read(void) {
    spin_us(1500);
    int32_t a = mixs_rd(0), b = mixs_rd(0), c = mixs_rd(0);
    if (a != b || b != c) LOG("  (unstable %d %d %d)", (int)a, (int)b, (int)c);
    return c;
}

int test_main(void) {
    out_open("sgc_level.txt");
    aica_quiet();
    set_sample(0x7FFF);
    slot_cfg_default(&C, SA, 32);
    slot_write(0, &C);
    ch_keyon(0);
    spin_us(5000);
    OUT("EG after keyon: %04lx\n", (unsigned long)egmon(0, 0));

    static const int16_t sv[] = {0x7FFF, 0x4000, -0x8000, 0x0101};
    for (unsigned k = 0; k < 4; k++) {
        set_sample(sv[k]);
        LOG("L1 sample %d TL 0..255:\n", sv[k]);
        for (int tl = 0; tl < 256; tl++) {
            C.TL = tl; aw(CH(0, 0x28), (C.TL << 8) | (C.VOFF << 6) | (C.LPOFF << 5) | C.Q);
            LOG(" %d", (int)settle_read());
            if ((tl & 15) == 15) LOG("\n");
        }
    }
    C.TL = 0; aw(CH(0, 0x28), (C.LPOFF << 5));
    set_sample(0x7FFF);
    LOG("L2 sample 32767 TL 0 IMXL 0..15:");
    for (int im = 0; im < 16; im++) { aw(CH(0, 0x20), (im << 4)); LOG(" %d", (int)settle_read()); }
    LOG("\n");
    aw(CH(0, 0x20), 0xF0);
    static const int tls[] = {0, 1, 16, 100};
    for (int t = 0; t < 4; t++) {
        aw(CH(0, 0x28), (tls[t] << 8) | (1 << 5));
        LOG("L3 TL %d samples -32768..32767 step 257:\n", tls[t]);
        for (int v = -32768, n = 0; v <= 32767; v += 257, n++) {
            set_sample((int16_t)v);
            LOG(" %d:%d", v, (int)settle_read());
            if ((n & 7) == 7) LOG("\n");
        }
        LOG("\n");
    }
    set_sample(0x7FFF);
    LOG("L4 (TL,IMXL) -> MIXS:");
    static const int combos[][2] = {{0, 15}, {16, 15}, {0, 14}, {8, 14}, {16, 13}, {24, 13}, {32, 12}, {40, 11}, {128, 15}, {112, 14}, {250, 15}, {240, 14}, {255, 1}, {0, 1}};
    for (unsigned i = 0; i < sizeof combos / sizeof combos[0]; i++) {
        aw(CH(0, 0x28), (combos[i][0] << 8) | (1 << 5));
        aw(CH(0, 0x20), combos[i][1] << 4);
        LOG(" (%d,%d)=%d", combos[i][0], combos[i][1], (int)settle_read());
    }
    LOG("\n");
    aw(CH(0, 0x20), 0xF0);
    aw(CH(0, 0x28), (1 << 5));
    LOG("L5 VOFF=1: %d\n", (int)(aw(CH(0, 0x28), (1 << 6) | (1 << 5)), settle_read()));
    LOG("L5 LPOFF=0 FLV 1FF8 Q 0..31:");
    for (int q = 0; q < 32; q++) { aw(CH(0, 0x28), q); LOG(" %d", (int)settle_read()); }
    LOG("\n");
    aw(CH(0, 0x28), (1 << 5));
    LOG("L5 DISDL 15 DIPAN 0/15/31: ");
    for (int p = 0; p < 32; p += 15) { aw(CH(0, 0x24), (15 << 8) | p); LOG(" %d", (int)settle_read()); }
    aw(CH(0, 0x24), 0);
    LOG("\n");
    ch_keyoff(0);
    aica_quiet();
    out_close();
    return 0;
}
