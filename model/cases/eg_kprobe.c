/* eg_kprobe.c -- K probes: what pins the envelope counter to the DSP ring counter, and what moves it.
 *
 * NOTES.md "Envelope clock": the envelope clock ticks on the samples with even MDEC_CT and its counter is
 * eg_cnt = K - MDEC_CT/2 (mod 2^14) with one constant K per console boot (6491 on the boot of tests/eg_lock; 165 mod
 * 1024 on an earlier boot).  Two counters free-run, so something during a boot sets their offset.  This case measures
 * K eight times ("probes") and performs a candidate action before each one; a K change after an action means that
 * action reset or offset one of the two counters.  Every probe also logs the head measurement (MDEC_CT at a known
 * SH4 time), so a jump of MDEC_CT against the SH4 clock separates "MDEC_CT was reset" from "the EG counter was reset".
 *
 * One probe = one capture (cap.h, 4 MIXS buses): 4 slots, constant 0x7FFF PCM16 looped [0,32) (the constant also
 * fills the RAM beyond LEA, so the pitch-1.5 loop-end interpolation stays constant), TL 0, VOFF 0, IMXL 15,
 * ISEL k -> MIXS k (the eg_lock.c AEG harness).  All slots AR 31, DL 31, D2R 0, RR 31, KRS 0, OCT 0, FNS 0x200
 * (s = 1: the attack is R 63 = instant, a = 0 on the key-on sample); D1R = 1, 6, 14, 22 -> decay 1 at effective R
 * 3, 13, 29, 45: ticks every 2048 / 128 / 8 / 1 clocks from rows 3 / 1 / 1 / 1 (+1 per tick from a = 0, every step
 * visible in the level).  R 3 is indexed by eg_cnt bits 13:11 (row 3 = {0,1,1,1,1,1,1,1}: one skipped tick per 8 =
 * per 32768 samples), so a 1.5 s capture pins K modulo 16384 -- bit 13 included (rate 2 pinned it only mod 8192).
 * Fitter: work/kprobe/kfit.cpp (tools/eg_phase.cpp starts every attack at a = 0x280 and cannot take the R 63 attack).
 *
 * Probes (the action is performed BEFORE the probe, after the previous one's save):
 *   p0  baseline
 *   p1  nothing (repeatability)
 *   p2  RBP/RBL (0x2804) written to another ring (RBL 0, RBP 0) and back
 *   p3  timers: TIMA/TIMB/TIMC (0x2890/4/8) each written 0x0000, 0x0700, 0x00FF, 0x0000 (500 us apart)
 *   p4  0x2800 written 0x000F then 0x0000 (MVOL only; bits 15/9/8 stay 0)
 *   p5  ARM7 released for 5 ms with every exception vector (wave RAM 0x00..0x3C) = 0xEAFFFFFE ("b .")
 *   p6  a full 128-step DSP program of NOPs loaded and run 10 ms, then unloaded
 *   p7  every channel register of all 64 slots written 0xFFFF, KYONEX, then the aica_quiet sequence
 * Output: eg_kprobe.txt (per probe: the slot programs, the cap_start line, "probe pN: c0 XXXX head n N t_head_us T
 * ... action ..."), p<i>.hdr/.bin.  Console time: about 15 s. */
#include "cap.h"
#define NS 4
#define NPROBE 8
#define MAXV (2u << 20)
#define SA_CONST 0x10000u
#define ON_US 1500000u
static int32_t capbuf[MAXV];
static const int mixs[NS] = {0, 1, 2, 3};
static const int d1r[NS] = {1, 6, 14, 22}; /* effective R 3, 13, 29, 45 with KRS 0 OCT 0 FNS 0x200 */

static void keys(int on) {
    for (int k = 0; k < NS; k++) aw(CH(k, 0), (ar(CH(k, 0)) & 0x3FFF) | (on ? 0x4000 : 0));
    aw(CH(0, 0), (ar(CH(0, 0)) & 0x7FFF) | 0x8000);
}

static void probe(int idx, const char *action) {
    char name[16];
    snprintf(name, sizeof name, "p%d", idx);
    aica_quiet();
    /* the constant continues past LEA: at pitch 1.5 (FNS 0x200) the sample before LEA interpolates against the RAM
     * word beyond the loop, which would halve one output sample per loop (NOTES: the +21 artefact of eg_lock odd_*) */
    for (int i = 0; i < 24; i++) ram_w32(SA_CONST + 4 * i, 0x7FFF7FFF);
    for (int k = 0; k < NS; k++) {
        slot_cfg_t c;
        slot_cfg_default(&c, SA_CONST, 32);
        c.ISEL = k; c.AR = 31; c.D1R = d1r[k]; c.DL = 31; c.D2R = 0; c.RR = 31;
        c.KRS = 0; c.OCT = 0; c.FNS = 0x200;
        slot_write(k, &c);
        LOG("%s stream %d: slot %d AR %d D1R %d DL %d D2R %d RR %d KRS %d OCT %d FNS %03x\n", name, k, k, c.AR, c.D1R,
            c.DL, c.D2R, c.RR, c.KRS, c.OCT, c.FNS);
    }
    if (cap_start(NS, mixs, capbuf, MAXV)) { OUT("%s: cap_start failed\n", name); return; }
    /* the head measurement right after cap_start: MDEC_CT of sample n_head is (c0 - n_head) & 0xFFFF at SH4 time
     * t_head (cap_poll may re-measure later, so record it now) */
    uint32_t c0 = CAP.c0, n_head = CAP.n_head;
    uint64_t t_head = CAP.t_head;
    cap_wait_us(5000);
    cap_mark(1); keys(1); cap_mark(2);
    cap_wait_us(ON_US);
    cap_mark(3); keys(0); cap_mark(4);
    cap_wait_us(20000);
    uint32_t n = cap_stop();
    cap_save(name, n);
    OUT("probe %s: c0 %04lx head n %lu t_head_us %lu%06lu mdec_head %04lx samples %lu errors %lu marks %lu action %s\n",
        name, (unsigned long)c0, (unsigned long)n_head, (unsigned long)(t_head / 1000000u),
        (unsigned long)(t_head % 1000000u), (unsigned long)((c0 - n_head) & 0xFFFF), (unsigned long)n,
        (unsigned long)CAP.errors, (unsigned long)CAP.nev, action);
}

/* ---- candidate actions ---- */
static void act_ring(void) {
    dsp_ring(0, 0);           /* RBL 0 (8K words) at RBP 0 */
    spin_us(2000);
    dsp_ring(CAP_RBP_BYTE >> 11, 3);
    spin_us(2000);
}
static void act_timers(void) {
    static const uint32_t regs[3] = {R_TIMA, R_TIMB, R_TIMC};
    static const uint16_t vals[4] = {0x0000, 0x0700, 0x00FF, 0x0000};
    for (int t = 0; t < 3; t++)
        for (int v = 0; v < 4; v++) { aw(regs[t], vals[v]); spin_us(500); }
    LOG("timers: SCIPD %04lx MCIPD %04lx after the writes\n", (unsigned long)ar(0x289C), (unsigned long)ar(R_MCIPD));
}
static void act_mvol(void) {
    aw(R_MVOL, 0x000F);
    spin_us(2000);
    aw(R_MVOL, 0x0000);
    spin_us(2000);
}
static void act_arm(void) {
    for (uint32_t a = 0; a < 0x40; a += 4) ram_w32(a, 0xEAFFFFFEu); /* every ARM exception vector: b . */
    uint32_t before = ar(R_ARMRST);
    aw(R_ARMRST, before & ~1u); /* ARM7 out of reset */
    uint32_t running = ar(R_ARMRST);
    spin_us(5000);
    aw(R_ARMRST, running | 1); /* back into reset */
    spin_us(2000);
    LOG("arm: 2C00 before %08lx, released %08lx, after %08lx; vectors 0x00 %08lx 0x3C %08lx\n", (unsigned long)before,
        (unsigned long)running, (unsigned long)ar(R_ARMRST), (unsigned long)ram_r32(0), (unsigned long)ram_r32(0x3C));
}
static void act_dsp_nops(void) {
    prog_reset();
    PN = 128;   /* 128 NOP steps (every MPRO word written) */
    prog_load();
    spin_us(10000);
    prog_reset();
    prog_load();
    spin_us(2000);
}
static void act_regsweep(void) {
    for (int c = 0; c < 64; c++)
        for (int r = 0; r < 0x80; r += 4) aw(CH(c, r), 0xFFFF); /* +00 = FFFF is itself a KYONEX with KYONB */
    spin_us(5000);
    for (int c = 0; c < 64; c++) { aw(CH(c, 0x00), 0); aw(CH(c, 0x14), 0x1F); }
    aw(CH(0, 0x00), 0x8000); /* KYONEX: key everything off at RR 31 */
    spin_us(20000);
    for (int c = 0; c < 64; c++) ch_zero_regs(c);
    spin_us(2000);
}

int test_main(void) {
    out_open("eg_kprobe.txt");
    LOG("eg_kprobe: %d probes, %u us key-on each; 2C00 at start %08lx\n", NPROBE, ON_US, (unsigned long)ar(R_ARMRST));
    static const char *actions[NPROBE] = {
        "none (baseline)",
        "none (repeatability)",
        "RBP/RBL 0x2804 written 0x0000 (RBL 0 RBP 0) then back to the capture ring",
        "TIMA/TIMB/TIMC each written 0x0000, 0x0700, 0x00FF, 0x0000",
        "0x2800 written 0x000F then 0x0000 (MVOL only)",
        "ARM7 released from reset for 5 ms (vectors 0x00..0x3C = b .), then held again",
        "128-step NOP DSP program loaded, run 10 ms, unloaded",
        "every channel register of 64 slots written 0xFFFF (with KYONEX), then quiet sequence and zeroed",
    };
    for (int i = 0; i < NPROBE; i++) {
        LOG("p%d action: %s\n", i, actions[i]);
        switch (i) {
        case 2: act_ring(); break;
        case 3: act_timers(); break;
        case 4: act_mvol(); break;
        case 5: act_arm(); break;
        case 6: act_dsp_nops(); break;
        case 7: act_regsweep(); break;
        default: break;
        }
        probe(i, actions[i]);
    }
    aica_quiet();
    out_close();
    return 0;
}
