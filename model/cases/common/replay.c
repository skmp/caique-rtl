/* replay.c -- the replay preamble (TODO 3.2), run by every platform's main before the case's test_main.
 *
 * Three quantities a console run cannot set are measured at the start of every run, so that the models can be put in
 * the same state (tools/replay_fit writes them to replay.txt; a model run with CAIQUE_REPLAY=<replay.txt> applies them
 * at the sync point below):
 *   MDEC_CT  the DSP's sample counter: the capture's counter words give it for every captured sample (RBL 3 = the full
 *            16 bits);
 *   K        the envelope counter constant of this boot (NOTES "Envelope clock"; eg_cnt = K - MDEC_CT/2): streams 0-2
 *            are cases/eg_kprobe.c's decay-1 probes at effective R 3 / 13 / 45 (R 3 = row 3 at eg_cnt bits 13:11, one
 *            skipped tick per 8, so the 1 s key-on pins K mod 16384; tools/kfit_core.h fits it);
 *   LFSR     the noise generator: stream 3 is a noise slot (SSCTL 1) with VOFF, LPOFF and IMXL 15, so its bus carries
 *            the slot's LFSR byte << 12 every sample.
 * Sync point: after the key-off the SH4 waits for the next counter word (the DSP's step 1, ph 68 of that sample) and
 * calls io_replay_point with that sample's MDEC_CT; the models apply the console's values at their next sample boundary.
 * Everything is then reset again (aica_quiet), so the case starts from the known state.
 * Output: replay_log.txt (the stream programs, the capture line, the sync line), replay.hdr / replay.bin (cap.h).
 * Console time: about 1.1 s.  replay_measure(prefix) is the measurement alone (cases/replay_check.c repeats it). */
#define AICA_TXTBUF_SIZE 4096   /* the case keeps its own 3 MB log buffer */
#include "../cap.h"


#define RP_NS 4
#define RP_ON_US 1000000u
#define RP_MAXV (RP_NS * 60000u)
#define RP_SA 0x10000u
static int32_t rp_buf[RP_MAXV];

static void rp_keys(int on) {
    for (int k = 0; k < RP_NS; k++) aw(CH(k, 0), (ar(CH(k, 0)) & 0x3FFF) | (on ? 0x4000 : 0));
    aw(CH(0, 0), (ar(CH(0, 0)) & 0x7FFF) | 0x8000);
}

/* the measurement: <prefix>_log.txt, <prefix>.hdr / .bin; returns the sync sample's MDEC_CT (0x10000: failed) */
uint32_t replay_measure(const char *prefix) {
    static const int mixs[RP_NS] = {0, 1, 2, 3};
    static const int d1r[3] = {1, 6, 22};   /* effective R 3, 13, 45 with KRS 0 OCT 0 FNS 0x200 */
    char name[64];
    snprintf(name, sizeof name, "%s_log.txt", prefix);
    out_open(name);
    aica_quiet();
    for (int i = 0; i < 24; i++) ram_w32(RP_SA + 4 * i, 0x7FFF7FFF);
    for (int k = 0; k < 3; k++) {
        slot_cfg_t c;
        slot_cfg_default(&c, RP_SA, 32);
        c.ISEL = k; c.AR = 31; c.D1R = d1r[k]; c.DL = 31; c.D2R = 0; c.RR = 31;
        c.KRS = 0; c.OCT = 0; c.FNS = 0x200;
        slot_write(k, &c);
        LOG("replay stream %d: slot %d AR %d D1R %d DL %d D2R %d RR %d KRS %d OCT %d FNS %03x\n", k, k, c.AR, c.D1R, c.DL,
            c.D2R, c.RR, c.KRS, c.OCT, c.FNS);
    }
    {
        slot_cfg_t c;
        slot_cfg_default(&c, RP_SA, 32);
        c.ISEL = 3; c.SSCTL = 1; c.VOFF = 1; c.LPOFF = 1; c.IMXL = 15;
        slot_write(3, &c);
        LOG("replay stream 3: slot 3 noise (SSCTL 1, VOFF 1, LPOFF 1, IMXL 15)\n");
    }
    if (cap_start(RP_NS, mixs, rp_buf, RP_MAXV)) { OUT("replay: cap_start failed\n"); out_close(); aica_quiet(); return 0x10000; }
    const uint32_t c0 = CAP.c0, n_head = CAP.n_head;
    const uint64_t t_head = CAP.t_head;
    cap_wait_us(5000);
    cap_mark(1); rp_keys(1); cap_mark(2);
    cap_wait_us(RP_ON_US);
    cap_mark(3); rp_keys(0); cap_mark(4);
    cap_wait_us(20000);
    /* sync: the first sample whose counter word appears after this point */
    uint32_t n = cap_head_now();
    while (!cap_cnt_ok(n + 1)) { }
    const uint64_t t_sync = now_us();
    const uint32_t sync_mdec = (c0 - (n + 1)) & 0xFFFF;
    io_replay_point(sync_mdec);   /* no-op outside the preamble (the models apply CAIQUE_REPLAY once) */
    const uint32_t ns = cap_stop();
    cap_save(prefix, ns);
    /* times as seconds and 6 digits (KOS's printf has no 64-bit conversions) */
    LOG("replay capture: c0 %04lx head n %lu t_head_us %lu%06lu samples %lu first %lu errors %lu marks %lu\n",
        (unsigned long)c0, (unsigned long)n_head, (unsigned long)(t_head / 1000000u), (unsigned long)(t_head % 1000000u),
        (unsigned long)ns, (unsigned long)CAP.n_first, (unsigned long)CAP.errors, (unsigned long)CAP.nev);
    LOG("replay sync: n %lu mdec %04lx t_sync_us %lu%06lu\n", (unsigned long)(n + 1), (unsigned long)sync_mdec,
        (unsigned long)(t_sync / 1000000u), (unsigned long)(t_sync % 1000000u));
    out_close();
    aica_quiet();
    return sync_mdec;
}

void replay_preamble(void) { (void)replay_measure("replay"); }
