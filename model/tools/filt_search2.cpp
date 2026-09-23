// filt_search2.cpp -- general fixed-point SVF search on one or more filt_id datasets (work/filt/aligned.bin).
//   band (FBB fraction bits) += R1(f*x) - R2(f*low) - R3(fq*band)      (all terms in band units)
//   low  (FL fraction bits)  += R4(f*band)                              (in low units)
//   output (1/8 sample)       = R5(low >> (FL - 3));  MIXS = -2 * output
// f = exact (512+m)<<e / 2^25, fq = f*q exact (q from the float fit); products rounded with mode 0 floor,
// 1 toward zero, 2 round-half-up, 3 ceil.  Orders: 0 band first, 1 low first, 2 simultaneous.
// Initial state: low from the first output, band = 0.  Score: first mismatching sample (higher is better).
// Build: make -C tools filt_search2 (-> build/tools/filt_search2) ; run from caique-rtl/model: filt_search2 F Q [F Q..]
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <vector>
#include <algorithm>

struct DS { int F, Q, L, pre; std::vector<int32_t> y; };
static std::vector<DS> ds;
static std::vector<int16_t> X;

static inline int64_t rnd(int64_t v, int sh, int mode) {
    if (sh <= 0) return v << -sh;
    switch (mode) {
    case 0: return v >> sh;
    case 1: return v >= 0 ? (v >> sh) : -((-v) >> sh);
    case 2: return (v + (1LL << (sh - 1))) >> sh;
    default: return -((-v) >> sh);
    }
}
static double qval(int Q) { return Q == 0 ? 1.5 : Q == 4 ? 1.0 : Q == 8 ? 0.75 : Q == 16 ? 0.375 : -1; }

struct Hyp { int FBB, FL, r1, r2, r3, r4, r5, order; };

static int run(const DS &d, const Hyp &h, int delay) {
    int e = d.F >> 9, m = d.F & 0x1FF;
    // f = fi / 2^25, fq = fqi / 2^27 (q in quarters... use 2^30 scale for safety)
    int64_t fi = (int64_t)(512 + m) << e;                  // / 2^25
    int64_t fqi = (int64_t)(qval(d.Q) * (double)fi * 32);  // / 2^30
    int64_t low = (int64_t)(-d.y[delay - 1] / 2) << (h.FL - 3), band = 0;
    int L = d.L - delay - 2;
    for (int n = 0; n < L; n++) {
        int64_t xb = (int64_t)X[n] << h.FBB;
        int64_t lowb = h.FBB >= h.FL ? low << (h.FBB - h.FL) : low >> (h.FL - h.FBB);
        int64_t nb = band, nl = low;
        auto bandupd = [&](int64_t b, int64_t lb) {
            return b + rnd(fi * xb, 25, h.r1) - rnd(fi * lb, 25, h.r2) - rnd(fqi * b, 30, h.r3);
        };
        auto lowupd = [&](int64_t l, int64_t b) { // b in band units -> low units
            return l + rnd(fi * b, 25 + h.FBB - h.FL, h.r4);
        };
        if (h.order == 0) { nb = bandupd(band, lowb); nl = lowupd(low, nb); }
        else if (h.order == 1) {
            nl = lowupd(low, band);
            int64_t lb2 = h.FBB >= h.FL ? nl << (h.FBB - h.FL) : nl >> (h.FL - h.FBB);
            nb = bandupd(band, lb2);
        } else { nb = bandupd(band, lowb); nl = lowupd(low, band); }
        band = nb; low = nl;
        int64_t o = rnd(low, h.FL - 3, h.r5);
        if ((int32_t)(-2 * o) != d.y[n + delay]) return n;
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
    for (int FBB = 3; FBB <= 10; FBB++)
    for (int FL = 3; FL <= 10; FL++)
    for (int order = 0; order < 3; order++)
    for (int r1 = 0; r1 < 4; r1++) for (int r2 = 0; r2 < 4; r2++) for (int r3 = 0; r3 < 4; r3++)
    for (int r4 = 0; r4 < 4; r4++) for (int r5 = 0; r5 < (FL > 3 ? 4 : 1); r5++)
    for (int delay = 7; delay <= 9; delay++) {
        Hyp h{FBB, FL, r1, r2, r3, r4, r5, order};
        int sc = 1 << 30;
        for (auto *d : use) sc = std::min(sc, run(*d, h, delay));
        if (sc > 60) res.push_back({sc, h, delay});
    }
    std::sort(res.begin(), res.end(), [](const R &a, const R &b) { return a.score > b.score; });
    for (int i = 0; i < 12 && i < (int)res.size(); i++) {
        auto &r = res[i];
        printf("first mismatch %8d: FBB %2d FL %2d order %d r %d%d%d%d%d delay %d\n", r.score, r.h.FBB, r.h.FL, r.h.order,
               r.h.r1, r.h.r2, r.h.r3, r.h.r4, r.h.r5, r.delay);
    }
    return 0;
}
