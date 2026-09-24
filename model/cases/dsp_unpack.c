/* dsp_unpack.c -- every 16-bit memory word through MRD (float, NOFL=0) -> IWT -> MEMS, read back as 24 bits.
 * 30 words per batch: MRD at odd steps 1..59 (TABLE=1, words 0x10..0x2D from RBP), IWT two steps later into
 * MEMS[0..29].  Output: unpack.bin (65536 x u32, the 24-bit MEMS value), dsp_unpack.txt (mismatches vs
 * src/dsp_float.h dsp_unpack). */
#include "aica_io.h"

#define RBP_BYTE 0x100000u
static uint32_t res[65536];

int test_main(void) {
    out_open("dsp_unpack.txt");
    aica_reset(RBP_BYTE, 0);
    prog_reset();
    for (int j = 0; j < 30; j++) {
        dsp_madrs(j, 0x10 + j);
        P[2 * j + 1].MRD = 1; P[2 * j + 1].TABLE = 1; P[2 * j + 1].MASA = j;
        P[2 * j + 3].IWT = 1; P[2 * j + 3].IWA = j;
    }
    PN = 62;
    prog_load();
    for (uint32_t base = 0; base < 65536; base += 30) {
        uint32_t n = 65536 - base < 30 ? 65536 - base : 30;
        for (uint32_t j = 0; j < 30; j += 2) {
            uint32_t a = base + j, b = base + j + 1;
            ram_w32(RBP_BYTE + 2 * (0x10 + j), (a & 0xFFFF) | ((b & 0xFFFF) << 16));
        }
        spin_us(60);
        for (uint32_t j = 0; j < n; j++)
            res[base + j] = ((ar(R_MEMS(j, 1)) & 0xFFFF) << 8) | (ar(R_MEMS(j, 0)) & 0xFF);
    }
    prog_reset(); prog_load();
    uint32_t bad = 0;
    for (uint32_t w = 0; w < 65536; w++) {
        uint32_t ref = (uint32_t)dsp_unpack((uint16_t)w) & 0xFFFFFF;
        if (res[w] != ref) {
            if (bad < 200) LOG("mismatch %04lx: hw %06lx ref %06lx\n", (unsigned long)w, (unsigned long)res[w], (unsigned long)ref);
            bad++;
        }
    }
    OUT("dsp_unpack: %lu of 65536 words differ from dsp_unpack()\n", (unsigned long)bad);
    out_bin("unpack.bin", res, sizeof res);
    aica_quiet();
    out_close();
    return 0;
}
