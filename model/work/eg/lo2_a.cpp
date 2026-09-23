// lo2_a.cpp -- sgc_loop lo_2 streams 2/3: attenuation candidates per sample from the ramp input (8*CA - 0x4000, pitch 1,
// LSA/LEA loop) and the level law; prints the a range consistent with each captured value at selected samples.
#include "../../tools/filt_capture.h"
#include <cstdlib>
static int32_t lv(int a, int32_t s) { int M = 127 - (a & 63), k = a >> 6; return 16 * (int32_t)(((int64_t)s * M) >> (7 + k)); }
int main() {
    auto c = cap("tests/sgc_loop/hw/lo_2"); int on = 227;
    for (int k = 2; k <= 3; k++) {
        int lsa = k == 2 ? 2000 : 1000, lea = 3000;
        printf("stream %d (LSA %d):\n", k, lsa);
        for (int i = on + 1990; i < (int)c.n; i += (i < on + 2060 ? 4 : 128)) {
            int ca = i - on; if (ca >= lea) ca = (ca - lsa) % (lea - lsa) + lsa;
            int32_t s = 8 * ca - 0x4000, v = c.v[i * 4 + k];
            int lo = -1, hi = -1;
            for (int a = 0; a < 1024; a++) if (lv(a, s) == v) { if (lo < 0) lo = a; hi = a; }
            printf("  i %5d CA %4d s %6d level %8d -> a in [%03x, %03x]\n", i, ca, s, v, lo, hi);
        }
    }
}
