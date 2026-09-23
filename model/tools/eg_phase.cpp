// eg_phase.cpp -- is the envelope clock locked to the DSP ring counter (MDEC_CT)?
// Observation (tests/feg_track): the three batches' fitted clock parity and counter (feg_fit) agree once expressed
// through the ring position of the onset sample.  cap.h writes sample n at ring address c0 - n = MDEC_CT, so the
// MDEC_CT of capture sample i is (c0 - n_first - i) & 0xFFFF (c0 from the case's cap_start log line).
// Hypothesis: the envelope clock is every sample with EVEN MDEC_CT, and its counter is cnt = K - MDEC_CT/2 mod 2^14
// with ONE constant K (free-running counters).  A key event takes effect on the sample after the write (any parity),
// never steps on that sample, and the envelope steps on every later clock.
// This tool predicts the amplitude envelope of constant-input captures (0x7FFF, TL 0, VOFF 0, IMXL 15: level =
// 16 * floor(32767 * (127 - (a & 63)) / 2^(7 + (a >> 6)))) sample by sample: K is searched modulo the largest
// period the stream's rates can see, first on the key-on phase (up to the key-off marks), then the key-off sample
// is searched between marks 3 and 4, with and without the R < 48 counter offset (-1) measured on the FEG, and
// with the release stepping on the key-off sample itself or not.  Reports every K residue that reproduces all samples.
//   eg_phase [-rot] [-v] [runs.txt]      (run from caique-rtl/model)
// runs.txt lines: "<capture prefix> <c0 hex> <AR D1R DL D2R RR> x4"; -rot uses the rotated rows for R = 1 mod 4.
// It also prints the ring-position derivation for the three feg_track batches (fits from tools/feg_validate.cpp).
// Build: make -C tools eg_phase (-> build/tools/eg_phase)
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include "filt_capture.h"

// the measured table (src/aica_model.cpp): rows 5, 9, 13 have the double step at index 1 and 5 (OPN: 3 and 7)
static uint8_t eg_inc[17][8] = {
    {0, 1, 0, 1, 0, 1, 0, 1}, {0, 1, 0, 1, 1, 1, 0, 1}, {0, 1, 1, 1, 0, 1, 1, 1}, {0, 1, 1, 1, 1, 1, 1, 1},
    {1, 1, 1, 1, 1, 1, 1, 1}, {1, 2, 1, 1, 1, 2, 1, 1}, {1, 2, 1, 2, 1, 2, 1, 2}, {1, 2, 2, 2, 1, 2, 2, 2},
    {2, 2, 2, 2, 2, 2, 2, 2}, {2, 4, 2, 2, 2, 4, 2, 2}, {2, 4, 2, 4, 2, 4, 2, 4}, {2, 4, 4, 4, 2, 4, 4, 4},
    {4, 4, 4, 4, 4, 4, 4, 4}, {4, 8, 4, 4, 4, 8, 4, 4}, {4, 8, 4, 8, 4, 8, 4, 8}, {4, 8, 8, 8, 4, 8, 8, 8},
    {8, 8, 8, 8, 8, 8, 8, 8}};
static inline uint32_t eg_increment(uint32_t R, uint32_t cnt, uint32_t slow_off) {
    if (R == 0) return 0;
    if (R < 48) {
        cnt += slow_off;
        uint32_t shift = 11 - (R >> 2);
        if (cnt & ((1u << shift) - 1)) return 0;
        return eg_inc[R & 3][(cnt >> shift) & 7];
    }
    return eg_inc[R >= 60 ? 16 : 4 + (R - 48)][cnt & 7];
}
static uint32_t period_of(uint32_t R) {   // the counter period this rate can distinguish
    if (R == 0) return 1;
    if (R < 48) return 1u << (14 - (R >> 2));
    if (R >= 60) return 1;
    int row = 4 + (R - 48);
    return (row & 3) == 0 ? 1 : (row & 3) == 2 ? 2 : 4;
}
static int32_t level_of(int a, bool off) {
    if (off) return 0;
    int M = 127 - (a & 63), k = a >> 6;
    return 16 * (int32_t)((32767LL * M) >> (7 + k));
}
struct Stream { int AR, D1R, DL, D2R, RR; int KRS = 15, OCT = 0, FNS = 0; int LSA = 0, LEA = 0, LPSLNK = 0; };
struct Run { std::string path; uint32_t c0ring; Stream s[4]; bool ramp = false; };
static int32_t level_of_s(int a, bool off, int32_t s) {   // level of sample s (16-bit) at attenuation a
    if (off) return 0;
    int M = 127 - (a & 63), k = a >> 6;
    return 16 * (int32_t)(((int64_t)s * M) >> (7 + k));
}
static uint32_t eff_rate(const Stream &st, int re) {
    if (re == 0) return 0;
    int s = 0;
    if (st.KRS != 15) {
        int k = st.KRS + ((st.OCT & 8) ? st.OCT - 16 : st.OCT);
        s = k < 0 ? 0 : 2 * std::min(k, 15) + ((st.FNS >> 9) & 1);
    }
    return std::min(63, 2 * re + s);
}
// simulate one stream from the onset sample to 'end'; returns matched consecutive samples.
static int sim(const Capture &c, int k, const Stream &st, uint32_t c0ring, int on, int end, uint32_t K, uint32_t slow_off,
               int keyoff, int koff_same, int *mm_n = nullptr, int *mm_a = nullptr, int32_t *mm_lv = nullptr, bool ramp = false) {
    int a = 0x280, state = 0;   // 0 attack 1 decay1 2 decay2 3 release
    bool off = false;
    uint32_t CA = 0; bool armed = false;
    for (int i = on; i < end; i++) {
        if (ramp && i > on) {   // stream_step of the previous sample: CA advances by 1 at pitch 1 (tests/sgc_loop rules)
            uint32_t nca = (CA + 1) & 0xFFFF;
            if (st.LPSLNK && state == 0 && nca >= (uint32_t)st.LSA) state = 1;
            bool was = armed;
            if (nca >= (uint32_t)st.LSA) armed = true;
            if (nca >= (uint32_t)st.LEA && was) nca = (nca - (st.LEA - st.LSA)) & 0xFFFF;
            CA = nca;
        }
        uint32_t md = (c0ring - c.first - (uint32_t)i) & 0xFFFF;
        bool clock = (md & 1) == 0;
        if (i == keyoff) state = 3;
        if (clock && i > on && !off && !(i == keyoff && !koff_same)) {
            uint32_t cnt = (K - (md >> 1)) & 0x3FFF;
            bool was_d1 = state == 1;
            int rate = state == 0 ? st.AR : state == 1 ? st.D1R : state == 2 ? st.D2R : st.RR;
            uint32_t inc = eg_increment(eff_rate(st, rate), cnt, slow_off);
            if (inc) {
                if (state == 0) { a += ((~a) * (int)inc) >> 4; if (a <= 0) { a = 0; state = 1; } }
                else { a += inc; if (a > 0x3FF) { a = 0x3FF; off = true; } }
            }
            if (was_d1 && !off && a >= (st.DL << 5)) state = 2;
        }
        int32_t lv = ramp ? level_of_s(a, off, (int32_t)(8 * (int32_t)CA - 0x4000)) : level_of(a, off);
        if (lv != c.v[i * c.ns + k]) {
            if (mm_n) { *mm_n = i; *mm_a = a; *mm_lv = lv; }
            return i - on;
        }
    }
    return end - on;
}
static void feg_derivation() {
    // feg_track: onset 138 in every batch; fits (feg_validate.cpp): c0 = counter at the key-on clock, kon = 1 when
    // the key-on clock precedes the onset (batch 2), p = parity of the clock samples relative to the onset.
    struct B { uint32_t c0ring, first; int on, p, c0, kon; } b[3] = {{0xe4be, 742, 138, 0, 4, 0}, {0xa802, 684, 138, 0, 21, 0}, {0x66ac, 621, 138, 1, 0, 1}};
    printf("feg_track batches: MDEC_CT of the onset sample and of the fitted key-on clock; K = c0 + MDEC_CT/2:\n");
    for (int i = 0; i < 3; i++) {
        uint32_t md_on = (b[i].c0ring - b[i].first - b[i].on) & 0xFFFF;
        int kon_sample = b[i].on - b[i].kon;                       // key-on clock sample (kon 1: one before the onset)
        uint32_t md_kon = (b[i].c0ring - b[i].first - kon_sample) & 0xFFFF;
        uint32_t K = (b[i].c0 + (md_kon >> 1)) & 0x3FFF;
        printf("  ft_%d: onset MDEC_CT %04x (%s), fitted clock parity p %d -> clock samples have MDEC_CT %s; key-on clock at MDEC_CT %04x, c0 %d -> K = %u (mod 32: %u, mod 8: %u)\n",
               i, md_on, (md_on & 1) ? "odd" : "even", b[i].p, ((md_on - b[i].p) & 1) ? "odd" : "even", md_kon, b[i].c0, K, K & 31, K & 7);
    }
}
int main(int argc, char **argv) {
    bool verbose = false, rot = false;
    const char *runfile = nullptr;
    long fixedK = -1; int fitrow = -1;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-v")) verbose = true;
        else if (!strcmp(argv[i], "-rot")) rot = true;
        else if (!strcmp(argv[i], "-K")) fixedK = atol(argv[++i]);
        else if (!strcmp(argv[i], "-fitrow")) fitrow = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-row")) { int r = atoi(argv[++i]); for (int j = 0; j < 8; j++) eg_inc[r][j] = (uint8_t)atoi(argv[++i]); }
        else runfile = argv[i];
    }
    if (rot) {   // -rot = the YM2612 (OPN) rows 5 / 9 / 13 as a control (double step at index 3 and 7)
        const uint8_t r5[8] = {1, 1, 1, 2, 1, 1, 1, 2}, r9[8] = {2, 2, 2, 4, 2, 2, 2, 4}, r13[8] = {4, 4, 4, 8, 4, 4, 4, 8};
        memcpy(eg_inc[5], r5, 8); memcpy(eg_inc[9], r9, 8); memcpy(eg_inc[13], r13, 8);
    }
    feg_derivation();
    std::vector<Run> runs = {
        {"tests/aeg_dl0/hw/dl0", 0x87bb, {{31, 31, 0, 0, 31}, {31, 20, 0, 0, 31}, {31, 10, 0, 0, 31}, {20, 31, 0, 10, 31}}},
    };
    if (runfile) {
        FILE *f = fopen(runfile, "r");
        if (!f) { perror(runfile); return 2; }
        char p[256]; unsigned c0;
        runs.clear();
        char line[512];
        while (fgets(line, sizeof line, f)) {
            if (sscanf(line, "%255s %x", p, &c0) != 2) continue;
            Run r; r.path = p; r.c0ring = c0; r.ramp = strstr(line, "ramp") != nullptr;
            for (int k = 0; k < 4; k++) {
                if (!fgets(line, sizeof line, f)) return 2;
                Stream &s = r.s[k];
                int got = sscanf(line, "%d %d %d %d %d %d %d %d %d %d %d", &s.AR, &s.D1R, &s.DL, &s.D2R, &s.RR, &s.KRS, &s.OCT, &s.FNS, &s.LSA, &s.LEA, &s.LPSLNK);
                if (got < 8 || (r.ramp && got < 11)) return 2;
            }
            runs.push_back(r);
        }
        fclose(f);
    }
    for (auto &r : runs) {
        auto c = cap(r.path);
        // marks from the header
        FILE *f = fopen((r.path + ".hdr").c_str(), "rb");
        uint32_t h[16 + 128] = {0};
        size_t nh = fread(h, 4, sizeof h / 4, f);
        fclose(f);
        int m3 = -1, m4 = -1;
        for (size_t i = 11; i + 1 < nh && i < 11 + 2 * h[6]; i += 2) {
            if (h[i] == 3) m3 = (int)(h[i + 1] - c.first);
            if (h[i] == 4) m4 = (int)(h[i + 1] - c.first);
        }
        int on = 1;
        while (on < (int)c.n && !c.v[on * c.ns])
            on++;
        uint32_t md_on = (r.c0ring - c.first - on) & 0xFFFF;
        printf("%s: n %u first %u onset %d (MDEC_CT %04x %s) c0 %04x key-off marks %d..%d\n", r.path.c_str(), c.n, c.first, on,
               md_on, (md_on & 1) ? "odd" : "even", r.c0ring, m3, m4);
        for (int k = 0; k < 4; k++) {
            const Stream &st = r.s[k];
            uint32_t P = 1;
            for (int re : {st.AR, st.D1R, st.D2R, st.RR}) P = std::max(P, period_of(eff_rate(st, re)));
            int on_end = m3 >= 0 ? std::min<int>(c.n, m3 - 4) : (int)c.n;
            if (fitrow >= 0) {   // which patterns of row fitrow reproduce the key-on phase at the fixed K?
                uint32_t R = eff_rate(st, st.AR);
                int row = R < 48 ? (int)(R & 3) : R >= 60 ? 16 : 4 + (int)(R - 48);
                if (row != fitrow || fixedK < 0) { printf("  stream %d: attack R %u uses row %d, skipped\n", k, R, row); continue; }
                uint8_t save[8]; memcpy(save, eg_inc[row], 8);
                int base = row < 4 ? 0 : 1 << ((row - 4) >> 2);
                int nfull = 0, bestm = -1, bestp = 0;
                printf("  stream %d AR %d (R %u, row %d): patterns reproducing %d samples of the key-on phase at K %ld:", k, st.AR, R, row, on_end - on, fixedK);
                for (int pat = 0; pat < 256; pat++) {
                    for (int j = 0; j < 8; j++) eg_inc[row][j] = row < 4 ? ((pat >> j) & 1) : (uint8_t)(base * (1 + ((pat >> j) & 1)));
                    int m = sim(c, k, st, r.c0ring, on, on_end, (uint32_t)fixedK, (uint32_t)-1, 1 << 30, 0, nullptr, nullptr, nullptr, r.ramp);
                    if (m > bestm) { bestm = m; bestp = pat; }
                    if (m == on_end - on) { nfull++; printf(" {"); for (int j = 0; j < 8; j++) printf("%s%d", j ? "," : "", eg_inc[row][j]); printf("}"); }
                }
                if (!nfull) printf(" none (best %d with pattern %02x)", bestm, bestp);
                printf("\n");
                memcpy(eg_inc[row], save, 8);
                continue;
            }
            int best = -1; uint32_t bestK = 0; int bestoff = 0, bestko = -1, bestsame = 0;
            std::vector<uint32_t> full[2][2];
            for (int slow = 0; slow < 2; slow++) {
                std::vector<uint32_t> onK;
                if (fixedK >= 0 && slow == 0) continue;
                for (uint32_t K = fixedK >= 0 ? (uint32_t)fixedK % P : 0; K < P; K++) {
                    int m = sim(c, k, st, r.c0ring, on, on_end, K, slow ? (uint32_t)-1 : 0, 1 << 30, 0, nullptr, nullptr, nullptr, r.ramp);
                    if (m > best) { best = m; bestK = K; bestoff = slow; bestko = -1; bestsame = 0; }
                    if (m == on_end - on) onK.push_back(K);
                    if (fixedK >= 0) break;
                }
                if (m3 < 0) continue;
                for (int same = 0; same < 2; same++)
                    for (uint32_t K : onK)
                        for (int ko = std::max(on + 1, m3 - 48); ko <= std::min<int>(c.n - 1, m4 + 400); ko++) {
                            int m = sim(c, k, st, r.c0ring, on, c.n, K, slow ? (uint32_t)-1 : 0, ko, same, nullptr, nullptr, nullptr, r.ramp);
                            if (m > best) { best = m; bestK = K; bestoff = slow; bestko = ko; bestsame = same; }
                            if (m == (int)c.n - on) full[slow][same].push_back(K);
                        }
            }
            printf("  stream %d AR %2d D1R %2d DL %2d D2R %2d RR %2d (R %u/%u/%u/%u, K period %u): best %d/%d (K %u slow_off %s koff %d koff_same %d)\n",
                   k, st.AR, st.D1R, st.DL, st.D2R, st.RR, eff_rate(st, st.AR), eff_rate(st, st.D1R), eff_rate(st, st.D2R), eff_rate(st, st.RR),
                   P, best, (int)c.n - on, bestK, bestoff ? "-1" : "0", bestko, bestsame);
            for (int slow = 0; slow < 2; slow++)
                for (int same = 0; same < 2; same++) {
                    auto &v = full[slow][same];
                    if (v.empty()) continue;
                    std::sort(v.begin(), v.end());
                    v.erase(std::unique(v.begin(), v.end()), v.end());
                    printf("    FULL with slow_off %s koff_same %d: K mod %u in {", slow ? "-1" : "0", same, P);
                    for (size_t i = 0; i < v.size() && i < 12; i++) printf("%s%u", i ? "," : "", v[i]);
                    if (v.size() > 12) printf(",... (%zu)", v.size());
                    printf("}; key-off samples:");
                    // list the key-off samples that work for the first full K
                    int shown = 0;
                    for (int ko = std::max(on + 1, m3 - 48); ko <= std::min<int>(c.n - 1, m4 + 400) && shown < 8; ko++)
                        if (sim(c, k, st, r.c0ring, on, c.n, v[0], slow ? (uint32_t)-1 : 0, ko, same, nullptr, nullptr, nullptr, r.ramp) == (int)c.n - on) {
                            uint32_t md = (r.c0ring - c.first - ko) & 0xFFFF;
                            printf(" %d(%s)", ko, (md & 1) ? "odd" : "even"); shown++;
                        }
                    printf("\n");
                }
            if (verbose && best < (int)c.n - on) {
                int mn, ma; int32_t ml;
                sim(c, k, st, r.c0ring, on, c.n, bestK, bestoff ? (uint32_t)-1 : 0, bestko < 0 ? 1 << 30 : bestko, bestsame, &mn, &ma, &ml, r.ramp);
                printf("    first mismatch at i %d (+%d): model a %03x level %d, hw %d\n", mn, mn - on, ma, ml, c.v[mn * c.ns + k]);
            }
        }
    }
    return 0;
}
