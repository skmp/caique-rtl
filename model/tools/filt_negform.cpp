// filt_negform.cpp -- the slot-filter recurrence with the band state sign-flipped is three plain floors.
// NOTES.md "Slot filter" (mathematical form, 1/8-sample units):
//   D = 2 * ceil(q128 * B / 256);  H = clamp24(x - L - D);  B += floor(k * H / 2^s);  L += ceil(k * B / 2^s);  out = -2L
// With Bh = -B (and, optionally, Lh = -L so that out = +2 Lh) every rounding becomes an arithmetic right shift:
//   Dh = 2 * ((q128 * Bh) >> 8);  H = clamp24(x - L + Dh);  Bh -= (k * H) >> s;  L -= (k * Bh) >> s;  out = -2L
//   or with Lh = -L:  H = clamp24(x + Lh + Dh);  Bh -= (k * H) >> s;  Lh += (k * Bh) >> s;  out = 2 Lh
// This checks the identity exhaustively-ish: 2^28 random states/inputs over all k, s, Q, including the clamp region
// and the 32-bit extremes, and reports any difference.  Exit 0 = identical.
// Build: make -C tools filt_negform (-> build/tools/filt_negform)
#include <cstdio>
#include <cstdint>
#include <cstdlib>
typedef int64_t I;
static const int qm[32] = {192, 176, 160, 144, 128, 120, 112, 104, 96, 88, 80, 72, 64, 60, 56, 52,
                           48, 44, 40, 36, 32, 30, 28, 26, 24, 22, 20, 18, 16, 15, 14, 13};
static inline I ceilshr(I n, int s) { return -((-n) >> s); }
static inline I clamp24(I v) { return v < -8388608 ? -8388608 : v > 8388607 ? 8388607 : v; }
__attribute__((noinline)) static void step_ref(I &L, I &B, I x, I k, int s, int q) {
    I D = 2 * ceilshr(qm[q] * B, 8);
    I H = clamp24(x - L - D);
    B += (k * H) >> s;
    L += ceilshr(k * B, s);
}
__attribute__((noinline)) static void step_neg(I &L, I &Bh, I x, I k, int s, int q) {   // Bh = -B
    I Dh = 2 * ((qm[q] * Bh) >> 8);
    I H = clamp24(x - L + Dh);
    Bh -= (k * H) >> s;
    L -= (k * Bh) >> s;
}
__attribute__((noinline)) static void step_neg2(I &Lh, I &Bh, I x, I k, int s, int q) {   // Lh = -L, Bh = -B
    I Dh = 2 * ((qm[q] * Bh) >> 8);
    I H = clamp24(x + Lh + Dh);
    Bh -= (k * H) >> s;
    Lh += (k * Bh) >> s;
}
int main() {
    uint64_t seed = 0x9E3779B97F4A7C15ull;
    auto rnd = [&]() { seed ^= seed << 13; seed ^= seed >> 7; seed ^= seed << 17; return seed; };
    long long bad = 0, n = 0;
    for (int e = 0; e <= 15; e++)
        for (int m = 0; m < 256; m++)
            for (int q = 0; q < 32; q++) {
                int flv = (e << 9) | (m << 1);
                I k = flv >= 0x1FFE ? 512 : 256 + m;
                int s = 24 - e;
                for (int t = 0; t < 2048; t++) {
                    // magnitudes: mostly small, sometimes up to 2^26 (beyond any reachable state)
                    int sh = rnd() % 27;
                    I L = (I)(rnd() % (2ull << sh)) - (1ll << sh), B = (I)(rnd() % (2ull << sh)) - (1ll << sh);
                    I x = (I)(rnd() % 524288) - 262144;
                    if (t < 64) { L = t & 1 ? 0 : (t & 2 ? -1 : 1); B = (t >> 2) % 3 - 1; x = 0; }   // the cycle states
                    I L1 = L, B1 = B, L2 = L, B2 = -B, L3 = -L, B3 = -B;
                    step_ref(L1, B1, x, k, s, q);
                    step_neg(L2, B2, x, k, s, q);
                    step_neg2(L3, B3, x, k, s, q);
                    n++;
                    if (L1 != L2 || B1 != -B2 || L1 != -L3 || B1 != -B3) {
                        if (bad < 10) printf("DIFF flv %04x q %d L %lld B %lld x %lld: ref (%lld,%lld) neg (%lld,%lld) neg2 (%lld,%lld)\n",
                                             flv, q, (long long)L, (long long)B, (long long)x, (long long)L1, (long long)B1,
                                             (long long)L2, (long long)-B2, (long long)-L3, (long long)-B3);
                        bad++;
                    }
                }
            }
    printf("filt_negform: %lld cases over all 131072 (FLV, Q) settings, %lld differences\n", n, bad);
    return bad != 0;
}
