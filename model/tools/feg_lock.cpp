// feg_lock.cpp -- FEG through the filter with the envelope clock locked to the ring counter (tools/eg_phase.cpp),
// for the eg_lock "feg_odd" run and the feg_track batches.  Two parts:
//   1. the tracker of tools/feg_track.cpp (every (low, band, u) state consistent with the output, u = v >> 1) for a
//      capture whose slot configuration is given on the command line -> u(n) per sample;
//   2. the FEG rules of tools/feg_fit.cpp (one comparator, overshoot in attack / decay 1, hold-short in decay 2 /
//      release) with the clock on even-MDEC_CT samples and cnt = K - MDEC_CT/2: K is searched modulo the largest
//      period the stream's rates can see (and the key-off sample near mark 3/4), with the OPN increment rows or the
//      rotated rows for R = 1 mod 4 (-rot).  Reports the K residues that reproduce every unambiguous sample.
//   feg_lock [-rot] <prefix> <c0 hex> <stream> <FLV0..4 hex> <FAR FD1R FD2R FRR> <KRS OCT FNS(hex)> [ref stream (3)]
// -row r d0..d7 (repeatable, after -rot) overrides one increment row.
// Build: make -C tools feg_lock (-> build/tools/feg_lock)
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include <tuple>
#include "filt_capture.h"
using I = int64_t;
// the measured table (src/aica_model.cpp): rows 5, 9, 13 have the double step at index 1 and 5 (OPN: 3 and 7)
static uint8_t eg_inc[17][8] = {
    {0, 1, 0, 1, 0, 1, 0, 1}, {0, 1, 0, 1, 1, 1, 0, 1}, {0, 1, 1, 1, 0, 1, 1, 1}, {0, 1, 1, 1, 1, 1, 1, 1},
    {1, 1, 1, 1, 1, 1, 1, 1}, {1, 2, 1, 1, 1, 2, 1, 1}, {1, 2, 1, 2, 1, 2, 1, 2}, {1, 2, 2, 2, 1, 2, 2, 2},
    {2, 2, 2, 2, 2, 2, 2, 2}, {2, 4, 2, 2, 2, 4, 2, 2}, {2, 4, 2, 4, 2, 4, 2, 4}, {2, 4, 4, 4, 2, 4, 4, 4},
    {4, 4, 4, 4, 4, 4, 4, 4}, {4, 8, 4, 4, 4, 8, 4, 4}, {4, 8, 4, 8, 4, 8, 4, 8}, {4, 8, 8, 8, 4, 8, 8, 8},
    {8, 8, 8, 8, 8, 8, 8, 8}};
static inline uint32_t eg_increment(uint32_t R, uint32_t cnt, uint32_t slow_off) {
    if (R == 0) return 0;
    if (R < 48) {
        cnt += slow_off;
        uint32_t shift = 11 - (R >> 2);
        if (cnt & ((1u << shift) - 1)) return 0;
        return eg_inc[R & 3][(cnt >> shift) & 7];
    }
    return eg_inc[R >= 60 ? 16 : 4 + (R - 48)][cnt & 7];
}
static uint32_t period_of(uint32_t R) {
    if (R == 0) return 1;
    if (R < 48) return 1u << (14 - (R >> 2));
    if (R >= 60) return 1;
    int row = 4 + (R - 48);
    return (row & 3) == 0 ? 1 : (row & 3) == 2 ? 2 : 4;
}
static const int qm[32] = {192, 176, 160, 144, 128, 120, 112, 104, 96, 88, 80, 72, 64, 60, 56, 52,
                           48, 44, 40, 36, 32, 30, 28, 26, 24, 22, 20, 18, 16, 15, 14, 13};
static I ceilshr(I n, int s) { return -((-n) >> s); }
struct St { I L, B; int u; bool operator<(const St &o) const { return std::tie(L, B, u) < std::tie(o.L, o.B, o.u); }
            bool operator==(const St &o) const { return L == o.L && B == o.B && u == o.u; } };
static void fstep(I &L, I &B, I x, int u, int Q) {
    int v = u << 1, k = v >= 0x1ffe ? 512 : 256 + (u & 255), s = 24 - (v >> 9);
    I d = 2 * ceilshr(qm[Q] * B, 8);
    I H = x - L - d;
    H = H < -8388608 ? -8388608 : H > 8388607 ? 8388607 : H;
    B += (k * H) >> s;
    L += ceilshr(k * B, s);
}
struct Cfg { int flv[5], rate[4], krs, oct, fns; };
static uint32_t eff_rate(const Cfg &c, int re) {
    if (re == 0) return 0;
    int s = 0;
    if (c.krs != 15) {
        int k = c.krs + ((c.oct & 8) ? c.oct - 16 : c.oct);
        s = k < 0 ? 0 : 2 * (k > 15 ? 15 : k) + ((c.fns >> 9) & 1);
    }
    return std::min(63, re * 2 + s);
}
// FEG rules (feg_fit sim, trans 1 / ov 3): key-on at the onset sample (v = FLV0, no step on that sample); steps on
// every later clock (even MDEC_CT); key-off at sample koff: release, and a step on that sample if it is a clock and
// koff_same.  Returns the number of samples (from the onset) whose unambiguous u agrees.
static int sim(const std::vector<int> &u, uint32_t c0ring, uint32_t first, int on, const Cfg &c, uint32_t K, uint32_t slow_off,
               int koff, int koff_same, int *mm_n = nullptr, int *mm_v = nullptr) {
    int v = c.flv[0], st = 0;
    auto dirof = [&](int s) { return v >= c.flv[s + 1] ? -1 : 1; };
    int dir = dirof(0);
    bool passed = false;
    for (int n = 0; n < (int)u.size(); n++) {
        int i = on + n;
        uint32_t md = (c0ring - first - (uint32_t)i) & 0xFFFF;
        bool clock = (md & 1) == 0;
        if (i == koff) { st = 3; dir = dirof(3); passed = false; }
        if (clock && n > 0 && !(i == koff && !koff_same)) {
            uint32_t cnt = (K - (md >> 1)) & 0x3FFF;
            if (passed && st < 2) { st++; dir = dirof(st); passed = false; }
            int target = c.flv[st + 1];
            uint32_t inc = eg_increment(eff_rate(c, c.rate[st]), cnt, slow_off);
            if (inc && !passed) {
                bool C = v >= target;
                int nv = v + dir * (int)inc;
                nv = nv < 0 ? 0 : nv > 0x1FFF ? 0x1FFF : nv;
                if (st >= 2) { if ((nv >= target) == C) v = nv; }
                else { v = nv; if ((nv >= target) != C) passed = true; }
            }
        }
        if (u[n] >= 0 && u[n] != (v >> 1)) { if (mm_n) { *mm_n = n; *mm_v = v; } return n; }
    }
    return (int)u.size();
}
int main(int argc, char **argv) {
    bool rot = false;
    int a = 1;
    if (argc > a && !strcmp(argv[a], "-rot")) { rot = true; a++; }
    while (argc > a + 9 && !strcmp(argv[a], "-row")) { int r = atoi(argv[a + 1]); for (int j = 0; j < 8; j++) eg_inc[r][j] = (uint8_t)atoi(argv[a + 2 + j]); a += 10; }
    if (argc < a + 15) { fprintf(stderr, "usage: feg_lock [-rot] <prefix> <c0 hex> <stream> <FLV0..4 hex> <FAR FD1R FD2R FRR> <KRS OCT FNS hex> [ref]\n"); return 2; }
    if (rot) {   // -rot = the YM2612 (OPN) rows 5 / 9 / 13 as a control
        const uint8_t r5[8] = {1, 1, 1, 2, 1, 1, 1, 2}, r9[8] = {2, 2, 2, 4, 2, 2, 2, 4}, r13[8] = {4, 4, 4, 8, 4, 4, 4, 8};
        memcpy(eg_inc[5], r5, 8); memcpy(eg_inc[9], r9, 8); memcpy(eg_inc[13], r13, 8);
    }
    std::string prefix = argv[a];
    uint32_t c0ring = strtoul(argv[a + 1], 0, 16);
    int k = atoi(argv[a + 2]);
    Cfg c;
    for (int j = 0; j < 5; j++) c.flv[j] = strtol(argv[a + 3 + j], 0, 16);
    for (int j = 0; j < 4; j++) c.rate[j] = atoi(argv[a + 8 + j]);
    c.krs = atoi(argv[a + 12]); c.oct = atoi(argv[a + 13]); c.fns = strtol(argv[a + 14], 0, 16);
    int ref = argc > a + 15 ? atoi(argv[a + 15]) : 3;
    const char *save = argc > a + 16 ? argv[a + 16] : nullptr;   /* write u(n) (int32, -1 = ambiguous) here */
    auto cp = cap(prefix);
    FILE *f = fopen((prefix + ".hdr").c_str(), "rb");
    uint32_t h[16 + 128] = {0};
    size_t nh = fread(h, 4, sizeof h / 4, f);
    fclose(f);
    int m3 = -1, m4 = -1;
    int m2 = -1;
    for (size_t i = 11; i + 1 < nh && i < 11 + 2 * h[6]; i += 2) { if (h[i] == 2) m2 = (int)(h[i + 1] - cp.first); if (h[i] == 3) m3 = (int)(h[i + 1] - cp.first); if (h[i] == 4) m4 = (int)(h[i + 1] - cp.first); }
    if (m3 < 0 && m2 >= 0) { m3 = m2 - 64; m4 = m2; }   /* feg_track: mark 2 follows the key-off write */
    unsigned on = 1;
    while (on < cp.n && !cp.v[on * cp.ns + ref]) on++;
    // 1. track u(n)
    std::vector<St> S, T;
    for (I b0 = -256; b0 <= 256; b0++) S.push_back({-I(cp.v[(on - 1) * cp.ns + k]) / 2, b0, c.flv[0] >> 1});
    std::vector<int> u(cp.n - on, -1);
    size_t maxset = 0;
    unsigned n = on;
    for (; n < cp.n; n++) {
        I x = I(cp.v[n * cp.ns + ref]) >> 1;
        T.clear();
        for (auto &s : S)
            for (int du = -4; du <= 4; du++) {
                int nu = s.u + du;
                if (nu < 0 || nu > 4095) continue;
                I L = s.L, B = s.B;
                fstep(L, B, x, nu, 4);
                if (std::clamp<I>(-2 * L, -524288, 524287) == cp.v[n * cp.ns + k]) T.push_back({L, B, nu});
            }
        std::sort(T.begin(), T.end());
        T.erase(std::unique(T.begin(), T.end()), T.end());
        if (T.empty()) break;
        if (T.size() > 400000) T.resize(400000);
        maxset = std::max(maxset, T.size());
        bool one = true;
        for (auto &s : T) one &= s.u == T[0].u;
        u[n - on] = one ? T[0].u : -1;
        std::swap(S, T);
    }
    int amb = 0;
    for (unsigned i = 0; i < n - on; i++) amb += u[i] < 0;
    if (save) { FILE *sf = fopen(save, "wb"); if (sf) { fwrite(u.data(), 4, n - on, sf); fclose(sf); } }
    uint32_t md_on = (c0ring - cp.first - on) & 0xFFFF;
    printf("%s stream %d: tracked %u/%u from onset %u (MDEC_CT %04x %s), ambiguous %d, max set %zu; R %u/%u/%u/%u; key-off marks %d..%d\n",
           prefix.c_str(), k, n - on, cp.n - on, on, md_on, (md_on & 1) ? "odd" : "even", amb, maxset,
           eff_rate(c, c.rate[0]), eff_rate(c, c.rate[1]), eff_rate(c, c.rate[2]), eff_rate(c, c.rate[3]), m3, m4);
    if (n < cp.n) { printf("  tracking lost at %u\n", n); u.resize(n - on); }
    // 2. ring-locked prediction
    uint32_t P = 1;
    for (int j = 0; j < 4; j++) P = std::max(P, period_of(eff_rate(c, c.rate[j])));
    int on_end = m3 >= 0 ? std::min<int>(u.size(), m3 - 4 - on) : (int)u.size();
    std::vector<int> uon(u.begin(), u.begin() + on_end);
    int best = -1; uint32_t bK = 0; int boff = 0, bko = -1, bsame = 0;
    for (int slow = 0; slow < 2; slow++) {
        std::vector<uint32_t> onK;
        for (uint32_t K = 0; K < P; K++) {
            int m = sim(uon, c0ring, cp.first, on, c, K, slow ? (uint32_t)-1 : 0, 1 << 30, 0);
            if (m > best) { best = m; bK = K; boff = slow; }
            if (m == on_end) onK.push_back(K);
        }
        printf("  slow_off %s: %zu of %u K residues reproduce the key-on phase (%d samples)", slow ? "-1" : "0", onK.size(), P, on_end);
        if (onK.size() && onK.size() <= 8) { printf(":"); for (auto K : onK) printf(" %u", K); }
        printf("\n");
        if (m3 < 0) continue;
        for (int same = 0; same < 2; same++) {
            std::vector<uint32_t> fullK; std::vector<int> fullko;
            for (uint32_t K : onK)
                for (int ko = std::max<int>(on + 1, m3 - 48); ko <= std::min<int>(cp.n - 1, m4 + 400); ko++) {
                    int m = sim(u, c0ring, cp.first, on, c, K, slow ? (uint32_t)-1 : 0, ko, same);
                    if (m > best) { best = m; bK = K; boff = slow; bko = ko; bsame = same; }
                    if (m == (int)u.size()) { fullK.push_back(K); fullko.push_back(ko); }
                }
            if (fullK.empty()) continue;
            printf("    FULL (%zu samples) with slow_off %s koff_same %d: K mod %u in {", u.size(), slow ? "-1" : "0", same, P);
            std::vector<uint32_t> ks = fullK; std::sort(ks.begin(), ks.end()); ks.erase(std::unique(ks.begin(), ks.end()), ks.end());
            for (size_t i = 0; i < ks.size() && i < 12; i++) printf("%s%u", i ? "," : "", ks[i]);
            if (ks.size() > 12) printf(",... (%zu)", ks.size());
            printf("}; key-off samples:");
            std::vector<int> kos = fullko; std::sort(kos.begin(), kos.end()); kos.erase(std::unique(kos.begin(), kos.end()), kos.end());
            for (size_t i = 0; i < kos.size() && i < 8; i++) printf(" %d(%s)", kos[i], ((c0ring - cp.first - kos[i]) & 1) ? "odd" : "even");
            printf("\n");
        }
    }
    if (best < (int)u.size()) {
        int mn = -1, mv = -1;
        sim(u, c0ring, cp.first, on, c, bK, boff ? (uint32_t)-1 : 0, bko < 0 ? 1 << 30 : bko, bsame, &mn, &mv);
        printf("  best %d/%zu (K %u slow_off %s koff %d same %d): first mismatch n %d hw u %03x model v %04x\n", best, u.size(), bK, boff ? "-1" : "0", bko, bsame, mn, mn >= 0 ? u[mn] : -1, mv);
    }
    return 0;
}
