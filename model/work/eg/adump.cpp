// adump.cpp -- attenuation per sample from a constant-0x7FFF capture (level law inverted), with MDEC_CT parity and
// the implied attack increment per envelope clock.   adump <prefix> <c0 hex> <stream> [n]
#include "../../tools/filt_capture.h"
#include <cstdlib>
#include <vector>
static int32_t level_of(int a) { int M = 127 - (a & 63), k = a >> 6; return 16 * (int32_t)((32767LL * M) >> (7 + k)); }
int main(int argc, char **argv) {
    auto c = cap(argv[1]); uint32_t c0 = strtoul(argv[2], 0, 16); int k = atoi(argv[3]); int N = argc > 4 ? atoi(argv[4]) : 60;
    int on = 1; while (on < (int)c.n && !c.v[on * c.ns + k]) on++;
    std::vector<int> byl(600000, -1);
    for (int a = 1023; a >= 0; a--) byl[level_of(a)] = a;   // smallest a for a level (levels are distinct while a < ~0x300)
    int prev = -1;
    for (int i = on; i < on + N && i < (int)c.n; i++) {
        int32_t v = c.v[i * c.ns + k]; int a = v >= 0 && v < 600000 ? byl[v] : -1;
        uint32_t md = (c0 - c.first - i) & 0xFFFF;
        printf("i %d (+%d) MDEC %04x %s: level %7d a %03x", i, i - on, md, md & 1 ? "odd " : "EVEN", v, a);
        if (prev >= 0 && a >= 0 && a != prev) {
            // attack: a' = a + ((~a * inc) >> 4): find inc
            int inc = -1; for (int t = 1; t <= 8; t++) if (prev + (((~prev) * t) >> 4) == a) inc = t;
            printf("  step from %03x: inc %d", prev, inc);
        }
        puts(""); if (a >= 0) prev = a;
    }
}
