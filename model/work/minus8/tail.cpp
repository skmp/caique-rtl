// tail.cpp -- print one stream of a CAP1 capture as RLE runs over a sample range, with the MDEC_CT of each run's
// first sample (MDEC_CT of sample i = (c0 - n_first - i) & 0xFFFF, c0 from the case's cap_start line).
//   tail <capture prefix> <stream> <c0 hex> <from> <to>      (from < 0: count from the end; to <= 0: to the end)
// Build: g++ -O2 -std=c++17 -o build/work/minus8_tail work/minus8/tail.cpp   (scratch, work/minus8)
#include "../../tools/filt_capture.h"
#include <cstdlib>
int main(int argc, char **argv) {
    if (argc < 6) { fprintf(stderr, "usage: tail <prefix> <stream> <c0hex> <from> <to>\n"); return 2; }
    auto c = cap(argv[1]);
    int k = atoi(argv[2]);
    unsigned c0 = (unsigned)strtoul(argv[3], nullptr, 16);
    long from = atol(argv[4]), to = atol(argv[5]);
    if (from < 0) from += c.n;
    if (to <= 0) to += c.n;
    if (from < 0) from = 0;
    if (to > (long)c.n) to = c.n;
    printf("%s stream %d: n %u n_first %u c0 %04x; samples [%ld, %ld) as start(MDEC_CT p):value*len\n", argv[1], k, c.n, c.first, c0, from, to);
    int prev = c.v[from * c.ns + k];
    long start = from;
    int printed = 0;
    for (long i = from + 1; i <= to; i++) {
        int cur = i < to ? c.v[i * c.ns + k] : 0x7fffffff;
        if (cur != prev) {
            unsigned md = (c0 - c.first - (unsigned)start) & 0xFFFF;
            printf(" %ld(%04x%c):%d*%ld", start, md, (md & 1) ? 'o' : 'E', prev, i - start);
            if (++printed % 5 == 0) puts("");
            prev = cur;
            start = i;
        }
    }
    puts("");
    return 0;
}
