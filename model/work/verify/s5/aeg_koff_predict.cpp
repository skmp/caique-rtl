// aeg_koff_predict.cpp -- what the orchestrator's AEG key-off experiment shows under S3 and the alternatives that
// survive the FEG data.  Constant 0x7FFF input, TL 0, VOFF 0, IMXL 15: MIXS = 16 * floor(32767 * (127 - (a & 63)) >> (7 + (a >> 6))).
// Mechanisms on the key-off sample K (the sample after the KYONEX write):
//   S3      a clock on K steps a += inc(old segment's rate)          (the model: rate from aeg_prev, release formula)
//   S3'     a clock on K performs one more OLD-segment step (attack formula if the old segment was the attack)
//   noS3    a clock on K steps with the release increment
//   noStep  no step on K (like the key-on sample); release from the next clock
//   odd K   nothing on K, release increment on the next clock (every mechanism)
// Usage: aeg_koff_predict            (prints the designs' sequences and the attack durations)
// Build: g++ -O2 -std=c++17 -o build/work/aeg_koff_predict work/verify/s5/aeg_koff_predict.cpp
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>
#include <algorithm>
static const uint8_t eg_inc[17][8] = {
    {0, 1, 0, 1, 0, 1, 0, 1}, {0, 1, 0, 1, 1, 1, 0, 1}, {0, 1, 1, 1, 0, 1, 1, 1}, {0, 1, 1, 1, 1, 1, 1, 1},
    {1, 1, 1, 1, 1, 1, 1, 1}, {1, 2, 1, 1, 1, 2, 1, 1}, {1, 2, 1, 2, 1, 2, 1, 2}, {1, 2, 2, 2, 1, 2, 2, 2},
    {2, 2, 2, 2, 2, 2, 2, 2}, {2, 4, 2, 2, 2, 4, 2, 2}, {2, 4, 2, 4, 2, 4, 2, 4}, {2, 4, 4, 4, 2, 4, 4, 4},
    {4, 4, 4, 4, 4, 4, 4, 4}, {4, 8, 4, 4, 4, 8, 4, 4}, {4, 8, 4, 8, 4, 8, 4, 8}, {4, 8, 8, 8, 4, 8, 8, 8},
    {8, 8, 8, 8, 8, 8, 8, 8}};
static inline uint32_t eg_increment(uint32_t R, uint32_t cnt) {
    if (R == 0) return 0;
    if (R < 48) { cnt -= 1; uint32_t sh = 11 - (R >> 2); if (cnt & ((1u << sh) - 1)) return 0; return eg_inc[R & 3][(cnt >> sh) & 7]; }
    return eg_inc[R >= 60 ? 16 : 4 + (R - 48)][cnt & 7];
}
static int32_t level_of(int a, bool off) { if (off) return 0; if (a > 0x3FF) a = 0x3FF; int M = 127 - (a & 63), k = a >> 6; return 16 * (int32_t)((32767LL * M) >> (7 + k)); }
enum Mech { S3, S3P, NOS3, NOSTEP };
static const char *mn[4] = {"S3", "S3'", "noS3", "noStep"};
struct Aeg { int a, state; bool off; };
// R per segment (effective), DL; one clock with the state's rate; returns inc used
static void att_step(Aeg &e, uint32_t inc, int dl) { int a = e.a; a += ((~a) * (int)inc) >> 4; if (a <= 0) { a = 0; e.state = 1; } e.a = a; (void)dl; }
static void clock(Aeg &e, const int R[4], int dl, uint32_t cnt, Mech m, bool koff_now, int prev) {
    if (e.off) return;
    if (koff_now && m == NOSTEP) return;
    int rs = (koff_now && (m == S3 || m == S3P)) ? prev : e.state;
    bool was_d1 = e.state == 1;
    uint32_t inc = eg_increment(R[rs], cnt);
    if (!inc) { if (was_d1 && (e.a >> 5) == dl) e.state = 2; return; }
    if (e.state == 0 || (koff_now && m == S3P && prev == 0)) { int a = e.a; a += ((~a) * (int)inc) >> 4; if (a <= 0) { a = 0; if (e.state == 0) e.state = 1; } e.a = a; }
    else { e.a += inc; if (e.a > 0x3FF) { e.a = 0x3FF; e.off = true; } }
    if (was_d1 && !e.off && (e.a >> 5) == dl) e.state = 2;
}
// run: key-on at sample 0 (no step), clocks on even samples with cnt = cnt0 + sample/2 (cnt increases as MDEC_CT decreases),
// key-off at sample K; print the a and MIXS around K
static std::vector<int> run(const int R[4], int dl, int K, Mech m, int nsamp, uint32_t cnt0, std::vector<int> *as = nullptr) {
    Aeg e{0x280, 0, false}; if (R[0] >= 63) e.a = 0;
    std::vector<int> lv; int prev = 0;
    for (int i = 0; i < nsamp; i++) {
        bool ko = false;
        if (i == K) { prev = e.state; e.state = 3; ko = true; }
        if (i > 0 && (i & 1) == 0) clock(e, R, dl, cnt0 + i / 2, m, ko, prev);
        lv.push_back(level_of(e.a, e.off)); if (as) as->push_back(e.a);
    }
    return lv;
}
struct Design { const char *name; int R[4]; int dl; int Ksamp; };
int main() {
    printf("Envelope clock on even samples (sample 0 = key-on, a clock, no step); cnt = cnt0 + i/2, cnt0 chosen so cnt&7 = 0 at sample 0.\n");
    printf("Attack durations from a = 0x280 (R 63: a = 0 at key-on): clocks / samples / us until decay 1\n");
    for (int R : {44, 48, 52, 56, 60}) { int Rs[4] = {R, 0, 0, 0}; std::vector<int> as; run(Rs, 31, 1 << 30, S3, 4000, 0, &as); int n = 0; while (n < (int)as.size() && as[n] > 0) n++; printf("  attack R %d: a reaches 0 at sample %d (%d clocks, %.1f ms)\n", R, n, n / 2, n * 1e3 / 44100); }
    printf("\n");
    // designs: decay 2 with D2R and release RR (effective R), the slot reaches decay 2 through AR 31 (R 62) / D1R 31 to DL 4
    struct D { const char *name; int R[4]; int dl; };
    std::vector<D> designs = {
        {"d2 +2 (R52) -> rel +1 (R48)", {62, 62, 52, 48}, 4},
        {"d2 +1 (R48) -> rel +4 (R56)", {62, 62, 48, 56}, 4},
        {"d2 +4 (R56) -> rel hold (R0)", {62, 62, 56, 0}, 4},
        {"d2 hold (R0) -> rel +2 (R52)", {62, 62, 0, 52}, 4},
        {"d2 +8 (R60) -> rel +1 (R48)", {62, 62, 60, 48}, 4},
        {"d2 +1 (R48) -> rel +8 (R60)", {62, 62, 48, 60}, 4},
        {"att R48 -> rel +1 (R48)", {48, 62, 0, 48}, 4},
        {"att R48 -> rel +4 (R56)", {48, 62, 0, 56}, 4},
        {"att R52 -> rel +1 (R48)", {52, 62, 0, 48}, 4},
        {"att R52 -> rel +4 (R56)", {52, 62, 0, 56}, 4},
        {"att R56 -> rel +1 (R48)", {56, 62, 0, 48}, 4},
        {"att R56 -> rel +4 (R56)", {56, 62, 0, 56}, 4},
    };
    for (auto &d : designs) {
        bool att = d.R[0] < 62;
        int K = att ? 20 : 120;   // attack designs: key-off 20 samples after key-on (still in the attack); decay 2: at 120
        printf("== %s, key-off sample K = %d (even) or %d (odd); a and MIXS per sample K-6..K+8\n", d.name, K, K + 1);
        for (int par = 0; par < 2; par++) {
            int Kp = K + par;
            printf("  K %s (%d):\n", par ? "odd " : "EVEN", Kp);
            for (int mi = 0; mi < 4; mi++) {
                std::vector<int> as; auto lv = run(d.R, d.dl, Kp, (Mech)mi, K + 12, 0, &as);
                printf("    %-7s a:", mn[mi]);
                for (int i = K - 6; i <= K + 8; i++) printf(" %s%03x", i == Kp ? "*" : " ", as[i]);
                printf("\n            L:");
                for (int i = K - 6; i <= K + 8; i++) printf(" %7d", lv[i]);
                printf("\n");
                if (par == 1) break;   // odd K: every mechanism is the same (only S3 shown)
            }
        }
        // equivalence check: S3 with even K vs noS3 with odd K+1 (and noStep with odd K-1...)
        std::vector<int> a1, a2, a3; run(d.R, d.dl, K, S3, K + 40, 0, &a1); run(d.R, d.dl, K + 1, NOS3, K + 40, 0, &a2); run(d.R, d.dl, K + 1, NOSTEP, K + 40, 0, &a3);
        printf("  S3(even K) == noS3(odd K+1) over the whole run: %s;  S3(even K) == noStep(odd K+1): %s\n", a1 == a2 ? "YES (indistinguishable without knowing K)" : "NO (decisive)", a1 == a3 ? "YES" : "NO");
    }
    return 0;
}
