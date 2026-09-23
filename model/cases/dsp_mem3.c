/* dsp_mem3.c -- MRD and MWT in the same step (and in adjacent steps), clean setup per sub-test.
 * X source MEMS20 = 0x7000, Y = COEF 2048 -> SHIFTED 0x380000 on every step (raw word 0x3800, float 0x1F00?).
 * IWT of the read into MEMS21 two steps after the read step.  RAM word w holds w near the tested addresses.
 * Output: dsp_mem3.txt
 */
#include "aica_io.h"

#define RBP_BYTE 0x100000u
static uint32_t mems_rd(int i) { return ((ar(R_MEMS(i, 1)) & 0xFFFF) << 8) | (ar(R_MEMS(i, 0)) & 0xFF); }
static uint16_t wword(uint32_t w) { return ram_r16(RBP_BYTE + 2 * w); }

struct sub {
    const char *name;
    int mrd_step, mwt_step, madrs, nxadr, table, nofl_w;
};

int test_main(void) {
    out_open("dsp_mem3.txt");
    aica_quiet();
    dsp_ring(RBP_BYTE >> 11, 0);
    for (int k = 0; k < 64; k++) dsp_madrs(k, 0);
    for (int s = 0; s < 128; s++) dsp_coef(s, 2048);
    static const struct sub subs[] = {
        {"MRD@5 alone", 5, -1, 0x320, 0, 1, 1},
        {"MWT@5 alone", -1, 5, 0x320, 0, 1, 1},
        {"MRD+MWT@5", 5, 5, 0x320, 0, 1, 1},
        {"MRD+MWT@5 odd madrs", 5, 5, 0x325, 0, 1, 1},
        {"MRD+MWT@5 NXADR", 5, 5, 0x330, 1, 1, 1},
        {"MRD+MWT@5 float write", 5, 5, 0x340, 0, 1, 0},
        {"MRD+MWT@4 (even)", 4, 4, 0x350, 0, 1, 1},
        {"MWT@4 MRD@5", 5, 4, 0x360, 0, 1, 1},
        {"MRD@4 MWT@5", 4, 5, 0x370, 0, 1, 1},
        {"MRD@5 MWT@6", 5, 6, 0x380, 0, 1, 1},
        {"MRD@5 MWT@7", 5, 7, 0x390, 0, 1, 1},
        {"MWT@5 MRD@7", 7, 5, 0x3A0, 0, 1, 1},
    };
    for (unsigned i = 0; i < sizeof subs / sizeof subs[0]; i++) {
        const struct sub *t = &subs[i];
        prog_reset(); prog_load(); spin_us(200);                /* nothing runs */
        for (uint32_t w = (t->madrs - 4) & ~1u; w < (uint32_t)t->madrs + 6; w += 2)
            ram_w32(RBP_BYTE + 2 * w, w | ((w + 1) << 16));
        aw(R_MEMS(20, 1), 0x7000);
        aw(R_MEMS(21, 1), 0xBAD0);
        dsp_madrs(3, t->madrs);
        prog_reset();
        for (int s = 0; s < 12; s++) { P[s].XSEL = 1; P[s].IRA = 20; P[s].YSEL = 1; P[s].ZERO = 1; P[s].SHIFT = 3; P[s].NOFL = 1; }
        if (t->mwt_step >= 0) {
            dsp_inst_t *m = &P[t->mwt_step];
            m->MWT = 1; m->MASA = 3; m->NXADR = t->nxadr; m->TABLE = t->table; m->NOFL = t->nofl_w;
        }
        if (t->mrd_step >= 0) {
            dsp_inst_t *m = &P[t->mrd_step];
            m->MRD = 1; m->MASA = 3; m->NXADR = t->nxadr; m->TABLE = t->table;
            int iw = t->mrd_step + ((t->mrd_step & 1) ? 2 : 3);
            P[iw].IWT = 1; P[iw].IWA = 21;
            P[iw - 2].NOFL = 1; /* raw conversion */
        }
        PN = 12;
        prog_load();
        spin_us(3000);
        prog_reset(); prog_load();
        LOG("%-24s madrs %04x: read %06lx  words %04x..%04x:", t->name, t->madrs, (unsigned long)mems_rd(21),
            t->madrs - 2, t->madrs + 3);
        for (int w = t->madrs - 2; w <= t->madrs + 3; w++) LOG(" %04x", wword(w));
        LOG("\n");
    }
    aica_quiet();
    out_close();
    io_print("dsp_mem3 done\n");
    return 0;
}
