// s1_indep.cpp -- independent re-derivation of claim S1 (HANDOVER.md): the envelope clock ticks on the samples with
// EVEN MDEC_CT and its counter is eg_cnt = K - MDEC_CT/2 (mod 2^14) with one K per boot.
// Uses only the capture files (own header parsing) and the formulas written in NOTES.md / the task text; none of
// the project's envelope tools (eg_phase, eg_model, eg_keys) and not the production model.
//   (a) invert the level law to the attenuation per sample, list the step samples, test their MDEC_CT parity
//   (b) brute-force K in [0, 16384) per stream with the increment law, intersect over all streams / runs
//   (c) controls: no R<48 offset, clock on odd MDEC_CT, YM2612 rows 5/9/13, K off by one
//   (d) att_slow stream 3 (R 2): the first 12 step samples, spacings, counter positions at the fitted K
// Data: tests/eg_lock/hw/att_slow (c0 44c9), tests/eg_lock/hw/att_mid (c0 3f0e), tests/aeg_dl0/hw/dl0 (c0 87bb);
// c0 from the cap_start: line of the case's text output, n_first from header word 3, MDEC_CT(i) = c0 - n_first - i.
// Build: g++ -O2 -std=c++17 -fopenmp -o build/work/s1_indep work/verify/s5/s1_indep.cpp
// Run (from caique-rtl/model): build/work/s1_indep
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>
#include <algorithm>

/* ---------------------------------------------------------------- capture file (cap.h format, own parser) */
struct Cap {
    uint32_t ns = 0, n = 0, first = 0, errors = 0, nev = 0;
    std::vector<int32_t> v;                                 /* v[i * ns + k] */
    std::vector<std::pair<uint32_t, uint32_t>> ev;          /* {id, n} */
};
static Cap load_cap(const std::string &p) {
    Cap c;
    FILE *f = fopen((p + ".hdr").c_str(), "rb");
    if (!f) { perror((p + ".hdr").c_str()); exit(2); }
    uint32_t h[16 + 2 * 64] = {0};
    size_t nh = fread(h, 4, sizeof h / 4, f);
    fclose(f);
    if (nh < 11 || h[0] != 0x31504143) { fprintf(stderr, "%s: bad header\n", p.c_str()); exit(2); }
    c.ns = h[1]; c.n = h[2]; c.first = h[3]; c.errors = h[4]; c.nev = h[6];
    for (uint32_t e = 0; e < c.nev && 12 + 2 * e < nh; e++) c.ev.push_back({h[11 + 2 * e], h[12 + 2 * e]});
    c.v.resize((size_t)c.ns * c.n);
    f = fopen((p + ".bin").c_str(), "rb");
    if (!f) { perror((p + ".bin").c_str()); exit(2); }
    size_t got = fread(c.v.data(), 4, c.v.size(), f);
    fclose(f);
    if (got != c.v.size()) { fprintf(stderr, "%s: short data (%zu of %zu)\n", p.c_str(), got, c.v.size()); exit(2); }
    return c;
}

/* ---------------------------------------------------------------- level law (NOTES "Slot levels") */
static int32_t LV[1024];
static void build_levels() {
    for (int a = 0; a < 1024; a++) {
        int M = 127 - (a & 63), k = a >> 6;
        LV[a] = 16 * (int32_t)((32767LL * M) >> (7 + k));
    }
}
/* inverse: the attenuation interval [lo, hi] producing a level (LV is non-increasing in a); lo = -1: no a */
static void invert_level(int32_t lv, int &lo, int &hi) {
    lo = hi = -1;
    for (int a = 0; a < 1024; a++)
        if (LV[a] == lv) { if (lo < 0) lo = a; hi = a; }
}

/* ---------------------------------------------------------------- increment law (NOTES "Amplitude envelope", "Envelope clock") */
static const uint8_t ROWS[17][8] = {
    {0, 1, 0, 1, 0, 1, 0, 1}, {0, 1, 0, 1, 1, 1, 0, 1}, {0, 1, 1, 1, 0, 1, 1, 1}, {0, 1, 1, 1, 1, 1, 1, 1},
    {1, 1, 1, 1, 1, 1, 1, 1}, {1, 2, 1, 1, 1, 2, 1, 1}, {1, 2, 1, 2, 1, 2, 1, 2}, {1, 2, 2, 2, 1, 2, 2, 2},
    {2, 2, 2, 2, 2, 2, 2, 2}, {2, 4, 2, 2, 2, 4, 2, 2}, {2, 4, 2, 4, 2, 4, 2, 4}, {2, 4, 4, 4, 2, 4, 4, 4},
    {4, 4, 4, 4, 4, 4, 4, 4}, {4, 8, 4, 4, 4, 8, 4, 4}, {4, 8, 4, 8, 4, 8, 4, 8}, {4, 8, 8, 8, 4, 8, 8, 8},
    {8, 8, 8, 8, 8, 8, 8, 8}};
static const uint8_t YM_ROWS[3][8] = {{1, 1, 1, 2, 1, 1, 1, 2}, {2, 2, 2, 4, 2, 2, 2, 4}, {4, 4, 4, 8, 4, 4, 4, 8}};

struct Law { int slow_off = -1; bool odd_clock = false; bool ym = false; };

static inline uint32_t inc_of(uint32_t R, uint32_t cnt, const Law &L) {
    if (R == 0) return 0;
    if (R < 48) {
        uint32_t c = cnt + (uint32_t)L.slow_off;
        uint32_t sh = 11 - (R >> 2);
        if (c & ((1u << sh) - 1)) return 0;
        return ROWS[R & 3][(c >> sh) & 7];
    }
    int row = R >= 60 ? 16 : 4 + (int)(R - 48);
    if (L.ym && (row == 5 || row == 9 || row == 13)) return YM_ROWS[(row - 5) / 4][cnt & 7];
    return ROWS[row][cnt & 7];
}
static inline int eff_rate(int rate) { return rate == 0 ? 0 : std::min(63, 2 * rate); }   /* KRS 15 everywhere here */
static int row_of(int R) { return R == 0 ? -1 : R < 48 ? (R & 3) : R >= 60 ? 16 : 4 + (R - 48); }

/* ---------------------------------------------------------------- runs */
struct Stream { int AR, D1R, DL, D2R, RR; };
struct Run {
    const char *name; std::string path; uint32_t c0; Stream s[4];
    Cap cap; int on = -1, m3 = -1, end = 0;
    std::vector<uint16_t> md;                               /* MDEC_CT per sample */
};

struct Mismatch { int i = -1, a = -1; int32_t lv = 0, hw = 0; int state = -1; };

/* simulate stream k from the onset with counter constant K; returns the number of consecutive matched samples */
static int sim(const Run &r, int k, uint32_t K, const Law &L, Mismatch *mm = nullptr) {
    const Stream &st = r.s[k];
    const int R[4] = {eff_rate(st.AR), eff_rate(st.D1R), eff_rate(st.D2R), eff_rate(st.RR)};
    int a = R[0] >= 63 ? 0 : 0x280, state = 0;             /* key-on load; the key-on sample takes no step */
    bool off = false;
    const int32_t *hw = r.cap.v.data() + k;
    const uint32_t ns = r.cap.ns;
    for (int i = r.on; i < r.end; i++) {
        uint16_t md = r.md[i];
        bool clock = L.odd_clock ? (md & 1) != 0 : (md & 1) == 0;
        if (clock && i > r.on && !off) {
            uint32_t cnt = (K - (md >> 1)) & 0x3FFF;
            bool was_d1 = state == 1;
            uint32_t inc = inc_of((uint32_t)R[state], cnt, L);
            if (inc) {
                if (state == 0) { a += ((~a) * (int)inc) >> 4; if (a <= 0) { a = 0; state = 1; } }
                else { a += (int)inc; if (a > 0x3FF) { a = 0x3FF; off = true; } }
            }
            if (was_d1 && !off && (a >> 5) == st.DL) state = 2;   /* equality on a[9:5], checked after the step */
        }
        int32_t lv = off ? 0 : LV[a];
        if (lv != hw[(size_t)i * ns]) {
            if (mm) { mm->i = i; mm->a = a; mm->lv = lv; mm->hw = hw[(size_t)i * ns]; mm->state = state; }
            return i - r.on;
        }
    }
    return r.end - r.on;
}

struct Fit { std::vector<char> ok; int nok = 0; int best = -1; uint32_t bestK = 0; Mismatch bestmm; };
static Fit search(const Run &r, int k, const Law &L) {
    Fit f; f.ok.assign(16384, 0);
    std::vector<int> matched(16384);
#pragma omp parallel for schedule(dynamic, 64)
    for (int K = 0; K < 16384; K++) matched[K] = sim(r, k, (uint32_t)K, L);
    for (int K = 0; K < 16384; K++) {
        if (matched[K] == r.end - r.on) { f.ok[K] = 1; f.nok++; }
        if (matched[K] > f.best) { f.best = matched[K]; f.bestK = (uint32_t)K; }
    }
    sim(r, k, f.bestK, L, &f.bestmm);
    return f;
}
static std::string kset_str(const std::vector<char> &ok) {
    std::vector<int> ks;
    for (int K = 0; K < 16384; K++) if (ok[K]) ks.push_back(K);
    if (ks.empty()) return "{}";
    int stride = ks.size() > 1 ? ks[1] - ks[0] : 0;
    bool uniform = true;
    for (size_t j = 2; j < ks.size(); j++) if (ks[j] - ks[j - 1] != stride) uniform = false;
    char b[256];
    if (ks.size() == 1) snprintf(b, sizeof b, "{%d}", ks[0]);
    else if (uniform) snprintf(b, sizeof b, "%zu values: %d + j*%d (j = 0..%zu), i.e. K = %d mod %d", ks.size(), ks[0], stride, ks.size() - 1, ks[0], stride);
    else snprintf(b, sizeof b, "%zu values (non-uniform), first %d", ks.size(), ks[0]);
    return b;
}
static void print_mm(const Run &r, int k, const Mismatch &m) {
    if (m.i < 0) { printf("(no mismatch)"); return; }
    printf("first mismatch at i %d (+%d from onset, MDEC_CT %04x %s): model a %03x state %d level %d, hw %d",
           m.i, m.i - r.on, r.md[m.i], (r.md[m.i] & 1) ? "odd" : "even", m.a, m.state, m.lv, m.hw);
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    build_levels();
    /* level-law facts */
    int distinct_to = 0;
    for (int a = 1; a < 1024; a++) { if (LV[a] == LV[a - 1]) break; distinct_to = a; }
    printf("level law: LV[0] %d, LV[0x280] %d (key-on level), LV[0x3FF] %d; levels pairwise distinct for a <= 0x%03x\n",
           LV[0], LV[0x280], LV[0x3FF], distinct_to);
    { int lo, hi; invert_level(LV[0x280], lo, hi); printf("  attenuations producing the key-on level %d: 0x%03x..0x%03x (treated as a = 0x280 at the onset)\n", LV[0x280], lo, hi); }

    std::vector<Run> runs = {
        {"att_slow", "tests/eg_lock/hw/att_slow", 0x44c9, {{6, 0, 0, 0, 31}, {4, 0, 0, 0, 31}, {2, 0, 0, 0, 31}, {1, 0, 0, 0, 31}}},
        {"att_mid", "tests/eg_lock/hw/att_mid", 0x3f0e, {{22, 0, 0, 0, 31}, {20, 0, 0, 0, 31}, {18, 0, 0, 0, 31}, {16, 0, 0, 0, 31}}},
        {"dl0", "tests/aeg_dl0/hw/dl0", 0x87bb, {{31, 31, 0, 0, 31}, {31, 20, 0, 0, 31}, {31, 10, 0, 0, 31}, {20, 31, 0, 10, 31}}},
    };
    for (auto &r : runs) {
        r.cap = load_cap(r.path);
        if (r.cap.ns != 4) { fprintf(stderr, "%s: ns %u\n", r.name, r.cap.ns); return 2; }
        r.md.resize(r.cap.n);
        for (uint32_t i = 0; i < r.cap.n; i++) r.md[i] = (uint16_t)((r.c0 - r.cap.first - i) & 0xFFFF);
        for (auto &e : r.cap.ev) if (e.first == 3) r.m3 = (int)(e.second - r.cap.first);
        /* onset: first sample where any stream is non-zero; check all four share it */
        int on[4];
        for (int k = 0; k < 4; k++) { on[k] = 0; while (on[k] < (int)r.cap.n && r.cap.v[(size_t)on[k] * 4 + k] == 0) on[k]++; }
        r.on = std::min(std::min(on[0], on[1]), std::min(on[2], on[3]));
        r.end = r.m3 - 200;
        printf("\n== %s: %s  n %u  n_first %u  c0 %04x  errors %u  marks:", r.name, r.path.c_str(), r.cap.n, r.cap.first, r.c0, r.cap.errors);
        for (auto &e : r.cap.ev) printf(" id%u@%d", e.first, (int)(e.second - r.cap.first));
        printf("\n   onset per stream %d %d %d %d -> onset %d (MDEC_CT %04x %s), analysis window [%d, %d) = mark3 - 200 (%d samples)\n",
               on[0], on[1], on[2], on[3], r.on, r.md[r.on], (r.md[r.on] & 1) ? "odd" : "even", r.on, r.end, r.end - r.on);
        for (int k = 0; k < 4; k++) {
            const Stream &st = r.s[k];
            printf("   stream %d: AR %2d D1R %2d DL %2d D2R %2d RR %2d -> R %2d/%2d/%2d/%2d rows %d/%d/%d/%d; onset level %d (expect %d)\n", k,
                   st.AR, st.D1R, st.DL, st.D2R, st.RR, eff_rate(st.AR), eff_rate(st.D1R), eff_rate(st.D2R), eff_rate(st.RR),
                   row_of(eff_rate(st.AR)), row_of(eff_rate(st.D1R)), row_of(eff_rate(st.D2R)), row_of(eff_rate(st.RR)),
                   r.cap.v[(size_t)r.on * 4 + k], eff_rate(st.AR) >= 63 ? LV[0] : LV[0x280]);
        }
    }

    /* ------------------------------------------------------------ (a) step samples and their parity */
    printf("\n#### (a) step samples (level changes after the onset, up to mark3 - 200) and their MDEC_CT parity\n");
    long tot_even = 0, tot_odd = 0;
    for (auto &r : runs) {
        for (int k = 0; k < 4; k++) {
            int ne = 0, no = 0, shown = 0;
            int R = eff_rate(r.s[k].AR);
            std::vector<int> steps;
            for (int i = r.on + 1; i < r.end; i++) {
                int32_t v = r.cap.v[(size_t)i * 4 + k], pv = r.cap.v[(size_t)(i - 1) * 4 + k];
                if (v == pv) continue;
                steps.push_back(i);
                if (r.md[i] & 1) no++; else ne++;
            }
            printf("  %s stream %d (attack R %d): %zu step samples, MDEC_CT even %d, odd %d\n", r.name, k, R, steps.size(), ne, no);
            for (int i : steps) {
                if (shown++ >= 4) break;
                int lo, hi; invert_level(r.cap.v[(size_t)i * 4 + k], lo, hi);
                int plo, phi; invert_level(r.cap.v[(size_t)(i - 1) * 4 + k], plo, phi);
                printf("      i %d (+%d) MDEC_CT %04x %s: level %d -> %d, a 0x%03x..0x%03x -> 0x%03x..0x%03x\n", i, i - r.on, r.md[i],
                       (r.md[i] & 1) ? "odd" : "even", r.cap.v[(size_t)(i - 1) * 4 + k], r.cap.v[(size_t)i * 4 + k], plo, phi, lo, hi);
            }
            /* analytic residue from the tick condition alone (R < 48, single-rate streams: attack never ends before mark 3):
             * with the -1 offset a step needs (K - md/2 - 1) = 0 mod 2^sh, i.e. K = md/2 + 1 (mod 2^sh) */
            if (R > 0 && R < 48 && r.s[k].D1R == 0) {
                uint32_t sh = 11 - (R >> 2), P = 1u << sh;
                std::vector<uint32_t> res;
                for (int i : steps) res.push_back(((r.md[i] >> 1) + 1) & (P - 1));
                std::sort(res.begin(), res.end()); res.erase(std::unique(res.begin(), res.end()), res.end());
                printf("      tick condition alone: K = MDEC_CT/2 + 1 mod %u from every step sample -> %zu distinct residue(s):", P, res.size());
                for (size_t j = 0; j < res.size() && j < 6; j++) printf(" %u", res[j]);
                printf("  (6491 mod %u = %u; without the offset the residue reads %u = 6490 mod %u)\n", P, 6491 % P, res.empty() ? 0 : (res[0] + P - 1) % P, P);
            }
            tot_even += ne; tot_odd += no;
        }
    }
    printf("  TOTAL step samples: even MDEC_CT %ld, odd MDEC_CT %ld -> claim 'clock on even MDEC_CT' %s; control 'clock on odd MDEC_CT' %s\n",
           tot_even, tot_odd, tot_odd == 0 ? "CONSISTENT" : "VIOLATED", tot_even == 0 ? "not refuted" : "FAILS");

    /* ------------------------------------------------------------ (b) fit K */
    printf("\n#### (b) K search in [0, 16384): eg_cnt = (K - MDEC_CT/2) & 0x3FFF, clock on even MDEC_CT, R<48 rows on cnt-1, measured rows\n");
    Law base;
    std::vector<char> all_ok(16384, 1);
    std::vector<std::vector<Fit>> fits(runs.size());
    for (size_t ri = 0; ri < runs.size(); ri++) {
        Run &r = runs[ri];
        std::vector<char> run_ok(16384, 1);
        for (int k = 0; k < 4; k++) {
            Fit f = search(r, k, base);
            fits[ri].push_back(f);
            printf("  %s stream %d: %d/%d samples reproduced at best; K reproducing ALL: %s\n", r.name, k, f.best, r.end - r.on, kset_str(f.ok).c_str());
            if (f.nok == 0) { printf("      "); print_mm(r, k, f.bestmm); printf("\n"); }
            for (int K = 0; K < 16384; K++) { run_ok[K] &= f.ok[K]; all_ok[K] &= f.ok[K]; }
        }
        printf("  %s all 4 streams: %s\n", r.name, kset_str(run_ok).c_str());
    }
    printf("  ALL 12 streams of the 3 runs: K in %s\n", kset_str(all_ok).c_str());
    int nall = 0; for (int K = 0; K < 16384; K++) nall += all_ok[K];
    printf("  expected {6491, 14683}: %s\n", (nall == 2 && all_ok[6491] && all_ok[14683]) ? "MATCH" : "DIFFERENT");

    /* ------------------------------------------------------------ (c) controls */
    printf("\n#### (c) controls\n");
    /* C1: no offset */
    {
        Law L; L.slow_off = 0;
        printf("  C1: R<48 rows indexed by cnt (no -1 offset)\n");
        std::vector<char> ok(16384, 1);
        for (auto &r : runs) for (int k = 0; k < 4; k++) {
            Fit f = search(r, k, L);
            for (int K = 0; K < 16384; K++) ok[K] &= f.ok[K];
            Mismatch m; int got = sim(r, k, 6491, L, &m);
            printf("     %s stream %d: free K -> %s; at K = 6491 %d/%d, ", r.name, k, kset_str(f.ok).c_str(), got, r.end - r.on);
            print_mm(r, k, m); printf("\n");
        }
        printf("     all streams, free K: %s  (every rate here is R < 48 or a cnt-independent row 16, so this control only relabels K by -1: the offset is NOT decidable from these three runs alone; see S4)\n", kset_str(ok).c_str());
    }
    /* C2: odd clock */
    {
        Law L; L.odd_clock = true;
        printf("  C2: clock on ODD MDEC_CT (K free)\n");
        for (auto &r : runs) for (int k = 0; k < 4; k++) {
            Fit f = search(r, k, L);
            printf("     %s stream %d: best %d/%d (K %u), full K set %s; ", r.name, k, f.best, r.end - r.on, f.bestK, kset_str(f.ok).c_str());
            print_mm(r, k, f.bestmm); printf("\n");
        }
    }
    /* C3: YM2612 rows */
    {
        Law L; L.ym = true;
        printf("  C3: YM2612 rows 5/9/13 (double step at index 3 and 7)\n");
        bool any = false;
        for (auto &r : runs) for (int k = 0; k < 4; k++) {
            const Stream &st = r.s[k];
            for (int re : {st.AR, st.D1R, st.D2R}) { int row = row_of(eff_rate(re)); if (row == 5 || row == 9 || row == 13) any = true; }
        }
        std::vector<char> ok(16384, 1);
        for (auto &r : runs) for (int k = 0; k < 4; k++) { Fit f = search(r, k, L); for (int K = 0; K < 16384; K++) ok[K] &= f.ok[K]; }
        printf("     any stream using rows 5/9/13 before its key-off: %s -> this control is %s here; all-stream K set with the YM rows: %s\n",
               any ? "yes" : "no", any ? "meaningful" : "VACUOUS (same result as the measured rows)", kset_str(ok).c_str());
    }
    /* C4: K off by one */
    {
        printf("  C4: K off by one (measured rows, -1 offset)\n");
        for (uint32_t K : {6490u, 6492u, 14682u, 14684u}) {
            for (auto &r : runs) for (int k = 0; k < 4; k++) {
                Mismatch m; int got = sim(r, k, K, base, &m);
                printf("     K %5u %s stream %d: %d/%d ", K, r.name, k, got, r.end - r.on);
                print_mm(r, k, m); printf("\n");
            }
        }
    }

    /* ------------------------------------------------------------ (d) timing law: att_slow stream 3 (R 2) */
    printf("\n#### (d) att_slow stream 3 (AR 1 -> R 2, row 2 = {0,1,1,1,0,1,1,1}, tick every 2^11 clocks = 4096 samples): first 12 step samples\n");
    {
        Run &r = runs[0]; int k = 3; uint32_t K = 6491;
        int prev = -1, shown = 0;
        printf("     %-8s %-8s %-7s %-8s %-9s %-6s %-6s %s\n", "i", "+onset", "MDEC_CT", "spacing", "level", "cnt", "cnt-1", "idx=((cnt-1)>>11)&7 / row[idx] / (cnt-1)&0x7FF");
        for (int i = r.on + 1; i < r.end && shown < 12; i++) {
            int32_t v = r.cap.v[(size_t)i * 4 + k], pv = r.cap.v[(size_t)(i - 1) * 4 + k];
            if (v == pv) continue;
            uint32_t cnt = (K - (r.md[i] >> 1)) & 0x3FFF, c1 = (cnt - 1) & 0x3FFF;
            int lo, hi; invert_level(v, lo, hi);
            printf("     %-8d %-8d %04x    %-8s %-9d %-6u %-6u idx %u row %u low11 %u   a 0x%03x..0x%03x\n", i, i - r.on, r.md[i],
                   prev < 0 ? "-" : std::to_string(i - prev).c_str(), v, cnt, c1, (c1 >> 11) & 7, ROWS[2][(c1 >> 11) & 7], c1 & 0x7FF, lo, hi);
            prev = i; shown++;
        }
        /* full-stream check of the timing law at K: every step sample is a tick with row[idx] = 1 and, while the attack
         * lasts (level below LV[0] = a 0; D1R 0 afterwards, so no further change is expected), every such tick is a step */
        int nsteps = 0, nticks = 0, bad_step = 0, bad_tick = 0, ticks_after = 0, a0_at = -1;
        for (int i = r.on + 1; i < r.end; i++) {
            bool step = r.cap.v[(size_t)i * 4 + k] != r.cap.v[(size_t)(i - 1) * 4 + k];
            bool tick = false;
            if ((r.md[i] & 1) == 0) { uint32_t c1 = ((K - (r.md[i] >> 1)) - 1) & 0x3FFF; tick = (c1 & 0x7FF) == 0 && ROWS[2][(c1 >> 11) & 7]; }
            bool in_attack = a0_at < 0;
            nsteps += step; nticks += tick;
            if (step && !tick) bad_step++;
            if (tick && !step) { if (in_attack) bad_tick++; else ticks_after++; }
            if (a0_at < 0 && r.cap.v[(size_t)i * 4 + k] == LV[0]) a0_at = i;
        }
        printf("     whole window: %d step samples, %d predicted ticks at K %u; steps without a predicted tick %d; the attack reaches a = 0 (level %d) at i %d (+%d);\n"
               "     predicted ticks without a step before that %d, after it (D1R 0: no change expected) %d\n",
               nsteps, nticks, K, bad_step, LV[0], a0_at, a0_at - r.on, bad_tick, ticks_after);
    }
    return 0;
}
