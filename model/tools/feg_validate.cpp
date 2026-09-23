// feg_validate.cpp -- the production AicaModel FEG against the per-sample values recovered from the console by
// feg_track (work/feg/ft_<b>_<k>.u, u = v >> 1).  Each stream runs in its own model instance, configured like
// cases/feg_track.c, with the envelope-clock parameters fitted by feg_fit (EGOFF -1): clock counter at the key-on
// clock, key-on at the onset (batches 0/1) or one sample before it (batch 2: the 1/8-pitch onset shows a sample
// late), key-off clock.  Batch 1 slot 2 takes the key-off one clock earlier and needs its own counter phase (open:
// per-slot timing inside the envelope period, NOTES.md).
// Build: make -C tools feg_validate (-> build/tools/feg_validate; links src/aica_model.cpp)
#include <cstdio>
#include <string>
#include <vector>
#include "../src/aica_model.h"

struct Cfg { int flv[5], rate[4], krs, oct, fns; };
static const Cfg cfg[3][3] = {
    {{{0x1800, 0x1C05, 0x1A03, 0x1B00, 0x1900}, {28, 26, 30, 27}, 15, 0, 0},
     {{0x1FF0, 0x1802, 0x1F01, 0x1E00, 0x1FFD}, {30, 29, 24, 30}, 15, 0, 0},
     {{0x1C00, 0x1C80, 0x1C00, 0x1C40, 0x1BF0}, {22, 23, 21, 25}, 15, 0, 0}},
    {{{0x1800, 0x1C00, 0x1800, 0x1A00, 0x1C00}, {16, 20, 24, 22}, 15, 3, 0x200},
     {{0x1800, 0x1C00, 0x1800, 0x1A00, 0x1C00}, {16, 20, 24, 22}, 0, 3, 0x200},
     {{0x1800, 0x1C00, 0x1800, 0x1A00, 0x1C00}, {16, 20, 24, 22}, 5, 3, 0x200}},
    {{{0x1800, 0x1FF0, 0x1800, 0x1800, 0x1A00}, {24, 26, 0, 28}, 15, 13, 0x155},
     {{0x1800, 0x1C00, 0x1900, 0x1A00, 0x1C00}, {18, 20, 22, 24}, 2, 13, 0x155},
     {{0x1C00, 0x1D00, 0x1D00, 0x1E00, 0x1B00}, {26, 26, 0, 26}, 15, 13, 0x155}}};
// fitted (feg_fit with EGOFF = -1): counter at the key-on clock, key-off clock, key-on one sample before the onset
static const struct { uint32_t c0; int koff; int kon; } fit[3][3] = {
    {{4, 2666, 0}, {4, 2666, 0}, {4, 2666, 0}},
    {{21, 3262, 0}, {21, 3262, 0}, {3, 3261, 0}},
    {{0, 334, 1}, {0, 334, 1}, {0, 334, 1}}};

int main() {
    int full = 0, total = 0;
    long matched = 0, samples = 0;
    for (int b = 0; b < 3; b++)
        for (int k = 0; k < 3; k++) {
            std::vector<int> u;
            FILE *f = fopen(("work/feg/ft_" + std::to_string(b) + "_" + std::to_string(k) + ".u").c_str(), "rb");
            if (!f) { printf("run build/tools/feg_track first\n"); return 1; }
            int32_t x;
            while (fread(&x, 4, 1, f) == 1) u.push_back(x);
            fclose(f);
            const Cfg &c = cfg[b][k];
            caique::AicaModel m;
            m.write(0x00, (1 << 9));                    /* LPCTL: loop (keeps the slot playing) */
            m.write(0x0C, 64);                           /* LEA */
            m.write(0x10, 31);                           /* AR 31 */
            m.write(0x14, (c.krs << 10));                /* KRS, RR 0 */
            m.write(0x18, (c.oct << 11) | c.fns);
            m.write(0x28, (1 << 6) | 4);                 /* VOFF, filter on, Q 4 */
            for (int j = 0; j < 5; j++) m.write(0x2C + 4 * j, c.flv[j]);
            m.write(0x40, (c.rate[0] << 8) | c.rate[1]);
            m.write(0x44, (c.rate[2] << 8) | c.rate[3]);
            /* reach an envelope clock sample, then key on there with the fitted counter */
            while ((m.samples & 1) != (uint64_t)caique::AicaModel::EG_PHASE) m.step();
            m.eg_cnt = fit[b][k].c0 - 1;                 /* incremented at the key-on clock */
            m.slot[0].key_pending = 1;
            m.step();                                    /* key-on clock */
            int n = fit[b][k].kon ? 0 : 1, clk = 0;      /* sample index (from the onset) of the next step */
            if (!fit[b][k].kon && u[0] >= 0 && u[0] != (m.slot[0].FEG.v >> 1)) n = 0;
            int bad = -1;
            if (!fit[b][k].kon && u[0] >= 0 && u[0] != (m.slot[0].FEG.v >> 1)) bad = 0;
            for (; n < (int)u.size() && bad < 0; n++) {
                bool clock = (m.samples & 1) == (uint64_t)caique::AicaModel::EG_PHASE;
                if (clock) { clk++; if (clk == fit[b][k].koff) m.slot[0].key_pending = -1; }
                m.step();
                if (u[n] >= 0 && u[n] != (m.slot[0].FEG.v >> 1)) bad = n;
            }
            int ok = bad < 0 ? (int)u.size() : bad;
            total++; full += bad < 0; samples += u.size(); matched += ok;
            printf("ft_%d s%d: %d/%zu", b, k, ok, u.size());
            if (bad >= 0) printf("  first mismatch n %d: hw u %03x model v %04x", bad, u[bad], m.slot[0].FEG.v);
            printf("\n");
        }
    printf("TOTAL full=%d/%d samples=%ld/%ld\n", full, total, matched, samples);
    return full != total;
}
