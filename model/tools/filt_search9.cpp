// filt_search9.cpp -- slot filter rounding search on the full filt_cyc step responses (rest -> 256 samples of DC ->
// zeros), exported by tools/filt_step.py to work/filt/step.bin (per stream: F, Q, A, on, N, int32 y[N]; y = -MIXS/2
// in 1/8 sample units; `on` = first sample whose output moves).
// State: low in units 2^-(3+HL) sample, band in 2^-(3+HB) sample (HB < 0: coarser than the output).  f = k 2^(e-24)
// (k = 256 + FLV[8:1]), q = q128/128.  Products are rounded to the destination unit with one rounding mode for all
// products (one shared multiplier) unless SEP is given; the output is y = RO(low) at 1/8 sample.
//   order 0 (band first):  band' = band + [f (x - low)] - [f q band];  low' = low + [f band']
//   order 1 (low first):   low'  = low + [f band];  band' = band + [f (x - low')] - [f q band]
//   order 2 (simultaneous): low' = low + [f band];  band' = band + [f (x - low)] - [f q band]
//   form 0: f x and f low rounded separately; form 1: f (x - low) one product; form 2: whole band increment one rounding
//   qf 0: f q one coefficient (exact 13-bit product); qf 1: q band rounded first (to band units), then f
// Rounding modes: 0 floor, 1 toward zero, 2 half away from zero, 3 half up, 4 ceil, 5 away from zero, 6 half even.
// The unknown start state (low hidden bits, band) is searched; the simulation starts REST samples before `on` and the
// input switches on at on + d (d = -1..1, order 1 also -2) and off 256 samples later.
// usage: filt_search9 [stream list, default all]   (run from caique-rtl/model)
// Build: make -C tools filt_search9 (-> build/tools/filt_search9)
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
struct Hyp { int HL, HB, order, form, qf, rm[4], ro; int scaled; int pl; };
static std::string hs(const Hyp &h) {
    char b[128];
    snprintf(b, sizeof b, "pl %d %sHL %d HB %d order %d form %d qf %d r %d%d%d%d ro %d", h.pl, h.scaled ? "scaled " : "", h.HL, h.HB, h.order, h.form, h.qf,
             h.rm[0], h.rm[1], h.rm[2], h.rm[3], h.ro);
    return b;
}
struct St { i64 low, band; };
static inline void step(const DS &d, const Hyp &h0, i64 X8, St &s) {
    int e = d.F >> 9;
    Hyp h = h0;
    if (h.scaled) h.HB = 15 - e + h0.HB;
    i64 k = 256 + ((d.F & 0x1FF) >> 1), qm = q128[d.Q];
    int sh = 24 - e;
    auto bandupd = [&](i64 low) -> i64 {
        i64 qt;
        if (h.pl) low = rnd(low, h.HL, h.pl - 1) << h.HL;   // pl > 0: the product sees low cut to 1/8 (mode pl - 1)
        if (h.form == 2) {
            // band units: [k (x - low) 2^(HB-HL) - k qm band 2^-7] 2^-sh, numerator scaled by 2^(7+HL)
            i64 xl = (X8 << h.HL) - low;
            return s.band + rnd(((k * xl) << (h.HB + 7 + 2)) - ((k * qm * s.band) << (h.HL + 2)), sh + 7 + h.HL + 2 + 0, h.rm[0]) ;
        }
        if (h.qf == 0) qt = rnd(k * qm * s.band, sh + 7, h.rm[2]);
        else qt = rnd(k * rnd(qm * s.band, 7, h.rm[2]), sh, h.rm[2]);
        if (h.form == 0)
            return s.band + rnd(k * X8, sh - h.HB, h.rm[0]) - rnd(k * low, sh - h.HB + h.HL, h.rm[1]) - qt;
        return s.band + rnd(k * ((X8 << h.HL) - low), sh - h.HB + h.HL, h.rm[0]) - qt;
    };
    if (h.order == 0) {
        s.band = bandupd(s.low);
        s.low += rnd(k * s.band, sh + h.HB - h.HL, h.rm[3]);
    } else if (h.order == 1) {
        s.low += rnd(k * s.band, sh + h.HB - h.HL, h.rm[3]);
        s.band = bandupd(s.low);
    } else {
        i64 nl = s.low + rnd(k * s.band, sh + h.HB - h.HL, h.rm[3]);
        s.band = bandupd(s.low);
        s.low = nl;
    }
}
static inline int32_t outv(const Hyp &h, i64 low) { return (int32_t)rnd(low, h.HL, h.ro); }

// max matched length over start states / input offsets, from sample on - REST up to nmax
static const int REST = getenv("REST") ? atoi(getenv("REST")) : 3;
static int match(const DS &d, const Hyp &h, int nmax) {
    int ns = d.on - REST, best = 0;
    int HBd = h.scaled ? 15 - (d.F >> 9) + h.HB : h.HB;
    int N = std::min(d.N, nmax);
    i64 fr = (i64)1 << h.HL;
    i64 Bm = (i64)48 << std::max(HBd, 0);
    for (int dn = (h.order ? -2 : -1); dn <= 1; dn++)
        for (i64 a = -fr; a < 2 * fr; a++) {
            i64 L0 = (i64)d.y[ns - 1] * fr + a;
            if (outv(h, L0) != d.y[ns - 1]) continue;
            for (i64 b = -Bm; b <= Bm; b++) {
                St s{L0, b};
                int n = ns;
                for (; n < N; n++) {
                    i64 X8 = (n >= d.on + dn && n < d.on + dn + 256) ? (i64)d.A * 8 : 0;
                    step(d, h, X8, s);
                    if (outv(h, s.low) != d.y[n]) break;
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
    if (use.empty()) for (int i = 0; i < (int)ds.size(); i++) use.push_back(i);
    const int W = REST + 256 + 250;   // window: rest, rise+settle, fall+settle
    struct R { int score, npass; Hyp h; std::vector<int> m; };
    std::vector<R> res;
    bool sep = getenv("SEP") != nullptr;   // SEP=1: an independent rounding mode per product (qf 0 only)
    int scaled = getenv("SCALED") != nullptr;   // SCALED=1: band unit 2^-(3 + 15 - e + HB) sample (exponent-scaled)
    int hlmax = getenv("HLMAX") ? atoi(getenv("HLMAX")) : 4;
    int hbmin = scaled ? -2 : -2, hbmax = scaled ? 3 : 4;
    for (int HL = 0; HL <= hlmax; HL++)
    for (int HB = hbmin; HB <= hbmax; HB++)
    for (int order = 0; order < 3; order++)
    for (int form = 0; form < 3; form++)
    for (int qf = 0; qf < (form == 2 || sep ? 1 : 2); qf++)
    for (int r = 0; r < 7; r++)
    for (int r1 = (sep ? 0 : r); r1 <= (sep ? 6 : r); r1++)
    for (int r2 = (sep && form == 0 ? 0 : r); r2 <= (sep && form == 0 ? 6 : r); r2++)
    for (int r3 = (sep && form < 2 ? 0 : r); r3 <= (sep && form < 2 ? 6 : r); r3++)
    for (int ro = 0; ro < (HL ? 7 : 1); ro++)
    for (int pl = 0; pl <= (HL && getenv("PL") ? 7 : 0); pl++)
        res.push_back({0, 0, Hyp{HL, HB, order, form, qf, {r1, r2, r3, r}, ro, scaled, pl}, {}});
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
            if (m < REST + 20) break;   // hopeless on this stream: skip the rest
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
