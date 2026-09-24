// coll_check.cpp -- tests/dsp_coll: the model's per-sample DSP logs against the console's.
//   coll_check [A_DIR B_DIR]      (default tests/dsp_coll/hw tests/dsp_coll/model; run from caique-rtl/model)
// Each run's log (dsp_coll_<run>.bin: N x {counter, L1, L2, L3, L4}) starts at a different sample on each platform
// (the SH4 and the model do not share a clock), so the two are aligned: the shift d with A[i] == B[i + d] on the most
// samples of the overlap.  A run passes when, at that shift, every word of every sample in the overlap (the first and
// last 8 samples of each log excluded) is equal and the overlap has at least 400 samples.  For the periodic logs
// (write runs) any shift that matches everything is as good as another.
// Exit 0 iff every run passes.
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <array>

static std::vector<std::array<uint16_t, 5>> load(const std::string &p) {
    std::vector<std::array<uint16_t, 5>> v;
    FILE *f = fopen(p.c_str(), "rb");
    if (!f) return v;
    std::array<uint16_t, 5> e;
    while (fread(e.data(), 2, 5, f) == 5) v.push_back(e);
    fclose(f);
    return v;
}

int main(int argc, char **argv) {
    const std::string A = argc > 2 ? argv[1] : "tests/dsp_coll/hw", B = argc > 2 ? argv[2] : "tests/dsp_coll/model";
    const char *runs[] = {"p16_k20", "p16_k3", "p16_k20_q", "p16_k20_f", "p8_k20", "adp_om2", "adp_om1", "adp_o0",
                          "adp_o1", "adp_o2", "p16_wr", "adp_om1_wr", "latch_odd", "latch_even", "p16_kon",
                          "p16_kon_k3", "p8_kon", "adp_kon"};
    int bad = 0;
    for (const char *r : runs) {
        auto a = load(A + "/dsp_coll_" + r + ".bin"), b = load(B + "/dsp_coll_" + r + ".bin");
        if (a.size() < 100 || b.size() < 100) { printf("%-12s missing log (%zu / %zu samples)\n", r, a.size(), b.size()); bad++; continue; }
        const int skip = 8, na = (int)a.size(), nb = (int)b.size();
        int best_d = 0, best_eq = -1, best_ov = 0;
        for (int d = -(nb - skip); d <= na - skip; d++) {   // a[i] vs b[i - d]
            int eq = 0, ov = 0;
            for (int i = skip; i < na - skip; i++) {
                const int j = i - d;
                if (j < skip || j >= nb - skip) continue;
                ov++;
                bool same = true;
                for (int k = 1; k < 5; k++) same &= a[i][k] == b[j][k];
                eq += same;
            }
            if (eq > best_eq || (eq == best_eq && ov > best_ov)) { best_eq = eq; best_d = d; best_ov = ov; }
        }
        const bool ok = best_eq == best_ov && best_ov >= 400;
        printf("%-12s %s: %d of %d samples identical at shift %d", r, ok ? "same" : "DIFFERENT", best_eq, best_ov, best_d);
        if (!ok) {   // the first difference at the best shift
            for (int i = skip; i < na - skip; i++) {
                const int j = i - best_d;
                if (j < skip || j >= nb - skip) continue;
                bool same = true;
                for (int k = 1; k < 5; k++) same &= a[i][k] == b[j][k];
                if (!same) {
                    printf("; first at %d: A %04x %04x %04x %04x  B %04x %04x %04x %04x", i, a[i][1], a[i][2], a[i][3],
                           a[i][4], b[j][1], b[j][2], b[j][3], b[j][4]);
                    break;
                }
            }
            bad++;
        }
        printf("\n");
    }
    printf("coll_check %s vs %s: %d of %zu runs differ\n", A.c_str(), B.c_str(), bad, sizeof runs / sizeof runs[0]);
    return bad ? 1 : 0;
}
