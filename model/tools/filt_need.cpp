// filt_need.cpp -- read the rounding off the captures: forward/backward set tracking in the "any rounding within one
// LSB" family (filt_rule pm1/pm1/pm1), then every sample where the band is pinned (one surviving value before and
// after) gives the exact product values and the results the hardware must have produced.
// Plain SVF, low L = y in 1/8 sample, band b in 2^-u sample (u = 3, or -u scaled: 18 - e):
//     b' = b + R1(k (x - L) / 2^s1) - R2(k qm b / 2^s2);   L' = L + R3(k b' / 2^s3)
// Output per pinned sample: n, x - L, b, b', dy and each product's exact value (numerator, shift) with the result
// forced by the data (R3 always; R1 - R2 as a pair).  Summary: R3 result - floor(v) tabulated by the fraction of v
// (in 1/2^s3 units) and sign; the same for the R1 - R2 combination.
// usage: filt_need [-u 3|4|scaled] [-form 1|2] [-v] stream   (form 2: band increment floor(f ((x - L) - q b)), one product)     Build: make -C tools filt_need (-> build/tools/filt_need)
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <map>
#include <set>
#include <algorithm>
#include <string>

typedef int64_t i64;
struct DS { int F, Q, A, on, N; std::vector<int32_t> y; };
static const int q128[32] = {192, 176, 160, 144, 128, 120, 112, 104, 96, 88, 80, 72, 64, 60, 56, 52,
                             48, 44, 40, 36, 32, 30, 28, 26, 24, 22, 20, 18, 16, 15, 14, 13};
static inline i64 fdiv(i64 N, int S) { return S <= 0 ? N << -S : N >> S; }
static inline bool exact(i64 N, int S) { return S <= 0 || (N & ((1LL << S) - 1)) == 0; }
// "pm1": floor/ceil, also allowing +/-1 at exact integer products
static inline int pm1(i64 N, int S, i64 *o) {
    i64 fl = fdiv(N, S);
    if (exact(N, S)) { o[0] = fl - 1; o[1] = fl; o[2] = fl + 1; return 3; }
    o[0] = fl; o[1] = fl + 1; return 2;
}

static int form = 1, lfloor = 0;   // lfloor: low update floor(f b') instead of pm1 (form 3: increment pm1 of one product)
static int incmode = -1, lowmode = -1, dn = 0, K255 = 511, SPAN = 64;   // dn: input onset offset; K255: mantissa for FLV[8:1] = 0xFF   // -inc floor|ceil|pm1 (one product), -low floor|pm1|fl1
static inline int rule(i64 N, int S, int mode, i64 *o) {   // 0 floor 1 ceil 2 pm1 3 fl1
    i64 fl = fdiv(N, S);
    bool ex = exact(N, S);
    switch (mode) {
    case 0: o[0] = fl; return 1;
    case 1: o[0] = ex ? fl : fl + 1; return 1;
    case 2: return pm1(N, S, o);
    default: if (ex) { o[0] = fl; o[1] = fl - 1; return 2; } o[0] = fl; return 1;
    }
}
static int mode_of(const char *s) { return !strcmp(s, "floor") ? 0 : !strcmp(s, "ceil") ? 1 : !strcmp(s, "pm1") ? 2 : 3; }
// band increment: form 1 = R1(f (x - L)) - R2(f q b), both "pm1"; form 2 = floor(f T), T = (x - L) - q b exact
static inline int incr(i64 k, i64 qm, i64 x, i64 L, i64 b, int s1, int s2, i64 *o) {
    if (incmode >= 0) return rule(k * ((x - L) * (1LL << (s2 - s1)) - qm * b), s2, incmode, o);
    if (form == 2) { o[0] = fdiv(k * ((x - L) * (1LL << (s2 - s1)) - qm * b), s2); return 1; }
    if (form == 3) return pm1(k * ((x - L) * (1LL << (s2 - s1)) - qm * b), s2, o);
    i64 o1[3], o2[3];
    int c1 = pm1(k * (x - L), s1, o1), c2 = pm1(k * qm * b, s2, o2), c = 0;
    for (int i = 0; i < c1; i++) for (int j = 0; j < c2; j++) o[c++] = o1[i] - o2[j];
    return c;
}
int main(int argc, char **argv) {
    int u_in = 3, si = -1, verbose = 0, vm = 0, vr = -1;   // -vm m r: verbose only where b' mod m == r
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-u")) { i++; u_in = !strcmp(argv[i], "scaled") ? -1 : atoi(argv[i]); }
        else if (!strcmp(argv[i], "-v")) verbose = 1;
        else if (!strcmp(argv[i], "-form")) form = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-lfloor")) lfloor = 1;
        else if (!strcmp(argv[i], "-inc")) { incmode = mode_of(argv[++i]); form = 2; }
        else if (!strcmp(argv[i], "-low")) lowmode = mode_of(argv[++i]);
        else if (!strcmp(argv[i], "-dn")) dn = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-k255")) K255 = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-span")) SPAN = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-vm")) { verbose = 1; vm = atoi(argv[++i]); vr = atoi(argv[++i]); }
        else if (!strcmp(argv[i], "-vexact")) { verbose = 1; vm = 0; vr = -2; }   // verbose: exact low products only
        else si = atoi(argv[i]);
    }
    std::vector<DS> ds;
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
    const DS &d = ds[si];
    int e = d.F >> 9, u = u_in < 0 ? 18 - e : u_in;
    i64 k = 256 + ((d.F & 0x1FF) >> 1), qm = q128[d.Q];
    if (k == 511) k = K255;
    int s1 = 27 - e - u, s2 = 31 - e, s3 = 21 - e + u;
    i64 span = (i64)SPAN << std::max(0, u - 3);
    int n0 = d.on - 1, NN = d.N - 1 - n0;
    auto xin = [&](int n) -> i64 { return (n + 1 >= d.on + dn && n + 1 < d.on + dn + 256) ? (i64)d.A * 8 : 0; };
    // forward: sets S[t] (t = 0..NN), S[t] = band values after sample n0 + t
    std::vector<std::set<i64>> S(NN + 1);
    for (i64 b = -span; b <= span; b++) S[0].insert(b);
    int t = 0;
    for (; t < NN; t++) {
        int n = n0 + t;
        i64 L = d.y[n], dy = d.y[n + 1] - L, x = xin(n);
        i64 oi[9], o3[3];
        for (i64 b : S[t]) {
            int ci = incr(k, qm, x, L, b, s1, s2, oi);
            for (int j = 0; j < ci; j++) {
                i64 nb = b + oi[j];
                int c3 = lowmode >= 0 ? rule(k * nb, s3, lowmode, o3) : lfloor ? (o3[0] = fdiv(k * nb, s3), 1) : pm1(k * nb, s3, o3);
                for (int m = 0; m < c3; m++) if (o3[m] == dy) { S[t + 1].insert(nb); break; }
            }
        }
        if (S[t + 1].empty()) { printf("forward set empty at sample %d (+%d)\n", n + 1, t + 1); return 1; }
    }
    // backward pruning
    for (t = NN - 1; t >= 0; t--) {
        int n = n0 + t;
        i64 L = d.y[n], x = xin(n);
        i64 oi[9];
        std::set<i64> keep;
        for (i64 b : S[t]) {
            int ci = incr(k, qm, x, L, b, s1, s2, oi);
            bool ok = false;
            for (int j = 0; j < ci && !ok; j++) {
                i64 nb = b + oi[j], o3[3];
                if (!S[t + 1].count(nb)) continue;
                int c3 = lowmode >= 0 ? rule(k * nb, s3, lowmode, o3) : lfloor ? (o3[0] = fdiv(k * nb, s3), 1) : pm1(k * nb, s3, o3);
                for (int m = 0; m < c3; m++) if (o3[m] == d.y[n + 1] - L) ok = true;
            }
            if (ok) keep.insert(b);
        }
        S[t] = keep;
    }
    // exact low products: which state feature decides result v vs v - 1 (feature -> {value: [count v-1, count v]})
    std::map<std::string, std::map<i64, std::pair<int, int>>> feat;
    // pinned samples
    std::map<std::pair<i64, int>, std::map<i64, int>> tab3;      // (fraction of v3 in 1/2^s3, sign) -> {r3 - floor: count}
    std::map<std::pair<i64, i64>, std::map<i64, int>> tab12;     // (frac1, frac2) -> {(r1 - r2) - (fl1 - fl2): count}
    std::map<std::pair<i64, int>, std::map<i64, int>> tabI;      // form 3: (fraction of k T / 2^s2, sign) -> {r - floor}
    std::map<i64, std::map<i64, int>> operand_results;
    int pinned = 0, width1 = 0;
    for (t = 0; t < NN; t++) {
        if (S[t].size() == 1) width1++;
        if (S[t].size() != 1 || S[t + 1].size() != 1) continue;
        pinned++;
        int n = n0 + t;
        i64 L = d.y[n], dy = d.y[n + 1] - L, x = xin(n), b = *S[t].begin(), nb = *S[t + 1].begin();
        operand_results[nb].emplace(dy, n);
        i64 N1 = k * (x - L), N2 = k * qm * b, N3 = k * nb;
        i64 m3 = (1LL << std::max(s3, 0)) - 1;
        tab3[{s3 > 0 ? (N3 & m3) : 0, N3 < 0}][dy - fdiv(N3, s3)]++;
        i64 m1 = (1LL << std::max(s1, 0)) - 1, m2 = (1LL << std::max(s2, 0)) - 1;
        i64 comb = (nb - b) - (fdiv(N1, s1) - fdiv(N2, s2));
        tab12[{s1 > 0 ? (N1 & m1) : 0, s2 > 0 ? (N2 & m2) : 0}][comb]++;
        i64 Tn = (x - L) * (1LL << (s2 - s1)) - qm * b;      // form 2 increment numerator: k Tn / 2^s2
        tabI[{(k * Tn) & ((1LL << s2) - 1), k * Tn < 0}][(nb - b) - fdiv(k * Tn, s2)]++;
        if (exact(N3, s3)) {
            int out = (int)(dy - fdiv(N3, s3));   // -1 or 0
            i64 inc = k * Tn, fr = inc & ((1LL << s2) - 1);
            auto add = [&](const char *nm, i64 v) { auto &p = feat[nm][v]; (out < 0 ? p.first : p.second)++; };
            add("sign b'", nb < 0); add("sign b", b < 0); add("sign x-L", (x - L) < 0); add("sign inc", inc < 0);
            add("inc exact", fr == 0); add("inc frac>=1/2", fr >= (1LL << (s2 - 1))); add("L mod 2", L & 1);
            add("L mod 8", L & 7); add("b mod 8", b & 7); add("b' mod 16", nb & 15); add("x on", x != 0);
            add("sign L", L < 0); add("sign dy", dy < 0); add("dy==0", dy == 0); add("b'-b sign", nb - b < 0);
            add("Tn mod 128", Tn & 127); add("(x-L) mod 8", (x - L) & 7);
            add("qm b mod 128 (q b frac)", (qm * b) & 127);
        }
        if (verbose && (!vm || ((nb % vm) + vm) % vm == vr) && (vr != -2 || exact(N3, s3)))
            printf("inc %lld/2^%d (frac %lld) ", (long long)(k * Tn), s2, (long long)((k * Tn) & ((1LL << s2) - 1))),
            printf("n %5d x-L %6lld b %6lld b' %6lld dy %5lld | v1 %lld/2^%d v2 %lld/2^%d v3 %lld/2^%d | r3-fl %lld  (r1-r2)-(fl1-fl2) %lld\n",
                   n, (long long)(x - L), (long long)b, (long long)nb, (long long)dy, (long long)N1, s1, (long long)N2, s2,
                   (long long)N3, s3, (long long)(dy - fdiv(N3, s3)), (long long)comb);
    }
    printf("stream %d F %04x Q %d A %d u %d: s1 %d s2 %d s3 %d; samples %d, band pinned at %d, pinned transitions %d\n",
           si, d.F, d.Q, d.A, u, s1, s2, s3, NN, width1, pinned);
    int conflicts = 0;
    for (const auto &entry : operand_results) if (entry.second.size() > 1) {
        conflicts++;
        if (conflicts <= 16) {
            printf("OPERAND CONFLICT b'=%lld:", (long long)entry.first);
            for (auto &r : entry.second) printf(" dy=%lld at n=%d", (long long)r.first, r.second);
            printf("\n");
        }
    }
    printf("distinct pinned low operands %zu, conflicting operands %d\n", operand_results.size(), conflicts);
    printf("exact low products: feature -> value [v-1 count / v count]\n");
    for (auto &f : feat) {
        int pure = 0, tot = 0;
        for (auto &v : f.second) { pure += std::max(v.second.first, v.second.second); tot += v.second.first + v.second.second; }
        printf("  %-24s purity %d/%d:", f.first.c_str(), pure, tot);
        if (f.second.size() <= 16) for (auto &v : f.second) printf(" %lld[%d/%d]", (long long)v.first, v.second.first, v.second.second);
        printf("\n");
    }
    printf("R3 (f b' -> low): fraction/sign -> {result - floor: count}\n");
    for (auto &kv : tab3) {
        printf("  frac %5lld %s:", (long long)kv.first.first, kv.first.second ? "neg" : "pos");
        for (auto &c : kv.second) printf(" %+lld:%d", (long long)c.first, c.second);
        printf("\n");
    }
    if (form >= 2) {
        if (incmode >= 0) return 0;
        printf("single-product increment f T: (fraction in 1/2^%d, sign) -> {result - floor: count}\n", s2);
        for (auto &kv : tabI) {
            printf("  frac %7lld %s:", (long long)kv.first.first, kv.first.second ? "neg" : "pos");
            for (auto &c : kv.second) printf(" %+lld:%d", (long long)c.first, c.second);
            printf("\n");
        }
        return 0;
    }
    printf("R1 - R2 (band increment): (frac1, frac2) -> {excess over floor-floor: count}\n");
    for (auto &kv : tab12) {
        printf("  f1 %5lld f2 %5lld:", (long long)kv.first.first, (long long)kv.first.second);
        for (auto &c : kv.second) printf(" %+lld:%d", (long long)c.first, c.second);
        printf("\n");
    }
    return 0;
}
