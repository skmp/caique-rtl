// rest.cpp -- zero-input rest states of the slot filter (NOTES "Slot filter" recurrence, 1/8 units):
//   D = 2*ceildiv(q128[Q]*B, 8);  H = clamp24(x - L - D);  B += (k*H) >> s;  L += ceildiv(k*B, s);  out = -2L
// with x = 0, k = 256 + ((v >> 1) & 255) (512 at v >= 0x1FFE), s = 24 - (v >> 9).
// For each cutoff v and Q: every start state (L, B) in [-box, box]^2 is iterated to its eventual cycle; fixed points
// and limit cycles are listed with their basin sizes; L = 4 (out -8, the eg_lock mixs residual) is flagged.
// Then "landing" statistics: the filter is driven by a full-scale random signal for a random length and cut to zero,
// or started from a large random state, and the rest state it settles in is tallied.
//   rest [-box N] [-Q q] [-land N] [v hex ...]
// Build: g++ -O2 -std=c++17 -o build/work/minus8_rest work/minus8/rest.cpp
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <vector>
#include <algorithm>
#include <unordered_map>
#include <string>

static const uint8_t lpf_q128[32] = {192, 176, 160, 144, 128, 120, 112, 104, 96, 88, 80, 72, 64, 60, 56, 52,
                                     48, 44, 40, 36, 32, 30, 28, 26, 24, 22, 20, 18, 16, 15, 14, 13};
static inline int64_t ceil_shr(int64_t v, int sh) { return -((-v) >> sh); }
struct F { int64_t k; int s; int64_t q; };
static F coef(unsigned v, int Q) { F f; f.k = v >= 0x1FFE ? 512 : 256 + ((v >> 1) & 0xFF); f.s = 24 - (int)(v >> 9); f.q = lpf_q128[Q & 31]; return f; }
static inline void stepf(const F &f, int64_t x8, int64_t &L, int64_t &B) {
    int64_t D = 2 * ceil_shr(f.q * B, 8);
    int64_t H = x8 - L - D;
    H = H < -8388608 ? -8388608 : H > 8388607 ? 8388607 : H;
    B = B + ((f.k * H) >> f.s);
    L = L + ceil_shr(f.k * B, f.s);
}
static inline int64_t key(int64_t L, int64_t B) { return (L << 32) ^ (B & 0xFFFFFFFFll); }

struct Cycle { std::vector<std::pair<int64_t, int64_t>> st; long basin = 0; };
// follow (L,B) until a state repeats; return the cycle in canonical rotation (lexicographically smallest state first)
static std::vector<std::pair<int64_t, int64_t>> cycle_of(const F &f, int64_t L, int64_t B, int maxit = 200000) {
    std::unordered_map<int64_t, int> seen;
    std::vector<std::pair<int64_t, int64_t>> path;
    for (int it = 0; it < maxit; it++) {
        int64_t kk = key(L, B);
        auto p = seen.find(kk);
        if (p != seen.end()) {
            std::vector<std::pair<int64_t, int64_t>> cyc(path.begin() + p->second, path.end());
            size_t m = 0;
            for (size_t i = 1; i < cyc.size(); i++) if (cyc[i] < cyc[m]) m = i;
            std::rotate(cyc.begin(), cyc.begin() + m, cyc.end());
            return cyc;
        }
        seen[kk] = (int)path.size();
        path.push_back({L, B});
        stepf(f, 0, L, B);
    }
    return {};
}
static std::string cyc_name(const std::vector<std::pair<int64_t, int64_t>> &c) {
    char b[512]; int n = 0;
    if (c.size() == 1) n += snprintf(b + n, sizeof b - n, "fixed (L %lld,B %lld) out %lld", (long long)c[0].first, (long long)c[0].second, (long long)-2 * c[0].first);
    else {
        n += snprintf(b + n, sizeof b - n, "cycle p%zu", c.size());
        for (auto &s : c) { if (n > 400) { n += snprintf(b + n, sizeof b - n, " ..."); break; } n += snprintf(b + n, sizeof b - n, " (%lld,%lld)", (long long)s.first, (long long)s.second); }
        n += snprintf(b + n, sizeof b - n, " out");
        for (auto &s : c) { if (n > 480) break; n += snprintf(b + n, sizeof b - n, " %lld", (long long)-2 * s.first); }
    }
    return b;
}

int main(int argc, char **argv) {
    int box = 64, Q = 4; long nland = 20000;
    std::vector<unsigned> vs;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-box")) box = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-Q")) Q = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-land")) nland = atol(argv[++i]);
        else vs.push_back((unsigned)strtoul(argv[i], nullptr, 16));
    }
    if (vs.empty()) vs = {0x1A00, 0x1B00, 0x1B80, 0x1BC0, 0x1BFE, 0x1BFF, 0x1C00};
    for (unsigned v : vs) {
        F f = coef(v, Q);
        printf("==== v %04x Q %d: k %lld s %d (k/2^s = %.5f) q128 %lld; box [-%d,%d]^2 = %d start states\n", v, Q, (long long)f.k, f.s,
               (double)f.k / (double)(1ll << f.s), (long long)f.q, box, box, (2 * box + 1) * (2 * box + 1));
        std::map<std::vector<std::pair<int64_t, int64_t>>, long> cycles;
        long fixedn = 0; std::map<int64_t, std::pair<int64_t, int64_t>> fixL;   // B -> [Lmin, Lmax] of the fixed points
        for (int L = -box; L <= box; L++)
            for (int B = -box; B <= box; B++) {
                auto c = cycle_of(f, L, B);
                cycles[c]++;
                // is (L,B) itself a fixed point?
                int64_t L2 = L, B2 = B; stepf(f, 0, L2, B2);
                if (L2 == L && B2 == B) {
                    fixedn++;
                    auto it = fixL.find(B);
                    if (it == fixL.end()) fixL[B] = {L, L};
                    else { it->second.first = std::min<int64_t>(it->second.first, L); it->second.second = std::max<int64_t>(it->second.second, L); }
                }
            }
        printf("  fixed points: %ld;  per B the L range (all fixed): ", fixedn);
        for (auto &p : fixL) printf(" B %lld: L %lld..%lld;", (long long)p.first, (long long)p.second.first, (long long)p.second.second);
        printf("\n");
        {
            int64_t L2 = 4, B2 = -4; stepf(f, 0, L2, B2);
            printf("  (L 4, B -4) -> (%lld, %lld): %s\n", (long long)L2, (long long)B2, (L2 == 4 && B2 == -4) ? "FIXED POINT (out -8)" : "not fixed");
            bool anyL4 = false;
            for (auto &p : fixL) if (p.second.first <= 4 && 4 <= p.second.second) { anyL4 = true; }
            printf("  fixed points with L = 4 (out -8): %s", anyL4 ? "" : "none");
            for (auto &p : fixL) if (p.second.first <= 4 && 4 <= p.second.second) printf(" (4,%lld)", (long long)p.first);
            printf("\n");
        }
        // cycles (non-fixed) and the basin of every rest state
        std::vector<std::pair<long, std::vector<std::pair<int64_t, int64_t>>>> byb;
        for (auto &c : cycles) byb.push_back({c.second, c.first});
        std::sort(byb.begin(), byb.end(), [](auto &a, auto &b) { return a.first > b.first; });
        long ncyc = 0; for (auto &c : cycles) if (c.first.size() > 1) ncyc++;
        printf("  distinct rest states reached from the box: %zu (%ld limit cycles of period > 1); largest basins:\n", cycles.size(), ncyc);
        int shown = 0;
        for (auto &c : byb) { if (shown++ >= 12) break; printf("    basin %6ld (%5.1f%%): %s\n", c.first, 100.0 * c.first / ((2 * box + 1) * (2 * box + 1)), cyc_name(c.second).c_str()); }
        if (ncyc) { printf("  limit cycles:\n"); for (auto &c : byb) if (c.second.size() > 1) printf("    basin %6ld: %s\n", c.first, cyc_name(c.second).c_str()); }
        // basin of L = 4 (any rest state whose cycle contains L = 4)
        long b4 = 0, b0 = 0;
        for (auto &c : cycles) { bool has4 = false, has0 = false; for (auto &s : c.first) { has4 |= s.first == 4; has0 |= s.first == 0; } if (has4) b4 += c.second; if (has0 && c.first.size() == 1) b0 += c.second; }
        printf("  from the box: %ld start states (%.1f%%) end in a rest state that visits L = 4 (out -8); %ld (%.1f%%) in a fixed point with L = 0 (out 0)\n",
               b4, 100.0 * b4 / ((2 * box + 1) * (2 * box + 1)), b0, 100.0 * b0 / ((2 * box + 1) * (2 * box + 1)));
        // landings of decaying signals: (a) full-scale random drive of random length, then zero; (b) large random start states
        uint32_t seed = 4242;
        auto rnd = [&]() { seed = seed * 1103515245u + 12345u; return seed >> 8; };
        for (int mode = 0; mode < 2; mode++) {
            std::map<int64_t, long> outs; std::map<std::vector<std::pair<int64_t, int64_t>>, long> land;
            long settle_max = 0, settle_sum = 0;
            for (long t = 0; t < nland; t++) {
                int64_t L = 0, B = 0;
                if (mode == 0) {
                    int len = 200 + (int)(rnd() % 3000);
                    for (int i = 0; i < len; i++) { int16_t x = (int16_t)(rnd() & 0xFFFF); if (!x) x = 1; stepf(f, (int64_t)x * 8, L, B); }
                } else { L = (int64_t)(rnd() % (1u << 18)) - (1 << 17); B = (int64_t)(rnd() % (1u << 18)) - (1 << 17); }
                // run to rest: iterate until the state repeats
                std::unordered_map<int64_t, int> seen; int it = 0;
                for (;; it++) { int64_t kk = key(L, B); if (seen.count(kk)) break; seen[kk] = it; if (it > 400000) break; stepf(f, 0, L, B); }
                auto c = cycle_of(f, L, B);
                land[c]++;
                for (auto &s : c) outs[-2 * s.first]++;
                settle_sum += it; settle_max = std::max(settle_max, (long)it);
            }
            printf("  landings of %ld %s (samples to rest: mean %.0f, max %ld):\n", nland, mode == 0 ? "random full-scale drives cut to zero" : "random start states |L|,|B| < 2^17", (double)settle_sum / nland, settle_max);
            std::vector<std::pair<long, std::vector<std::pair<int64_t, int64_t>>>> lb;
            for (auto &c : land) lb.push_back({c.second, c.first});
            std::sort(lb.begin(), lb.end(), [](auto &a, auto &b) { return a.first > b.first; });
            int sh = 0;
            for (auto &c : lb) { if (sh++ >= 10) break; printf("    %6ld (%5.1f%%): %s\n", c.first, 100.0 * c.first / nland, cyc_name(c.second).c_str()); }
            if (lb.size() > 10) printf("    ... %zu distinct rest states in all\n", lb.size());
            printf("    output values at rest (counted per rest state visited):");
            for (auto &o : outs) printf(" %lld:%ld", (long long)o.first, o.second);
            printf("\n");
        }
    }
    return 0;
}
