// filt_rule.cpp -- exact test of per-product rounding RULES for the slot filter (replaces filt_rule.py/filt_bound.py).
// Plain Chamberlin SVF, low L in 1/8 sample (default: no hidden bits, so L = y, the captured output), band b in
// 2^-u sample (u = 3 same as low; "scaled": u = 18 - e):
//     b' = b + P1(f (x - L)) - P2(f q b)      [form 1]   or   b + P1(f x) - P1(f L) - P2(f q b)   [form 0]
//     or b' = b + P1(f * T), T = (x - L) - q b exact  [form 2: one multiply for the whole increment, rule r1; r2 unused]
//     form 4: as 2, but the low update multiplies the UNROUNDED band sum b + f T (r3); the stored band is r1 of it
//     form 5: D = 2^qshift * R2(q*b/2^qshift), b' = b + R1(f*((x-L)-D));
//             measured rule: u=3, qshift=1, floor/ceil/ceil (coarse damping before cutoff multiply)
//     L' = L + P3(f b')
// f = k 2^(e-24), k = 256 + FLV[8:1], q = qm/128.  Every exact product is N / 2^S (integers), so rules are integer ops.
// A rule maps the exact value v to the SET of allowed integer results; since L is known at every sample, the set of
// band values consistent with the capture is tracked exactly.  A set-valued rule (fl1, ce1, pm1, ...) tests a whole
// family at once: passing means SOME per-sample choice within the family reproduces the capture.
// Rules:  floor  ceil  tz  away  he (half even)  c<n> floor(v + n/8)  t<n> toward zero, magnitude + n/8
//         ceilm1 ceil(v) - 1 (floor of v minus a hair)
//         fl1 [v-1, v]  ce1 [v, v+1]  tzb (fl1 for v >= 0, ce1 below)  awb (the reverse)  pm1 [v-1, v+1]  near |r-v|<=1/2
//         bV<g> / hV<g>: truncated radix-4 Booth array, the DATA operand recoded, g guard columns, floor / half up
//         bC<g> / hC<g>: the same with the coefficient recoded
//         aVtt / aCtt / AVtt / ACtt: truncation at the ABSOLUTE column tt (two digits) instead of S - g
// Options: -span n initial band range +-n (default 64); -k255 n mantissa for FLV[8:1] = 0xFF (default 511); -hl n hidden low bits (output floor, or -ro 1 sign-magnitude read as one's complement); -lop 1: the
// f (x - L) product sees low cut to 1/8.
// usage: filt_rule [-u 3|4|scaled] [-form 0|1|2|4|5] [-qshift n] [-hl n] [-ro r] [-lop 1] [-top n] -set r,r,.. | -rules r1,r2,r3  stream...
// Build: g++ -O2 -fopenmp -std=c++17 -o work/filt/filt_rule tools/filt_rule.cpp   (run from caique-rtl/model)
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>

typedef __int128 i128;
typedef int64_t i64;
struct DS { int F, Q, A, on, N; std::vector<int32_t> y; };
static std::vector<DS> ds;
static const int q128[32] = {192, 176, 160, 144, 128, 120, 112, 104, 96, 88, 80, 72, 64, 60, 56, 52,
                             48, 44, 40, 36, 32, 30, 28, 26, 24, 22, 20, 18, 16, 15, 14, 13};

enum Kind { FLOOR, CEIL, TZ, AWAY, HE, CB, TB, CEILM1, FL1, CE1, TZB, AWB, PM1, NEAR, BV, BC, HV, HC, AV, AC, AHV, AHC };
struct Rule { Kind k; int n; std::string name; };
static Rule parse_rule(const std::string &s) {
    static const struct { const char *nm; Kind k; } tab[] = {
        {"floor", FLOOR}, {"ceil", CEIL}, {"tz", TZ}, {"away", AWAY}, {"he", HE}, {"ceilm1", CEILM1}, {"fl1", FL1},
        {"ce1", CE1}, {"tzb", TZB}, {"awb", AWB}, {"pm1", PM1}, {"near", NEAR}};
    for (auto &t : tab) if (s == t.nm) return {t.k, 0, s};
    if (s.size() == 2 && s[0] == 'c' && isdigit(s[1])) return {CB, s[1] - '0', s};
    if (s.size() == 2 && s[0] == 't' && isdigit(s[1])) return {TB, s[1] - '0', s};
    if (s.size() == 3 && s[0] == 'b' && s[1] == 'V' && isdigit(s[2])) return {BV, s[2] - '0', s};
    if (s.size() == 3 && s[0] == 'b' && s[1] == 'C' && isdigit(s[2])) return {BC, s[2] - '0', s};
    if (s.size() == 3 && s[0] == 'h' && s[1] == 'V' && isdigit(s[2])) return {HV, s[2] - '0', s};
    if (s.size() == 3 && s[0] == 'h' && s[1] == 'C' && isdigit(s[2])) return {HC, s[2] - '0', s};
    // absolute truncation column: aVtt / aCtt (floor at S), AVtt / ACtt (half up at S), tt = two digits
    if (s.size() == 4 && (s[0] == 'a' || s[0] == 'A') && (s[1] == 'V' || s[1] == 'C') && isdigit(s[2]) && isdigit(s[3])) {
        Kind kk = s[0] == 'a' ? (s[1] == 'V' ? AV : AC) : (s[1] == 'V' ? AHV : AHC);
        return {kk, (s[2] - '0') * 10 + (s[3] - '0'), s};
    }
    fprintf(stderr, "bad rule %s\n", s.c_str());
    exit(1);
}
static inline i128 fdiv(i128 N, int S) { return S <= 0 ? N << -S : N >> S; }       // floor(N / 2^S)
static inline i128 cdiv(i128 N, int S) { return -fdiv(-N, S); }                     // ceil
// radix-4 Booth product P * R / 2^S with the recoded operand R: rows d_j P 4^j, a negative row as ~(|d| P) << 2j plus
// a hot one at column 2j; columns below t = S - g are dropped (the hot one too when 2j < t); the kept sum is rounded
// at column S (half: + 2^(S-1) first)
static inline i128 booth(i128 P, i128 R, int S, int g, bool half, int tabs = -1) {
    int t = tabs >= 0 ? (tabs > S ? S : tabs) : (S - g < 0 ? 0 : S - g);
    i128 sum = 0;
    int prev = 0;
    for (int j = 0; j < 24; j++) {
        int b0 = (int)((R >> (2 * j)) & 1), b1 = (int)((R >> (2 * j + 1)) & 1);
        int d = -2 * b1 + b0 + prev;
        prev = b1;
        if (!d) continue;
        i128 m = (d < 0 ? -d : d) * P;
        if (d > 0) sum += fdiv(m << (2 * j), t);
        else {
            sum += fdiv((~m) << (2 * j), t);
            if (2 * j >= t) sum += (i128)1 << (2 * j - t);
        }
    }
    int gs = S - t;
    if (half && gs > 0) sum += (i128)1 << (gs - 1);
    return fdiv(sum, gs);
}
// allowed results of v = N / 2^S under rule r; returns count, results in out[]
static inline int outs(i128 N, int S, const Rule &r, i64 *out) {
    if (S < 0) { N <<= -S; S = 0; }
    i128 fl = fdiv(N, S);
    bool exact = S == 0 || (N & (((i128)1 << S) - 1)) == 0;
    i128 ce = exact ? fl : fl + 1;
    bool neg = N < 0;
    switch (r.k) {
    case FLOOR: out[0] = fl; return 1;
    case CEIL: out[0] = ce; return 1;
    case TZ: out[0] = neg ? ce : fl; return 1;
    case AWAY: out[0] = neg ? fl : ce; return 1;
    case HE: {
        if (exact) { out[0] = fl; return 1; }
        i128 twice = fdiv(N, S - 1);            // floor(2v)
        bool half = (N & (((i128)1 << (S - 1)) - 1)) == 0;   // 2v exact -> v = m + 1/2
        if (!half) { out[0] = (twice & 1) ? ce : fl; return 1; }
        out[0] = (fl & 1) ? ce : fl;
        return 1;
    }
    case CB: out[0] = fdiv(8 * N + ((i128)r.n << S), S + 3); return 1;                       // floor(v + n/8)
    case TB: { i128 a = neg ? -N : N; i128 m = fdiv(8 * a + ((i128)r.n << S), S + 3); out[0] = neg ? -m : m; return 1; }
    case CEILM1: out[0] = ce - 1; return 1;
    case FL1: if (exact) { out[0] = fl; out[1] = fl - 1; return 2; } out[0] = fl; return 1;
    case CE1: if (exact) { out[0] = fl; out[1] = fl + 1; return 2; } out[0] = ce; return 1;
    case TZB: return neg ? outs(N, S, {CE1, 0, ""}, out) : outs(N, S, {FL1, 0, ""}, out);
    case AWB: return neg ? outs(N, S, {FL1, 0, ""}, out) : outs(N, S, {CE1, 0, ""}, out);
    case PM1: if (exact) { out[0] = fl - 1; out[1] = fl; out[2] = fl + 1; return 3; } out[0] = fl; out[1] = ce; return 2;
    case NEAR: {
        // |r - v| <= 1/2  <=>  2N - 2^S r in [-2^S, 2^S]   (compare at scale 2^(S+1))
        int c = 0;
        i128 one = (i128)1 << S;
        for (i128 z : {fl, ce}) {
            i128 d = 2 * N - 2 * z * one;
            if (d >= -one && d <= one && (c == 0 || out[0] != (i64)z)) out[c++] = z;
        }
        return c;
    }
    }
    return 0;
}
// product C * V / 2^S (C the coefficient, V the data operand)
static inline int prod(i64 C, i64 V, int S, const Rule &r, i64 *out) {
    switch (r.k) {
    case BV: out[0] = booth(C, V, S, r.n, false); return 1;
    case HV: out[0] = booth(C, V, S, r.n, true); return 1;
    case BC: out[0] = booth(V, C, S, r.n, false); return 1;
    case HC: out[0] = booth(V, C, S, r.n, true); return 1;
    case AV: out[0] = booth(C, V, S, 0, false, r.n); return 1;
    case AC: out[0] = booth(V, C, S, 0, false, r.n); return 1;
    case AHV: out[0] = booth(C, V, S, 0, true, r.n); return 1;
    case AHC: out[0] = booth(V, C, S, 0, true, r.n); return 1;
    default: return outs((i128)C * V, S, r, out);
    }
}
static inline bool allows(i64 C, i64 V, int S, const Rule &r, i64 v) {
    i64 o[3];
    int c = prod(C, V, S, r, o);
    for (int i = 0; i < c; i++) if (o[i] == v) return true;
    return false;
}

// returns the number of samples from `on` reproduced (N - on = all).  HL hidden low bits: low is kept in
// 2^-(3+HL) sample and the output is floor(low / 2^HL) (ro 0) or its one's-complement read of the sign-magnitude
// value (ro 1); lop 1: the f (x - L) product sees low cut to 1/8 (floor)
struct Opt { int form, u, HL, ro, lop; };
static int QSHIFT = 1;
static int K255 = 511, SPAN = 64;   // SPAN: initial band search range +-SPAN (1/8 units, scaled by 2^(u-3))   // -k255 n: the mantissa used when FLV[8:1] = 0xFF (nominal 511; the console reads f = 1.0)
static inline i64 outv(i64 Lf, const Opt &o) {
    if (o.ro == 0) return Lf >> o.HL;
    i64 m = (Lf < 0 ? -Lf : Lf) >> o.HL;
    return Lf < 0 ? -m - 1 : m;
}
static int run(const DS &d, const Rule &r1, const Rule &r2, const Rule &r3, const Opt &o, int dn) {
    int e = d.F >> 9;
    int u = o.u < 0 ? 18 - e : o.u, HL = o.HL;
    i64 k = 256 + ((d.F & 0x1FF) >> 1), qm = q128[d.Q];
    if (k == 511) k = K255;
    // P1 = k (x - L) / 2^(27-e-u+HL) [L in 2^-(3+HL)] ; P2 = k qm b / 2^(31-e) ; P3 = k b' / 2^(21-e+u-HL)
    int s1 = 27 - e - u + HL, s2 = 31 - e, s3 = 21 - e + u - HL;
    i64 span = (i64)SPAN << std::max(0, u - 3);
    std::vector<std::pair<i64, i64>> cand, nxt;
    for (i64 a = -(1 << HL); a < (2 << HL); a++) {
        i64 Lf = ((i64)d.y[d.on - 1] << HL) + a;
        if (outv(Lf, o) != d.y[d.on - 1]) continue;
        for (i64 b = -span; b <= span; b++) cand.push_back({Lf, b});
    }
    i64 o1[9], o2[3], o3[3];
    for (int n = d.on - 1; n < d.N - 1; n++) {
        i64 x = (n + 1 >= d.on + dn && n + 1 < d.on + dn + 256) ? (i64)d.A * 8 : 0;
        nxt.clear();
        i64 lastL = INT64_MIN;
        int c1 = 0;
        for (auto &st : cand) {
            i64 Lf = st.first, b = st.second;
            if (Lf != lastL) {
                lastL = Lf;
                i64 Lop = o.lop ? (Lf >> HL) << HL : Lf;
                if (o.form == 1) c1 = prod(k, (x << HL) - Lop, s1, r1, o1);
                else {
                    i64 a[3], bb[3];
                    int ca = prod(k, x << HL, s1, r1, a), cb = prod(k, Lop, s1, r1, bb);
                    c1 = 0;
                    for (int i = 0; i < ca; i++) for (int j = 0; j < cb; j++) o1[c1++] = a[i] - bb[j];
                }
            }
            int c2;
            if (o.form == 5) {
                i64 qb[3];
                int cq = prod(qm, b, 7 + QSHIFT, r2, qb);
                i64 xl = (x << HL) - Lf;
                for (int iq = 0; iq < cq; iq++) {
                    i64 hi = xl * ((i64)1 << (s2 - s1)) - qb[iq] * ((i64)1 << (7 + QSHIFT));
                    i64 inc[3], lo[3]; int ci = prod(k, hi, s2, r1, inc);
                    for (int ii = 0; ii < ci; ii++) {
                        i64 nb = b + inc[ii]; int cl = prod(k, nb, s3, r3, lo);
                        for (int il = 0; il < cl; il++) {
                            i64 nL = Lf + lo[il];
                            if (outv(nL, o) == d.y[n + 1]) nxt.push_back({nL, nb});
                        }
                    }
                }
                continue;
            } else if (o.form == 2) {
                // one product f * T, T = (x - L) - q b exact (2^-7 band units): the increment rounded once (rule r1,
                // Booth rules recode T (bV/hV) or k (bC/hC)); r2 unused
                i64 xl = (x << HL) - ((o.lop ? (Lf >> HL) << HL : Lf));
                i64 T = xl * ((i64)1 << (s2 - s1)) - qm * b;
                c2 = prod(k, T, s2, r1, o2);
                for (int j = 0; j < c2; j++) o2[j] = -o2[j];
                c1 = 1; o1[0] = 0;
            } else if (o.form == 4) {
                // the band sum is formed at full precision: NBf = b + f T (units 2^-s2 band LSB); the STORED band is
                // r1(NBf) at 1/8, but the low update multiplies the unrounded NBf (rule r3)
                i64 xl = (x << HL) - ((o.lop ? (Lf >> HL) << HL : Lf));
                i64 T = xl * ((i64)1 << (s2 - s1)) - qm * b;
                i128 NBf = ((i128)b << s2) + (i128)k * T;
                i64 sb[3], lo3[3];
                int cs = outs(NBf, s2, r1, sb);
                int cl = outs((i128)k * NBf, s3 + s2, r3, lo3);
                for (int j = 0; j < cs; j++)
                    for (int m = 0; m < cl; m++) {
                        i64 nL = Lf + lo3[m];
                        if (outv(nL, o) == d.y[n + 1]) nxt.push_back({nL, sb[j]});
                    }
                continue;
            } else c2 = prod(k * qm, b, s2, r2, o2);
            for (int j = 0; j < c2; j++)
                for (int i = 0; i < c1; i++) {
                    i64 nb = b + o1[i] - o2[j];
                    int c3 = prod(k, nb, s3, r3, o3);
                    for (int m = 0; m < c3; m++) {
                        i64 nL = Lf + o3[m];
                        if (outv(nL, o) == d.y[n + 1]) nxt.push_back({nL, nb});
                    }
                }
        }
        std::sort(nxt.begin(), nxt.end());
        nxt.erase(std::unique(nxt.begin(), nxt.end()), nxt.end());
        std::swap(cand, nxt);
        if (cand.empty()) return n + 1 - d.on;
    }
    return d.N - d.on;
}

int main(int argc, char **argv) {
    int u = 3, form = 1, top = 20, HL = 0, ro = 0, lop = 0;
    std::vector<Rule> set;
    std::vector<std::vector<Rule>> combos;
    std::vector<int> use;
    auto split = [](const char *s) {
        std::vector<Rule> v;
        std::string t;
        for (const char *p = s;; p++) {
            if (*p == ',' || !*p) { if (!t.empty()) v.push_back(parse_rule(t)); t.clear(); if (!*p) break; }
            else t += *p;
        }
        return v;
    };
    if (argc > 1 && !strcmp(argv[1], "-selftest")) {   // booth() without truncation == floor, over random operands
        uint64_t z = 88172645463325252ull, bad = 0;
        for (int it = 0; it < 2000000; it++) {
            z ^= z << 13; z ^= z >> 7; z ^= z << 17;
            i64 C = (i64)(z % 200000), V = (i64)((z >> 20) % 2000000) - 1000000;
            int S = (int)((z >> 50) % 30);
            i128 ref = fdiv((i128)C * V, S);
            if (booth(C, V, S, 64, false) != ref || booth(V, C, S, 64, false) != ref) bad++;
        }
        printf("selftest: %llu mismatches\n", (unsigned long long)bad);
        return bad != 0;
    }
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-u")) { i++; u = !strcmp(argv[i], "scaled") ? -1 : atoi(argv[i]); }
        else if (!strcmp(argv[i], "-form")) form = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-top")) top = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-hl")) HL = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-ro")) ro = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-lop")) lop = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-k255")) K255 = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-qshift")) QSHIFT = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-span")) SPAN = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-set")) set = split(argv[++i]);
        else if (!strcmp(argv[i], "-rules")) combos.push_back(split(argv[++i]));
        else use.push_back(atoi(argv[i]));
    }
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
    if (use.empty()) use = {12, 16, 18, 4, 7, 8};
    if (combos.empty())
        for (auto &a : set) for (auto &b : set) for (auto &c : set) combos.push_back({a, b, c});
    struct R { i64 score; int full; std::vector<int> m; };
    std::vector<R> res(combos.size());
#pragma omp parallel for schedule(dynamic)
    for (size_t j = 0; j < combos.size(); j++) {
        R &r = res[j];
        r.score = 0; r.full = 0;
        for (int i : use) {
            const DS &d = ds[i];
            int best = 0;
            for (int dn = -1; dn <= 1 && best < d.N - d.on; dn++)
                best = std::max(best, run(d, combos[j][0], combos[j][1], combos[j][2], Opt{form, u, HL, ro, lop}, dn));
            r.m.push_back(best);
            r.score += best;
            r.full += best == d.N - d.on;
        }
    }
    std::vector<size_t> ord(combos.size());
    for (size_t j = 0; j < ord.size(); j++) ord[j] = j;
    std::sort(ord.begin(), ord.end(), [&](size_t a, size_t b) {
        return res[a].full != res[b].full ? res[a].full > res[b].full : res[a].score > res[b].score; });
    for (int t = 0; t < top && t < (int)ord.size(); t++) {
        size_t j = ord[t];
        printf("full %d/%zu  %-6s/%-6s/%-6s u %s form %d HL %d ro %d lop %d:", res[j].full, use.size(),
               combos[j][0].name.c_str(), combos[j][1].name.c_str(), combos[j][2].name.c_str(),
               u < 0 ? "scaled" : std::to_string(u).c_str(), form, HL, ro, lop);
        for (size_t i = 0; i < use.size(); i++) printf(" %d:%d", use[i], res[j].m[i]);
        printf("\n");
    }
    return 0;
}
