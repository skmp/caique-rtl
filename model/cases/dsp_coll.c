/* dsp_coll.c -- a DSP memory access on a playing channel's wave RAM step: what the DSP gets, sample by sample.
 *
 * wren7-rtl tests/hw/collide2 (ARM in reset, read 5 ms later) showed that a DSP MWT at channel K's step 2K - 14 is
 * dropped and that a DSP MRD there returns "the channel's word".  This case pins WHICH word and WHEN: the DSP logs,
 * every sample, into a 32K-word ring (RBP 0x100000, RBL 2; region r at MADRS[5 + r] = r * 6144):
 *   region 0  counter -3 (n + 1)   steps 2/3 (TEMP-persistent, as cap.h): finds sample 0 of the log
 *   region 1  L1 = -(MEMS0 >> 8)   steps s+6/s+7: the read under test (normally an MRD at s, NOFL, IWT at s+3)
 *   region 2  L2 = -(MIXS0 >> 4)   steps 60/61: the channel's output of the previous sample (IMXL 15, TL 0)
 *   region 3  L3 = MWT at s        written (0xC3C4 = -MEMS16) or dropped (0 stays)
 *   region 4  L4 = MWT at s + 2    control (0xA5A6 = -MEMS17)
 * The MRD's own word is TABLE word 0xC000 = 0x1234 (outside the ring).  One channel K plays at a time, SA 0x180000:
 *   PCM16 word i = 0x4000 + i (loop 4096); PCM8 byte j = 0x10 + (j & 63) (loop 8192); ADPCM word j = 0x2000 + j.
 * Runs: see RUNS[].  Output: dsp_coll.txt (per run: the key sequence of 48 samples from a canonical start, and the
 * distinct keys) and dsp_coll_<run>.bin (N x 5 words: counter, L1, L2, L3, L4 of each logged sample), which
 * tools/coll_check reads. */
#include "aica_io.h"

#define RBP_BYTE 0x100000u
#define RING_WORDS 32768u
#define REGION 6144u
#define OWN_WORD 0x1234u
#define OWN_ADDR 0xC000u
#define SA 0x180000u
#define LOGN 1600u

enum { F_RD = 1, F_WR = 2, F_ODD = 4, F_EVEN = 8, F_KON = 16 };
typedef struct { const char *name; int K, pcms, oct, fns, flags; } run_t;
static const run_t RUNS[] = {
    {"p16_k20", 20, 0, 0, 0, F_RD},        /* PCM16, pitch 1: which of the channel's words */
    {"p16_k3", 3, 0, 0, 0, F_RD},          /* a channel whose step (120) is in the DSP sample's last eighth */
    {"p16_k20_q", 20, 0, 14, 0, F_RD},     /* PCM16, pitch 1/4 (OCT -2): fetches without advancing */
    {"p16_k20_f", 20, 0, 0, 0x155, F_RD},  /* PCM16, pitch 1.33 (a step of 2 on some samples) */
    {"p8_k20", 20, 1, 0, 0, F_RD},         /* PCM8, pitch 1 */
    {"adp_om2", 20, 2, 14, 0, F_RD},       /* ADPCM OCT -2 .. +2 */
    {"adp_om1", 20, 2, 15, 0, F_RD},
    {"adp_o0", 20, 2, 0, 0, F_RD},
    {"adp_o1", 20, 2, 1, 0, F_RD},
    {"adp_o2", 20, 2, 2, 0, F_RD},
    {"p16_wr", 20, 0, 0, 0, F_WR},         /* MWT at the channel's step: dropped?  and 2 steps later */
    {"adp_om1_wr", 20, 2, 15, 0, F_WR},    /* ... with ADPCM: only in the samples it fetches? */
    {"latch_odd", 20, 0, 0, 0, F_ODD},     /* MRD at s-1 (odd, IWT s+1): does the channel's fetch at s replace it? */
    {"latch_even", 20, 0, 0, 0, F_EVEN},   /* MRD at s-2 (even: its odd slot s-1, IWT s+1) */
    {"p16_kon", 20, 0, 0, 0, F_RD | F_KON},/* key-on while logging: the first samples */
    {"p16_kon_k3", 3, 0, 0, 0, F_RD | F_KON},
    {"p8_kon", 20, 1, 0, 0, F_RD | F_KON},
    {"adp_kon", 20, 2, 0, 0, F_RD | F_KON},
};
#define NRUNS (int)(sizeof RUNS / sizeof RUNS[0])

static uint16_t ring[RING_WORDS];
static uint16_t logw[LOGN][5];

static void mems_set(int i, uint32_t v24) { aw(R_MEMS(i, 1), (v24 >> 8) & 0xFFFF); }
static void acc_neg_mems(int st, int i) {   /* ACC = -MEMS[i] (X = MEMS, Y = COEF -4096, B = 0) */
    dsp_coef(st, -4096); P[st].XSEL = 1; P[st].IRA = i; P[st].YSEL = 1; P[st].ZERO = 1;
}
static void mwt_ring(int st, int region) { P[st].MWT = 1; P[st].NOFL = 1; P[st].MASA = 5 + region; P[st].SHIFT = 0; }

static void build(const run_t *r) {
    const int s = (2 * r->K - 14) & 127;
    prog_reset();
    if (r->flags & (F_RD | F_ODD | F_EVEN)) {
        int m = (r->flags & F_ODD) ? s - 1 : (r->flags & F_EVEN) ? s - 2 : s;
        int iwt = (m & 1) ? m + 2 : m + 3;
        P[m].MRD = 1; P[m].TABLE = 1; P[m].MASA = 0;
        for (int k = m; k <= iwt; k++) P[k & 127].NOFL = 1;
        P[iwt & 127].IWT = 1; P[iwt & 127].IWA = 0;
        acc_neg_mems((s + 6) & 127, 0); mwt_ring((s + 7) & 127, 1);
    }
    if (r->flags & F_WR) {
        acc_neg_mems(s - 1, 16); mwt_ring(s, 3);
        acc_neg_mems(s + 1, 17); mwt_ring(s + 2, 4);
    }
    /* region 2: MIXS0 */
    dsp_coef(60, -4096); P[60].XSEL = 1; P[60].IRA = IRA_MIXS(0); P[60].YSEL = 1; P[60].ZERO = 1;
    mwt_ring(61, 2);
    PN = 128;
}
/* region 0, the counter (step 2: ACC = TEMP[1] - 0x300; step 3: TEMP[0] = SHIFTED, MWT), loaded after the rest: step 2
 * first, then step 3 with its TWT word last, so that no sample writes TEMP without logging it */
static void load_counter(void) {
    dsp_inst_t c2, c3; uint16_t w[4];
    memset(&c2, 0, sizeof c2); memset(&c3, 0, sizeof c3);
    dsp_coef(2, -4096); c2.XSEL = 1; c2.IRA = 30; c2.YSEL = 1; c2.TRA = 1;
    dsp_put(2, &c2);
    c3.TWT = 1; c3.TWA = 0; c3.MWT = 1; c3.NOFL = 1; c3.MASA = 5;
    dsp_encode(&c3, w);
    for (int k = 3; k >= 0; k--) aw(R_MPRO(3, k), w[k]);
}

static void chan(const run_t *r) {
    slot_cfg_t c;
    slot_cfg_default(&c, SA, r->pcms == 0 ? 4096 : r->pcms == 1 ? 8192 : 0xFFFF);
    c.PCMS = r->pcms; c.OCT = r->oct; c.FNS = r->fns; c.VOFF = 1;   /* no level stage: MIXS0 = sample x 16 */
    slot_write(r->K, &c);
}

static int run_one(const run_t *r, int idx) {
    const int K = r->K;
    /* quiet: channel off, program cleared, ring and TEMP cleared, constants */
    ch_keyoff(K);
    prog_reset(); PN = 0; prog_load();
    spin_us(2000);
    ch_zero_regs(K);
    ram_fill(RBP_BYTE, 0, RING_WORDS * 2);
    for (int i = 0; i < 128; i++) { aw(R_TEMP(i, 0), 0); aw(R_TEMP(i, 1), 0); }
    mems_set(0, 0); mems_set(16, 0x3C3C00); mems_set(17, 0x5A5A00); mems_set(30, 0x000300);
    ram_w16(RBP_BYTE + 2 * OWN_ADDR, OWN_WORD);
    /* channel data */
    if (r->pcms == 0) for (uint32_t i = 0; i < 4096; i += 2) ram_w32(SA + 2 * i, (0x4000 + i) | ((0x4001u + i) << 16));
    else if (r->pcms == 1) for (uint32_t j = 0; j < 8192; j += 4)
        ram_w32(SA + j, (0x10 + (j & 63)) | ((0x10 + ((j + 1) & 63)) << 8) | ((0x10 + ((j + 2) & 63)) << 16) | ((uint32_t)(0x10 + ((j + 3) & 63)) << 24));
    else for (uint32_t j = 0; j < 16384; j += 2) ram_w32(SA + 2 * j, (0x2000 + j) | ((0x2001u + j) << 16));
    chan(r);
    if (!(r->flags & F_KON)) { ch_keyon(K); spin_us(5000); }
    build(r);
    prog_load();
    load_counter();
    if (r->flags & F_KON) { spin_us(10000); ch_keyon(K); spin_us(30000); }
    else spin_us(40000);
    prog_reset(); PN = 0; prog_load();
    ch_keyoff(K);
    /* read the ring, find sample 0 (counter 0xFFFD) */
    for (uint32_t a = 0; a < RING_WORDS; a += 2) { uint32_t v = ram_r32(RBP_BYTE + 2 * a); ring[a] = (uint16_t)v; ring[a + 1] = (uint16_t)(v >> 16); }
    int a0 = -1;   /* sample 0: counter -3 followed (one address lower) by -6 */
    for (uint32_t a = 0; a < RING_WORDS; a++)
        if (ring[a] == 0xFFFD && ring[(a - 1) & (RING_WORDS - 1)] == 0xFFFA) { a0 = (int)a; break; }
    if (a0 < 0) { LOG("%-12s no counter found\n", r->name); return 1; }
    uint32_t n = 0;
    for (; n < LOGN; n++) {
        uint32_t a = (uint32_t)(a0 - (int)n) & (RING_WORDS - 1);
        if (ring[a] != (uint16_t)(-3 * (int32_t)(n + 1))) break;
        for (int k = 0; k < 5; k++) logw[n][k] = ring[(a + k * REGION) & (RING_WORDS - 1)];
    }
    char bn[64]; snprintf(bn, sizeof bn, "dsp_coll_%s.bin", r->name);
    out_bin(bn, logw, n * 10);
    /* summary: the read's word as "o" (own) or the channel word's index; relative to the MIXS0 word for PCM16 */
    LOG("%-12s K %d s %d: %u samples logged\n", r->name, K, (2 * K - 14) & 127, (unsigned)n);
    const uint32_t skip = 8;
    if (n < skip * 2 + 64) return 1;
    if (r->flags & F_WR) {
        uint32_t w3 = 0, w4 = 0;
        for (uint32_t i = skip; i < n - skip; i++) { w3 += logw[i][3] != 0; w4 += logw[i][4] != 0; }
        LOG("  MWT at s written in %u of %u samples, at s+2 in %u\n", (unsigned)w3, (unsigned)(n - 2 * skip), (unsigned)w4);
        uint32_t st = skip; while (st < n - skip - 48 && !(logw[st][3] == 0 && logw[st + 1][3] != 0)) st++;
        LOG("  pattern from a dropped->written edge:");
        for (uint32_t i = st; i < st + 48; i++) LOG("%c", logw[i][3] ? 'W' : '.');
        LOG("\n");
        return 0;
    }
    /* per sample: key of L1 (the read), and for PCM16 the MIXS0 word */
    uint32_t st = skip;
    if (r->flags & F_KON) {   /* around the first sample whose read is not the own word */
        while (st < n - 16 && (uint16_t)(-logw[st][1]) == OWN_WORD) st++;
        st = st >= skip + 4 ? st - 4 : st;
    } else {                  /* from the first sample whose MIXS0 word differs from the previous sample's */
        while (st < n - skip - 48 && logw[st][2] == logw[st - 1][2]) st++;
    }
    LOG("  L1 (read) / L2 (MIXS0 word) from sample %u:\n ", (unsigned)(st - skip));
    for (uint32_t i = st; i < st + 48 && i < n; i++) {
        uint16_t w1 = (uint16_t)(-logw[i][1]), w2 = (uint16_t)(-logw[i][2]);
        if (w1 == OWN_WORD) LOG(" o/%04x", w2);
        else LOG(" %04x/%04x", w1, w2);
        if ((i - st) % 12 == 11) LOG("\n ");
    }
    LOG("\n");
    (void)idx;
    return 0;
}

int test_main(void) {
    out_open("dsp_coll.txt");
    aica_reset(RBP_BYTE, 2);
    dsp_madrs(0, OWN_ADDR);
    for (int r = 0; r < 5; r++) dsp_madrs(5 + r, (uint16_t)(r * REGION));
    int bad = 0;
    for (int i = 0; i < NRUNS; i++) bad += run_one(&RUNS[i], i);
    OUT("dsp_coll: %d runs, %d without a usable log\n", NRUNS, bad);
    aica_quiet();
    out_close();
    return 0;
}
