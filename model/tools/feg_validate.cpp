// feg_validate.cpp -- the production AicaModel FEG against the per-sample values recovered from the console by
// feg_track (work/feg/ft_<b>_<k>.u, u = v >> 1).  Each stream runs in its own model instance configured like
// cases/feg_track.c, with the envelope clock locked to the capture's ring position (NOTES.md "Envelope clock"):
// the model's MDEC_CT is set to the capture's MDEC_CT of the onset sample (c0 - n_first - onset, from the cap_start
// log line), eg_K = 6491 (the console boot of tests/feg_track and tests/eg_lock), the key-on lands on the onset
// sample, and the key-off sample is searched between the mark and mark + 400 samples (the marks are head estimates).
// Expected: TOTAL full=9/9 samples=77862/77862.
// Build: make -C tools feg_validate (-> build/tools/feg_validate; links sample-model/aica_model.cpp)
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "aica_model.h"   /* -I../sample-model or -I../cycle-model (tools/Makefile) */
#include "filt_capture.h"

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
static const uint32_t c0ring[3] = {0xe4be, 0xa802, 0x66ac};   /* cap_start lines of tests/feg_track/hw/feg_track.txt */

struct Snap { caique::AicaModel m; uint8_t *ram; };
static void snap_take(Snap &s, const caique::AicaModel &m) { memcpy((void *)&s.m, &m, sizeof m); memcpy(s.ram, m.ram, caique::AicaModel::RAM_SIZE); }
static void snap_restore(caique::AicaModel &m, const Snap &s) { uint8_t *ram = m.ram; memcpy((void *)&m, &s.m, sizeof m); m.ram = ram; memcpy(ram, s.ram, caique::AicaModel::RAM_SIZE); }

int main() {
    int full = 0, total = 0;
    long matched = 0, samples = 0;
    Snap *snap = (Snap *)calloc(1, sizeof(Snap));
    snap->ram = (uint8_t *)malloc(caique::AicaModel::RAM_SIZE);
    for (int b = 0; b < 3; b++) {
        auto cp = cap("tests/feg_track/hw/ft_" + std::to_string(b));
        FILE *hf = fopen(("tests/feg_track/hw/ft_" + std::to_string(b) + ".hdr").c_str(), "rb");
        uint32_t h[64] = {0}; size_t nh = fread(h, 4, 64, hf); fclose(hf);
        int m2 = -1;
        for (size_t i = 11; i + 1 < nh && i < 11 + 2 * h[6]; i += 2) if (h[i] == 2) m2 = (int)(h[i + 1] - cp.first);
        unsigned on = 1;
        while (on < cp.n && !cp.v[on * 4 + 3]) on++;
        uint32_t md_on = (c0ring[b] - cp.first - on) & 0xFFFF;
        for (int k = 0; k < 3; k++) {
            std::vector<int> u;
            FILE *f = fopen(("work/feg/ft_" + std::to_string(b) + "_" + std::to_string(k) + ".u").c_str(), "rb");
            if (!f) { printf("run build/tools/feg_track first\n"); return 1; }
            int32_t x;
            while (fread(&x, 4, 1, f) == 1) u.push_back(x);
            fclose(f);
            const Cfg &c = cfg[b][k];
            caique::AicaModel m;
            m.eg_K = 6491;
            m.write(0x00, (1 << 9));                    /* LPCTL: loop (keeps the slot playing) */
            m.write(0x0C, 64);                           /* LEA */
            m.write(0x10, 31);                           /* AR 31 */
            m.write(0x14, (c.krs << 10));                /* KRS, RR 0 */
            m.write(0x18, (c.oct << 11) | c.fns);
            m.write(0x28, (1 << 6) | 4);                 /* VOFF, filter on, Q 4 */
            for (int j = 0; j < 5; j++) m.write(0x2C + 4 * j, c.flv[j]);
            m.write(0x40, (c.rate[0] << 8) | c.rate[1]);
            m.write(0x44, (c.rate[2] << 8) | c.rate[3]);
            for (int i = 0; i < 16; i++) m.step();
            m.MDEC_CT = (md_on + 2) & 0xFFFF;   /* the step of a sample has MDEC_CT = capture + 1 */
            m.write(0x00, (1 << 9) | 0x4000 | 0x8000); m.step();   /* KYONB + KYONEX: boundary sample, then the onset */
            auto run = [&](int koff, int *bad) -> int {   /* samples from the onset that agree with u; -1 = all */
                for (int n = 0; n < (int)u.size(); n++) {
                    if (n + 1 == koff) m.write(0x00, (1 << 9) | 0x8000);   /* KYONB 0 + KYONEX: key-off on sample koff */
                    m.step();
                    if (u[n] >= 0 && u[n] != (m.slot[0].FEG.vo >> 1)) { *bad = n; return n; }
                }
                return (int)u.size();
            };
            int lo = m2 - 256, hi = m2 + 400, bad = -1, best = -1, bestko = -1;   /* the mark is a head estimate */
            // run to lo - 1, snapshot, then try every key-off sample
            for (int n = 0; n < lo - 1; n++) { m.step(); if (u[n] >= 0 && u[n] != (m.slot[0].FEG.vo >> 1)) { bad = n; break; } }
            if (bad < 0) {
                snap_take(*snap, m);
                for (int ko = lo; ko <= hi; ko++) {
                    snap_restore(m, *snap);
                    int b2 = -1;
                    // continue from lo
                    int got = lo - 1;
                    for (int n = lo - 1; n < (int)u.size(); n++) {
                        if (n + 1 == ko) m.write(0x00, (1 << 9) | 0x8000);
                        m.step();
                        if (u[n] >= 0 && u[n] != (m.slot[0].FEG.vo >> 1)) { b2 = n; break; }
                        got = n + 1;
                    }
                    if (got > best) { best = got; bestko = ko; bad = b2; }
                    if (b2 < 0) { best = (int)u.size(); bestko = ko; bad = -1; break; }
                }
            } else best = bad;
            (void)run;
            int ok = bad < 0 ? (int)u.size() : bad;
            total++; full += bad < 0; samples += u.size(); matched += ok;
            uint32_t md_ko = (c0ring[b] - cp.first - on - bestko) & 0xFFFF;
            printf("ft_%d s%d: %d/%zu (onset MDEC_CT %04x, key-off sample +%d MDEC_CT %04x %s)", b, k, ok, u.size(), md_on, bestko, md_ko, (md_ko & 1) ? "odd" : "even");
            if (bad >= 0) printf("  first mismatch n %d: hw u %03x model v %04x", bad, u[bad], m.slot[0].FEG.v);
            printf("\n");
        }
    }
    printf("TOTAL full=%d/%d samples=%ld/%ld\n", full, total, matched, samples);
    return full != total;
}
