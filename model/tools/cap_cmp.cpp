// cap_cmp.cpp -- compare two captures (cases/cap.h .hdr/.bin), e.g. console vs model, after aligning both at the
// first non-zero sample of a chosen stream.  Replaces tools/cap.py cmp (C++ only).
//   cap_cmp <prefix A> <prefix B> [align stream (default: first stream that ever becomes non-zero)] [skip]
// Per stream: identical count from the aligned onset (+skip samples), first mismatch.
// Build: make -C tools cap_cmp (-> build/tools/cap_cmp)
#include <cstdio>
#include <cstdlib>
#include <string>
#include "filt_capture.h"

static long onset(const Capture &c, unsigned s) {
    for (unsigned n = 0; n < c.n; n++) if (c.v[n * c.ns + s]) return n;
    return -1;
}
int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: cap_cmp A B [align_stream] [skip]\n"); return 2; }
    Capture a = cap(argv[1]), b = cap(argv[2]);
    int al = argc > 3 ? atoi(argv[3]) : -1;
    long skip = argc > 4 ? atol(argv[4]) : 0;
    if (a.ns != b.ns) { printf("stream counts differ: %u vs %u\n", a.ns, b.ns); return 1; }
    if (al < 0) for (unsigned s = 0; s < a.ns && al < 0; s++) if (onset(a, s) >= 0) al = s;
    long oa = onset(a, al), ob = onset(b, al);
    if (oa < 0 || ob < 0) { printf("align stream %d silent (A %ld, B %ld)\n", al, oa, ob); return 1; }
    long m = std::min<long>(a.n - oa, b.n - ob) - skip;
    int bad = 0;
    printf("aligned on stream %d: A onset %ld, B onset %ld, %ld samples compared (skip %ld)\n", al, oa, ob, m, skip);
    for (unsigned s = 0; s < a.ns; s++) {
        long diff = 0, first = -1;
        for (long i = skip; i < skip + m; i++) {
            int32_t x = a.v[(oa + i) * a.ns + s], y = b.v[(ob + i) * b.ns + s];
            if (x != y) { if (first < 0) first = i; diff++; }
        }
        printf("  stream %u: %ld/%ld differ", s, diff, m);
        if (first >= 0)
            printf(", first at +%ld: A %d B %d", first, a.v[(oa + first) * a.ns + s], b.v[(ob + first) * b.ns + s]);
        printf("\n");
        bad |= diff != 0;
    }
    return bad;
}
