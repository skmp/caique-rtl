/* mixs_write.c -- which slots rewrite their MIXS bus every sample?  (the bus-0 retention asymmetry of tests/eg_lock
 * mixs and tests/slot_tail tail_c; work/verify/s5/model_fixes.md section 5)
 *
 * Known (NOTES "MIXS retention"): a MIXS bus keeps its value across samples unless the SGC rewrites it, and the DSP
 * reads one of two banks on alternate samples.  So a CPU write to a bus nobody rewrites shows on every other sample
 * for the rest of the capture (alternating with the other bank's old value), while a bus some slot rewrites shows the
 * CPU value on at most 1-2 samples.  Model rule (H_M): a bus is rewritten iff a slot with IMXL != 0 has ISEL = bus.
 * In both captures where a bus lost its retained value (eg_lock mixs: bus 0 read 0 while bus 2 kept -8; tail_c: bus 0
 * read 0 while buses 1/2 kept 2) 61..64 slots with reg 0x20 = 0 pointed at bus 0 (reg 0x20 = 0 is ISEL 0 as well as
 * IMXL 0) and NO slot pointed at the buses that retained.  Candidate rules, each a different set of "writers":
 *   H_M  IMXL 0 never writes (model)                  H_G  every slot writes its ISEL bus (IMXL is a gain; 0 -> 0)
 *   H_B  slot 0 always writes its ISEL bus            H_V  an IMXL-0 slot writes 0 iff VOFF 0 (H_D: "gain 0")
 *   H_F  an IMXL-0 slot writes 0 iff LPOFF 0          H_O  an IMXL-0 slot writes 0 iff it is off (not playing)
 *   H_S  an IMXL-0 slot writes 0 iff SA == 0          H_0  bus 0 is always rewritten, whoever points at it
 * Probe: aica_quiet (every register 0: ISEL 0 IMXL 0 VOFF 0 LPOFF 0 TL 0 SA 0, AEG off), configure a few slots,
 * cap_start on 4 buses, 10 ms, then write a distinctive positive 20-bit value V = 0x1PB25 (P = probe index, B = bus)
 * to up to three of the captured buses 10 ms apart (aw(R_MIXS(b,1), V >> 4); aw(R_MIXS(b,0), V & 15) as eg_lock
 * mixs_run does), 30 ms after the last, cap_stop.  Marks: 0x1BVVVVV right before a write, 0x2BVVVVV right after
 * (B = bus in bits 23:20, V in 19:0), so the captures are self-describing.
 *   P0   nothing configured: write bus 0 (all 64 zeroed slots point at it) and bus 1 (nobody)
 *   P1   slot 0 reg 0x20 = 0x01 (ISEL 1): write bus 1 (slot 0 alone) and bus 0 (slots 1..63)
 *   P1b  slot 0 -> 1, slots 1..63 -> 15 (reg 0x20 = 0x0F): write bus 0 (nobody), 1 (slot 0), 15 (slots 1..63)
 *   P2   slot 7 -> 2 (reg 0x20 = 0x02, all else 0); slot 8 -> 3 with IMXL 15 (0xF3: the LOST control, NOTES says a
 *        configured silent slot rewrites its bus with 0); slot 9 -> 4 with SA 0x10000 (reg 0x00 = 0x0001), never
 *        keyed: write 2, 3, 4
 *   P3   slot 7 -> 2 with VOFF 1 (reg 0x28 = 0x40); slot 8 -> 3 with LPOFF 1 (0x20); slot 9 -> 4 with VOFF 1 and
 *        LPOFF 1 (0x60); all off, IMXL 0: write 2, 3, 4   (the task's P3 / P4 / both)
 *   P5   slots 7/8/9 keyed on, playing a constant 0x7FFF (SA 0x10000, loop [0,32), AR 31, D1R 0, TL 0, IMXL 0) to
 *        buses 2/3/4 with (VOFF, LPOFF) = (0,1) / (1,1) / (0,0): write 2, 3, 4   (the task's P5 / P6 / P5 + filter)
 *   P7   slot 0 -> 3 with VOFF 1; slots 1..63 -> 15: write 3 (slot 0, VOFF 1), 15 (slots 1..63), 0 (nobody)
 *   P8   slot 63 -> 4 (all else 0); slots 0..62 -> 0: write 4 (slot 63 alone) and 0 (slots 0..62)
 * The 4th captured bus of each probe is not written (bus 0 or another one) and serves as a reference stream.  Logged:
 * the reg 0x20/0x28 readbacks of the configured slots, MIXS readbacks (mixs_rd) of the captured buses before the
 * writes and at the end, the raw hi/lo readback of each written bus right after its write, and for P5 the AEG
 * monitor of the playing slots before and after the capture.
 * Output: mixs_write.txt, P<n>.hdr/.bin.  Analysis: work/mixsw/mixsw_check.cpp (build/work/mixsw_check DIR), report
 * work/verify/s5/case_mixs_write.md. */
#include "cap.h"
#define NS 4
#define MAXV (1u << 16)
#define SA_CONST 0x10000u
static int32_t capbuf[MAXV];
static int cap_bus[NS];

static uint32_t probe_value(int idx, int bus) { return 0x10025u | ((uint32_t)idx << 12) | ((uint32_t)bus << 8); }

static void log_rd(const char *name, const char *when) {
    LOG("%s: MIXS readback %s:", name, when);
    for (int k = 0; k < NS; k++) LOG(" [%d]=%ld", cap_bus[k], (long)mixs_rd(cap_bus[k]));
    LOG("\n");
}
static void log_slot(const char *name, int ch) {
    LOG("%s: slot %d regs 00 %04lx 04 %04lx 20 %04lx 28 %04lx\n", name, ch, (unsigned long)ar(CH(ch, 0x00)),
        (unsigned long)ar(CH(ch, 0x04)), (unsigned long)ar(CH(ch, 0x20)), (unsigned long)ar(CH(ch, 0x28)));
}
static void write_bus(const char *name, int idx, int bus) {
    uint32_t v = probe_value(idx, bus);
    cap_mark(0x10000000u | ((uint32_t)bus << 20) | v);
    aw(R_MIXS(bus, 1), v >> 4);
    aw(R_MIXS(bus, 0), v & 0xF);
    cap_mark(0x20000000u | ((uint32_t)bus << 20) | v);
    uint32_t hi = ar(R_MIXS(bus, 1)), lo = ar(R_MIXS(bus, 0));
    LOG("%s: wrote %05lx to MIXS%d (hi %04lx lo %lx); readback right after: hi %04lx lo %lx\n", name,
        (unsigned long)v, bus, (unsigned long)(v >> 4), (unsigned long)(v & 0xF), (unsigned long)hi, (unsigned long)lo);
}
/* capture buses[0..3]; 10 ms; write buses[0..nw-1] 10 ms apart; 30 ms; stop */
static void probe(const char *name, int idx, const int *buses, int nw) {
    for (int k = 0; k < NS; k++) cap_bus[k] = buses[k];
    if (cap_start(NS, buses, capbuf, MAXV)) { OUT("%s: cap_start failed\n", name); return; }
    cap_wait_us(10000);
    log_rd(name, "before the writes");
    for (int j = 0; j < nw; j++) {
        if (j) cap_wait_us(10000);
        write_bus(name, idx, buses[j]);
    }
    cap_wait_us(30000);
    log_rd(name, "at the end");
    uint32_t n = cap_stop();
    cap_save(name, n);
    OUT("%s: %lu samples, errors %lu, %lu marks\n", name, (unsigned long)n, (unsigned long)CAP.errors, (unsigned long)CAP.nev);
}
static void point_all(int from, int to, uint32_t r20) { for (int c = from; c <= to; c++) aw(CH(c, 0x20), r20); }

int test_main(void) {
    out_open("mixs_write.txt");
    aica_quiet();
    for (int i = 0; i < 16; i++) ram_w32(SA_CONST + 4 * i, 0x7FFF7FFF);   /* 32 samples of 0x7FFF for P5 */

    /* P0: everything zeroed */
    {
        static const int b[NS] = {0, 1, 2, 3};
        aica_quiet();
        LOG("P0: all 64 slots zeroed (reg 0x20 = 0: ISEL 0, IMXL 0)\n");
        log_slot("P0", 0);
        probe("P0", 0, b, 2);
    }
    /* P1: slot 0 alone at bus 1, slots 1..63 zeroed at bus 0 */
    {
        static const int b[NS] = {1, 0, 2, 3};
        aica_quiet();
        aw(CH(0, 0x20), 0x01);
        LOG("P1: slot 0 reg 0x20 = 0x01 (ISEL 1, IMXL 0), slots 1..63 zeroed (ISEL 0)\n");
        log_slot("P1", 0); log_slot("P1", 1);
        probe("P1", 1, b, 2);
    }
    /* P1b: slot 0 at bus 1, slots 1..63 at bus 15, nobody at bus 0 */
    {
        static const int b[NS] = {0, 1, 15, 2};
        aica_quiet();
        aw(CH(0, 0x20), 0x01);
        point_all(1, 63, 0x0F);
        LOG("P1b: slot 0 reg 0x20 = 0x01 (ISEL 1), slots 1..63 reg 0x20 = 0x0F (ISEL 15), all IMXL 0\n");
        log_slot("P1b", 0); log_slot("P1b", 1); log_slot("P1b", 63);
        probe("P1b", 2, b, 3);
    }
    /* P2: one zeroed slot per bus: slot 7 -> 2 (IMXL 0), slot 8 -> 3 (IMXL 15, LOST control), slot 9 -> 4 (SA 0x10000) */
    {
        static const int b[NS] = {2, 3, 4, 0};
        aica_quiet();
        aw(CH(7, 0x20), 0x02);
        aw(CH(8, 0x20), 0xF3);
        aw(CH(9, 0x20), 0x04); aw(CH(9, 0x00), 0x0001);
        LOG("P2: slot 7 reg 0x20 = 0x02 (ISEL 2, IMXL 0); slot 8 = 0xF3 (ISEL 3, IMXL 15); slot 9 = 0x04 (ISEL 4) with SA 0x10000; all off\n");
        log_slot("P2", 7); log_slot("P2", 8); log_slot("P2", 9);
        probe("P2", 3, b, 3);
    }
    /* P3: off slots with IMXL 0 and VOFF / LPOFF set */
    {
        static const int b[NS] = {2, 3, 4, 0};
        aica_quiet();
        aw(CH(7, 0x20), 0x02); aw(CH(7, 0x28), 0x40);
        aw(CH(8, 0x20), 0x03); aw(CH(8, 0x28), 0x20);
        aw(CH(9, 0x20), 0x04); aw(CH(9, 0x28), 0x60);
        LOG("P3: slot 7 -> 2 VOFF 1 (0x28 = 0x40); slot 8 -> 3 LPOFF 1 (0x20); slot 9 -> 4 VOFF 1 LPOFF 1 (0x60); IMXL 0, off\n");
        log_slot("P3", 7); log_slot("P3", 8); log_slot("P3", 9);
        probe("P3", 4, b, 3);
    }
    /* P5: playing slots with IMXL 0 */
    {
        static const int b[NS] = {2, 3, 4, 0};
        static const int voff[3] = {0, 1, 0}, lpoff[3] = {1, 1, 0};
        aica_quiet();
        for (int s = 0; s < 3; s++) {
            slot_cfg_t c;
            slot_cfg_default(&c, SA_CONST, 32);
            c.IMXL = 0; c.ISEL = 2 + s; c.TL = 0; c.VOFF = voff[s]; c.LPOFF = lpoff[s];
            slot_write(7 + s, &c);
            LOG("P5: slot %d SA %05lx loop [0,32) AR %d D1R %d RR %d KRS %d ISEL %d IMXL %d TL %d VOFF %d LPOFF %d Q %d FLV %04x\n",
                7 + s, (unsigned long)c.SA, c.AR, c.D1R, c.RR, c.KRS, c.ISEL, c.IMXL, c.TL, c.VOFF, c.LPOFF, c.Q, c.FLV[0]);
        }
        for (int s = 7; s <= 9; s++) aw(CH(s, 0x00), (ar(CH(s, 0x00)) & 0x3FFF) | 0x4000);
        aw(CH(7, 0x00), (ar(CH(7, 0x00)) & 0x7FFF) | 0x8000);   /* KYONEX */
        spin_us(5000);
        for (int s = 7; s <= 9; s++) {
            log_slot("P5", s);
            LOG("P5: slot %d before the capture: EGMON %04lx CA %lu\n", s, (unsigned long)egmon(s, 0), (unsigned long)ar(R_CAMON));
        }
        probe("P5", 5, b, 3);
        for (int s = 7; s <= 9; s++)
            LOG("P5: slot %d after the capture: EGMON %04lx CA %lu\n", s, (unsigned long)egmon(s, 0), (unsigned long)ar(R_CAMON));
    }
    /* P7: slot 0 with VOFF 1 at bus 3, slots 1..63 at bus 15, nobody at bus 0 */
    {
        static const int b[NS] = {3, 15, 0, 1};
        aica_quiet();
        aw(CH(0, 0x20), 0x03); aw(CH(0, 0x28), 0x40);
        point_all(1, 63, 0x0F);
        LOG("P7: slot 0 reg 0x20 = 0x03 (ISEL 3, IMXL 0) VOFF 1; slots 1..63 reg 0x20 = 0x0F (ISEL 15)\n");
        log_slot("P7", 0); log_slot("P7", 1);
        probe("P7", 6, b, 3);
    }
    /* P8: the last processed slot alone at bus 4 */
    {
        static const int b[NS] = {4, 0, 1, 2};
        aica_quiet();
        aw(CH(63, 0x20), 0x04);
        LOG("P8: slot 63 reg 0x20 = 0x04 (ISEL 4, IMXL 0), slots 0..62 zeroed (ISEL 0)\n");
        log_slot("P8", 63);
        probe("P8", 7, b, 2);
    }
    aica_quiet();
    out_close();
    return 0;
}
