// filt_voff.cpp -- where the level (TL) multiply sits relative to the slot filter when VOFF = 0 (tests/filt_voff).
// Streams 0, 1: filter + VOFF=0 at TL t0/t1; stream 2: the same filter with VOFF=1; stream 3: the input (x8 = MIXS/2).
// Level: a = 4 TL (AEG at 0, no ALFO), M = 127 - (a & 63), shift 7 + (a >> 6)  (tests/sgc_level), lev(v) = floor(v M / 2^sh).
// Candidates for the VOFF=0 MIXS, with y16 = clamp(-2 low) the filter output in 1/16 units:
//   A  after:        (lev(y16) >> 4) * 16                (the model; level on the 1/8 value is identical)
//   F  after, whole: lev(y16 >> 4) * 16                  (filter output cut to whole samples first)
//   C  before:       filter fed with lev(x16) >> 1, output (clamp(-2 low) >> 4) * 16
//   E  after, 1/16:  lev(y16)
// Every candidate is run from every start state (low in [-64, 64], band in [-256, 256]) and must match to the end.
// Build: make -C tools filt_voff (-> build/tools/filt_voff) ; run from caique-rtl/model
#include <cstdio>
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
static I lev(I v, int tl) { int a = 4 * tl; I M = 127 - (a & 63); return (v * M) >> (7 + (a >> 6)); }
static I y16(I L) { return std::clamp<I>(-2 * L, -524288, 524287); }
static I out(char cand, I L, int tl) {
    switch (cand) {
    case 'A': return (lev(y16(L), tl) >> 4) * 16;
    case 'F': return lev(y16(L) >> 4, tl) * 16;
    case 'C': return (y16(L) >> 4) * 16;
    default: return lev(y16(L), tl);
    }
}
int main() {
    const struct { int F, Q, tl[2]; } bt[] = {{0x1c00, 4, {0x00, 0x13}}, {0x1e00, 4, {0x2a, 0x55}},
                                             {0x1c00, 31, {0x07, 0x40}}, {0x1a00, 16, {0x01, 0xa3}}};
    const char cands[] = "AFCE";
    int full[4] = {0}, total = 0;
    for (int b = 0; b < 4; b++) {
        auto c = cap("tests/filt_voff/hw/fv_" + std::to_string(b));
        unsigned on = 1;
        while (on < c.n && !c.v[on * 4 + 3]) on++;
        int n16 = 0, nn = 0;
        for (unsigned n = on; n < c.n; n++) for (int k = 0; k < 2; k++) { nn++; n16 += (c.v[n * 4 + k] & 15) == 0; }
        printf("fv_%d F %04x Q %d: onset %u, %u samples; VOFF=0 MIXS multiples of 16: %d/%d\n", b, bt[b].F, bt[b].Q, on,
               c.n - on, n16, nn);
        for (int k = 0; k < 3; k++) {
            int tl = k < 2 ? bt[b].tl[k] : -1;
            if (k < 2) total++;
            printf("  stream %d %s:", k, k < 2 ? ("TL " + std::to_string(tl)).c_str() : "VOFF=1");
            for (int ci = 0; ci < (k < 2 ? 4 : 1); ci++) {
                char cand = cands[ci];
                int best = 0;
                for (I L0 = -64; L0 <= 64 && best < (int)(c.n - on); L0++) {
                    if (k == 2 && y16(L0) != c.v[(on - 1) * 4 + k]) continue;
                    for (I B0 = -256; B0 <= 256 && best < (int)(c.n - on); B0++) {
                        I L = L0, B = B0;
                        unsigned n = on;
                        for (; n < c.n; n++) {
                            I x16 = c.v[n * 4 + 3];
                            I x8 = (k < 2 && cand == 'C') ? lev(x16, tl) >> 1 : x16 >> 1;
                            step(L, B, x8, bt[b].F, bt[b].Q);
                            I o = k == 2 ? y16(L) : out(cand, L, tl);
                            if (o != c.v[n * 4 + k]) break;
                        }
                        best = std::max(best, (int)(n - on));
                    }
                }
                if (k < 2) full[ci] += best == (int)(c.n - on);
                printf(" %c %d", k < 2 ? cand : '-', best);
            }
            printf("\n");
        }
    }
    printf("full VOFF=0 streams (of %d):", total);
    for (int ci = 0; ci < 4; ci++) printf(" %c %d", cands[ci], full[ci]);
    printf("\n");
    return 0;
}
