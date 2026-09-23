// filt_wide.cpp -- integrator width of the slot filter (tests/filt_wide: resonant Q31 square-wave drive).
// The verified recurrence (NOTES.md "Slot filter") is run unbounded and with both integrators clamped or wrapped
// to W-bit two's complement (1/8-sample units), W = 20..25; each variant must reproduce every captured sample
// (initial low from the capture, initial band searched in [-256, 256]).  Also reports how large |low| and |band|
// got on the matching unbounded trajectory and how many output samples sat on the MIXS rails.
// Build: make -C tools filt_wide (-> build/tools/filt_wide) ; run from caique-rtl/model
#include <cstdio>
#include <string>
#include <vector>
#include <algorithm>
#include "filt_capture.h"
using I = int64_t;
static const int qm[32] = {192, 176, 160, 144, 128, 120, 112, 104, 96, 88, 80, 72, 64, 60, 56, 52,
                           48, 44, 40, 36, 32, 30, 28, 26, 24, 22, 20, 18, 16, 15, 14, 13};
static I ceilshr(I n, int s) { return -((-n) >> s); }
static I lim(I v, int W, int mode) {   // mode 0 unbounded, 1 clamp, 2 wrap
    if (!mode) return v;
    I hi = (I(1) << (W - 1)) - 1, lo = -(I(1) << (W - 1));
    if (mode == 1) return std::clamp(v, lo, hi);
    I m = I(1) << W;
    v &= m - 1;
    return v > hi ? v - m : v;
}
struct Res { int matched; I maxL, maxB; };
static Res run(const Capture &c, int k, int F, int Q, unsigned on, int W, int mode) {
    int kk = F >= 0x1ffe ? 512 : 256 + ((F >> 1) & 255), s = 24 - (F >> 9);
    Res best{0, 0, 0};
    for (int b0 = -256; b0 <= 256 && best.matched < (int)(c.n - on); b0++) {
        I L = -I(c.v[(on - 1) * 4 + k]) / 2, B = b0, mL = 0, mB = 0;
        unsigned n = on;
        for (; n < c.n; n++) {
            I x = I(c.v[n * 4 + 3] / 16) * 8;
            I d = 2 * ceilshr(qm[Q] * B, 8);
            B = lim(B + ((kk * (x - L - d)) >> s), W, mode);
            L = lim(L + ceilshr(kk * B, s), W, mode);
            mL = std::max(mL, std::abs(L)); mB = std::max(mB, std::abs(B));
            if (std::clamp<I>(-2 * L, -524288, 524287) != c.v[n * 4 + k]) break;
        }
        if ((int)(n - on) > best.matched) best = {int(n - on), mL, mB};
    }
    return best;
}
int main() {
    const struct { int F, Q[3]; } bt[] = {{0x1800, {31, 28, 24}}, {0x1a00, {31, 30, 20}}, {0x1600, {31, 29, 16}},
                                         {0x1400, {31, 31, 31}}};
    for (int b = 0; b < 4; b++) {
        auto c = cap("tests/filt_wide/hw/fw_" + std::to_string(b));
        unsigned on = 1;
        while (on < c.n && !c.v[on * 4 + 3]) on++;
        for (int k = 0; k < 3; k++) {
            int rails = 0;
            for (unsigned n = on; n < c.n; n++) rails += c.v[n * 4 + k] == -524288 || c.v[n * 4 + k] == 524287;
            Res u = run(c, k, bt[b].F, bt[b].Q[k], on, 64, 0);
            printf("fw_%d s%d F %04x Q %2d: %u samples, %d on the rails | unbounded %d (max |low| %lld = 2^%.2f, |band| %lld)",
                   b, k, bt[b].F, bt[b].Q[k], c.n - on, rails, u.matched, (long long)u.maxL,
                   u.maxL > 0 ? __builtin_log2((double)u.maxL) : 0.0, (long long)u.maxB);
            printf(" | clamp/wrap W:");
            for (int W = 20; W <= 25; W++) {
                Res cl = run(c, k, bt[b].F, bt[b].Q[k], on, W, 1), wr = run(c, k, bt[b].F, bt[b].Q[k], on, W, 2);
                printf(" %d:%d/%d", W, cl.matched, wr.matched);
            }
            printf("\n");
        }
    }
    return 0;
}
