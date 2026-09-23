// filt_search6.cpp -- filter rounding search on DC-to-zero trajectories (tests/filt_cyc, work/filt/decay.bin: per
// dataset F, Q, A, N, int32 low[N] in 1/8 sample units).  Input x = A until the switch n0 (found from the data: the
// first big change, or one sample before), then 0.  The trajectory starts in the DC steady state: low0 from the data,
// band0 unknown (searched in -48..48 state units).  Structure: Chamberlin SVF, integer state low (FL fraction bits,
// output = low >> (FL-3) rounded by ro) and band (FBB bits); f = (256 + FLV[8:1]) / 2^(24-e); q = q128/128.
// Products rounded separately: r1 f*x, r2 f*low, r3 the q term, r4 f*band (modes: 0 floor, 1 toward zero,
// 2 half away from zero, 3 half up, 4 ceil).  q term: 0 one coefficient f*q, 1 f then q, 2 q then f.
// A hypothesis passes a dataset if some (band0, n0) reproduces all of it.  Output: hypotheses by datasets passed.
// Build: make -C tools filt_search6 (-> build/tools/filt_search6)
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <vector>
#include <algorithm>
#include <cstring>

struct DS { int F, Q, A, N; std::vector<int32_t> y; int drop; };
static std::vector<DS> ds;
static const int q128[32] = {192, 176, 160, 144, 128, 120, 112, 104, 96, 88, 80, 72, 64, 60, 56, 52,
                             48, 44, 40, 36, 32, 30, 28, 26, 24, 22, 20, 18, 16, 15, 14, 13};
static inline int64_t rnd(int64_t v, int sh, int mode) {
    if (sh <= 0) return v << -sh;
    int64_t a = v < 0 ? -v : v, r;
    switch (mode) {
    case 0: return v >> sh;
    case 1: r = a >> sh; break;
    case 2: r = (a + (1LL << (sh - 1))) >> sh; break;
    case 3: return (v + (1LL << (sh - 1))) >> sh;
    default: return -((-v) >> sh);
    }
    return v < 0 ? -r : r;
}
struct Hyp { int FBB, FL, r1, r2, r3, r4, ro, order, qm; };

// returns number of samples matched (N if all)
static int sim(const DS &d, const Hyp &h, int n0, int64_t band0, int nmax) {
    int e = d.F >> 9, m = d.F & 0x1FF;
    int64_t k = 256 + (m >> 1);
    int sh = 24 - e;
    int64_t qv = q128[d.Q];
    int64_t low = (int64_t)d.y[0] << (h.FL - 3), band = band0;
    auto toB = [&](int64_t l) { return h.FBB >= h.FL ? l << (h.FBB - h.FL) : rnd(l, h.FL - h.FBB, 1); };
    auto qterm = [&](int64_t b) -> int64_t {
        if (h.qm == 0) return rnd(k * qv * b, sh + 7, h.r3);
        if (h.qm == 1) return rnd(qv * rnd(k * b, sh, h.r3), 7, h.r3);
        return rnd(k * rnd(qv * b, 7, h.r3), sh, h.r3);
    };
    int N = std::min(d.N, nmax);
    for (int n = 1; n < N; n++) {
        int64_t x = (n < n0 ? (int64_t)d.A : 0) << (h.FBB + 3 - 3); // x in band units: A samples = A<<3 (1/8) -> <<FBB-3
        x = (int64_t)(n < n0 ? d.A : 0) * 8;
        x = h.FBB >= 3 ? x << (h.FBB - 3) : x;
        int64_t nb, nl;
        auto bupd = [&](int64_t lw) -> int64_t {
            if (h.qm == 3) /* one rounding of f * (x - low - q*band), q*band exact */
                return band + rnd(k * ((x - toB(lw)) * 128 - qv * band), sh + 7, h.r1);
            if (h.qm == 4) /* one rounding of f * (x - low - R(q*band)) */
                return band + rnd(k * (x - toB(lw) - rnd(qv * band, 7, h.r3)), sh, h.r1);
            return band + rnd(k * x, sh, h.r1) - rnd(k * toB(lw), sh, h.r2) - qterm(band);
        };
        if (h.order == 0) {
            nb = bupd(low);
            nl = low + rnd(k * nb, sh + h.FBB - h.FL, h.r4);
        } else {
            nl = low + rnd(k * band, sh + h.FBB - h.FL, h.r4);
            nb = bupd(nl);
        }
        band = nb; low = nl;
        if ((int32_t)rnd(low, h.FL - 3, h.ro) != d.y[n]) return n;
    }
    return N;
}

static bool pass(const DS &d, const Hyp &h, int nmax) {
    for (int dn = -1; dn <= 1; dn++) {
        int n0 = d.drop + dn;
        for (int64_t b0 = 0; b0 <= 48; b0 = b0 <= 0 ? 1 - b0 : -b0) {
            int ok = sim(d, h, n0, b0 << (h.FBB - 3 > 0 ? 0 : 0), nmax);
            if (ok >= std::min(d.N, nmax)) return true;
        }
    }
    return false;
}

int main(int argc, char **argv) {
    FILE *f = fopen("work/filt/decay.bin", "rb");
    int hdr[4];
    while (fread(hdr, 4, 4, f) == 4) {
        DS d; d.F = hdr[0]; d.Q = hdr[1]; d.A = hdr[2]; d.N = hdr[3];
        d.y.resize(d.N);
        if (fread(d.y.data(), 4, d.N, f) != (size_t)d.N) break;
        d.drop = 1;
        while (d.drop < d.N && abs(d.y[d.drop] - d.y[d.drop - 1]) < 8) d.drop++;
        bool skip = d.F == 0x1F55 || d.F == 0x1D55 || (argc > 1 && d.F == 0x1FFE && !strchr(argv[1], 'T'));
        if (!skip && d.drop < d.N - 10) ds.push_back(d);
    }
    fclose(f);
    printf("%zu datasets:", ds.size());
    for (auto &d : ds) printf(" %04x/%d/%d(drop %d)", d.F, d.Q, d.A, d.drop);
    printf("\n");
    struct R { int passed; long matched; Hyp h; };
    std::vector<R> res;
    for (int FBB = 3; FBB <= 6; FBB++)
    for (int FL = 3; FL <= 6; FL++)
    for (int order = 0; order < 2; order++)
    for (int qm = 3; qm < 5; qm++)
    for (int r1 = 0; r1 < 5; r1++) for (int r2 = 0; r2 < 1; r2++) for (int r3 = 0; r3 < (qm == 4 ? 5 : 1); r3++)
    for (int r4 = 0; r4 < 5; r4++) for (int ro = 0; ro < (FL > 3 ? 5 : 1); ro++) {
        Hyp h{FBB, FL, r1, r2, r3, r4, ro, order, qm};
        int passed = 0;
        long mask = 0;
        for (size_t i = 0; i < ds.size(); i++) {
            auto &d = ds[i];
            if (!pass(d, h, 200)) continue;       // quick filter on the first 200 samples
            if (pass(d, h, 1 << 30)) { passed++; mask |= 1L << i; }
        }
        if (passed >= 3) res.push_back({passed, mask, h});
    }
    std::sort(res.begin(), res.end(), [](const R &a, const R &b) { return a.passed > b.passed; });
    printf("%zu hypotheses pass >= 3 datasets\n", res.size());
    for (int i = 0; i < 30 && i < (int)res.size(); i++) {
        auto &r = res[i];
        printf("passed %2d/%zu (mask %04lx): FBB %d FL %d order %d qm %d r x%d l%d q%d b%d o%d\n", r.passed, ds.size(),
               r.matched, r.h.FBB, r.h.FL, r.h.order, r.h.qm, r.h.r1, r.h.r2, r.h.r3, r.h.r4, r.h.ro);
    }
    return 0;
}
