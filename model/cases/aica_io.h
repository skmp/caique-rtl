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
uint32_t io_ram_r32(uint32_t off);           /* wave RAM */
void io_ram_w32(uint32_t off, uint32_t v);
void io_wait_us(uint32_t us);                /* let the AICA run */
uint64_t io_now_us(void);
int io_write_file(const char *name, const void *data, uint32_t bytes); /* into tests/<case>/<platform>/ */
void io_print(const char *s);                /* console */
extern const char *io_platform;              /* "hw" or "model" */
int test_main(void);
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
static inline void dsp_clear_prog(void) {
    for (int s = 0; s < 128; s++)
        for (int k = 0; k < 4; k++) aw(R_MPRO(s, k), 0);
}
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
static inline void slot_write(int ch, const slot_cfg_t *c) {
    aw(CH(ch, 0x04), c->SA & 0xFFFF);
    aw(CH(ch, 0x08), c->LSA);
    aw(CH(ch, 0x0C), c->LEA);
    aw(CH(ch, 0x10), (c->D2R << 11) | (c->D1R << 6) | c->AR);
    aw(CH(ch, 0x14), (c->LPSLNK << 14) | (c->KRS << 10) | (c->DL << 5) | c->RR);
    aw(CH(ch, 0x18), (c->OCT << 11) | c->FNS);
    aw(CH(ch, 0x1C), (c->LFORE << 15) | (c->LFOF << 10) | (c->PLFOWS << 8) | (c->PLFOS << 5) | (c->ALFOWS << 3) | c->ALFOS);
    aw(CH(ch, 0x20), (c->IMXL << 4) | c->ISEL);
    aw(CH(ch, 0x24), (c->DISDL << 8) | c->DIPAN);
    aw(CH(ch, 0x28), (c->TL << 8) | (c->VOFF << 6) | (c->LPOFF << 5) | c->Q);
    for (int i = 0; i < 5; i++) aw(CH(ch, 0x2C + 4 * i), c->FLV[i]);
    aw(CH(ch, 0x40), (c->FAR << 8) | c->FD1R);
    aw(CH(ch, 0x44), (c->FD2R << 8) | c->FRR);
    aw(CH(ch, 0x00), (ar(CH(ch, 0x00)) & 0x4000) | (c->SSCTL << 10) | (c->LPCTL << 9) | (c->PCMS << 7) | ((c->SA >> 16) & 0x7F));
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
static char g_txtbuf[3 << 20];
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

/* Quiet, known state: every channel keyed off with zeroed registers, DSP program cleared, CDDA (EXTS) sends off. */
static inline void aica_quiet(void) {
    aw(R_ARMRST, ar(R_ARMRST) | 1); /* hold the ARM7 in reset: the tests own wave RAM */
    for (int c = 0; c < 64; c++) {
        aw(CH(c, 0x00), 0);
        aw(CH(c, 0x14), 0x1F); /* RR = 31 */
    }
    aw(CH(0, 0x00), 0x8000); /* KYONEX: key everything off */
    spin_us(20000);
    for (int c = 0; c < 64; c++) ch_zero_regs(c);
    dsp_clear_prog();
}

#endif
