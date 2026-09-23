// eg_keys.cpp -- key timing per slot from the eg_lock "keys" runs (AR 31 / RR 31, 16 key-on/off cycles on four
// slots, constant 0x7FFF input).  For every cycle and slot: the onset sample (key-on level 496 = attenuation 0x280),
// how many samples that level lasts (1 or 2: the first attack step comes on the first envelope clock after the onset),
// the sample of the first release step, and the MDEC_CT parity of each.  Under the ring lock (tools/eg_phase.cpp)
// the first step after any key event must be on an even MDEC_CT sample, and a key-on / key-off on an even sample
// never steps on that sample.  Slots that see the same KYONEX one sample apart show where the write landed within
// the sample (slot processing order).
//   eg_keys <capture prefix> <c0 hex>        Build: make -C tools eg_keys (-> build/tools/eg_keys)
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <string>
#include "filt_capture.h"
int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: eg_keys <prefix> <c0 hex>\n"); return 2; }
    auto c = cap(argv[1]);
    uint32_t c0 = strtoul(argv[2], 0, 16);
    auto md = [&](int i) { return (c0 - c.first - (uint32_t)i) & 0xFFFF; };
    int bad = 0, cycles = 0, onsets_diff = 0, drops_diff = 0;
    int i = 0;
    while (i < (int)c.n) {
        // next cycle: first sample where any stream is non-zero
        int on = -1;
        for (; i < (int)c.n; i++) { bool nz = false; for (unsigned k = 0; k < c.ns; k++) nz |= c.v[i * c.ns + k] != 0; if (nz) { on = i; break; } }
        if (on < 0) break;
        cycles++;
        printf("cycle %2d:", cycles);
        int onset[4], hold[4], drop[4], end = on;
        for (unsigned k = 0; k < c.ns; k++) {
            int o = on; while (o < (int)c.n && !c.v[o * c.ns + k]) o++;
            onset[k] = o;
            int h = 0; while (o + h < (int)c.n && c.v[(o + h) * c.ns + k] == 496) h++;
            hold[k] = h;
            // full level 520176 (a = 0) is reached, then the release: first sample below the running value after it
            int d = o + h; while (d < (int)c.n && c.v[d * c.ns + k] != 520176) d++;
            while (d + 1 < (int)c.n && c.v[(d + 1) * c.ns + k] == 520176) d++;
            drop[k] = d + 1 < (int)c.n ? d + 1 : -1;
            int e = drop[k] > 0 ? drop[k] : d; while (e < (int)c.n && c.v[e * c.ns + k]) e++;
            if (e > end) end = e;
            printf("  s%u on %d(%s) hold %d step@%d(%s) rel@%d(%s)", k, o, md(o) & 1 ? "odd" : "even", h, o + h, md(o + h) & 1 ? "odd" : "even",
                   drop[k], drop[k] > 0 ? (md(drop[k]) & 1 ? "odd" : "even") : "-");
            if (md(o + h) & 1) bad++;                       // the first attack step must be on an even sample
            if (drop[k] > 0 && (md(drop[k]) & 1)) bad++;    // and the first release step too
            if (h != 1 && h != 2) bad++;
            // an even onset never steps on itself: hold must be 2; an odd onset: hold 1
            if (((md(o) & 1) == 0 && h != 2) || ((md(o) & 1) == 1 && h != 1)) bad++;
        }
        for (unsigned k = 1; k < c.ns; k++) { onsets_diff += onset[k] != onset[0]; drops_diff += drop[k] != drop[0]; }
        printf("\n");
        i = end + 1;
    }
    printf("%s: %d cycles, %d rule violations, onsets differing between slots %d, release steps differing %d\n", argv[1], cycles, bad, onsets_diff, drops_diff);
    return bad != 0;
}
