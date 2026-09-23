// scratch: aeg_dl0 -- per stream, the level after the attack reaches full, and any dip before key-off
#include "../tools/filt_capture.h"
int main(int argc, char **argv) {
    auto c = cap(argv[1]);
    for (unsigned k = 0; k < c.ns; k++) {
        unsigned on = 0; while (on < c.n && !c.v[on * c.ns + k]) on++;
        int32_t mx = 0; unsigned tmax = 0;
        for (unsigned n = on; n < c.n; n++) if (c.v[n * c.ns + k] > mx) { mx = c.v[n * c.ns + k]; tmax = n; }
        // after reaching full level: first sample below it (before the release, which is where the level falls for good)
        unsigned first_dip = 0; int32_t dipv = 0;
        for (unsigned n = tmax; n < c.n; n++) if (c.v[n * c.ns + k] < mx) { first_dip = n; dipv = c.v[n * c.ns + k]; break; }
        printf("stream %u: onset %u, full level %d first reached at +%u, first sample below it at +%u (%d), end %d\n",
               k, on, mx, tmax - on, first_dip ? first_dip - on : 0, dipv, c.v[(c.n - 1) * c.ns + k]);
    }
}
