// feg_track.cpp -- recover the filter envelope value per sample from tests/feg_track captures.
// The slot filter is bit-exact (NOTES.md "Slot filter"), so with a full-scale random input the output pins the
// cutoff every sample.  From key-on (the reference stream's onset; the FEG holds FLV0 there) the tool tracks every
// (low, band, u) state that reproduces the output, u = v >> 1 (v bit 0 is unused by the filter), allowing u to move
// by at most 4 per sample (the FEG steps at most 8 per envelope clock of 2 samples).  Writes the recovered u(n) to
// work/feg/ft_<b>_<k>.u (int32 per sample from the onset; -1 where ambiguous) and prints the change events.
//   feg_track [-v]          (run from caique-rtl/model)
// Build: make -C tools feg_track (-> build/tools/feg_track)
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include <tuple>
#include "filt_capture.h"
using I = int64_t;
static const int qm[32] = {192, 176, 160, 144, 128, 120, 112, 104, 96, 88, 80, 72, 64, 60, 56, 52,
                           48, 44, 40, 36, 32, 30, 28, 26, 24, 22, 20, 18, 16, 15, 14, 13};
static I ceilshr(I n, int s) { return -((-n) >> s); }
struct St { I L, B; int u; bool operator<(const St &o) const { return std::tie(L, B, u) < std::tie(o.L, o.B, o.u); }
            bool operator==(const St &o) const { return L == o.L && B == o.B && u == o.u; } };
static void step(I &L, I &B, I x, int u, int Q) {
    int v = u << 1, k = v >= 0x1ffe ? 512 : 256 + (u & 255), s = 24 - (v >> 9);
    I d = 2 * ceilshr(qm[Q] * B, 8);
    B += (k * (x - L - d)) >> s;
    L += ceilshr(k * B, s);
}
static const int FLV0[3][3] = {{0x1800, 0x1FF0, 0x1C00}, {0x1800, 0x1800, 0x1800}, {0x1800, 0x1800, 0x1C00}};
int main(int argc, char **argv) {
    bool verbose = argc > 1 && !strcmp(argv[1], "-v");
    if (system("mkdir -p work/feg")) {}
    for (int b = 0; b < 3; b++) {
        auto c = cap("tests/feg_track/hw/ft_" + std::to_string(b));
        unsigned on = 1;
        while (on < c.n && !c.v[on * 4 + 3]) on++;
        for (int k = 0; k < 3; k++) {
            std::vector<St> S, T;
            for (I b0 = -256; b0 <= 256; b0++) S.push_back({-I(c.v[(on - 1) * 4 + k]) / 2, b0, FLV0[b][k] >> 1});
            std::vector<int32_t> u(c.n - on, -1);
            size_t maxset = 0;
            unsigned n = on;
            for (; n < c.n; n++) {
                I x = I(c.v[n * 4 + 3]) >> 1;   // floor(s16 / 2) (tests/filt_frac)
                T.clear();
                for (auto &s : S)
                    for (int du = -4; du <= 4; du++) {
                        int nu = s.u + du;
                        if (nu < 0 || nu > 4095) continue;
                        I L = s.L, B = s.B;
                        step(L, B, x, nu, 4);
                        if (std::clamp<I>(-2 * L, -524288, 524287) == c.v[n * 4 + k]) T.push_back({L, B, nu});
                    }
                std::sort(T.begin(), T.end());
                T.erase(std::unique(T.begin(), T.end()), T.end());
                if (T.empty()) break;
                if (T.size() > 400000) T.resize(400000);   // safety cap (reported as max set)
                maxset = std::max(maxset, T.size());
                bool one = true;
                for (auto &s : T) one &= s.u == T[0].u;
                u[n - on] = one ? T[0].u : -1;
                std::swap(S, T);
            }
            int amb = 0;
            for (unsigned i = 0; i < n - on; i++) amb += u[i] < 0;
            printf("ft_%d stream %d: tracked %u/%u samples from onset %u, ambiguous u at %d, max set %zu\n", b, k,
                   n - on, c.n - on, on, amb, maxset);
            FILE *f = fopen(("work/feg/ft_" + std::to_string(b) + "_" + std::to_string(k) + ".u").c_str(), "wb");
            fwrite(u.data(), 4, u.size(), f);
            fclose(f);
            // change events: runs of equal u, printed as (start sample, u, run length)
            int prev = -2;
            unsigned start = 0, printed = 0;
            std::string line;
            for (unsigned i = 0; i <= n - on; i++) {
                int cur = i < n - on ? u[i] : -3;
                if (cur != prev) {
                    if (prev != -2 && (verbose || printed < 60)) {
                        char buf[64];
                        snprintf(buf, sizeof buf, " %u:%03x*%u", start, prev < 0 ? 0xfff : prev, i - start);
                        line += buf;
                        printed++;
                    }
                    prev = cur;
                    start = i;
                }
            }
            printf("  runs (start:u*len, first 60):%s\n", line.c_str());
        }
    }
    return 0;
}
