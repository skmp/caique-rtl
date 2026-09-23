// filt_search.cpp -- bit-exact search for the AICA slot filter's fixed-point form.
// Input: work/filt/aligned.bin (per dataset: F, Q, L, pre, int32 MIXS[L]) and work/filt/input.bin (int16 x[]),
// exported from the console captures of tests/filt_id by tools/filt.py.
// Hypotheses: Chamberlin SVF with integer state in 2^-FB sample units, coefficient f with FBITS fraction bits,
// q exact (per Q, from the float fit), several rounding modes / update orders / output quantizations.
// Build: g++ -O2 -o work/filt/filt_search tools/filt_search.cpp ; run from caique-rtl/model.
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <vector>
#include <algorithm>
#include <cstring>

struct DS { int F, Q, L, pre; std::vector<int32_t> y; };
static std::vector<DS> ds;
static std::vector<int16_t> X;

static inline int64_t rnd(int64_t v, int sh, int mode) { // v / 2^sh
    if (sh <= 0) return v << -sh;
    switch (mode) {
    case 0: return v >> sh;                                   // floor
    case 1: return v >= 0 ? (v >> sh) : -((-v) >> sh);        // toward zero
    case 2: return (v + (1LL << (sh - 1))) >> sh;             // round half up
    default: return -((-v) >> sh);                            // ceil
    }
}

struct Hyp { int FBITS, QFB, rm1, rm2, rm3, rm4, fqmode; };

static double qval(int Q) { return Q == 0 ? 1.5 : Q == 4 ? 1.0 : Q == 8 ? 0.75 : Q == 16 ? 0.375 : -1; }

// state in 1/8 sample units: band += R1(f*x) - R2(f*low) - R3(fq*band); low += R4(f*band); MIXS = -2*low
// The initial state: low from the first output sample, band chosen (-512..512) so the pre-impulse samples match.
static long sim(const DS &d, const Hyp &h, int delay, long cap, int64_t band0, int nmax) {
    int e = d.F >> 9, m = d.F & 0x1FF;
    int64_t f = rnd(((int64_t)(512 + m) << e), 25 - h.FBITS, 0);   // f / 2^FBITS
    double q = qval(d.Q);
    if (q < 0) return -1;
    int64_t fq;
    if (h.fqmode == 0) fq = (int64_t)(q * f * (1 << (h.QFB - h.FBITS)));
    else fq = (int64_t)(q * (double)((int64_t)(512 + m) << e) / (double)(1LL << (25 - h.QFB)));
    // y index i corresponds to input n = i - delay
    int64_t low = -(int64_t)d.y[delay > 0 ? delay - 1 : 0] / 2, band = band0;
    long bad = 0;
    int L = d.L - 2;
    for (int n = 0; n < L && n < nmax; n++) {
        int64_t x = (int64_t)X[n] << 3;
        band += rnd(f * x, h.FBITS, h.rm1) - rnd(f * low, h.FBITS, h.rm2) - rnd(fq * band, h.QFB, h.rm3);
        low += rnd(f * band, h.FBITS, h.rm4);
        int idx = n + delay;
        if (idx < 0 || idx >= d.L) continue;
        if ((int32_t)(-2 * low) != d.y[idx]) { if (++bad > cap) return bad; }
    }
    return bad;
}
static long run(const DS &d, const Hyp &h, int delay, long cap) {
    for (int64_t b0 = 0; b0 <= 512; b0 = b0 <= 0 ? 1 - b0 : -b0) { // 0, 1, -1, 2, -2, ...
        if (sim(d, h, delay, 0, b0, 60) == 0) return sim(d, h, delay, cap, b0, 1 << 30);
    }
    return cap + 1;
}

int main(int argc, char **argv) {
    FILE *f = fopen("work/filt/aligned.bin", "rb");
    if (!f) { perror("aligned.bin"); return 1; }
    int hdr[4];
    while (fread(hdr, 4, 4, f) == 4) {
        DS d; d.F = hdr[0]; d.Q = hdr[1]; d.L = hdr[2]; d.pre = hdr[3];
        d.y.resize(d.L);
        if (fread(d.y.data(), 4, d.L, f) != (size_t)d.L) break;
        ds.push_back(d);
    }
    fclose(f);
    f = fopen("work/filt/input.bin", "rb");
    X.resize(3200);
    if (fread(X.data(), 2, 3200, f) != 3200) { fprintf(stderr, "input\n"); return 1; }
    fclose(f);
    int fmin = argc > 1 ? (int)strtol(argv[1], 0, 16) : 0x1A00;
    std::vector<const DS *> use;
    for (auto &d : ds) if (d.F >= fmin && (d.Q == 0 || d.Q == 4 || d.Q == 8 || d.Q == 16)) use.push_back(&d);
    printf("%zu datasets\n", use.size());
    struct R { long bad; Hyp h; int delay; };
    // per dataset: the best hypotheses (full count)
    for (auto *d : use) {
        std::vector<R> res;
        for (int FBITS = 10; FBITS <= 16; FBITS++)
        for (int QFB = 10; QFB <= 16; QFB++)
        for (int fqmode = 0; fqmode < 2; fqmode++)
        for (int rm1 = 0; rm1 < 4; rm1++)
        for (int rm2 = 0; rm2 < 4; rm2++)
        for (int rm3 = 0; rm3 < 4; rm3++)
        for (int rm4 = 0; rm4 < 4; rm4++)
        for (int delay = 4; delay <= 12; delay++) {
            Hyp h{FBITS, QFB, rm1, rm2, rm3, rm4, fqmode};
            res.push_back({run(*d, h, delay, 200), h, delay});
        }
        std::sort(res.begin(), res.end(), [](const R &a, const R &b) { return a.bad < b.bad; });
        int nz = 0;
        for (auto &r : res) if (r.bad == 0) nz++;
        auto &r = res[0];
        printf("F %04x Q %2d: best %ld (%d exact hyps): FBITS %2d QFB %2d fqmode %d rm %d%d%d%d delay %d\n", d->F, d->Q,
               r.bad, nz, r.h.FBITS, r.h.QFB, r.h.fqmode, r.h.rm1, r.h.rm2, r.h.rm3, r.h.rm4, r.delay);
    }
    return 0;
}
