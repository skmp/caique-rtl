// filt_search11.cpp -- truncated-multiplier search for the slot filter on the filt_cyc step responses
// (work/filt/step.bin, tools/filt_step.py).  Structure (stored band, established by filt_search9/10): the band
// register holds b 2^(18-e) (b in samples), low L in 2^-(3+HL) sample:
//     b' = b + M(k, x - L, sx) - D(b)        [f (x - L) in band units = k (x - L)_{1/8} / 2^9]
//     L' = L + M(k, b', sl)                  [f b in L units = k b 2^(2e-39) 2^HL]
//     D(b) = M(k qm, b, 31 - e)  (dv 0)  or  M(qm, M(k, b, 24 - e... ), 7) (dv 1: f then q)
// M(C, V, s) ~ C V / 2^s computed by a multiplier model:
//   mm 0: exact product, rounded with mode r (0 floor, 1 toward zero, 2 half away, 3 half up)
//   mm 1: radix-4 Booth on V (the data operand), rows d_j C 4^j; negative rows as ~(|d| C) << 2j plus a hot one at
//         column 2j; columns below t = s - g dropped (the hot one too when 2j < t); the kept sum is then rounded
//         at column s with mode r (floor or half up via a constant)
//   mm 2: as 1 but Booth on C (the coefficient), rows d_j V 4^j
// usage: [HLS=..] [TOP=n] filt_search11 stream...
// Build: g++ -O2 -fopenmp -std=c++17 -o work/filt/filt_search11 tools/filt_search11.cpp
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <vector>
#include <algorithm>
#include <string>

typedef int64_t i64;
struct DS { int F, Q, A, on, N; std::vector<int32_t> y; };
static std::vector<DS> ds;
static const int q128[32] = {192, 176, 160, 144, 128, 120, 112, 104, 96, 88, 80, 72, 64, 60, 56, 52,
                             48, 44, 40, 36, 32, 30, 28, 26, 24, 22, 20, 18, 16, 15, 14, 13};
static inline i64 rnd(i64 v, int sh, int mode) {
    if (sh <= 0) return v << -sh;
    i64 a = v < 0 ? -v : v, r, h = (i64)1 << (sh - 1);
    switch (mode) {
    case 0: return v >> sh;
    case 1: r = a >> sh; break;
    case 2: r = (a + h) >> sh; break;
    default: return (v + h) >> sh;
    }
    return v < 0 ? -r : r;
}
static inline i64 fl(i64 v, int t) { return t <= 0 ? v << -t : v >> t; }
// Booth radix-4 truncated product: rows from the recoding of R (width 32), multiplicand P
static inline i64 booth(i64 P, i64 R, int s, int g, int mode) {
    int t = s - g;
    if (t < 0) t = 0;
    i64 sum = 0;   // in units of 2^t
    uint64_t u = (uint64_t)R;
    int prev = 0;
    for (int j = 0; j < 20; j++) {
        int b0 = (u >> (2 * j)) & 1, b1 = (u >> (2 * j + 1)) & 1;
        int d = -2 * b1 + b0 + prev;
        prev = b1;
        if (!d) continue;
        i64 m = (d < 0 ? -d : d) * P;
        if (d > 0) sum += fl(m << (2 * j), t);
        else {
            sum += fl((~m) << (2 * j), t);
            if (2 * j >= t) sum += (i64)1 << (2 * j - t);
        }
    }
    // round the kept sum at column s
    int gs = s - t;
    if (mode == 0) return fl(sum, gs);
    return gs > 0 ? (sum + ((i64)1 << (gs - 1))) >> gs : sum << -gs;
}
struct Mul { int mm, r, g; };
static inline i64 M(const Mul &m, i64 C, i64 V, int s) {
    if (m.mm == 0 || s <= 0) return rnd(C * V, s, m.r);
    if (m.mm == 1) return booth(C, V, s, m.g, m.r);
    return booth(V, C, s, m.g, m.r);
}
struct Hyp { int HL, dv; Mul mx, md, ml; };
static std::string hs(const Hyp &h) {
    char b[160];
    snprintf(b, sizeof b, "HL %d dv %d  x:mm%d r%d g%d  d:mm%d r%d g%d  l:mm%d r%d g%d", h.HL, h.dv, h.mx.mm, h.mx.r,
             h.mx.g, h.md.mm, h.md.r, h.md.g, h.ml.mm, h.ml.r, h.ml.g);
    return b;
}
struct St { i64 L, b; };
static inline void step(const DS &d, const Hyp &h, i64 X8, St &s) {
    int e = d.F >> 9;
    i64 k = 256 + ((d.F & 0x1FF) >> 1), qm = q128[d.Q];
    i64 t1 = M(h.mx, k, (X8 << h.HL) - s.L, 9 + h.HL);
    i64 dd = h.dv == 0 ? M(h.md, k * qm, s.b, 31 - e) : M(h.md, qm, M(h.md, k, s.b, 24 - e), 7);
    s.b = s.b + t1 - dd;
    s.L += M(h.ml, k, s.b, 39 - 2 * e - h.HL);
}
static inline i64 outv(const Hyp &h, i64 L) { return L >> h.HL; }
static const int REST = 3;
static int match(const DS &d, const Hyp &h, int nmax) {
    int e = d.F >> 9;
    int ns = d.on - REST, best = 0, N = std::min(d.N, nmax);
    i64 Bm = (i64)96 << (15 - e);
    i64 fr = (i64)1 << h.HL;
    for (int dn = -1; dn <= 1; dn++)
        for (i64 a = 0; a < fr; a++) {
            i64 L0 = (i64)d.y[ns - 1] * fr + a;
            for (i64 B0 = -Bm; B0 <= Bm; B0++) {
                St s{L0, B0};
                int n = ns;
                for (; n < N; n++) {
                    i64 X8 = (n >= d.on + dn && n < d.on + dn + 256) ? (i64)d.A * 8 : 0;
                    step(d, h, X8, s);
                    if (outv(h, s.L) != d.y[n]) break;
                }
                if (n - ns > best) {
                    best = n - ns;
                    if (n >= N) return best;
                }
            }
        }
    return best;
}

int main(int argc, char **argv) {
    FILE *f = fopen("work/filt/step.bin", "rb");
    if (!f) { perror("work/filt/step.bin"); return 1; }
    int hdr[5];
    while (fread(hdr, 4, 5, f) == 5) {
        DS d{hdr[0], hdr[1], hdr[2], hdr[3], hdr[4], {}};
        d.y.resize(d.N);
        if (fread(d.y.data(), 4, d.N, f) != (size_t)d.N) break;
        ds.push_back(d);
    }
    fclose(f);
    std::vector<int> use;
    for (int i = 1; i < argc; i++) use.push_back(atoi(argv[i]));
    const int W = REST + 256 + 250;
    std::vector<int> hls = {0};
    if (getenv("HLS")) { hls.clear(); for (const char *p = getenv("HLS"); *p; p++) hls.push_back(*p - '0'); }
    std::vector<Mul> muls;
    for (int r = 0; r < 4; r++) muls.push_back({0, r, 0});
    for (int mm = 1; mm <= 2; mm++)
        for (int r = 0; r < 4; r += 3)
            for (int g = 0; g <= 10; g++) muls.push_back({mm, r, g});
    struct R { int score, npass; Hyp h; std::vector<int> m; };
    std::vector<R> res;
    for (int HL : hls)
        for (int dv = 0; dv < 2; dv++)
            for (auto &mx : muls) for (auto &md : muls) for (auto &ml : muls)
                res.push_back({0, 0, Hyp{HL, dv, mx, md, ml}, {}});
    fprintf(stderr, "%zu hypotheses x %zu streams\n", res.size(), use.size());
#pragma omp parallel for schedule(dynamic)
    for (size_t j = 0; j < res.size(); j++) {
        R &rr = res[j];
        for (int i : use) {
            const DS &d = ds[i];
            int lim = d.on - REST + W;
            int m = match(d, rr.h, lim);
            rr.m.push_back(m);
            rr.score += m;
            if (m >= std::min(d.N, lim) - (d.on - REST)) rr.npass++;
            if (m < REST + 20) break;
        }
    }
    std::sort(res.begin(), res.end(), [](const R &a, const R &b) { return a.score > b.score; });
    int top = getenv("TOP") ? atoi(getenv("TOP")) : 30;
    for (int i = 0; i < top && i < (int)res.size(); i++) {
        printf("score %6d pass %2d  %s :", res[i].score, res[i].npass, hs(res[i].h).c_str());
        for (int m : res[i].m) printf(" %d", m);
        printf("\n");
    }
    return 0;
}
