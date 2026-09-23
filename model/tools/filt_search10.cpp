// filt_search10.cpp -- normalized-band SVF rounding search on the filt_cyc step responses (work/filt/step.bin from
// tools/filt_step.py).  filt_search9 showed the band state's precision scales with the cutoff exponent (unit
// 2^-(18-e) sample), i.e. the stored band is B = band / f in fixed units.  Family searched (f = k 2^(e-24),
// k = 256 + FLV[8:1], q = qm/128, L = low in 1/8 sample, B in 2^-(2+cB) sample):
//   B' = B + R1((x - L) / 2^(1-cB)) - D(B)            D = damping f q B (in B units), variants dv:
//        dv 0: R3(f q B) one product            dv 1: R3b(q R3(f B)) with f B rounded to B units first
//        dv 2: R3(q b) with b = the stored/cached f*B (unit ub, below) from the previous sample
//   L' = L + U(B')                                 U = f^2 B' in L units, variants lv:
//        lv 0: R4(f^2 B') one rounding      lv 1: b' = R2(f B') to unit ub = 2^-(3+cb) sample, then R4(f b')
//   order 0: B first (uses old L), then L;  order 1: L first (uses old B), then B (uses new L)
// Rounding modes: 0 floor, 1 toward zero, 2 half away, 3 half up, 4 ceil, 5 away from zero, 6 half even.
// Output y = L.  Start: L0 = y[on-REST-1] + a (a = 0 only: L has no hidden bits here), B0 searched.
// usage: [TOP=n] filt_search10 stream...      Build: make -C tools filt_search10 (-> build/tools/filt_search10)
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
    i64 a = v < 0 ? -v : v, r, h = (i64)1 << (sh - 1), m = ((i64)1 << sh) - 1;
    switch (mode) {
    case 0: return v >> sh;
    case 1: r = a >> sh; break;
    case 2: r = (a + h) >> sh; break;
    case 3: return (v + h) >> sh;
    case 4: return -((-v) >> sh);
    case 5: r = (a + m) >> sh; break;
    default: {
        i64 fl = v >> sh, fr = v & m;
        if (fr > h || (fr == h && (fl & 1))) fl++;
        return fl;
    }
    }
    return v < 0 ? -r : r;
}
struct Hyp { int cB, cb, dv, lv, order, r1, r2, r3, r4, HL, ro; };
static std::string hs(const Hyp &h) {
    char b[128];
    snprintf(b, sizeof b, "HL %d ro %d cB %d cb %d dv %d lv %d order %d r %d%d%d%d", h.HL, h.ro, h.cB, h.cb, h.dv, h.lv,
             h.order, h.r1, h.r2, h.r3, h.r4);
    return b;
}
struct St { i64 L, B, b; };
// units: L 2^-3, B 2^-(2+cB), b 2^-(3+cb) sample; f = k 2^(e-24)
struct Ctx { i64 k, qm; int sh; };
static inline i64 fB_to_b(const Ctx &c, const Hyp &h, i64 B) {  // f B in b units: k B 2^(e-24) 2^-(2+cB) / 2^-(3+cb)
    return rnd(c.k * B, c.sh + h.cB - 1 - h.cb, h.r2);
}
static inline void step(const Ctx &c, const Hyp &h, i64 X8, St &s) {
    auto bupd = [&](i64 L) {
        i64 xl = (X8 << h.HL) - L;                         // L units -> B units: * 2^(cB-1-HL)
        i64 t1 = rnd(xl, 1 - h.cB + h.HL, h.r1);
        i64 d;
        if (h.dv == 0) d = rnd(c.k * c.qm * s.B, c.sh + 7, h.r3);
        else if (h.dv == 1) d = rnd(c.qm * rnd(c.k * s.B, c.sh, h.r3), 7, h.r3);
        else d = rnd(c.qm * s.b, 7 + h.cb - h.cB - 1, h.r3);   // q b: b units -> B units (2^-(3+cb) / 2^-(2+cB))
        s.B = s.B + t1 - d;
    };
    auto lupd = [&]() {
        if (h.lv == 0) {
            // f^2 B in 1/8 units: k^2 B 2^(2e-48) 2^-(2+cB) / 2^-3
            s.L += rnd(c.k * c.k * s.B, 2 * c.sh + h.cB - 1 - h.HL, h.r4);
        } else {
            s.b = fB_to_b(c, h, s.B);
            s.L += rnd(c.k * s.b, c.sh - h.cb - h.HL, h.r4); // f b: k b 2^(e-24) 2^-(3+cb) / 2^-(3+HL)
        }
    };
    if (h.order == 0) { bupd(s.L); lupd(); }
    else { i64 L0 = s.L; lupd(); (void)L0; bupd(s.L); }
    if (h.lv == 0 && h.dv == 2) s.b = fB_to_b(c, h, s.B);
}

static inline i64 outv(const Hyp &h, i64 L) {
    if (h.ro == 0) return L >> h.HL;                      // floor
    i64 m = (L < 0 ? -L : L) >> h.HL;                     // magnitude truncated, negative read as one's complement
    return L < 0 ? -m - 1 : m;
}
static const int REST = 3;
static int match(const DS &d, const Hyp &h, int nmax) {
    Ctx c{256 + ((d.F & 0x1FF) >> 1), q128[d.Q], 24 - (d.F >> 9)};
    int e = d.F >> 9;
    int ns = d.on - REST, best = 0, N = std::min(d.N, nmax);
    i64 Bm = (i64)96 << (15 - e + h.cB);
    i64 fr = (i64)1 << h.HL;
    for (int dn = -1; dn <= 1; dn++)
      for (i64 a = -fr; a < 2 * fr; a++) {
        i64 L0 = (i64)d.y[ns - 1] * fr + a;
        if (outv(h, L0) != d.y[ns - 1]) continue;
        for (i64 B0 = -Bm; B0 <= Bm; B0++) {
            St s{L0, B0, 0};
            s.b = fB_to_b(c, h, B0);
            int n = ns;
            for (; n < N; n++) {
                i64 X8 = (n >= d.on + dn && n < d.on + dn + 256) ? (i64)d.A * 8 : 0;
                step(c, h, X8, s);
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
    struct R { int score, npass; Hyp h; std::vector<int> m; };
    std::vector<R> res;
    // MODES: rounding modes to try (default all 7); HLS: hidden low bits list; single-product variants only unless ALL
    std::vector<int> modes = {0, 1, 2, 3, 4, 5, 6}, hls = {0};
    if (getenv("MODES")) { modes.clear(); for (const char *p = getenv("MODES"); *p; p++) modes.push_back(*p - '0'); }
    if (getenv("HLS")) { hls.clear(); for (const char *p = getenv("HLS"); *p; p++) hls.push_back(*p - '0'); }
    bool all = getenv("ALL") != nullptr;
    for (int HL : hls)
    for (int ro = 0; ro < 2; ro++)
    for (int cB = 0; cB <= 2; cB++)
    for (int dv = 0; dv < (all ? 3 : 1); dv++)
    for (int lv = 0; lv < (all ? 2 : 1); lv++)
    for (int cb = 0; cb <= ((lv == 1 || dv == 2) ? 8 : 0); cb++)
    for (int order = 0; order < (all ? 2 : 1); order++)
    for (int r1 : modes) for (int r2 : modes) for (int r3 : modes) for (int r4 : modes) {
        if ((cB > HL + 1 || cB == HL + 1) && r1 != modes[0]) continue;           // (x - L) exact in B units
        if (!(lv == 1 || dv == 2) && r2 != modes[0]) continue;
        res.push_back({0, 0, Hyp{cB, cb, dv, lv, order, r1, r2, r3, r4, HL, ro}, {}});
    }
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
