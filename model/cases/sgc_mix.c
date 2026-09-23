/* sgc_mix.c -- several slots on one MIXS bus: accumulation and saturation (VOFF=1, LPOFF=1, constant samples,
 * read back from MIXS by the CPU).  N slots of +0x7FFF on MIXS 0 (N = 1..20), N slots of -0x8000 on MIXS 1, mixed
 * signs, and IMXL-scaled sums.  Output: sgc_mix.txt */
#include "aica_io.h"
static int32_t settle(int i) { io_wait_us(2000); return mixs_rd(i); }
int test_main(void) {
    out_open("sgc_mix.txt");
    aica_quiet();
    for (int i = 0; i < 64; i++) { ram_w32(0x20000 + 4 * i, 0x7FFF7FFF); ram_w32(0x21000 + 4 * i, 0x80008000); ram_w32(0x22000 + 4 * i, 0x01230123); }
    for (int n = 1; n <= 20; n++) {
        for (int k = 0; k < n; k++) {
            slot_cfg_t c;
            slot_cfg_default(&c, 0x20000, 64);
            c.VOFF = 1; c.ISEL = 0;
            slot_write(k, &c);
            aw(CH(k, 0x00), (ar(CH(k, 0x00)) & 0x3FFF) | 0x4000);
        }
        aw(CH(0, 0x00), (ar(CH(0, 0x00)) & 0x7FFF) | 0x8000);
        LOG("N %2d x +7FFF: MIXS0 %ld\n", n, (long)settle(0));
    }
    aica_quiet();
    for (int n = 1; n <= 20; n++) {
        for (int k = 0; k < n; k++) {
            slot_cfg_t c;
            slot_cfg_default(&c, 0x21000, 64);
            c.VOFF = 1; c.ISEL = 1;
            slot_write(k, &c);
            aw(CH(k, 0x00), (ar(CH(k, 0x00)) & 0x3FFF) | 0x4000);
        }
        aw(CH(0, 0x00), (ar(CH(0, 0x00)) & 0x7FFF) | 0x8000);
        LOG("N %2d x -8000: MIXS1 %ld\n", n, (long)settle(1));
    }
    aica_quiet();
    /* 20 positive then 19 negative on the same bus: saturates in between or only at the end? */
    for (int k = 0; k < 40; k++) {
        slot_cfg_t c;
        slot_cfg_default(&c, k < 20 ? 0x20000 : 0x21000, 64);
        c.VOFF = 1; c.ISEL = 2;
        slot_write(k, &c);
        aw(CH(k, 0x00), (ar(CH(k, 0x00)) & 0x3FFF) | 0x4000);
    }
    aw(CH(0, 0x00), (ar(CH(0, 0x00)) & 0x7FFF) | 0x8000);
    LOG("slots 0-19 +7FFF, 20-39 -8000 on MIXS2: %ld\n", (long)settle(2));
    aica_quiet();
    for (int k = 0; k < 40; k++) {
        slot_cfg_t c;
        slot_cfg_default(&c, k < 20 ? 0x21000 : 0x20000, 64);
        c.VOFF = 1; c.ISEL = 3;
        slot_write(k, &c);
        aw(CH(k, 0x00), (ar(CH(k, 0x00)) & 0x3FFF) | 0x4000);
    }
    aw(CH(0, 0x00), (ar(CH(0, 0x00)) & 0x7FFF) | 0x8000);
    LOG("slots 0-19 -8000, 20-39 +7FFF on MIXS3: %ld\n", (long)settle(3));
    aica_quiet();
    /* 64 slots of 0x0123 with IMXL 13 (x1/2): rounding accumulates per slot or once? */
    for (int k = 0; k < 64; k++) {
        slot_cfg_t c;
        slot_cfg_default(&c, 0x22000, 64);
        c.VOFF = 1; c.ISEL = 4; c.IMXL = 7;
        slot_write(k, &c);
        aw(CH(k, 0x00), (ar(CH(k, 0x00)) & 0x3FFF) | 0x4000);
    }
    aw(CH(0, 0x00), (ar(CH(0, 0x00)) & 0x7FFF) | 0x8000);
    LOG("64 slots 0x0123 IMXL 7: MIXS4 %ld (one slot alone: 0x0123*16*3/32 floored = %ld)\n", (long)settle(4),
        (long)((0x123 * 16 * 3) >> 5));
    aica_quiet();
    out_close();
    return 0;
}
