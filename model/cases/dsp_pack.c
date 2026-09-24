/* dsp_pack.c -- every 24-bit value through PACK (float MWT, NOFL=0), checked on the console against
 * src/dsp_float.h dsp_pack.  Value = -(h << 8) + L: MEMS[j].h = h (X = INPUTS, Y = COEF -4096), every TEMP slot = L
 * (B = TEMP), SHIFT 3; 30 values per batch (MWT at odd steps 1..59, TABLE=1, words 0x10..0x2D).
 * Output: dsp_pack.txt (mismatch count per L, first mismatches, CRC of all results in (L, h) order). */
#include "aica_io.h"

#define RBP_BYTE 0x100000u

static uint32_t crc32_upd(uint32_t c, uint16_t v) {
    for (int i = 0; i < 16; i++) {
        uint32_t b = ((c ^ (v >> i)) & 1);
        c = (c >> 1) ^ (b ? 0xEDB88320u : 0);
    }
    return c;
}

int test_main(void) {
    out_open("dsp_pack.txt");
    aica_reset(RBP_BYTE, 0);
    prog_reset();
    for (int j = 0; j < 30; j++) {
        dsp_madrs(j, 0x10 + j);
        dsp_coef(2 * j, -4096);
        dsp_coef(2 * j + 1, 0);
        P[2 * j].XSEL = 1; P[2 * j].IRA = j; P[2 * j].YSEL = 1; P[2 * j].TRA = 0;
        P[2 * j + 1].SHIFT = 3; P[2 * j + 1].MWT = 1; P[2 * j + 1].TABLE = 1; P[2 * j + 1].MASA = j;
        P[2 * j + 1].ZERO = 1; P[2 * j + 1].YSEL = 1;
    }
    PN = 60;
    for (int i = 0; i < 128; i++) { aw(R_TEMP(i, 1), 0); aw(R_TEMP(i, 0), 0); }
    prog_load();
    uint32_t bad = 0, crc = 0xFFFFFFFFu;
    uint64_t t0 = now_us();
    for (uint32_t L = 0; L < 256; L++) {
        for (int i = 0; i < 128; i++) aw(R_TEMP(i, 0), L);
        uint32_t badL = 0;
        for (uint32_t base = 0; base < 65536; base += 30) {
            uint32_t n = 65536 - base < 30 ? 65536 - base : 30;
            for (uint32_t j = 0; j < n; j++) aw(R_MEMS(j, 1), base + j);
            spin_us(L == 0 && base == 0 ? 5000 : 60); /* first batch: TEMP fill must have gone round */
            for (uint32_t j = 0; j < n; j += 2) {
                uint32_t w2 = ram_r32(RBP_BYTE + 2 * (0x10 + j));
                for (uint32_t k = 0; k < 2 && j + k < n; k++) {
                    uint16_t got = (uint16_t)(w2 >> (16 * k));
                    uint32_t h = base + j + k;
                    int32_t v = (int32_t)((uint32_t)(-(int32_t)((int32_t)(h << 16) >> 8) + (int32_t)L) << 8) >> 8;
                    uint16_t ref = dsp_pack(v);
                    crc = crc32_upd(crc, got);
                    if (got != ref) {
                        if (bad < 300) LOG("mismatch L %02lx h %04lx value %06lx: hw %04x ref %04x\n", (unsigned long)L,
                                           (unsigned long)h, (unsigned long)(v & 0xFFFFFF), got, ref);
                        bad++;
                        badL++;
                    }
                }
            }
        }
        if (badL) LOG("L %02lx: %lu mismatches\n", (unsigned long)L, (unsigned long)badL);
        if ((L & 31) == 31) { char b[80]; snprintf(b, sizeof b, "L %lu done, %lu mismatches so far\n", (unsigned long)L, (unsigned long)bad); io_print(b); }
    }
    prog_reset(); prog_load();
    OUT("dsp_pack: %lu of 16777216 values differ from dsp_pack(); results crc32 %08lx; %lu ms\n", (unsigned long)bad,
        (unsigned long)(crc ^ 0xFFFFFFFFu), (unsigned long)((now_us() - t0) / 1000));
    aica_quiet();
    out_close();
    return 0;
}
