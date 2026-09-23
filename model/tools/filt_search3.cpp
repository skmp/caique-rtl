// filt_search3.cpp -- direct-form searches for the AICA slot filter (work/filt/aligned.bin, zero initial state).
// Transfer function fixed by the SVF parameters: b0 = f^2, a1 = 2 - f^2 - f q, a2 = 1 - f q.
//   form 0: y = R(b0 x) + R(a1 y1) - R(a2 y2)
//   form 1: y = 2 y1 - y2 + R(f^2 (x - y1)) + R(f q (y2 - y1))
//   form 2: y = y1 + R(f^2 (x - y1) + f q (y2 - y1)) + (y1 - y2)            (one rounding)
//   form 3: y = R(b0 x + a1 y1 - a2 y2)                                     (one rounding)
// y has FB fraction bits (sample units), output (1/8 sample) = RO(y >> (FB - 3)); MIXS = -2 * output.
// Coefficients exact (scale 2^50) or truncated to CB fraction bits.  Rounding modes: 0 floor 1 toward-zero
// 2 round-half-up 3 ceil.  Score: first mismatch over all given datasets (min).
// Build: g++ -O2 -std=c++17 -o work/filt/filt_search3 tools/filt_search3.cpp ; run: filt_search3 F Q [F Q ...]
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <vector>
#include <algorithm>

typedef __int128 i128;
struct DS { int F, Q, L, pre; std::vector<int32_t> y; };
static std::vector<DS> ds;
static std::vector<int16_t> X;

static inline i128 rnd(i128 v, int sh, int mode) {
    if (sh <= 0) return v << -sh;
    switch (mode) {
    case 0: return v >> sh;
    case 1: return v >= 0 ? (v >> sh) : -((-v) >> sh);
    case 2: return (v + ((i128)1 << (sh - 1))) >> sh;
    default: return -((-v) >> sh);
    }
}
static double qval(int Q) { return Q == 0 ? 1.5 : Q == 4 ? 1.0 : Q == 8 ? 0.75 : Q == 16 ? 0.375 : -1; }

struct Hyp { int form, FB, CB, r1, r2, r3, ro; };

static int run(const DS &d, const Hyp &h, int delay) {
    const int S = 50;
    int e = d.F >> 9, m = d.F & 0x1FF;
    i128 f = (i128)(512 + m) << (e + S - 25);               // f * 2^S
    int qn = (int)(qval(d.Q) * 8 + 0.5);                     // q * 8
    i128 f2 = (f * f) >> S, fq = (f * qn) >> 3;
    i128 one = (i128)1 << S;
    i128 b0 = f2, a1 = 2 * one - f2 - fq, a2 = one - fq;
    if (h.CB < S) {
        int sh = S - h.CB;
        b0 = (b0 >> sh) << sh; a1 = (a1 >> sh) << sh; a2 = (a2 >> sh) << sh; f2 = (f2 >> sh) << sh; fq = (fq >> sh) << sh;
    }
    i128 y1 = 0, y2 = 0;
    int L = d.L - delay - 2;
    for (int n = 0; n < L; n++) {
        i128 x = (i128)X[n] << h.FB, y;
        switch (h.form) {
        case 0: y = rnd(b0 * x, S, h.r1) + rnd(a1 * y1, S, h.r2) - rnd(a2 * y2, S, h.r3); break;
        case 1: y = 2 * y1 - y2 + rnd(f2 * (x - y1), S, h.r1) + rnd(fq * (y2 - y1), S, h.r2); break;
        case 2: y = y1 + rnd(f2 * (x - y1) + fq * (y2 - y1), S, h.r1) + (y1 - y2); break;
        default: y = rnd(b0 * x + a1 * y1 - a2 * y2, S, h.r1); break;
        }
        y2 = y1; y1 = y;
        i128 o = rnd(y, h.FB - 3, h.ro);
        if ((int32_t)(-2 * (int64_t)o) != d.y[n + delay]) return n;
    }
    return 1 << 30;
}

int main(int argc, char **argv) {
    FILE *f = fopen("work/filt/aligned.bin", "rb");
    int hdr[4];
    while (fread(hdr, 4, 4, f) == 4) {
        DS d; d.F = hdr[0]; d.Q = hdr[1]; d.L = hdr[2]; d.pre = hdr[3];
        d.y.resize(d.L);
        if (fread(d.y.data(), 4, d.L, f) != (size_t)d.L) break;
        ds.push_back(d);
    }
    fclose(f);
    f = fopen("work/filt/input.bin", "rb");
    X.resize(3200);
    if (fread(X.data(), 2, 3200, f) != 3200) return 1;
    fclose(f);
    std::vector<const DS *> use;
    for (int a = 1; a + 1 < argc; a += 2) {
        int F = (int)strtol(argv[a], 0, 16), Q = atoi(argv[a + 1]);
        for (auto &d : ds) if (d.F == F && d.Q == Q) use.push_back(&d);
    }
    struct R { int score; Hyp h; int delay; };
    std::vector<R> res;
    int CBs[] = {10, 11, 12, 13, 14, 15, 16, 18, 20, 24, 50};
    for (int form = 0; form < 4; form++)
    for (int FB = 3; FB <= 16; FB++)
    for (int CB : CBs)
    for (int r1 = 0; r1 < 4; r1++) for (int r2 = 0; r2 < (form < 2 ? 4 : 1); r2++)
    for (int r3 = 0; r3 < (form == 0 ? 4 : 1); r3++)
    for (int ro = 0; ro < (FB > 3 ? 4 : 1); ro++)
    for (int delay = 7; delay <= 8; delay++) {
        Hyp h{form, FB, CB, r1, r2, r3, ro};
        int sc = 1 << 30;
        for (auto *d : use) sc = std::min(sc, run(*d, h, delay));
        res.push_back({sc, h, delay});
    }
    std::sort(res.begin(), res.end(), [](const R &a, const R &b) { return a.score > b.score; });
    for (int i = 0; i < 12 && i < (int)res.size(); i++) {
        auto &r = res[i];
        printf("first mismatch %10d: form %d FB %2d CB %2d r %d%d%d ro %d delay %d\n", r.score, r.h.form, r.h.FB, r.h.CB,
               r.h.r1, r.h.r2, r.h.r3, r.h.ro, r.delay);
    }
    return 0;
}
