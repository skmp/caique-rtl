// filt_reach.cpp -- how large can the slot filter's states get?  Worst-case search over every cutoff/Q setting with
// the production recurrence (NOTES.md "Slot filter": x - low - damping clamped to signed 24 bits before the cutoff
// multiply; integrators unbounded here, so any growth past a register width is visible in the result).
// Input amplitude: full scale, x = +-32767 * 8 (1/8 sample units).  Drives, each run for N samples from rest:
//   dc     constant +A
//   alt    alternating +A, -A (the Nyquist mode; undamped at unity cutoff, Q 0)
//   irev   A * sign(h[N-1-n]) with h the filter's own impulse response (the optimum for a linear filter: the output
//          at the last sample reaches A * ||h||_1)
//   pump   A * sign(band) every sample: push in phase with the band ("velocity"), the classic resonance pump; it
//          adapts to the actual (clamped) dynamics
// Output: the global maxima of |low| and |band| with the settings that reach them, the top settings per drive, and
// how many settings exceed 2^22, 2^23.  -noclamp: without the 24-bit clamp (for comparison).
//   filt_reach [-noclamp] [-n N] [-emin e]        Build: make -C tools filt_reach (-> build/tools/filt_reach)
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <algorithm>

typedef int64_t I;
static const int qm[32] = {192, 176, 160, 144, 128, 120, 112, 104, 96, 88, 80, 72, 64, 60, 56, 52,
                           48, 44, 40, 36, 32, 30, 28, 26, 24, 22, 20, 18, 16, 15, 14, 13};
static bool CLAMP = true;
static inline I ceilshr(I n, int s) { return -((-n) >> s); }
struct F { I k; int s, q; };
static inline void step(const F &f, I x, I &L, I &B) {
    I D = 2 * ceilshr(qm[f.q] * B, 8);
    I H = x - L - D;
    if (CLAMP) H = H < -8388608 ? -8388608 : H > 8388607 ? 8388607 : H;
    B += (f.k * H) >> f.s;
    L += ceilshr(f.k * B, f.s);
}
struct Res { I maxL = 0, maxB = 0; };
static const I A = 32767 * 8;
static Res drive(const F &f, int kind, int N, const std::vector<int8_t> &sg) {
    I L = 0, B = 0;
    Res r;
    for (int n = 0; n < N; n++) {
        I x;
        switch (kind) {
        case 0: x = A; break;
        case 1: x = (n & 1) ? -A : A; break;
        case 2: x = A * sg[N - 1 - n]; break;
        default: x = B >= 0 ? A : -A; break;
        }
        step(f, x, L, B);
        r.maxL = std::max(r.maxL, L < 0 ? -L : L);
        r.maxB = std::max(r.maxB, B < 0 ? -B : B);
    }
    return r;
}
int main(int argc, char **argv) {
    int N = 16384, emin = 10;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-noclamp")) CLAMP = false;
        else if (!strcmp(argv[i], "-n")) N = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-emin")) emin = atoi(argv[++i]);
    }
    struct Row { int flv, q, kind; Res r; };
    std::vector<Row> rows;
    std::vector<int> settings;
    for (int e = emin; e <= 15; e++)
        for (int m = 0; m < 256; m++)
            for (int q = 0; q < 32; q++) settings.push_back(((e << 9) | (m << 1)) << 5 | q);
    rows.resize(settings.size() * 4);
#pragma omp parallel for schedule(dynamic, 64)
    for (size_t i = 0; i < settings.size(); i++) {
        int flv = settings[i] >> 5, q = settings[i] & 31;
        F f{flv >= 0x1FFE ? 512 : 256 + ((flv >> 1) & 255), 24 - (flv >> 9), q};
        // impulse response of low (full-scale impulse, same arithmetic) -> sign pattern for the irev drive
        std::vector<int8_t> sg(N);
        I L = 0, B = 0;
        for (int n = 0; n < N; n++) {
            step(f, n == 0 ? A : 0, L, B);
            sg[n] = L >= 0 ? 1 : -1;
        }
        for (int kind = 0; kind < 4; kind++) rows[i * 4 + kind] = {flv, q, kind, drive(f, kind, N, sg)};
    }
    const char *kname[4] = {"dc", "alt", "irev", "pump"};
    printf("filt_reach: %s, N %d, FLV e %d..15 (all mantissas, bit 0 = 0), Q 0..31: %zu settings x 4 drives\n",
           CLAMP ? "24-bit clamp of x - low - damping" : "NO clamp", N, emin, settings.size());
    for (int kind = 0; kind < 4; kind++) {
        std::vector<const Row *> v;
        for (auto &r : rows) if (r.kind == kind) v.push_back(&r);
        std::sort(v.begin(), v.end(), [](const Row *a, const Row *b) {
            return std::max(a->r.maxL, a->r.maxB) > std::max(b->r.maxL, b->r.maxB); });
        int over22 = 0, over23 = 0, over24 = 0;
        for (auto *r : v) {
            I m = std::max(r->r.maxL, r->r.maxB);
            over22 += m >= (I(1) << 22); over23 += m >= (I(1) << 23); over24 += m >= (I(1) << 24);
        }
        printf("%-4s: settings with max(|low|,|band|) >= 2^22: %d, >= 2^23: %d, >= 2^24: %d; top 5:\n", kname[kind],
               over22, over23, over24);
        for (int t = 0; t < 5 && t < (int)v.size(); t++)
            printf("      FLV %04x Q %2d  max|low| %11lld  max|band| %11lld\n", v[t]->flv, v[t]->q,
                   (long long)v[t]->r.maxL, (long long)v[t]->r.maxB);
    }
    return 0;
}
