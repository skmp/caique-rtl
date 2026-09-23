// tail_dump.cpp -- print hw samples [from, to) of one stream of a CAP1 capture with MDEC_CT (c0 hex), for reading tails
#include <cstdio>
#include <cstdlib>
#include <string>
#include "../../tools/filt_capture.h"
int main(int argc, char **argv) {
    if (argc < 6) { fprintf(stderr, "tail_dump <prefix> <stream> <c0hex> <from> <to>\n"); return 2; }
    Capture c = cap(argv[1]); int k = atoi(argv[2]); uint32_t c0 = strtoul(argv[3], 0, 16); int from = atoi(argv[4]), to = atoi(argv[5]);
    for (int i = from; i < to && i < (int)c.n; i++) { uint32_t md = (c0 - c.first - i) & 0xFFFF; printf("%d %04x %s %d\n", i, md, md & 1 ? "odd" : "EVEN", c.v[(size_t)i * c.ns + k]); }
    return 0;
}
