/* aica_io.h -- portable AICA access for the caique test cases.
 *
 * Every test case (cases/<name>.c, entry point test_main) is built twice: against the console (hw/io_kos.c: G2 bus,
 * run through shrike4 hwrun.sh) and against the model (host/io_model.cpp: caique::AicaModel).  Both write the same
 * files, to tests/<name>/hw/ and tests/<name>/model/, so the two can be diffed.
 *
 * Registers: offsets from 0x00700000 (16-bit registers in 32-bit slots).  Wave RAM: offsets from 0x00800000.
 */
#ifndef CAIQUE_AICA_IO_H
#define CAIQUE_AICA_IO_H
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "../src/dsp_asm.h"
#include "../src/dsp_float.h"

#ifdef __cplusplus
extern "C" {
#endif
uint32_t io_r(uint32_t off);                 /* AICA register read (32-bit slot) */
void io_w(uint32_t off, uint32_t v);         /* AICA register write */
void io_wn(int n, const uint32_t *off, const uint32_t *v); /* n register writes back to back (console: one G2 FIFO
                                              * wait, then all n queued; they reach the AICA a few steps apart) */
uint32_t io_ram_r32(uint32_t off);           /* wave RAM */
void io_ram_w32(uint32_t off, uint32_t v);
void io_wait_us(uint32_t us);                /* let the AICA run */
uint64_t io_now_us(void);
int io_write_file(const char *name, const void *data, uint32_t bytes); /* into tests/<case>/<platform>/ */
void io_print(const char *s);                /* console */
uint32_t io_sh4_irq(void);                   /* the AICA's SH4 interrupt line (console: SB_ISTEXT bit 1) */
extern const char *io_platform;              /* "hw" or "model" */
int test_main(void);
void replay_preamble(void);                  /* cases/common/replay.c: every platform's main runs it before test_main */
uint32_t replay_measure(const char *prefix); /* its measurement alone (cases/replay_check.c) */
void io_replay_point(uint32_t sync_mdec);    /* the preamble's sync point (models: apply CAIQUE_REPLAY there) */
#ifdef __cplusplus
}
#endif
#define IS_HW (io_platform[0] == 'h')

/* common registers */
#define R_MVOL    0x2800
#define R_RBPL    0x2804
#define R_MSLC    0x280C
#define R_EGMON   0x2810
#define R_CAMON   0x2814
#define R_TIMA    0x2890
#define R_TIMB    0x2894
#define R_TIMC    0x2898
#define R_MCIPD   0x28B8
#define R_MCIRE   0x28BC
#define R_ARMRST  0x2C00
/* DSP */
#define R_COEF(i)   (0x3000 + 4 * (i))
#define R_MADRS(i)  (0x3200 + 4 * (i))
#define R_MPRO(s,k) (0x3400 + 16 * (s) + 4 * (k))
#define R_TEMP(i,k) (0x4000 + 8 * (i) + 4 * (k)) /* k=0: bits 7:0, k=1: bits 23:8 */
#define R_MEMS(i,k) (0x4400 + 8 * (i) + 4 * (k))
#define R_MIXS(i,k) (0x4500 + 8 * (i) + 4 * (k)) /* k=0: bits 3:0, k=1: bits 19:4 */
#define R_EFREG(i)  (0x4580 + 4 * (i))
#define R_EXTS(i)   (0x45C0 + 4 * (i))
/* channel registers */
#define CH(c, r)    (0x80 * (c) + (r))

static inline uint32_t ar(uint32_t off) { return io_r(off); }
static inline void aw(uint32_t off, uint32_t v) { io_w(off, v); }
static inline uint32_t ram_r32(uint32_t off) { return io_ram_r32(off); }
static inline void ram_w32(uint32_t off, uint32_t v) { io_ram_w32(off, v); }
/* 16-bit wave RAM words through 32-bit accesses (little endian: word at even address is the low half) */
static inline uint16_t ram_r16(uint32_t off) {
    uint32_t w = ram_r32(off & ~3u);
    return (off & 2) ? (uint16_t)(w >> 16) : (uint16_t)w;
}
static inline void ram_w16(uint32_t off, uint16_t v) {
    uint32_t w = ram_r32(off & ~3u);
    w = (off & 2) ? ((w & 0xFFFFu) | ((uint32_t)v << 16)) : ((w & 0xFFFF0000u) | v);
    ram_w32(off & ~3u, w);
}
static inline void ram_write(uint32_t off, const void *src, uint32_t bytes) {
    const uint32_t *s = (const uint32_t *)src;
    for (uint32_t i = 0; i < (bytes + 3) / 4; i++) ram_w32(off + 4 * i, s[i]);
}
static inline void ram_read(uint32_t off, void *dst, uint32_t bytes) {
    uint32_t *d = (uint32_t *)dst;
    for (uint32_t i = 0; i < (bytes + 3) / 4; i++) d[i] = ram_r32(off + 4 * i);
}
static inline void ram_fill(uint32_t off, uint32_t v, uint32_t bytes) {
    for (uint32_t i = 0; i < bytes / 4; i++) ram_w32(off + 4 * i, v);
}

/* ---- DSP ---- */
static inline void dsp_nop_all(void);
static inline void dsp_clear_prog(void) { dsp_nop_all(); }
static inline void dsp_put(int step, const dsp_inst_t *in) {
    uint16_t w[4];
    dsp_encode(in, w);
    for (int k = 0; k < 4; k++) aw(R_MPRO(step, k), w[k]);
}
#define DSP(step, ...) do { dsp_inst_t _i = { __VA_ARGS__ }; dsp_put((step), &_i); } while (0)
/* program buffer: build P[0..PN-1], then prog_load() writes it (and zeroes steps a previous load used).  Steps are
 * written from high to low so that, while the DSP keeps running, a later step (an IWT, say) is never left behind
 * without the earlier step it depends on (an MRD and its NOFL). */
static dsp_inst_t P[128];
static int PN, prevN;
static inline void prog_reset(void) { memset(P, 0, sizeof P); PN = 0; }
static inline void prog_load(void) {
    static const dsp_inst_t z = {0};
    for (int s = (PN > prevN ? PN : prevN) - 1; s >= 0; s--) dsp_put(s, s < PN ? &P[s] : &z);
    prevN = PN;
}
static inline void prog_run(uint32_t wait_us) { prog_load(); io_wait_us(wait_us); }
static inline void dsp_coef(int i, int c13) { aw(R_COEF(i), COEF_REG(c13)); }
static inline void dsp_madrs(int i, uint16_t v) { aw(R_MADRS(i), v); }
/* ring buffer: RBP = byte address >> 11, RBL size code (0..3 = 8K/16K/32K/64K words per minicast) */
static inline void dsp_ring(uint32_t rbp_reg, uint32_t rbl) { aw(R_RBPL, (rbl << 13) | (rbp_reg & 0xFFF)); }

/* ---- channels ---- */
static inline void ch_keyon(int c) { /* KYONB on c, then KYONEX (applies KYONB of every channel) */
    aw(CH(c, 0x00), (ar(CH(c, 0x00)) & 0x3FFF) | 0x4000);
    aw(CH(c, 0x00), (ar(CH(c, 0x00)) & 0x7FFF) | 0x8000);
}
static inline void ch_keyoff(int c) {
    aw(CH(c, 0x00), ar(CH(c, 0x00)) & 0x3FFF);
    aw(CH(c, 0x00), (ar(CH(c, 0x00)) & 0x3FFF) | 0x8000);
}
/* one slot's full register image (field names as in the AICA register map) */
typedef struct {
    uint32_t SA; int PCMS, LPCTL, SSCTL;
    uint16_t LSA, LEA;
    int AR, D1R, D2R, RR, DL, KRS, LPSLNK;
    int OCT, FNS;
    int LFORE, LFOF, PLFOWS, PLFOS, ALFOWS, ALFOS;
    int ISEL, IMXL, DISDL, DIPAN;
    int TL, VOFF, LPOFF, Q;
    uint16_t FLV[5];
    int FAR, FD1R, FD2R, FRR;
} slot_cfg_t;
/* defaults: PCM16 at SA, looped over [LSA,LEA), instant attack, no decay, fastest release, pitch 1.0, no LFO,
 * send to MIXS[0] at IMXL 15, no direct send, TL 0, filter off, FEG levels at max */
static inline void slot_cfg_default(slot_cfg_t *c, uint32_t sa, uint16_t lea) {
    memset(c, 0, sizeof *c);
    c->SA = sa; c->LPCTL = 1; c->LEA = lea;
    c->AR = 31; c->RR = 31; c->KRS = 15;
    c->IMXL = 15; c->LPOFF = 1;
    for (int i = 0; i < 5; i++) c->FLV[i] = 0x1FF8;
    c->FAR = c->FD1R = c->FD2R = c->FRR = 31;
}
/* the 18 register words (0x00 .. 0x44) slot_write writes; 0x00 without KYONB (slot_write keeps the slot's) */
static inline void slot_regs(const slot_cfg_t *c, uint16_t r[18]) {
    r[0] = (uint16_t)((c->SSCTL << 10) | (c->LPCTL << 9) | (c->PCMS << 7) | ((c->SA >> 16) & 0x7F));
    r[1] = (uint16_t)(c->SA & 0xFFFF);
    r[2] = c->LSA;
    r[3] = c->LEA;
    r[4] = (uint16_t)((c->D2R << 11) | (c->D1R << 6) | c->AR);
    r[5] = (uint16_t)((c->LPSLNK << 14) | (c->KRS << 10) | (c->DL << 5) | c->RR);
    r[6] = (uint16_t)((c->OCT << 11) | c->FNS);
    r[7] = (uint16_t)((c->LFORE << 15) | (c->LFOF << 10) | (c->PLFOWS << 8) | (c->PLFOS << 5) | (c->ALFOWS << 3) | c->ALFOS);
    r[8] = (uint16_t)((c->IMXL << 4) | c->ISEL);
    r[9] = (uint16_t)((c->DISDL << 8) | c->DIPAN);
    r[10] = (uint16_t)((c->TL << 8) | (c->VOFF << 6) | (c->LPOFF << 5) | c->Q);
    for (int i = 0; i < 5; i++) r[11 + i] = c->FLV[i];
    r[16] = (uint16_t)((c->FAR << 8) | c->FD1R);
    r[17] = (uint16_t)((c->FD2R << 8) | c->FRR);
}
static inline void slot_write(int ch, const slot_cfg_t *c) {
    uint16_t r[18];
    slot_regs(c, r);
    for (int i = 1; i < 18; i++) aw(CH(ch, 4 * i), r[i]);
    aw(CH(ch, 0x00), (ar(CH(ch, 0x00)) & 0x4000) | r[0]);
}
/* MIXS[i] as a signed 20-bit value */
static inline int32_t mixs_rd(int i) {
    uint32_t v = ((ar(R_MIXS(i, 1)) & 0xFFFF) << 4) | (ar(R_MIXS(i, 0)) & 0xF);
    return (int32_t)(v << 12) >> 12;
}
static inline uint32_t egmon(int ch, int afsel) { aw(R_MSLC, (afsel << 14) | (ch << 8)); return ar(R_EGMON); }

static inline void ch_zero_regs(int c) {
    for (int r = 0; r < 0x80; r += 4) aw(CH(c, r), 0);
}

/* ---- time ---- */
static inline uint64_t now_us(void) { return io_now_us(); }
static inline void spin_us(uint32_t us) { io_wait_us(us); }

/* ---- output ----  text is collected in RAM and written to the host once (every /pc/ call is a network round trip) */
#ifndef AICA_TXTBUF_SIZE
#define AICA_TXTBUF_SIZE (3 << 20)
#endif
static char g_txtbuf[AICA_TXTBUF_SIZE];
static uint32_t g_txtlen;
static char g_txtname[128];
static inline int out_open(const char *name) {
    snprintf(g_txtname, sizeof g_txtname, "%s", name);
    g_txtlen = 0;
    return 0;
}
#define LOG(...) do { if (g_txtlen < sizeof g_txtbuf - 512) \
    g_txtlen += snprintf(g_txtbuf + g_txtlen, sizeof g_txtbuf - g_txtlen, __VA_ARGS__); } while (0)
#define OUT(...) do { char _b[512]; snprintf(_b, sizeof _b, __VA_ARGS__); io_print(_b); LOG("%s", _b); } while (0)
static inline int out_bin(const char *name, const void *data, uint32_t bytes) { return io_write_file(name, data, bytes); }
static inline void out_close(void) {
    if (g_txtname[0]) out_bin(g_txtname, g_txtbuf, g_txtlen);
    g_txtname[0] = 0;
}

/* "slotregs <capture> <stream> <slot> r00 .. r44" (hex): a slot's register image for the replay tools (tools/
 * stream_replay) */
static inline void slot_log(const char *cap, int stream, int ch, const slot_cfg_t *c) {
    uint16_t r[18];
    slot_regs(c, r);
    LOG("slotregs %s %d %d", cap, stream, ch);
    for (int i = 0; i < 18; i++) LOG(" %04x", r[i]);
    LOG("\n");
}

/* ---- known DSP state (every case and every sub-test starts from it) ----
 * dsp_nop_all: every MPRO step a NOP, from step 127 down (a running program never keeps a later step without the earlier
 * one it depends on), each step's w2 first (MRD / MWT / EWT / FRCL / ADRL go before their operands: an MWT left with its
 * MASA cleared would write into another region).
 * dsp_reset(rbp_byte, rbl): NOPs, one sample for in-flight reads to land, then COEF / MADRS / EFREG / TEMP 0, the ring
 * (RBP rbp_byte, RBL rbl: 8K << rbl words) cleared and selected, MEMS 0 in all 24 bits (the CPU writes bits 23:8 only:
 * a CPU read of a zero ring word leaves 0 in the memory read latch, and a one-sample program of IWT with NOFL stores it
 * into every MEMS), and every MIXS bus 0 in both banks (slots 0..15 pointed at buses 0..15 for three samples: a silent
 * slot writes 0 every sample; their 0x20 is restored after).  Other channel registers are left alone, so it can run after
 * a case has configured its slots (cap_start, flog_start). */
#define DSP_RING_RBP 0x1E0000u  /* the default ring: 64K words at 1.875 MB (cases/cap.h uses the same) */
static inline void dsp_nop_all(void) {
    static const int ko[4] = {2, 0, 1, 3};
    for (int s = 127; s >= 0; s--)
        for (int k = 0; k < 4; k++) aw(R_MPRO(s, ko[k]), 0);
    prevN = 0;
}
static inline void dsp_reset(uint32_t rbp_byte, uint32_t rbl) {
    dsp_nop_all();
    io_wait_us(50);
    for (int i = 0; i < 128; i++) aw(R_COEF(i), 0);
    for (int i = 0; i < 64; i++) aw(R_MADRS(i), 0);
    for (int i = 0; i < 16; i++) aw(R_EFREG(i), 0);
    for (int i = 0; i < 128; i++) { aw(R_TEMP(i, 0), 0); aw(R_TEMP(i, 1), 0); }
    for (int i = 0; i < 32; i++) aw(R_MEMS(i, 1), 0);
    dsp_ring(rbp_byte >> 11, rbl);
    ram_fill(rbp_byte & ~2047u, 0, (8192u << rbl) * 2);
    (void)ram_r32(rbp_byte & ~2047u);               /* the memory read latch = 0 */
    for (int s = 33; s >= 0; s--) {                  /* MEMS[i] at step i + 2: NOFL on the step two before */
        uint16_t w[4] = {0, 0, 0, 0x8000};
        if (s >= 2) w[1] = (uint16_t)(0x40 | ((s - 2) << 1));
        for (int k = 3; k >= 0; k--) aw(R_MPRO(s, k), w[k]);
    }
    io_wait_us(100);
    dsp_nop_all();
    uint32_t r20[16];
    for (int c = 0; c < 16; c++) r20[c] = ar(CH(c, 0x20)) & 0xFFFF;
    for (int c = 0; c < 16; c++) aw(CH(c, 0x20), (uint32_t)c);   /* ISEL c, IMXL 0 */
    io_wait_us(100);
    for (int c = 0; c < 16; c++) aw(CH(c, 0x20), r20[c]);
}

/* Known state (TODO 2.1), at the start of every case and sub-test: the ARM held in reset, every channel keyed off with
 * zeroed registers, then dsp_reset (every MPRO step a NOP, COEF / MADRS / EFREG / TEMP / MEMS 0, the ring at rbp_byte
 * with size code rbl cleared and selected, the MIXS buses 0), and every slot's LP flag cleared (an EG monitor read per
 * slot; tests/oneshot's first read saw LP 1 left by an earlier program), and the interrupt / timer registers.
 * aica_quiet uses the default ring. */
static inline void aica_reset(uint32_t rbp_byte, uint32_t rbl) {
    aw(R_ARMRST, ar(R_ARMRST) | 1); /* hold the ARM7 in reset: the tests own wave RAM */
    for (int c = 0; c < 64; c++) {
        aw(CH(c, 0x00), 0);
        aw(CH(c, 0x14), 0x1F); /* RR = 31 */
    }
    aw(CH(0, 0x00), 0x8000); /* KYONEX: key everything off */
    spin_us(20000);
    for (int c = 0; c < 64; c++) ch_zero_regs(c);
    dsp_reset(rbp_byte, rbl);
    for (int c = 0; c < 64; c++) (void)egmon(c, 0);
    aw(R_MSLC, 0);
    /* interrupts and timers (tests/timer_irq: SCIEB was 0x0400 from an earlier program): enables and levels 0, the
     * timers at prescale 0 count 0 (their prescaler cannot be reset), then both pending registers cleared */
    aw(0x289C, 0); aw(0x28B4, 0);
    for (int i = 0; i < 3; i++) aw(0x28A8 + 4 * i, 0);
    aw(R_TIMA, 0); aw(R_TIMB, 0); aw(R_TIMC, 0);
    aw(0x28A4, 0x7FF); aw(R_MCIRE, 0x7FF);
}
static inline void aica_quiet(void) { aica_reset(DSP_RING_RBP, 3); }

#endif
