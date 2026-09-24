// koff_replay.cpp -- spot check of the AEG key-off rules (F1 / F2) through the production model: replay ONE cycle of a
// tests/aeg_koff run (cases/aeg_koff.c) with the key-on sample E_A and the key-off sample E_B pinned by the fitter
// (work/koff/koff_fit_hw_<run>.txt), the envelope clock locked to the capture's ring position (MDEC_CT of sample i =
// (c0 - n_first - i) & 0xFFFF, c0 from tests/aeg_koff/hw/aeg_koff.txt), and compare every MIXS sample of the four
// streams from E_A to E_A + N.  The model starts from reset (every slot off), which is the console's state at E_A for
// the runs used here (all releases finished, the witness off), except a held RR 0 release (koff_d2 slot 2), whose
// key-on during release loads 0x280 either way.  Template: tools/eg_model.cpp.
//   koff_replay <capture prefix> <c0 hex> <run koff_att|koff_d2|koff_d2b> <E_A> <E_B> [N=1200]
// Build: g++ -O2 -std=c++17 -o build/work/koff_replay work/model5/koff_replay.cpp sample-model/aica_model.cpp   (from model/)
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "../../sample-model/aica_model.h"
#include "../../tools/filt_capture.h"
using namespace caique;

struct St { int AR, D1R, DL, D2R, RR, KRS; };
static const St WIT = {31, 31, 31, 31, 31, 1};
static void write_slot(AicaModel &m, int ch, const St &s) {
    auto aw = [&](uint32_t o, uint32_t v) { m.write(0x80 * ch + o, v); };
    aw(0x04, 0x0000); aw(0x08, 0); aw(0x0C, 32);
    aw(0x10, (s.D2R << 11) | (s.D1R << 6) | s.AR);
    aw(0x14, (s.KRS << 10) | (s.DL << 5) | s.RR);
    aw(0x18, 0); aw(0x1C, 0);
    aw(0x20, (15 << 4) | ch);
    aw(0x24, 0);
    aw(0x28, (0 << 6) | (1 << 5));   /* TL 0, VOFF 0, LPOFF 1 */
    for (int i = 0; i < 5; i++) aw(0x2C + 4 * i, 0x1FF8);
    aw(0x40, (31 << 8) | 31); aw(0x44, (31 << 8) | 31);
    aw(0x00, (1 << 9) | 0x01);       /* LPCTL, SA 0x10000 */
}
static const char *stname(int s) { return s == 0 ? "att" : s == 1 ? "d1" : s == 2 ? "d2" : "rel"; }

int main(int argc, char **argv) {
    if (argc < 6) { fprintf(stderr, "usage: koff_replay <prefix> <c0hex> <run> <E_A> <E_B> [N]\n"); return 2; }
    std::string path = argv[1], run = argv[3];
    uint32_t c0 = strtoul(argv[2], 0, 16);
    int EA = atoi(argv[4]), EB = atoi(argv[5]), N = argc > 6 ? atoi(argv[6]) : 1200;
    St s[3];
    if (run == "koff_att") { St t[3] = {{24, 0, 0, 0, 28, 15}, {26, 0, 0, 0, 24, 15}, {28, 0, 0, 0, 26, 15}}; memcpy(s, t, sizeof s); }
    else if (run == "koff_d2") { St t[3] = {{31, 31, 2, 26, 24, 15}, {31, 31, 2, 24, 28, 15}, {31, 31, 2, 28, 0, 15}}; memcpy(s, t, sizeof s); }
    else if (run == "koff_d2b") { St t[3] = {{31, 31, 2, 30, 24, 15}, {31, 31, 2, 26, 30, 15}, {31, 31, 2, 0, 26, 15}}; memcpy(s, t, sizeof s); }
    else { fprintf(stderr, "unknown run\n"); return 2; }
    Capture cp = cap(path);
    AicaModel m;
    m.eg_K = 6491;
    for (int i = 0; i < 32; i++) { m.ram[0x10000 + 2 * i] = 0xFF; m.ram[0x10000 + 2 * i + 1] = 0x7F; }   /* 32 samples of 0x7FFF, looped [0, 32) */
    for (int k = 0; k < 3; k++) write_slot(m, k, s[k]);
    write_slot(m, 3, WIT);
    for (int i = 0; i < 16; i++) m.step();
    uint32_t md_A = (c0 - cp.first - (uint32_t)EA) & 0xFFFF, md_B = (c0 - cp.first - (uint32_t)EB) & 0xFFFF;
    m.MDEC_CT = md_A;
    /* key-on of the test slots: KYONB on 0..2 (the witness keeps KYONB 0), KYONEX -> the next sample is E_A */
    for (int k = 0; k < 3; k++) m.write(0x80 * k, m.chr(k, 0) | 0x4000);
    m.write(0, m.chr(0, 0) | 0x8000);
    printf("%s %s: E_A %d (MDEC_CT %04x %s) E_B %d (MDEC_CT %04x %s), on %d samples, compare %d samples\n", path.c_str(), run.c_str(),
           EA, md_A, md_A & 1 ? "odd" : "EVEN", EB, md_B, md_B & 1 ? "odd" : "EVEN", EB - EA, N);
    for (int k = 0; k < 3; k++) printf("  stream %d: AR %d D1R %d DL %d D2R %d RR %d KRS %d\n", k, s[k].AR, s[k].D1R, s[k].DL, s[k].D2R, s[k].RR, s[k].KRS);
    int first_bad[4] = {-1, -1, -1, -1}, nbad[4] = {0, 0, 0, 0};
    printf("  around the key-off (i MDEC_CT: per stream hw/model [a state]):\n");
    for (int i = EA; i < EA + N && i < (int)cp.n; i++) {
        if (i == EB) {   /* key-off of 0..2 + key-on of the witness with ONE KYONEX -> the next sample is E_B */
            for (int k = 0; k < 3; k++) m.write(0x80 * k, m.chr(k, 0) & 0x3FFF);
            m.write(0x80 * 3, m.chr(3, 0) | 0x4000);
            m.write(0, m.chr(0, 0) | 0x8000);
        }
        m.step();
        uint32_t md = (c0 - cp.first - (uint32_t)i) & 0xFFFF;
        bool show = (i >= EB - 3 && i <= EB + 7) || i == EA || i == EA + 1;
        if (show) printf("    %d %04x %s:", i, md, md & 1 ? "odd " : "EVEN");
        for (int k = 0; k < 4; k++) {
            int32_t hw = cp.v[(size_t)i * cp.ns + k], mo = m.MIXS[k];
            if (hw != mo) { nbad[k]++; if (first_bad[k] < 0) first_bad[k] = i; }
            if (show) printf("  s%d %7d/%-7d [%03x %s%s]%s", k, hw, mo, m.slot[k].AEG.a, stname(m.slot[k].AEG.state), m.slot[k].AEG.off ? " OFF" : "", hw != mo ? " <-" : "");
        }
        if (show) printf("\n");
    }
    int full = 0;
    for (int k = 0; k < 4; k++) {
        if (first_bad[k] < 0) { full++; printf("  stream %d: FULL %d/%d\n", k, N, N); }
        else printf("  stream %d: first mismatch at %d (+%d from E_A, %+d from E_B), %d mismatching samples\n", k, first_bad[k], first_bad[k] - EA, first_bad[k] - EB, nbad[k]);
    }
    printf("RESULT %s cycle E_A %d E_B %d: %d/4 streams FULL over %d samples\n", run.c_str(), EA, EB, full, N);
    return full == 4 ? 0 : 1;
}
