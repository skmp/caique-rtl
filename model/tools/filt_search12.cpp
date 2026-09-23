// filt_search12.cpp -- stored-band filter family with operand truncation (work/filt/step.bin, tools/filt_step.py).
// The band register V holds b 2^(18-e+HB) (b in samples; filt_search9/10: the band precision scales with the cutoff
// exponent), low L is in 2^-(3+HL) sample, k = 256 + FLV[8:1], q = qm/128:
//   P1 = f (x - L) in V units = k (x - Lop) 2^(HB-HL) / 2^9                 rounding r1
//        Lop = L (pl 0) or L cut to 1/8 sample (pl 1 floor, pl 2 toward zero)
//   P2 = f q V:  dv 0: k qm V / 2^(31-e) rounded r3 (exact product)
//                dv 1: (k qm T(V, 15-e) / 2^16 rounded r3) << (15-e)    (V shifted down to e=15 scale first)
//   V' = V + P1 - P2
//   P3 = f b in L units = k V 2^(2e-39-HB+HL):  lv 0: one rounding r4
//                lv 1: k T(V, 15-e) / 2^(24-e+HB-HL) rounded r4           (V shifted down first, T mode tm)
//   L' = L + P3;  y = RO(L)
// T(V, s): V >> s with mode tm (0 floor, 1 toward zero).  Rounding modes: 0 floor, 1 toward zero, 2 half away,
// 3 half up, 6 half even.  REST (env, default 3): rest samples simulated before the input.
// usage: [REST=n] [TOP=n] filt_search12 stream...
// Build: make -C tools filt_search12 (-> build/tools/filt_search12)
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
struct Hyp { int HL, HB, pl, dv, lv, tm, r1, r3, r4, ro; };
static std::string hs(const Hyp &h) {
    char b[160];
    snprintf(b, sizeof b, "HL %d HB %d pl %d dv %d lv %d tm %d r1 %d r3 %d r4 %d ro %d", h.HL, h.HB, h.pl, h.dv, h.lv,
             h.tm, h.r1, h.r3, h.r4, h.ro);
    return b;
}
struct St { i64 L, V; };
static inline void step(const DS &d, const Hyp &h, i64 X8, St &s) {
    int e = d.F >> 9;
    i64 k = 256 + ((d.F & 0x1FF) >> 1), qm = q128[d.Q];
    i64 Lop = h.pl ? rnd(s.L, h.HL, h.pl - 1) << h.HL : s.L;
    i64 p1 = rnd(k * ((X8 << h.HL) - Lop), 9 + h.HL - h.HB, h.r1);
    i64 p2 = h.dv == 0 ? rnd(k * qm * s.V, 31 - e, h.r3)
                       : rnd(k * qm * rnd(s.V, 15 - e, h.tm), 16, h.r3) << (15 - e);
    s.V = s.V + p1 - p2;
    i64 p3 = h.lv == 0 ? rnd(k * s.V, 39 - 2 * e + h.HB - h.HL, h.r4)
                       : rnd(k * rnd(s.V, 15 - e, h.tm), 24 - e + h.HB - h.HL, h.r4);
    s.L += p3;
}
static int REST = 3;
static int match(const DS &d, const Hyp &h, int nmax) {
    int e = d.F >> 9;
    int ns = d.on - REST, best = 0, N = std::min(d.N, nmax);
    i64 Vm = (i64)64 << (15 - e + h.HB);
    i64 fr = (i64)1 << h.HL;
    for (int dn = -1; dn <= 1; dn++)
        for (i64 a = -fr; a < 2 * fr; a++) {
            i64 L0 = (i64)d.y[ns - 1] * fr + a;
            if (rnd(L0, h.HL, h.ro) != d.y[ns - 1]) continue;
            for (i64 V0 = -Vm; V0 <= Vm; V0++) {
                St s{L0, V0};
                int n = ns;
                for (; n < N; n++) {
                    i64 X8 = (n >= d.on + dn && n < d.on + dn + 256) ? (i64)d.A * 8 : 0;
                    step(d, h, X8, s);
                    if (rnd(s.L, h.HL, h.ro) != d.y[n]) break;
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
    if (getenv("REST")) REST = atoi(getenv("REST"));
    std::vector<int> use;
    for (int i = 1; i < argc; i++) use.push_back(atoi(argv[i]));
    const int W = REST + 256 + 250;
    const int modes[] = {0, 1, 2, 3, 6};
    struct R { int score, npass; Hyp h; std::vector<int> m; };
    std::vector<R> res;
    for (int HL = 0; HL <= 2; HL++)
    for (int HB = 0; HB <= 2; HB++)
    for (int pl = 0; pl <= (HL ? 2 : 0); pl++)
    for (int dv = 0; dv < 2; dv++)
    for (int lv = 0; lv < 2; lv++)
    for (int tm = 0; tm < ((dv || lv) ? 2 : 1); tm++)
    for (int r1 : modes) for (int r3 : modes) for (int r4 : modes)
    for (int ro : modes) {
        if (!HL && ro) continue;
        res.push_back({0, 0, Hyp{HL, HB, pl, dv, lv, tm, r1, r3, r4, ro}, {}});
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
