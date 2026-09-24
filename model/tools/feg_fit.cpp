// feg_fit.cpp -- fit the filter envelope (FEG) model to the per-sample values recovered by feg_track.
// Model (sample-model/aica_model.cpp feg_clock): envelope clock every 2 samples, global counter eg_cnt, increments from the
// AEG tables at the effective rate R (eff_rate, KRS as for the AEG); key-on loads FLV0 at an envelope clock and the
// FEG first moves on the next clock; key-off switches to release at a clock and moves on that same clock.
// Unknown per batch: the clock parity relative to the onset (p), eg_cnt at the key-on clock (c0 mod 2^14) and the
// key-off clock.  Hypotheses: trans 0 = the model (on a clock where v == target the state advances and v does not
// move), 1 = advance and move toward the new target in the same clock, 2 = advance as soon as a step lands on the
// target; krs 0/1 = KRS not applied / applied to FEG rates.
// Every hypothesis is scored by how many unambiguous samples it reproduces before the first mismatch.
//   feg_fit (run from caique-rtl/model after feg_track)
// Build: make -C tools feg_fit (-> build/tools/feg_fit)
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>
#include <algorithm>
#include <cstring>
#include <cstdlib>

static const uint8_t eg_inc[17][8] = {
    {0, 1, 0, 1, 0, 1, 0, 1}, {0, 1, 0, 1, 1, 1, 0, 1}, {0, 1, 1, 1, 0, 1, 1, 1}, {0, 1, 1, 1, 1, 1, 1, 1},
    {1, 1, 1, 1, 1, 1, 1, 1}, {1, 1, 1, 2, 1, 1, 1, 2}, {1, 2, 1, 2, 1, 2, 1, 2}, {1, 2, 2, 2, 1, 2, 2, 2},
    {2, 2, 2, 2, 2, 2, 2, 2}, {2, 2, 2, 4, 2, 2, 2, 4}, {2, 4, 2, 4, 2, 4, 2, 4}, {2, 4, 4, 4, 2, 4, 4, 4},
    {4, 4, 4, 4, 4, 4, 4, 4}, {4, 4, 4, 8, 4, 4, 4, 8}, {4, 8, 4, 8, 4, 8, 4, 8}, {4, 8, 8, 8, 4, 8, 8, 8},
    {8, 8, 8, 8, 8, 8, 8, 8}};
static uint32_t EGOFF = getenv("EGOFF") ? (uint32_t)atoi(getenv("EGOFF")) : 0;   // R < 48 counter offset (fitted)
static uint32_t eg_increment(uint32_t R, uint32_t cnt) {
    if (R == 0) return 0;
    if (R < 48) {
        cnt += EGOFF;
        uint32_t shift = 11 - (R >> 2);
        if (cnt & ((1u << shift) - 1)) return 0;
        return eg_inc[R & 3][(cnt >> shift) & 7];
    }
    return eg_inc[R >= 60 ? 16 : 4 + (R - 48)][cnt & 7];
}
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
static uint32_t eff_rate(const Cfg &c, int re, int krs_on) {
    if (re == 0) return 0;
    int s = 0;
    if (krs_on && c.krs != 15) {
        int k = c.krs + ((c.oct & 8) ? c.oct - 16 : c.oct);
        s = k < 0 ? 0 : 2 * (k > 15 ? 15 : k) + ((c.fns >> 9) & 1);
    }
    int R = re * 2 + s;
    return R > 63 ? 63 : R;
}
// simulate; returns the number of consecutive samples (from the onset) whose u agrees (ambiguous samples skipped).
// ov 0: clamp at the target (the model); ov 1: no clamp -- a segment ends once a step reaches or passes its target
// (direction fixed when the segment starts); decay 2 and release then hold.
static int KON = 0;   // 1: the key-on clock precedes the onset (the first clock at n = p already moves)
static int sim(const Cfg &c, const std::vector<int> &u, int p, uint32_t c0, int koff, int trans, int krs_on,
               int ov, int *mism_n = nullptr, int *mism_model = nullptr) {
    int v = c.flv[0], st = 0;          // 0 attack 1 decay1 2 decay2 3 release
    uint32_t cnt = c0;
    int clk = KON;
    auto dirof = [&](int s) { int t = c.flv[s + 1]; if (ov == 3) return v >= t ? -1 : 1; return v < t ? 1 : v > t ? -1 : 0; };
    int dir = dirof(0);
    bool done = dir == 0;
    for (int n = 0; n < (int)u.size(); n++) {
        if (n >= p && ((n - p) & 1) == 0) {      // envelope clock
            if (clk > 0) {                          // the key-on clock itself does not move the FEG
                cnt++;
                if (clk == koff) { st = 3; dir = dirof(3); done = dir == 0; }
                bool idle = false;
                if (done && st < 2 && trans != 2) {
                    st++; dir = dirof(st); done = dir == 0;
                    if (trans == 0) idle = true;
                }
                if (!idle && !done) {
                    int target = c.flv[st + 1];
                    uint32_t inc = eg_increment(eff_rate(c, c.rate[st], krs_on), cnt);
                    if (inc && ov == 3) {
                        // one comparator C = (v >= target): attack / decay 1 step until C flips (up ends at or past
                        // the target, down strictly below it); decay 2 / release only take a step that keeps C
                        bool C = v >= target;
                        int nv = v + dir * (int)inc;
                        if (st >= 2 && ((nv >= target) != C)) { /* hold short */ }
                        else { v = nv; if (st < 2 && ((v >= target) != C)) done = true; }
                        if (v < 0) v = 0;
                        if (v > 0x1FFF) v = 0x1FFF;
                        inc = 0;
                    }
                    if (inc && ov == 2) {
                        // attack / decay 1: step until strictly past the target (overshoot), then advance;
                        // decay 2 / release: skip a step that would pass the target (hold short of it)
                        int nv = v + dir * (int)inc;
                        if (st >= 2 && (nv - target) * dir > 0) inc = 0;
                        else { v = nv; if ((v - target) * dir > 0 || (st >= 2 && v == target)) done = true; }
                        if (v < 0) v = 0;
                        if (v > 0x1FFF) v = 0x1FFF;
                        inc = 0;
                    }
                    if (inc) {
                        v += dir * (int)inc;
                        if ((v - target) * dir >= 0) { if (!ov) v = target; done = true; }
                        if (v < 0) v = 0;
                        if (v > 0x1FFF) v = 0x1FFF;
                        if (done && trans == 2 && st < 2) { st++; dir = dirof(st); done = dir == 0; }
                    }
                }
            } else cnt = c0;
            clk++;
        }
        if (u[n] >= 0 && u[n] != (v >> 1)) {
            if (mism_n) { *mism_n = n; *mism_model = v >> 1; }
            return n;
        }
    }
    return (int)u.size();
}
static void per_stream(int b, const std::vector<std::vector<int>> &us, int trans, int ov, int krs_on) {
    for (int k = 0; k < 3; k++) {
        int best = -1, bp = 0, bk = 1 << 30;
        uint32_t bc = 0;
        for (int p = 0; p < 2; p++)
            for (uint32_t c0 = 0; c0 < 16384; c0++) {
                int m = sim(cfg[b][k], us[k], p, c0, 1 << 30, trans, krs_on, ov);
                if (m > best) { best = m; bp = p; bc = c0; }
            }
        int bestk = best;
        for (int ko = 1; ko < (int)us[k].size() / 2; ko++) {
            int m = sim(cfg[b][k], us[k], bp, bc, ko, trans, krs_on, ov);
            if (m > bestk) { bestk = m; bk = ko; }
        }
        int mn = -1, mm = -1;
        int m = sim(cfg[b][k], us[k], bp, bc, bk, trans, krs_on, ov, &mn, &mm);
        printf("  ft_%d s%d: p %d c0 %5u (mod 8 = %u) koff %5d: %d/%zu", b, k, bp, bc, bc & 7, bk, m, us[k].size());
        if (m < (int)us[k].size()) printf(" first mismatch n %d hw %03x model %03x", mn, us[k][mn], mm);
        printf("\n");
    }
}
int main(int argc, char **argv) {
    if (argc > 4) KON = atoi(argv[4]);
    if (argc > 5 && !strcmp(argv[5], "egoff")) {   // feg_fit <trans> <ov> <krs> <kon> egoff <batch> <p> <koff>
        int trans = atoi(argv[1]), ov = atoi(argv[2]), krs_on = atoi(argv[3]), b = atoi(argv[6]), p = atoi(argv[7]), ko = atoi(argv[8]);
        std::vector<std::vector<int>> us(3);
        for (int k = 0; k < 3; k++) {
            FILE *f = fopen(("work/feg/ft_" + std::to_string(b) + "_" + std::to_string(k) + ".u").c_str(), "rb");
            int32_t x;
            while (f && fread(&x, 4, 1, f) == 1) us[k].push_back(x);
            if (f) fclose(f);
        }
        for (EGOFF = 0; EGOFF < 16384; EGOFF++) {
            int nfull = 0; uint32_t first = 0;
            for (uint32_t c0 = 0; c0 < 16384; c0 += 1) {
                bool all = true;
                for (int k = 0; k < 3 && all; k++)
                    if (!getenv("MASK") || strchr(getenv("MASK"), '0' + k))
                        all = sim(cfg[b][k], us[k], p, c0, ko, trans, krs_on, ov) == (int)us[k].size();
                if (all) { if (!nfull) first = c0; nfull++; }
            }
            if (nfull) printf("EGOFF %u: %d shared c0 values give all 3 streams, first %u\n", EGOFF, nfull, first);
            if (EGOFF >= 64 && !nfull) { EGOFF += 63; }
        }
        return 0;
    }
    if (argc > 5 && !strcmp(argv[5], "one")) {   // feg_fit <trans> <ov> <krs> <kon> one <batch> <stream> <p> <c0> <koff>
        int b = atoi(argv[6]), k = atoi(argv[7]);
        std::vector<int> u;
        FILE *f = fopen(("work/feg/ft_" + std::to_string(b) + "_" + std::to_string(k) + ".u").c_str(), "rb");
        int32_t x;
        while (f && fread(&x, 4, 1, f) == 1) u.push_back(x);
        if (f) fclose(f);
        int mn = -1, mm = -1;
        int m = sim(cfg[b][k], u, atoi(argv[8]), atoi(argv[9]), atoi(argv[10]), atoi(argv[1]), atoi(argv[3]), atoi(argv[2]), &mn, &mm);
        printf("ft_%d s%d: %d/%zu", b, k, m, u.size());
        if (mn >= 0) printf(" first mismatch n %d hw %03x model %03x", mn, u[mn], mm);
        printf("\n");
        return 0;
    }
    if (argc > 5 && !strcmp(argv[5], "sets")) {   // feg_fit <trans> <ov> <krs> <kon> sets <batch> <p> <koff0> <koff1>
        int trans = atoi(argv[1]), ov = atoi(argv[2]), krs_on = atoi(argv[3]), b = atoi(argv[6]), p = atoi(argv[7]);
        std::vector<std::vector<int>> us(3);
        for (int k = 0; k < 3; k++) {
            FILE *f = fopen(("work/feg/ft_" + std::to_string(b) + "_" + std::to_string(k) + ".u").c_str(), "rb");
            int32_t x;
            while (f && fread(&x, 4, 1, f) == 1) us[k].push_back(x);
            if (f) fclose(f);
        }
        for (int ko = atoi(argv[8]); ko <= atoi(argv[9]); ko++)
            for (int k = 0; k < 3; k++) {
                std::vector<uint32_t> ok;
                int bestm = 0;
                for (uint32_t c0 = 0; c0 < 16384; c0++) {
                    int m = sim(cfg[b][k], us[k], p, c0, ko, trans, krs_on, ov);
                    bestm = std::max(bestm, m);
                    if (m == (int)us[k].size()) ok.push_back(c0);
                }
                printf("koff %d s%d: %zu full c0 values (best %d/%zu)", ko, k, ok.size(), bestm, us[k].size());
                for (size_t i = 0; i < ok.size() && i < 12; i++) printf(" %u", ok[i]);
                printf("\n");
            }
        return 0;
    }
    if (argc > 5 && !strcmp(argv[5], "shared")) {   // shared per batch: feg_fit <trans> <ov> <krs> <kon> shared
        int trans = atoi(argv[1]), ov = atoi(argv[2]), krs_on = atoi(argv[3]);
        for (int b = 0; b < 3; b++) {
            std::vector<std::vector<int>> us(3);
            for (int k = 0; k < 3; k++) {
                FILE *f = fopen(("work/feg/ft_" + std::to_string(b) + "_" + std::to_string(k) + ".u").c_str(), "rb");
                int32_t x;
                while (f && fread(&x, 4, 1, f) == 1) us[k].push_back(x);
                if (f) fclose(f);
            }
            long best = -1; int bp = 0, bk = 1 << 30; uint32_t bc = 0;
            for (int p = 0; p < 2; p++)
                for (uint32_t c0 = 0; c0 < 16384; c0++)
                    for (int ko : {1 << 30}) {
                        int mn = 1 << 30, s = 0;
                        for (int k = 0; k < 3; k++) { int m = sim(cfg[b][k], us[k], p, c0, ko, trans, krs_on, ov); mn = std::min(mn, m); s += m; }
                        long sc = (long)mn * 100000 + s;
                        if (sc > best) { best = sc; bp = p; bc = c0; }
                    }
            long bestk = -1;
            for (int ko = 1; ko < (int)us[0].size() / 2; ko++) {
                int mn = 1 << 30, s = 0;
                for (int k = 0; k < 3; k++) { int m = sim(cfg[b][k], us[k], bp, bc, ko, trans, krs_on, ov); mn = std::min(mn, m); s += m; }
                long sc = (long)mn * 100000 + s;
                if (sc > bestk) { bestk = sc; bk = ko; }
            }
            printf("ft_%d shared p %d c0 %u koff %d:", b, bp, bc, bk);
            for (int k = 0; k < 3; k++) {
                int mn = -1, mm = -1;
                int m = sim(cfg[b][k], us[k], bp, bc, bk, trans, krs_on, ov, &mn, &mm);
                if (m == (int)us[k].size()) printf("  s%d ALL %d", k, m);
                else printf("  s%d %d/%zu (n %d: hw %03x model %03x)", k, m, us[k].size(), mn, us[k][mn], mm);
            }
            printf("\n");
        }
        return 0;
    }
    if (argc > 1) {   // per-stream fit: feg_fit <trans> <ov> <krs> [kon]
        for (int b = 0; b < 3; b++) {
            std::vector<std::vector<int>> us(3);
            for (int k = 0; k < 3; k++) {
                FILE *f = fopen(("work/feg/ft_" + std::to_string(b) + "_" + std::to_string(k) + ".u").c_str(), "rb");
                int32_t x;
                while (f && fread(&x, 4, 1, f) == 1) us[k].push_back(x);
                if (f) fclose(f);
            }
            per_stream(b, us, atoi(argv[1]), atoi(argv[2]), atoi(argv[3]));
        }
        return 0;
    }
    const char *tname[] = {"idle-clock (model)", "advance+move", "advance-on-landing"};
    for (int b = 0; b < 3; b++) {
        std::vector<std::vector<int>> us(3);
        for (int k = 0; k < 3; k++) {
            FILE *f = fopen(("work/feg/ft_" + std::to_string(b) + "_" + std::to_string(k) + ".u").c_str(), "rb");
            if (!f) { printf("run feg_track first\n"); return 1; }
            int32_t x;
            while (fread(&x, 4, 1, f) == 1) us[k].push_back(x);
            fclose(f);
        }
        for (int ov = 1; ov >= 0; ov--)
        for (int krs_on = 1; krs_on >= 0; krs_on--)
            for (int trans = 0; trans < 3; trans++) {
                // shared per batch: p, c0, key-off clock; score = sum over the three streams
                // score: the worst stream first (all three share the clock), then the sum
                long best = -1;
                int bp = 0, bk = 0;
                uint32_t bc = 0;
                for (int p = 0; p < 2; p++)
                    for (uint32_t c0 = 0; c0 < 16384; c0++) {
                        int mn = 1 << 30, s = 0;
                        for (int k = 0; k < 3; k++) { int m = sim(cfg[b][k], us[k], p, c0, 1 << 30, trans, krs_on, ov); mn = std::min(mn, m); s += m; }
                        long sc = (long)mn * 100000 + s;
                        if (sc > best) { best = sc; bp = p; bc = c0; }
                    }
                // key-off clock (one for all three streams: they are keyed off by one KYONEX)
                long bestk = -1;
                for (int ko = 1; ko < (int)us[0].size() / 2; ko++) {
                    int mn = 1 << 30, s = 0;
                    for (int k = 0; k < 3; k++) { int m = sim(cfg[b][k], us[k], bp, bc, ko, trans, krs_on, ov); mn = std::min(mn, m); s += m; }
                    long sc = (long)mn * 100000 + s;
                    if (sc > bestk) { bestk = sc; bk = ko; }
                }
                printf("ft_%d ov %d krs %d trans %-20s p %d c0 %5u koff clock %5d:", b, ov, krs_on, tname[trans], bp, bc, bk);
                for (int k = 0; k < 3; k++) {
                    int mn = -1, mm = -1;
                    int m = sim(cfg[b][k], us[k], bp, bc, bk, trans, krs_on, ov, &mn, &mm);
                    if (m == (int)us[k].size()) printf("  s%d ALL %d", k, m);
                    else printf("  s%d %d/%zu (n %d: hw %03x model %03x)", k, m, us[k].size(), mn, us[k][mn], mm);
                }
                printf("\n");
            }
    }
    return 0;
}
