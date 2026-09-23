// filt_frac.cpp -- which conversion takes the interpolated 1/16-sample input (s16) to the filter's 1/8 units?
// tests/filt_frac/hw/ff_<b>: streams 0..2 filtered (F/Q below, VOFF=1), stream 3 = unfiltered reference whose MIXS
// is s16 itself.  For each candidate conversion the verified filter recurrence (NOTES.md "Slot filter") is run from
// every initial band in [-256, 256] with the initial low read from the capture; a candidate must reproduce every
// sample from the onset to the end.
// Build: make -C tools filt_frac (-> build/tools/filt_frac) ; run from caique-rtl/model
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>
#include <algorithm>
#include "filt_capture.h"
using I = int64_t;
static const int qm[32] = {192, 176, 160, 144, 128, 120, 112, 104, 96, 88, 80, 72, 64, 60, 56, 52,
                           48, 44, 40, 36, 32, 30, 28, 26, 24, 22, 20, 18, 16, 15, 14, 13};
static I ceilshr(I n, int s) { return -((-n) >> s); }
static void step(I &L, I &B, I x, int F, int Q) {
    int k = F >= 0x1ffe ? 512 : 256 + ((F >> 1) & 255), s = 24 - (F >> 9);
    I d = 2 * ceilshr(qm[Q] * B, 8);
    B += (k * (x - L - d)) >> s;
    L += ceilshr(k * B, s);
}
static const char *conv_name[] = {"floor", "ceil", "toward0", "half-up", "half-even", "away"};
static I conv(I s16, int m) {
    I fl = s16 >> 1;
    bool odd = s16 & 1;
    switch (m) {
    case 0: return fl;
    case 1: return fl + odd;
    case 2: return s16 < 0 ? fl + odd : fl;
    case 3: return fl + odd;              // half up == ceil for a single dropped bit
    case 4: return odd ? (fl & 1 ? fl + 1 : fl) : fl;
    default: return s16 < 0 ? fl : fl + odd;
    }
}
int main() {
    const int F[3] = {0x1ffe, 0x1e00, 0x1c00}, Q[3] = {4, 4, 31};
    int total = 0, fullc[6] = {0};
    for (int b = 0; b < 3; b++) {
        auto c = cap("tests/filt_frac/hw/ff_" + std::to_string(b));
        unsigned on = 1;
        while (on < c.n && !c.v[on * 4 + 3]) on++;
        int odd = 0, neg_odd = 0;
        for (unsigned n = on; n < c.n; n++) { int v = c.v[n * 4 + 3]; odd += v & 1; neg_odd += (v & 1) && v < 0; }
        printf("ff_%d: onset %u, %u samples, reference odd s16 %d (negative %d)\n", b, on, c.n - on, odd, neg_odd);
        for (int k = 0; k < 3; k++) {
            total++;
            printf("  stream %d F %04x Q %2d:", k, F[k], Q[k]);
            for (int m = 0; m < 6; m++) {
                int best = 0;
                for (int b0 = -256; b0 <= 256 && best < (int)(c.n - on); b0++) {
                    I L = -I(c.v[(on - 1) * 4 + k]) / 2, B = b0;
                    unsigned n = on;
                    for (; n < c.n; n++) {
                        step(L, B, conv(c.v[n * 4 + 3], m), F[k], Q[k]);
                        if (std::clamp<I>(-2 * L, -524288, 524287) != c.v[n * 4 + k]) break;
                    }
                    best = std::max(best, (int)(n - on));
                }
                fullc[m] += best == (int)(c.n - on);
                printf(" %s %d", conv_name[m], best);
            }
            printf("\n");
        }
    }
    printf("full streams per conversion (of %d):", total);
    for (int m = 0; m < 6; m++) printf(" %s %d", conv_name[m], fullc[m]);
    printf("\n");
    return 0;
}
