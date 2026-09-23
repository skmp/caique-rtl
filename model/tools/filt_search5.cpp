// filt_search5.cpp -- hypothesis: the SVF's band state is stored in the AICA 16-bit float format (src/dsp_float.h
// PACK/UNPACK on a 24-bit container, 12 significant bits), low kept exactly.  band is placed in the container as
// band << BS (BS = container scaling).  Other parameters: state fraction bits FB (1/2^FB sample units), product
// rounding per term (0 floor, 1 toward zero, 2 half away, 3 half up), update order.
// Score: total mismatches over the given datasets (lower is better) and first mismatch.
// Build: make -C tools filt_search5 (-> build/tools/filt_search5) ; run: filt_search5 F Q [F Q ...]
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <vector>
#include <algorithm>
#include "../src/dsp_float.h"

struct DS { int F, Q, L, pre; std::vector<int32_t> y; };
static std::vector<DS> ds;
static std::vector<int16_t> X;
static inline int64_t rnd(int64_t v, int sh, int mode) {
    if (sh <= 0) return v << -sh;
    int64_t a = v < 0 ? -v : v, r;
    switch (mode) {
    case 0: return v >> sh;
    case 1: r = a >> sh; break;
    case 2: r = (a + (1LL << (sh - 1))) >> sh; break;
    default: return (v + (1LL << (sh - 1))) >> sh;
    }
    return v < 0 ? -r : r;
}
static const int q128[32] = {192, 176, 160, 144, 128, 120, 112, 104, 96, 88, 80, 72, 64, 60, 56, 52,
                             48, 44, 40, 36, 32, 30, 28, 26, 24, 22, 20, 18, 16, 15, 14, 13};
struct Hyp { int FB, BS, rx, rl, rq, rb, order, qpack; };

static long run(const DS &d, const Hyp &h, int delay, int *first) {
    int e = d.F >> 9, m = d.F & 0x1FF;
    int64_t k = 256 + (m >> 1);
    int sh = 24 - e;
    int64_t kq = k * q128[d.Q];
    int64_t low = (int64_t)(-d.y[delay - 1] / 2) << (h.FB - 3), band = 0;
    long bad = 0;
    *first = 1 << 30;
    int L = d.L - delay - 2;
    auto q = [&](int64_t b) -> int64_t {
        if (!h.qpack) return b;
        int32_t c = (int32_t)(b << h.BS);
        if (c > 0x7FFFFF) c = 0x7FFFFF;
        if (c < -0x800000) c = -0x800000;
        return (int64_t)dsp_unpack(dsp_pack(c)) >> h.BS;
    };
    for (int n = 0; n < L; n++) {
        int64_t x = (int64_t)X[n] << h.FB;
        int64_t nb, nl;
        if (h.order == 0) {
            nb = band + rnd(k * x, sh, h.rx) - rnd(k * low, sh, h.rl) - rnd(kq * band, sh + 7, h.rq);
            nb = q(nb);
            nl = low + rnd(k * nb, sh, h.rb);
        } else {
            nl = low + rnd(k * band, sh, h.rb);
            nb = band + rnd(k * x, sh, h.rx) - rnd(k * nl, sh, h.rl) - rnd(kq * band, sh + 7, h.rq);
            nb = q(nb);
        }
        band = nb; low = nl;
        int64_t o = rnd(low, h.FB - 3, 0);
        if ((int32_t)(-2 * o) != d.y[n + delay]) { if (*first == (1 << 30)) *first = n; bad++; }
    }
    return bad;
}

int main(int argc, char **argv) {
    FILE *f = fopen("work/filt/aligned.bin", "rb");
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
    if (fread(X.data(), 2, 3200, f) != 3200) return 1;
    fclose(f);
    std::vector<const DS *> use;
    for (int a = 1; a + 1 < argc; a += 2) {
        int F = (int)strtol(argv[a], 0, 16), Q = atoi(argv[a + 1]);
        for (auto &d : ds) if (d.F == F && d.Q == Q) use.push_back(&d);
    }
    struct R { long bad; int first; Hyp h; int delay; };
    std::vector<R> res;
    for (int qpack = 0; qpack < 2; qpack++)
    for (int FB = 3; FB <= 5; FB++)
    for (int BS = -4; BS <= 6; BS++)
    for (int order = 0; order < 2; order++)
    for (int rx = 0; rx < 4; rx++) for (int rl = 0; rl < 4; rl++) for (int rq = 0; rq < 4; rq++) for (int rb = 0; rb < 4; rb++)
    for (int delay = 7; delay <= 8; delay++) {
        if (!qpack && BS) continue;
        Hyp h{FB, BS, rx, rl, rq, rb, order, qpack};
        long tot = 0; int fm = 1 << 30;
        for (auto *d : use) { int fi; tot += run(*d, h, delay, &fi); fm = std::min(fm, fi); }
        res.push_back({tot, fm, h, delay});
    }
    std::sort(res.begin(), res.end(), [](const R &a, const R &b) { return a.bad < b.bad; });
    for (int i = 0; i < 10 && i < (int)res.size(); i++) {
        auto &r = res[i];
        printf("mismatches %6ld (first %5d): pack %d FB %d BS %2d order %d r x%d l%d q%d b%d delay %d\n", r.bad, r.first,
               r.h.qpack, r.h.FB, r.h.BS, r.h.order, r.h.rx, r.h.rl, r.h.rq, r.h.rb, r.delay);
    }
    return 0;
}
