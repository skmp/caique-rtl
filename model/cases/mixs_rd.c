/* mixs_rd.c -- the CPU readback path of a MIXS bus: after the SH4 writes the two halves (0x4500 + 8b: low nibble,
 * +4: bits 19:4), what do immediate and later reads return, on a bus that a slot rewrites every sample and on one
 * nobody points at?  (tests/probe read the write back at once on bus 0; tests/eg_lock read hi 0 / lo as written on a
 * bus with a silent writer; the model returns the DSP-side value.)  Every run: aica_quiet (all slots ISEL 0, IMXL 0:
 * they write 0 to bus 0 every sample), then the configuration, then the write of hi 0x1234 / lo 0x5 to bus B followed
 * by 96 back-to-back reads alternating hi / lo with timestamps (about 5 us per pair = 4 pairs per sample).
 *   R1 bus 0 (64 silent writers writing 0)          R2 bus 5 (no writer)
 *   R3 bus 5 with slot 5 playing 0x0100 x16 = 4096 per sample at IMXL 15 (a nonzero writer)
 *   R4 bus 5 with slot 5 pointing at it, IMXL 0, off (a writer writing 0)
 *   R5 bus 0, low nibble written first, then hi   R6 bus 5 (no writer), hi only   R7 bus 5 (no writer), lo only
 * Output: mixs_rd.txt */
#include "aica_io.h"
static void readback(const char *tag, int b) {
    uint32_t hi[96], lo[96]; uint64_t ts[96];
    uint64_t t0 = now_us();
    for (int i = 0; i < 96; i++) { ts[i] = now_us() - t0; hi[i] = ar(R_MIXS(b, 1)) & 0xFFFF; lo[i] = ar(R_MIXS(b, 0)) & 0xF; }
    LOG("%s bus %d reads (t_us hi/lo):", tag, b);
    for (int i = 0; i < 96; i++) LOG("%s %lu %04lx/%lx", i % 8 ? "" : "\n   ", (unsigned long)ts[i], (unsigned long)hi[i], (unsigned long)lo[i]);
    LOG("\n");
    spin_us(30000);
    LOG("%s bus %d after 30 ms: %04lx/%lx\n", tag, b, (unsigned long)(ar(R_MIXS(b, 1)) & 0xFFFF), (unsigned long)(ar(R_MIXS(b, 0)) & 0xF));
}
static void writer(int b, int imxl, int play) {
    for (int i = 0; i < 16; i++) ram_w32(0x10000 + 4 * i, 0x01000100);
    slot_cfg_t c;
    slot_cfg_default(&c, 0x10000, 32);
    c.ISEL = b; c.IMXL = imxl; c.VOFF = 1;
    slot_write(5, &c);
    if (play) { ch_keyon(5); spin_us(5000); }
}
int test_main(void) {
    out_open("mixs_rd.txt");
    aica_quiet(); spin_us(5000);
    LOG("R1 bus 0 before: %04lx/%lx\n", (unsigned long)(ar(R_MIXS(0, 1)) & 0xFFFF), (unsigned long)(ar(R_MIXS(0, 0)) & 0xF));
    aw(R_MIXS(0, 1), 0x1234); aw(R_MIXS(0, 0), 0x5); readback("R1", 0);
    aica_quiet(); spin_us(5000);
    LOG("R2 bus 5 before: %04lx/%lx\n", (unsigned long)(ar(R_MIXS(5, 1)) & 0xFFFF), (unsigned long)(ar(R_MIXS(5, 0)) & 0xF));
    aw(R_MIXS(5, 1), 0x1234); aw(R_MIXS(5, 0), 0x5); readback("R2", 5);
    aica_quiet(); writer(5, 15, 1);
    LOG("R3 bus 5 before: %04lx/%lx\n", (unsigned long)(ar(R_MIXS(5, 1)) & 0xFFFF), (unsigned long)(ar(R_MIXS(5, 0)) & 0xF));
    aw(R_MIXS(5, 1), 0x1234); aw(R_MIXS(5, 0), 0x5); readback("R3", 5);
    aica_quiet(); writer(5, 0, 0);
    LOG("R4 bus 5 before: %04lx/%lx\n", (unsigned long)(ar(R_MIXS(5, 1)) & 0xFFFF), (unsigned long)(ar(R_MIXS(5, 0)) & 0xF));
    aw(R_MIXS(5, 1), 0x1234); aw(R_MIXS(5, 0), 0x5); readback("R4", 5);
    aica_quiet(); spin_us(5000);
    aw(R_MIXS(0, 0), 0x5); aw(R_MIXS(0, 1), 0x1234); readback("R5", 0);
    aica_quiet(); spin_us(5000);
    aw(R_MIXS(5, 1), 0x1234); readback("R6", 5);
    aica_quiet(); spin_us(5000);
    aw(R_MIXS(5, 0), 0x5); readback("R7", 5);
    aica_quiet();
    out_close();
    return 0;
}
