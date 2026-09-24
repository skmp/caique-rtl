// replay_fit.cpp -- the replay parameters of one run (TODO 3.2): reads the preamble's replay_log.txt and replay.hdr /
// replay.bin (cases/common/replay.c) in DIR and writes DIR/replay.txt, "mdec X lfsr L K k":
//   X  MDEC_CT of the preamble's sync sample (the DSP sample whose counter word the SH4 waited for);
//   L  the noise LFSR at the sample boundary after it (the ph 0 at which MDEC_CT = X - 1: cycle-model/io_cycle.cpp);
//   k  the envelope counter constant (eg_cnt = K - MDEC_CT/2).
// K: streams 0-2 (decay 1 at effective R 3 / 13 / 45 from an R 63 attack) with kfit's rules (kfit_core.h), searched over
// all 2^14 values per stream from the key-on to 400 samples before the key-off mark; the sets are intersected (R 3 alone
// pins K mod 16384 once its 8-tick cycle is inside the window).
// LFSR: stream 3 is a noise slot at VOFF / LPOFF / IMXL 15, so capture sample i (DSP counter X_i = c0 - first - i) holds
// the slot-3 byte of the previous sweep << 12: byte_i = low8(step^4(f(X_i + 1))), f(X) = the LFSR at the ph 0 at which
// MDEC_CT = X, f(X - 1) = step^64(f(X)) (the cycle model: one step per slot, slot k samples after k + 1 steps).  Every
// 17-bit state is tried at the first sample after the key-on and checked on every sample up to the key-off window.
//   replay_fit [-v] DIR [PREFIX]   reads PREFIX_log.txt / PREFIX.hdr / .bin (default replay), writes PREFIX.txt; exit 0 iff K
//                                  and the LFSR are both unique and fit every sample
//   replay_fit -same A.txt B.txt   two parameter lines (B's sync later): K equal, and B's LFSR = A's stepped 64 times per
//                                  sample in between, for some number of MDEC_CT wraps w in -2..3 (printed); exit 0 iff so
// Build: make -C tools replay_fit (single file, no model link).  Run from caique-rtl/model.
#include <set>
#include "kfit_core.h"

static uint32_t lfsr_step(uint32_t l) { return (l >> 1) | ((((l >> 0) ^ (l >> 5)) & 1) << 16); }
static uint32_t lfsr_steps(uint32_t l, uint64_t n) { n %= 131071; for (uint64_t i = 0; i < n; i++) l = lfsr_step(l); return l; }   // x^17 + x^12 + 1: period 2^17 - 1

static bool read_params(const char *path, uint32_t &x, uint32_t &l, uint32_t &k) {
    FILE *f = fopen(path, "r");
    if (!f) { perror(path); return false; }
    char line[256]; bool ok = false;
    while (fgets(line, sizeof line, f)) if (sscanf(line, "mdec %x lfsr %x K %u", &x, &l, &k) == 3) ok = true;
    fclose(f);
    if (!ok) fprintf(stderr, "%s: no parameter line\n", path);
    return ok;
}
static uint32_t par_of(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    char line[256]; uint32_t p = 0;
    while (fgets(line, sizeof line, f)) { const char *q = strstr(line, " par "); if (!strncmp(line, "mdec ", 5) && q) p = (uint32_t)atoi(q + 5) & 1; }
    fclose(f);
    return p;
}
static int same(const char *a, const char *b) {
    uint32_t xa, la, ka, xb, lb, kb;
    if (!read_params(a, xa, la, ka) || !read_params(b, xb, lb, kb)) return 2;
    ka |= par_of(a) << 14; kb |= par_of(b) << 14;   // K + 16384 par
    const uint32_t d = (xa - xb) & 0xFFFF;   // samples from A's sync to B's, modulo 65536
    printf("A %s: mdec %04x lfsr %05x K %u\nB %s: mdec %04x lfsr %05x K %u\n", a, xa, la, ka, b, xb, lb, kb);
    int found = 0;
    for (int w = -2; w <= 3; w++) {
        const int64_t D = (int64_t)d + 65536 * (int64_t)w;
        if (D < 0) continue;
        if (lfsr_steps(la, 64ull * (uint64_t)D) == lb) { printf("  LFSR consistent with %lld samples between the syncs (%d wraps)\n", (long long)D, w); found++; }
    }
    if (!found) printf("  LFSR: no number of wraps in -2..3 relates the two\n");
    printf("  K and clock parity %s\n", ka == kb ? "equal" : "DIFFERENT");
    const bool ok = found == 1 && ka == kb;
    printf("replay_fit -same: %s\n", ok ? "consistent" : "INCONSISTENT");
    return ok ? 0 : 1;
}

int main(int argc, char **argv) {
    bool verbose = false; std::string dir, prefix = "replay";
    if (argc == 4 && !strcmp(argv[1], "-same")) return same(argv[2], argv[3]);
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-v")) verbose = true;
        else if (dir.empty()) dir = argv[i];
        else prefix = argv[i];
    }
    if (dir.empty()) { fprintf(stderr, "usage: replay_fit [-v] DIR [PREFIX] | replay_fit -same A.txt B.txt\n"); return 2; }
    // replay_log.txt: the stream programs, the capture line, the sync line
    Stream st[3] = {};
    bool have_st[3] = {false, false, false}, have_cap = false, have_sync = false;
    unsigned c0 = 0, sync_mdec = 0;
    {
        std::string p = dir + "/" + prefix + "_log.txt";
        FILE *f = fopen(p.c_str(), "r");
        if (!f) { perror(p.c_str()); return 2; }
        char line[512];
        while (fgets(line, sizeof line, f)) {
            int k, slot; Stream s; unsigned x; unsigned long n;
            if (sscanf(line, "replay stream %d: slot %d AR %d D1R %d DL %d D2R %d RR %d KRS %d OCT %d FNS %x", &k, &slot, &s.AR,
                       &s.D1R, &s.DL, &s.D2R, &s.RR, &s.KRS, &s.OCT, &s.FNS) == 10 && k >= 0 && k < 3) { st[k] = s; have_st[k] = true; }
            else if (sscanf(line, "replay capture: c0 %x", &x) == 1) { c0 = x; have_cap = true; }
            else if (sscanf(line, "replay sync: n %lu mdec %x", &n, &x) == 2) { sync_mdec = x; have_sync = true; }
        }
        fclose(f);
        if (!have_st[0] || !have_st[1] || !have_st[2] || !have_cap || !have_sync) { fprintf(stderr, "%s: incomplete\n", p.c_str()); return 2; }
    }
    Capture c;
    try { c = cap(dir + "/" + prefix); } catch (std::exception &e) { fprintf(stderr, "%s/%s: %s\n", dir.c_str(), prefix.c_str(), e.what()); return 2; }
    if (c.ns != 4) { fprintf(stderr, "%s/%s: %u streams, expected 4\n", dir.c_str(), prefix.c_str(), c.ns); return 2; }
    int m3 = -1, m4 = -1;
    read_marks(dir + "/" + prefix, c, m3, m4);
    int on = 1;
    while (on < (int)c.n && !c.v[(size_t)on * c.ns]) on++;
    const int end = m3 >= 0 ? std::min<int>(c.n, m3 - 400) : (int)c.n;
    printf("replay_fit %s: %u samples, first %u, c0 %04x, key-on sample %d (MDEC_CT %04x), fit window %d..%d, sync MDEC_CT %04x\n",
           dir.c_str(), c.n, c.first, c0, on, (c0 - c.first - (unsigned)on) & 0xFFFF, on, end, sync_mdec);
    int bad = 0;

    // ---- K (and the envelope clock's MDEC_CT parity: candidates K + 16384 p) ----
    std::vector<uint32_t> common; bool first = true;
    for (int k = 0; k < 3; k++) {
        std::vector<uint32_t> full;
        int best = -1; uint32_t bestK = 0;
        for (uint32_t Kp = 0; Kp < 32768; Kp++) {
            k_par = Kp >> 14;
            int m = sim(c, k, st[k], c0, on, end, Kp & 0x3FFF, 1 << 30, 1);
            if (m > best) { best = m; bestK = Kp; }
            if (m == end - on) full.push_back(Kp);
        }
        k_par = bestK >> 14;
        printf("  stream %d (D1R %d, R %u): ", k, st[k].D1R, eff_rate(st[k], st[k].D1R));
        if (full.empty()) {
            Mismatch mm; sim(c, k, st[k], c0, on, end, bestK & 0x3FFF, 1 << 30, 1, &mm);
            printf("NO K fits: best %d/%d at K %u, first mismatch i %d a %03x level %d capture %d\n", best, end - on, bestK, mm.i, mm.a,
                   mm.lv, c.v[(size_t)mm.i * c.ns + k]);
            bad++;
            continue;
        }
        printf("%zu K fit", full.size());
        if (verbose) for (size_t i = 0; i < full.size() && i < 8; i++) printf(" %u", full[i]);
        printf("\n");
        if (first) { common = full; first = false; }
        else { std::vector<uint32_t> t; std::set_intersection(common.begin(), common.end(), full.begin(), full.end(), std::back_inserter(t)); common = t; }
    }
    long K = -1, par = 0;
    if (common.size() == 1) { K = common[0] & 0x3FFF; par = common[0] >> 14; printf("  K = %ld, envelope clock on %s MDEC_CT\n", K, par ? "ODD" : "even"); }
    else { printf("  K: %zu candidates after intersecting the streams\n", common.size()); bad++; }

    // ---- LFSR ----
    const int i0 = on + 1;
    std::vector<uint8_t> b;
    int lowbits = 0;
    for (int i = i0; i < end; i++) {
        int32_t v = c.v[(size_t)i * c.ns + 3];
        if (v & 0xFFF) lowbits++;
        b.push_back((uint8_t)((v >> 12) & 0xFF));
    }
    if (lowbits) { printf("  stream 3: %d samples with bits 11:0 set (not a noise byte << 12)\n", lowbits); bad++; }
    std::vector<uint32_t> fits;
    for (uint32_t F = 1; F < 0x20000; F++) {   // F = f(X_i0 + 1)
        uint32_t s = F;
        size_t j = 0;
        for (; j < b.size(); j++) {
            if ((lfsr_steps(s, 4) & 0xFF) != b[j]) break;
            s = lfsr_steps(s, 64);
        }
        if (j == b.size()) fits.push_back(F);
    }
    long L = -1;
    const uint32_t x0 = (c0 - c.first - (uint32_t)i0) & 0xFFFF;              // DSP counter of sample i0
    if (fits.size() == 1) {
        const uint32_t D = ((x0 + 1) - (sync_mdec - 1)) & 0xFFFF;            // samples from f(x0 + 1) to f(sync - 1)
        L = lfsr_steps(fits[0], 64ull * D);
        printf("  LFSR: f(%04x) = %05x fits all %zu noise samples; f(sync %04x - 1) = %05lx (%u samples on)\n", (x0 + 1) & 0xFFFF,
               fits[0], b.size(), sync_mdec, L, D);
    } else { printf("  LFSR: %zu states fit the %zu noise samples\n", fits.size(), b.size()); bad++; }

    if (bad) { printf("replay_fit %s: FAIL\n", dir.c_str()); return 1; }
    std::string p = dir + "/" + prefix + ".txt";
    FILE *f = fopen(p.c_str(), "w");
    if (!f) { perror(p.c_str()); return 2; }
    fprintf(f, "# tools/replay_fit %s: sync sample MDEC_CT, the LFSR at the sample boundary after it, the envelope constant\n",
            dir.c_str());
    fprintf(f, "mdec %04x lfsr %05lx K %ld par %ld\n", sync_mdec, L, K, par);
    fclose(f);
    printf("replay_fit %s: mdec %04x lfsr %05lx K %ld par %ld -> %s\n", dir.c_str(), sync_mdec, L, K, par, p.c_str());
    return 0;
}
