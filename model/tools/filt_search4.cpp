// filt_search4.cpp -- SVF fixed-point search with the facts from tests/filt_imp and filt_reset:
//  * symmetric (sign-magnitude) rounding, * cutoff mantissa candidates 8-bit (FLV[8:1]) or 9-bit,
//  * the filter state is never cleared: initial band in -3..3 (state units), low from the first output.
// Integer only.  band (FBB fraction bits) += R(f*x) - R(f*low) - R(fq*band); low (FL bits) += R(f*band);
// output 1/8 sample = R(low >> (FL-3)); MIXS = -2 * output.  q = qn/8 (Q 0: 12, 4: 8, 8: 6, 16: 3).
// fq either one product (f*qn/8 as a coefficient) or two steps (R(q*R(f*band))).
// Rounding modes: 0 floor, 1 toward zero, 2 half away from zero, 3 half up.
// Build: g++ -O2 -std=c++17 -o work/filt/filt_search4 tools/filt_search4.cpp; run: filt_search4 F Q [F Q ...]
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
    int64_t a = v < 0 ? -v : v, r;
    switch (mode) {
    case 0: return v >> sh;
    case 1: r = a >> sh; break;
    case 2: r = (a + (1LL << (sh - 1))) >> sh; break;
    default: return (v + (1LL << (sh - 1))) >> sh;
    }
    return v < 0 ? -r : r;
}
static int qn_of(int Q) { return Q == 0 ? 12 : Q == 4 ? 8 : Q == 8 ? 6 : Q == 16 ? 3 : -1; }

struct Hyp { int mant9, FBB, FL, rx, rl, rq, rb, ro, fqmode, order, OPM, SQB, SQL, sqm; };

/* keep the top OPM significant bits of |v| (log/float-style operand), sign-magnitude; OPM 0 = full precision */
static inline int64_t opq(int64_t v, int OPM) {
    if (!OPM) return v;
    int64_t a = v < 0 ? -v : v;
    int bl = 64 - __builtin_clzll((uint64_t)a | 1);
    if (bl > OPM) { int s = bl - OPM; a = (a >> s) << s; }
    return v < 0 ? -a : a;
}

static int run(const DS &d, const Hyp &h, int delay, int b0) {
    int e = d.F >> 9, m = d.F & 0x1FF;
    int64_t k; int sh;
    if (h.mant9) { k = 512 + m; sh = 25 - e; } else { k = 256 + (m >> 1); sh = 24 - e; }
    int qn = qn_of(d.Q);
    int64_t low = (int64_t)(-d.y[delay - 1] / 2) << (h.FL - 3), band = b0;
    int L = d.L - delay - 2;
    for (int n = 0; n < L; n++) {
        int64_t xb = (int64_t)X[n] << h.FBB;
        auto lowb = [&](int64_t l) { return h.FBB >= h.FL ? l << (h.FBB - h.FL) : rnd(l, h.FL - h.FBB, 1); };
        auto mul = [&](int64_t v, int s2, int mode) { return rnd(k * opq(v, h.OPM), s2, mode); };
        auto qterm = [&](int64_t b) {
            if (h.fqmode == 0) return rnd(k * qn * opq(b, h.OPM), sh + 3, h.rq);
            return rnd(qn * mul(b, sh, h.rq), 3, h.rq);
        };
        int64_t nb, nl;
        if (h.order == 0) {
            nb = band + mul(xb, sh, h.rx) - mul(lowb(low), sh, h.rl) - qterm(band);
            nl = low + mul(nb, sh + h.FBB - h.FL, h.rb);
        } else {
            nl = low + mul(band, sh + h.FBB - h.FL, h.rb);
            nb = band + mul(xb, sh, h.rx) - mul(lowb(nl), sh, h.rl) - qterm(band);
        }
        /* stored-state quantization: keep SQ significant bits (log/float-style storage), sqm: 1 trunc, 2 round */
        if (h.SQB) nb = h.sqm == 1 ? opq(nb, h.SQB) : opq(nb + (nb >= 0 ? 1 : -1) * ((1LL << 62) >> (63 - std::max(0, 64 - __builtin_clzll((uint64_t)(nb < 0 ? -nb : nb) | 1) - h.SQB))) * (64 - __builtin_clzll((uint64_t)(nb < 0 ? -nb : nb) | 1) > h.SQB), h.SQB);
        if (h.SQL) nl = h.sqm == 1 ? opq(nl, h.SQL) : opq(nl + (nl >= 0 ? 1 : -1) * ((1LL << 62) >> (63 - std::max(0, 64 - __builtin_clzll((uint64_t)(nl < 0 ? -nl : nl) | 1) - h.SQL))) * (64 - __builtin_clzll((uint64_t)(nl < 0 ? -nl : nl) | 1) > h.SQL), h.SQL);
        band = nb; low = nl;
        int64_t o = rnd(low, h.FL - 3, h.ro);
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
    int OPMs[] = {0, 11, 12};
    int SQs[] = {0, 10, 11, 12, 13};
    for (int SQB : SQs) for (int SQL : SQs) for (int sqm = 1; sqm <= 2; sqm++)
    for (int OPM : OPMs)
    for (int mant9 = 0; mant9 < 2; mant9++)
    for (int FBB = 3; FBB <= 4; FBB++)
    for (int FL = 3; FL <= 4; FL++)
    for (int order = 0; order < 2; order++)
    for (int fqmode = 0; fqmode < 2; fqmode++)
    for (int rx = 0; rx < 4; rx++) for (int rl = 0; rl < 4; rl++) for (int rq = 0; rq < 4; rq++)
    for (int rb = 0; rb < 4; rb++) for (int ro = 0; ro < (FL > 3 ? 4 : 1); ro++)
    for (int delay = 7; delay <= 8; delay++) {
        Hyp h{mant9, FBB, FL, rx, rl, rq, rb, ro, fqmode, order, OPM, SQB, SQL, sqm};
        int sc = 1 << 30;
        for (auto *d : use) {
            int best = 0;
            for (int b0 = -3; b0 <= 3; b0++) best = std::max(best, run(*d, h, delay, b0));
            sc = std::min(sc, best);
        }
        res.push_back({sc, h, delay});
    }
    std::sort(res.begin(), res.end(), [](const R &a, const R &b) { return a.score > b.score; });
    for (int i = 0; i < 12 && i < (int)res.size(); i++) {
        auto &r = res[i];
        printf("SQB %2d SQL %2d sqm %d ", r.h.SQB, r.h.SQL, r.h.sqm);
        printf("first mismatch %10d: OPM %2d mant%d FBB %d FL %d order %d fq %d r x%d l%d q%d b%d o%d delay %d\n", r.score,
               r.h.OPM, r.h.mant9 ? 9 : 8, r.h.FBB, r.h.FL, r.h.order, r.h.fqmode, r.h.rx, r.h.rl, r.h.rq, r.h.rb, r.h.ro, r.delay);
    }
    return 0;
}
