// filt_search7.cpp -- filter structure/rounding search on the DC-to-zero trajectories (work/filt/decay.bin) allowing
// hidden fraction bits in the state: the output is the state truncated/rounded to 1/8 sample, the state keeps
// H more bits (H = 0..5).  Initial state: the DC steady state with unknown sub-LSB parts (searched).
// Structures:
//   0 SVF, products rounded separately:  band += R(fx) - R(f low) - R(fq band); low += R(f band)
//   1 SVF, one rounding of the band increment: band += R(f (x - low - q band)); low += R(f band)
//   2 DF, one rounding: y = R(b0 x + a1 y1 - a2 y2)
//   3 DF, products rounded separately: y = R(b0 x) + R(a1 y1) - R(a2 y2)
//   4 SVF with the band state scaled by 2^-s (s = 15 - e, m = (256 + FLV[8:1])/512):
//       B += R(m (x - low) >> 2s) - R(m q B >> s);  low += R(m B)
//   5 as 4 with x and low products separate: B += R(m x >> 2s) - R(m low >> 2s) - R(m q B >> s); low += R(m B)
//   6 band in units of 2^(s+c)/8 sample (c = H here, may be negative via H-3): k = 256 + FLV[8:1], q = q128/128,
//       B += R(k X >> (9+2s+c)) - R(k L >> (9+2s+c)) - R(k q128 B >> (16+s));  L += R(k B >> (9-c))
//     (X = 8x, L in 1/8 units; the output has no hidden bits in this family)
// All state in 1/2^(3+H) sample units (SVF band in the same units).  Coefficients exact (f = (256 + FLV[8:1]) /
// 2^(24-e), q = q128/128).  Rounding modes 0 floor, 1 toward zero, 2 half away, 3 half up, 4 ceil; output mode ro.
// Build: g++ -O2 -std=c++17 -o work/filt/filt_search7 tools/filt_search7.cpp ; run from caique-rtl/model
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <vector>
#include <algorithm>
#include <cstring>

typedef __int128 i128;
struct DS { int F, Q, A, N; std::vector<int32_t> y; int drop; };
static std::vector<DS> ds;
static const int q128[32] = {192, 176, 160, 144, 128, 120, 112, 104, 96, 88, 80, 72, 64, 60, 56, 52,
                             48, 44, 40, 36, 32, 30, 28, 26, 24, 22, 20, 18, 16, 15, 14, 13};
static inline i128 rnd(i128 v, int sh, int mode) {
    if (sh <= 0) return v << -sh;
    i128 a = v < 0 ? -v : v, r;
    switch (mode) {
    case 0: return v >> sh;
    case 1: r = a >> sh; break;
    case 2: r = (a + ((i128)1 << (sh - 1))) >> sh; break;
    case 3: return (v + ((i128)1 << (sh - 1))) >> sh;
    default: return -((-v) >> sh);
    }
    return v < 0 ? -r : r;
}
struct Hyp { int st, H, r1, r2, r3, r4, ro; };

// coefficients as integers over 2^S
static const int S = 40;
struct Co { i128 f, fq, b0, a1, a2; };
static Co coefs(const DS &d) {
    int e = d.F >> 9, m = d.F & 0x1FF;
    Co c;
    c.f = (i128)(256 + (m >> 1)) << (S - 24 + e);
    c.fq = (c.f * q128[d.Q]) >> 7;
    c.b0 = (c.f * c.f) >> S;
    i128 one = (i128)1 << S;
    c.a1 = 2 * one - c.b0 - c.fq;
    c.a2 = one - c.fq;
    return c;
}

// simulate from state (s1, s2) [SVF: low, band; DF: y1, y2] ; returns matched count
static int sim(const DS &d, const Co &c, const Hyp &h, int n0, i128 s1, i128 s2, int nmax) {
    int N = std::min(d.N, nmax);
    for (int n = 1; n < N; n++) {
        i128 x = (i128)((n < n0) ? d.A : 0) << (3 + h.H);
        i128 out;
        if (h.st == 0) {
            i128 band = s2 + rnd(c.f * x, S, h.r1) - rnd(c.f * s1, S, h.r2) - rnd(c.fq * s2, S, h.r3);
            s1 = s1 + rnd(c.f * band, S, h.r4); s2 = band; out = s1;
        } else if (h.st == 1) {
            i128 band = s2 + rnd(c.f * (x - s1) - c.fq * s2, S, h.r1);
            s1 = s1 + rnd(c.f * band, S, h.r4); s2 = band; out = s1;
        } else if (h.st == 4 || h.st == 5) {
            int e = d.F >> 9, sft = 15 - e;
            i128 m = (i128)(256 + ((d.F & 0x1FF) >> 1));       // m / 512
            i128 qv = q128[d.Q];                                  // q / 128
            i128 band;
            if (h.st == 4) band = s2 + rnd(m * (x - s1), 9 + 2 * sft, h.r1) - rnd(m * qv * s2, 9 + 7 + sft, h.r3);
            else band = s2 + rnd(m * x, 9 + 2 * sft, h.r1) - rnd(m * s1, 9 + 2 * sft, h.r2) - rnd(m * qv * s2, 9 + 7 + sft, h.r3);
            s1 = s1 + rnd(m * band, 9, h.r4); s2 = band; out = s1;
        } else if (h.st == 6) {
            int e = d.F >> 9, sft = 15 - e, cc = h.H - 3;
            i128 k = (i128)(256 + ((d.F & 0x1FF) >> 1)), qv = q128[d.Q];
            i128 X = (i128)((n < n0) ? d.A : 0) * 8;
            i128 band = s2 + rnd(k * X, 9 + 2 * sft + cc, h.r1) - rnd(k * s1, 9 + 2 * sft + cc, h.r2) - rnd(k * qv * s2, 16 + sft, h.r3);
            s1 = s1 + rnd(k * band, 9 - cc, h.r4); s2 = band; out = s1;
            if ((int32_t)out != d.y[n]) return n;
            continue;
        } else if (h.st == 2) {
            i128 y = rnd(c.b0 * x + c.a1 * s1 - c.a2 * s2, S, h.r1);
            s2 = s1; s1 = y; out = y;
        } else {
            i128 y = rnd(c.b0 * x, S, h.r1) + rnd(c.a1 * s1, S, h.r2) - rnd(c.a2 * s2, S, h.r3);
            s2 = s1; s1 = y; out = y;
        }
        if ((int32_t)rnd(out, h.H, h.ro) != d.y[n]) return n;
    }
    return N;
}

static bool pass(const DS &d, const Hyp &h, int nmax) {
    Co c = coefs(d);
    i128 L = h.st == 6 ? (i128)d.y[0] : (i128)d.y[0] << h.H;
    int fr = h.st == 6 ? 1 : 1 << h.H;
    for (int dn = -1; dn <= 1; dn++) {
        int n0 = d.drop + dn;
        if (h.st <= 1 || h.st >= 4) {
            for (int a = 0; a < fr; a++)
                for (int b = -48 * fr; b <= 48 * fr; b++)
                    if (sim(d, c, h, n0, L + a, b, nmax) >= std::min(d.N, nmax)) return true;
        } else {
            for (int a = 0; a < fr; a++)
                for (int b = 0; b < fr; b++)
                    if (sim(d, c, h, n0, L + a, L + b, nmax) >= std::min(d.N, nmax)) return true;
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
        bool skip = d.F == 0x1F55 || d.F == 0x1D55 || d.F == 0x1FFE;
        if (!skip && d.drop < d.N - 10) ds.push_back(d);
    }
    fclose(f);
    int stmin = argc > 1 ? atoi(argv[1]) : 0, stmax = argc > 2 ? atoi(argv[2]) : 3;
    int Hmax = argc > 3 ? atoi(argv[3]) : 3;
    printf("%zu datasets\n", ds.size());
    struct R { int passed; long mask; Hyp h; };
    std::vector<R> res;
    for (int st = stmin; st <= stmax; st++)
    for (int H = 0; H <= Hmax; H++)
    for (int r1 = 0; r1 < 5; r1++) for (int r2 = 0; r2 < (st == 0 || st == 3 || st == 5 || st == 6 ? 5 : 1); r2++)
    for (int r3 = 0; r3 < (st == 0 || st >= 3 ? 5 : 1); r3++) for (int r4 = 0; r4 < (st <= 1 || st >= 4 ? 5 : 1); r4++)
    for (int ro = 0; ro < (H && st != 6 ? 5 : 1); ro++) {
        Hyp h{st, H, r1, r2, r3, r4, ro};
        int passed = 0; long mask = 0;
        for (size_t i = 0; i < ds.size(); i++) {
            if (!pass(ds[i], h, 40)) continue;
            if (pass(ds[i], h, 1 << 30)) { passed++; mask |= 1L << i; }
        }
        if (passed >= 1) res.push_back({passed, mask, h});
    }
    std::sort(res.begin(), res.end(), [](const R &a, const R &b) { return a.passed > b.passed; });
    printf("%zu hypotheses pass >= 4\n", res.size());
    for (int i = 0; i < 25 && i < (int)res.size(); i++) {
        auto &r = res[i];
        printf("passed %2d/%zu mask %04lx: st %d H %d r %d%d%d%d o%d\n", r.passed, ds.size(), r.mask, r.h.st, r.h.H,
               r.h.r1, r.h.r2, r.h.r3, r.h.r4, r.h.ro);
    }
    return 0;
}
