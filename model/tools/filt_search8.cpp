// filt_search8.cpp -- slot filter structure/rounding search with hidden precision in both state variables, on the
// DC-to-zero trajectories (work/filt/decay.bin).  low has HL bits below the 1/8-sample output LSB (output =
// RO(low >> HL)); band is kept in units of 1/2^(3+HB) sample (HB may be negative: coarser than the output).
// Structures (f, q exact; d = 1 - f q):
//   0 SVF separate:   band' = band + R1(f x) - R2(f low) - R3(f q band);          low' = low + R4(f band')
//   1 SVF single:     band' = band + R1(f (x - low) - f q band);                 low' = low + R4(f band')
//   2 SVF damp-mult:  band' = R3(d band) + R1(f (x - low));                      low' = low + R4(f band')
//   3 SVF damp-sep:   band' = R3(d band) + R1(f x) - R2(f low);                  low' = low + R4(f band')
// (x, low converted to band units exactly before the products; f band' converted to low units by the product shift.)
// Initial state: low = L0 << HL + hidden part, band chosen so that (low, band) is a fixed point under the DC input
// (only such states are tried).  A hypothesis passes a dataset if a fixed-point start + switch point reproduces it.
// Build: g++ -O2 -std=c++17 -o work/filt/filt_search8 tools/filt_search8.cpp ; run from caique-rtl/model
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
struct Hyp { int st, HL, HB, r1, r2, r3, r4, ro; };
static const int S = 40;
struct Co { i128 f, fq, d; };
static Co coefs(const DS &dd) {
    int e = dd.F >> 9, m = dd.F & 0x1FF;
    Co c;
    c.f = (i128)(256 + (m >> 1)) << (S - 24 + e);
    c.fq = (c.f * q128[dd.Q]) >> 7;
    c.d = ((i128)1 << S) - c.fq;
    return c;
}
// band units u_b = 2^-(3+HB) sample, low units u_l = 2^-(3+HL).  Terms for band' in band units:
//   f * x_samples / u_b = f * X8 * 2^HB  (X8 = 8x)          -> product (f X8) with shift S - HB
//   f * low / u_b       = f * low * 2^(HB-HL)              -> shift S - (HB - HL)
//   f*band' in low units = f band' 2^(HL-HB)               -> shift S - (HL - HB)
static inline void step(const Co &c, const Hyp &h, i128 X8, i128 &low, i128 &band) {
    i128 nb;
    int sx = S - h.HB, sl = S - (h.HB - h.HL);
    switch (h.st) {
    case 0: nb = band + rnd(c.f * X8, sx, h.r1) - rnd(c.f * low, sl, h.r2) - rnd(c.fq * band, S, h.r3); break;
    case 1: {
        // common scale: everything in band units * 2^S ... use low scaled into band units with extra precision
        int sh = S + 8;
        i128 t = c.f * (X8 << (h.HB + 8)) - c.f * (h.HB - h.HL + 8 >= 0 ? low << (h.HB - h.HL + 8) : low >> -(h.HB - h.HL + 8)) - ((c.fq * band) << 8);
        nb = band + rnd(t, sh, h.r1);
        break;
    }
    case 2: {
        int sh = S + 8;
        i128 t = c.f * (X8 << (h.HB + 8)) - c.f * (h.HB - h.HL + 8 >= 0 ? low << (h.HB - h.HL + 8) : low >> -(h.HB - h.HL + 8));
        nb = rnd(c.d * band, S, h.r3) + rnd(t, sh, h.r1);
        break;
    }
    default:
        nb = rnd(c.d * band, S, h.r3) + rnd(c.f * X8, sx, h.r1) - rnd(c.f * low, sl, h.r2);
        break;
    }
    low = low + rnd(c.f * nb, S - (h.HL - h.HB), h.r4);
    band = nb;
}

static int sim(const DS &d, const Co &c, const Hyp &h, int n0, i128 low, i128 band, int nmax) {
    int N = std::min(d.N, nmax);
    for (int n = 1; n < N; n++) {
        i128 X8 = (i128)((n < n0) ? d.A : 0) * 8;
        step(c, h, X8, low, band);
        if ((int32_t)rnd(low, h.HL, h.ro) != d.y[n]) return n;
    }
    return N;
}

static bool pass(const DS &d, const Hyp &h, int nmax, const Co &c) {
    int fr = 1 << h.HL;
    i128 X8 = (i128)d.A * 8;
    int brange = 64 * (h.HB >= 0 ? (1 << h.HB) : 1);
    for (int a = 0; a < fr; a++) {
        i128 low0 = ((i128)d.y[0] << h.HL) + a;
        if ((int32_t)rnd(low0, h.HL, h.ro) != d.y[0]) continue;
        for (int b = -brange; b <= brange; b++) {
            i128 l = low0, bb = b;
            step(c, h, X8, l, bb);
            if (l != low0 || bb != b) continue; // not a fixed point under DC
            for (int dn = -1; dn <= 1; dn++)
                if (sim(d, c, h, d.drop + dn, low0, b, nmax) >= std::min(d.N, nmax)) return true;
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
    printf("%zu datasets\n", ds.size());
    struct R { int passed; long mask; Hyp h; };
    std::vector<R> res;
    std::vector<Co> co;
    for (auto &d : ds) co.push_back(coefs(d));
    for (int st = stmin; st <= stmax; st++)
    for (int HL = 0; HL <= 3; HL++)
    for (int HB = -2; HB <= 3; HB++)
    for (int r1 = 0; r1 < 5; r1++) for (int r2 = 0; r2 < (st == 0 || st == 3 ? 5 : 1); r2++)
    for (int r3 = 0; r3 < (st == 1 ? 1 : 5); r3++) for (int r4 = 0; r4 < 5; r4++)
    for (int ro = 0; ro < (HL ? 5 : 1); ro++) {
        Hyp h{st, HL, HB, r1, r2, r3, r4, ro};
        int passed = 0; long mask = 0;
        for (size_t i = 0; i < ds.size(); i++) {
            if (!pass(ds[i], h, 30, co[i])) continue;
            if (pass(ds[i], h, 1 << 30, co[i])) { passed++; mask |= 1L << i; }
        }
        if (passed >= 5) res.push_back({passed, mask, h});
    }
    std::sort(res.begin(), res.end(), [](const R &a, const R &b) { return a.passed > b.passed; });
    printf("%zu hypotheses pass >= 5\n", res.size());
    for (int i = 0; i < 25 && i < (int)res.size(); i++) {
        auto &r = res[i];
        printf("passed %2d/%zu mask %04lx: st %d HL %d HB %d r %d%d%d%d o%d\n", r.passed, ds.size(), r.mask, r.h.st,
               r.h.HL, r.h.HB, r.h.r1, r.h.r2, r.h.r3, r.h.r4, r.h.ro);
    }
    return 0;
}
