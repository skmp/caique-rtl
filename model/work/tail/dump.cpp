// dump.cpp -- print samples [from,to) of every stream of a CAP1 capture with MDEC_CT: dump <prefix> <c0 hex> <from> <to>
#include "../../tools/filt_capture.h"
#include <cstdlib>
int main(int argc, char **argv) {
    auto c = cap(argv[1]); uint32_t c0 = strtoul(argv[2], 0, 16); int from = atoi(argv[3]), to = atoi(argv[4]);
    for (int i = from; i < to && i < (int)c.n; i++) {
        uint32_t md = (c0 - c.first - i) & 0xFFFF;
        printf("%6d md %04x %s", i, md, md & 1 ? "odd " : "EVEN");
        for (unsigned k = 0; k < c.ns; k++) printf(" %9d", c.v[i * c.ns + k]);
        puts("");
    }
}
