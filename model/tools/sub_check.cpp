// sub_check.cpp -- tests/sub_frame (TODO 1.6): the sample in which a register write acts, console against the cycle model.
//   sub_check [DIR] [-v] [-a]        (default tests/sub_frame/hw; run from caique-rtl/model)
// Every event of sub_frame.txt is a register write queued between two MEMS31 markers; cases/flog.h's ring
// (flog_<k>_<exp>.bin) holds, per DSP sample n, the marker the DSP read in every frame (pair p at clock
// t = 512 n + 65 + 8 p) and slot k's bus (1).  A marker whose X0 is at clock X shows in the first read after X, so each
// marker's X0 lies between the last read without it and the first read with it; the write under test lies between the
// two markers' X0 (the bus serves one SH4 register access per DSP step, X0 on clocks = 2 mod 4).
// The effect: the first DSP sample whose bus word is the new one shows sweep n - 1.  The prediction: the cycle model run
// with the write at every candidate clock (backdoor, landing at the end of that clock); an event is determinate when every
// candidate predicts the same sweep.  Output per slot and experiment: events, determinate, agreeing, failing; the
// spacing of the SH4's writes at the AICA; with -a, per experiment the threshold T (the write acts in its own sweep iff
// its X0 < slot k's frame start + T) that the console's events allow; -v prints every event.
#include "aica_model.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <algorithm>
#include <climits>
#include <array>

using caique::AicaModel;

enum { NEXP = 5 };
static const char *exp_name[NEXP] = {"SA (0x00)", "IMXL (0x20)", "TL (0x28)", "VOFF (0x28)", "LPOFF (0x28)"};
struct Ev { int k, exp, e; uint32_t m1, m2, w; };

// cases/sub_frame.c setup() and toggle(): the slot of experiment x in its first state, and the write of a toggle
static void sub_setup(AicaModel &m, int k, int x) {
    const uint32_t ch = 0x80 * k;
    const int voff = x == 2 ? 0 : 1, tl = x == 3 ? 32 : 0, lpoff = x == 4 ? 0 : 1;
    m.write(ch + 0x04, 0x0000); m.write(ch + 0x08, 0); m.write(ch + 0x0C, 64);
    m.write(ch + 0x10, 31); m.write(ch + 0x14, (15 << 10) | 31); m.write(ch + 0x18, 0); m.write(ch + 0x1C, 0);
    m.write(ch + 0x20, (15 << 4) | 1); m.write(ch + 0x24, 0); m.write(ch + 0x28, (tl << 8) | (voff << 6) | (lpoff << 5));
    for (int i = 0; i < 5; i++) m.write(ch + 0x2C + 4 * i, 0x1FF8);
    m.write(ch + 0x40, 0x1F1F); m.write(ch + 0x44, 0x1F1F);
    m.write(ch + 0x00, (1 << 9) | 1);
}
static void sub_toggle(int x, bool on, uint32_t &reg, uint32_t &val) {
    switch (x) {
    case 0: reg = 0x00; val = 0x4000 | (1 << 9) | (on ? 2 : 1); break;
    case 1: reg = 0x20; val = ((on ? 13 : 15) << 4) | 1; break;
    case 2: reg = 0x28; val = ((on ? 32 : 0) << 8) | (1 << 5); break;
    case 3: reg = 0x28; val = (32 << 8) | ((on ? 0 : 1) << 6) | (1 << 5); break;
    default: reg = 0x28; val = (1 << 6) | ((on ? 1 : 0) << 5); break;
    }
}
static int32_t bus_word(int32_t mixs) {   // flog.h's stream word: SHIFTED[23:8] of clamp24(-(MIXS << 4))
    int32_t a = -(mixs * 16);
    a = a > 0x7FFFFF ? 0x7FFFFF : a < -0x800000 ? -0x800000 : a;
    return (int16_t)(a >> 8);
}

// the model's lag table of one slot / experiment / direction (on: toward the toggled state): for the write at X0 phase ph
// (2 mod 4) of sweep S, the first sweep whose bus 1 sum is the new one, minus S (-1: none within 3 sweeps); and the old /
// new bus words.  The runs never write wave RAM, so every run shares the base model's.
static void lag_table(int k, int x, bool on, int8_t *out, int32_t &w_old, int32_t &w_new) {
    AicaModel *b = new AicaModel();
    AicaModel &m0 = *b;
    for (int i = 0; i < 32; i++) { m0.ram_write32(0x010000 + 4 * i, 0x08000800u); m0.ram_write32(0x020000 + 4 * i, 0x10001000u); }
    const uint32_t ch = 0x80 * k;
    uint32_t reg, val;
    sub_setup(m0, k, x);
    if (!on) { sub_toggle(x, true, reg, val); m0.write(ch + reg, val); }   // start from the toggled state
    m0.write(ch + 0x00, m0.r(ch + 0x00) | 0x4000);
    m0.write(ch + 0x00, m0.r(ch + 0x00) | 0xC000);
    for (int i = 0; i < 128; i++) m0.step();       // the attack (R 62 from 0x280) settles; the case waits 2.5 ms
    sub_toggle(x, on, reg, val);
    AicaModel *mp = new AicaModel();
    uint8_t *own = mp->ram;
    // the references: the bus word before, and the settled word after the write (the filter at DC runs in a small limit
    // cycle, so a word is classed by the nearer reference)
    w_old = bus_word(m0.MIXS[1]);
    {
        AicaModel &m = *mp;
        memcpy((void *)&m, b, sizeof m);
        m.write(ch + reg, val);
        for (int s = 0; s < 4; s++) m.step();
        w_new = bus_word(m.MIXS[1]);
    }
    auto is_new = [&](int32_t w) { return std::abs(w - w_new) < std::abs(w - w_old); };
    for (uint32_t ph = 2; ph < 512; ph += 4) {
        AicaModel &m = *mp;
        memcpy((void *)&m, b, sizeof m);           // shares b's wave RAM
        m.run(ph + 1);                              // clocks 0..ph: the write lands at the end of clock ph
        m.write(ch + reg, val);
        // the bus as the DSP reads it: from ph 64 of sweep S + j the bank of sweep S + j - 1 (a write early in S can
        // still reach the sends of slots 57..63 of S - 1)
        int lag = 99;
        for (int j = ph < 64 ? 0 : 1; j <= 3 && lag == 99; j++) {
            while (m.ph != 64 || m.clocks < 512 * (uint64_t)j + b->clocks) m.clock();
            if (is_new(bus_word(m.mixs[m.dsp_bank][1]))) lag = j - 1;
        }
        out[ph] = (int8_t)(lag == 99 ? -9 : lag);
    }
    mp->ram = own;
    delete mp;
    delete b;
}

int main(int argc, char **argv) {
    std::string dir = "tests/sub_frame/hw";
    bool verbose = false, analyse = false;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-v")) verbose = true;
        else if (!strcmp(argv[i], "-a")) analyse = true;
        else dir = argv[i];
    }
    FILE *f = fopen((dir + "/sub_frame.txt").c_str(), "r");
    if (!f) { perror((dir + "/sub_frame.txt").c_str()); return 2; }
    std::vector<Ev> evs;
    char line[256];
    while (fgets(line, sizeof line, f)) {
        Ev e; unsigned long w;
        if (sscanf(line, "E %d %d %d %u %u %lx", &e.k, &e.exp, &e.e, &e.m1, &e.m2, &w) == 6) { e.w = (uint32_t)w; evs.push_back(e); }
    }
    fclose(f);
    // model lag tables and bus words, per slot / experiment / direction
    static int8_t lag[64][NEXP][2][512];
    static int32_t wold[64][NEXP][2], wnew[64][NEXP][2];
#pragma omp parallel for schedule(dynamic)
    for (int i = 0; i < 64 * NEXP * 2; i++) {
        const int k = i / (NEXP * 2), x = (i / 2) % NEXP, on = i & 1;
        lag_table(k, x, on, lag[k][x][on], wold[k][x][on], wnew[k][x][on]);
    }
    printf("model: the write acts in its own sweep iff its X0 < frame start + T; T per experiment (slot 0 .. 63 alike):");
    for (int x = 0; x < NEXP; x++) {
        std::set<int> ts;
        for (int k = 0; k < 64; k++) {
            for (uint32_t ph = 2; ph < 1024; ph += 4)
                if ((int)(ph / 512) + lag[k][x][1][ph & 511] >= 1) { ts.insert((int)ph - 8 * k); break; }
        }
        printf("  %s:", exp_name[x]);
        for (int t : ts) printf(" %d", t);
    }
    printf("\n");
    long tot = 0, det = 0, agree = 0, bad = 0, nodata = 0;
    std::map<int, long> spacing;
    std::map<std::pair<int, int>, std::map<int, long>> hist[NEXP];   // -a: [exp][(lo, hi) X0 vs frame start][lag] events
    std::map<int, std::map<int, long>> mid[NEXP];   // -a: [exp][estimated X0 vs frame start, 4-clock bucket][lag] events
    std::map<int, std::array<long, 2>> midp[NEXP][2];   // -a: the same by slot parity, [est][own, next]
    std::vector<uint16_t> ring(65536);
    for (int k = 0; k < 64; k++) {
        for (int x = 0; x < NEXP; x++) {
            char name[64];
            snprintf(name, sizeof name, "/flog_%d_%d.bin", k, x);
            FILE *rf = fopen((dir + name).c_str(), "rb");
            if (!rf || fread(ring.data(), 2, 65536, rf) != 65536) { if (rf) fclose(rf); continue; }
            fclose(rf);
            // counter: region 0, word -(n + 1) at address m; n + m is one constant C for every word the logger wrote
            std::map<uint32_t, int> cnt;
            for (uint32_t m = 0; m < 65536; m++) cnt[(uint32_t)((-(int32_t)(int16_t)ring[m] - 1) + m) & 0xFFFF]++;
            uint32_t C = 0; int best = -1;
            for (auto &kv : cnt) if (kv.second > best) { best = kv.second; C = kv.first; }
            // the batch's samples: region-0-consistent, every marker in the batch's range (the previous batch's last marker
            // .. this batch's last); ordered by the counter relative to a sample showing the last marker everywhere (the
            // counter is 16 bits and runs on while the ring is read); n offset by 100000
            uint32_t mlo = UINT32_MAX, mhi = 0;
            for (const Ev &ev : evs) if (ev.k == k && ev.exp == x) { mlo = std::min(mlo, ev.m1 - 1); mhi = std::max(mhi, ev.m2); }
            auto marker = [&](int p, uint32_t m) { return (uint32_t)(uint16_t)(-(int32_t)(int16_t)ring[(1024 * p + m) & 0xFFFF]); };
            struct Rd { int64_t t; uint32_t marker; };
            std::vector<Rd> rd;
            std::map<int64_t, int32_t> bus;
            std::vector<std::pair<uint32_t, uint32_t>> smp;
            int64_t nref = -1;
            for (uint32_t m = 0; m < 65536; m++) {
                const uint32_t n16 = (uint32_t)(-(int32_t)(int16_t)ring[m] - 1) & 0xFFFF;
                if (((n16 + m) & 0xFFFF) != C) continue;
                bool inb = true, last = true;
                for (int p = 2; p < 64 && inb; p++) { const uint32_t mk = marker(p, m); inb = mk >= mlo && mk <= mhi; last &= mk == mhi; }
                if (!inb) continue;
                smp.push_back({n16, m});
                if (last) nref = n16;
            }
            for (auto &s : smp) {
                if (nref < 0) break;
                const int64_t n = 100000 + (int16_t)(uint16_t)(s.first - nref);
                bus[n] = (int16_t)ring[(1024 + s.second) & 0xFFFF];
                for (int p = 2; p < 64; p++) rd.push_back({512 * n + 65 + 8 * p, marker(p, s.second)});
            }
            std::sort(rd.begin(), rd.end(), [](const Rd &a, const Rd &b) { return a.t < b.t; });
            long b_tot = 0, b_det = 0, b_agree = 0, b_bad = 0;
            for (const Ev &ev : evs) {
                if (ev.k != k || ev.exp != x) continue;
                tot++; b_tot++;
                // each marker's X0: after the last read without it, before the first read with it (the first marker may
                // be invisible when both land between two reads)
                auto window = [&](uint32_t mk, int64_t &lo, int64_t &hi) {
                    for (size_t i = 0; i < rd.size(); i++)
                        if (rd[i].marker == mk || (mk == ev.m1 && rd[i].marker == ev.m2)) {
                            if (i == 0) return false;
                            lo = rd[i - 1].t; hi = rd[i].t - 1;
                            return true;
                        }
                    return false;
                };
                int64_t l1, h1, l2, h2;
                if (!window(ev.m1, l1, h1) || !window(ev.m2, l2, h2)) { nodata++; continue; }
                spacing[(int)(((l2 + h2) - (l1 + h1)) / 2)]++;
                // the write's candidates: X = 2 mod 4, a step after the earliest possible M1 X0, a step before the latest
                // possible M2 X0
                int64_t a = -1, b = -1;
                for (int64_t y = l1; y <= h1; y++) if ((y & 3) == 2) { a = y; break; }
                for (int64_t y = h2; y >= l2; y--) if ((y & 3) == 2) { b = y; break; }
                std::vector<int64_t> cand;
                if (a >= 0 && b >= 0)
                    for (int64_t X = a + 4; X <= b - 4; X += 4) cand.push_back(X);
                const bool on = !(ev.e & 1);
                const int32_t newv = wnew[k][x][on], oldv = wold[k][x][on];
                // observed: the first DSP sample from the bracket on with the new word after the old one
                int64_t nobs = -1;
                for (int64_t n = l1 / 512; n <= l1 / 512 + 6; n++) {
                    auto it = bus.find(n), ip = bus.find(n - 1);
                    if (it != bus.end() && ip != bus.end() && std::abs(it->second - newv) < std::abs(it->second - oldv) &&
                        std::abs(ip->second - oldv) < std::abs(ip->second - newv)) { nobs = n; break; }
                }
                std::set<int64_t> pred;
                for (int64_t X : cand) {
                    const int L = lag[k][x][on][X & 511];
                    pred.insert(L == -9 ? -1 : X / 512 + L + 1);
                }
                if (analyse && !cand.empty() && nobs >= 0 && cand.front() / 512 == cand.back() / 512) {
                    const int64_t base = cand.front() / 512 * 512 + 8 * k;
                    hist[x][{(int)(cand.front() - base), (int)(cand.back() - base)}][(int)(nobs - 1 - cand.front() / 512)]++;
                    // the write's X0 estimated as the midpoint of the two markers' windows (the SH4's writes reach the AICA
                    // evenly spaced), relative to the frame start, in 4-clock buckets
                    const int est = (int)((l1 + h1 + l2 + h2) / 4 - base);
                    mid[x][est >= 0 ? est / 4 * 4 : -((-est + 3) / 4 * 4)][(int)(nobs - 1 - cand.front() / 512)]++;
                    { const int lg = (int)(nobs - 1 - cand.front() / 512); if (lg == 0 || lg == 1) midp[x][k & 1][est][lg]++; }
                }
                const bool d = pred.size() == 1;
                if (d) { det++; b_det++; }
                const bool ok = pred.count(nobs) > 0;
                if (d && ok) { agree++; b_agree++; }
                if (!ok) { bad++; b_bad++; }
                if (getenv("SUB_DUMP") && !ok && tot <= atoi(getenv("SUB_DUMP"))) {
                    printf("    old %d new %d; bus:", oldv, newv);
                    for (int64_t n = l1 / 512 - 2; n <= l1 / 512 + 4; n++) { auto it = bus.find(n); printf(" %lld:%d", (long long)n, it == bus.end() ? 99999 : it->second); }
                    printf("\n");
                }
                if (verbose || !ok) {
                    std::string ps;
                    for (int64_t p : pred) ps += (ps.empty() ? "" : "/") + std::to_string(p);
                    printf("  k %2d exp %d e %2d: M1 X0 in [%lld,%lld] M2 in [%lld,%lld]: write X0 in sweep %lld ph %lld..%lld "
                           "(%zu candidates), predicted %s, observed DSP sample %lld%s\n", k, x, ev.e, (long long)l1, (long long)h1,
                           (long long)l2, (long long)h2, cand.empty() ? -1LL : (long long)(cand.front() / 512),
                           cand.empty() ? -1LL : (long long)(cand.front() % 512),
                           cand.empty() ? -1LL : (long long)(cand.back() - cand.front() / 512 * 512), cand.size(), ps.c_str(),
                           (long long)nobs, ok ? "" : "  <-- FAIL");
                }
            }
            if (verbose || b_bad)
                printf("slot %2d exp %d: %ld events, %ld determinate, %ld agree, %ld fail\n", k, x, b_tot, b_det, b_agree, b_bad);
        }
    }
    if (analyse) {
        // by slot parity: the midpoint threshold that best separates "own sweep" from "next" (two interleaved engines with
        // different stage phases would put even and odd slots 8 clocks apart)
        for (int x = 0; x < NEXP; x++) {
            printf("exp %d %-12s best midpoint threshold:", x, exp_name[x]);
            for (int par = 0; par < 2; par++) {
                int bestT = 0; long bestE = LONG_MAX, n = 0;
                for (int T = -64; T <= 160; T++) {
                    long err = 0;
                    for (auto &kv : midp[x][par]) err += kv.first < T ? kv.second[1] : kv.second[0];
                    if (err < bestE) { bestE = err; bestT = T; }
                }
                for (auto &kv : midp[x][par]) n += kv.second[0] + kv.second[1];
                printf("  %s slots %d (%ld of %ld misplaced)", par ? "odd" : "even", bestT, bestE, n);
            }
            printf("\n");
        }
        for (int x = 0; x < NEXP; x++) {
            printf("exp %d %s, write X0 estimated as the markers' midpoint, relative to the frame start: acted in its own sweep / in the next\n ", x, exp_name[x]);
            int col = 0;
            for (auto &kv : mid[x]) {
                if (kv.first < -40 || kv.first > 140) continue;
                printf(" %4d:%ld/%ld", kv.first, kv.second.count(0) ? kv.second.at(0) : 0L, kv.second.count(1) ? kv.second.at(1) : 0L);
                if (++col % 10 == 0) printf("\n ");
            }
            printf("\n");
        }
        for (int x = 0; x < NEXP; x++) {
            // a threshold T must lie above every lag-0 event's earliest X0 and at or below every lag-1 event's latest
            int64_t lo0 = LLONG_MIN, hi1 = LLONG_MAX;
            long n0 = 0, n1 = 0, nother = 0;
            for (auto &kv : hist[x])
                for (auto &lv : kv.second) {
                    if (lv.first == 0) { n0 += lv.second; lo0 = std::max<int64_t>(lo0, kv.first.first); }
                    else if (lv.first == 1) { n1 += lv.second; hi1 = std::min<int64_t>(hi1, kv.first.second); }
                    else nother += lv.second;
                }
            printf("exp %d %-12s %5ld acted in the write's sweep, %5ld in the next, %ld other: T in (%lld, %lld]%s\n", x,
                   exp_name[x], n0, n1, nother, (long long)lo0, (long long)hi1, lo0 < hi1 ? "" : "  -- NO SINGLE THRESHOLD");
        }
    }
    printf("marker-to-marker X0 spacing at the AICA (clocks, midpoints):");
    for (auto &kv : spacing) printf(" %d:%ld", kv.first, kv.second);
    printf("\nsub_check %s: %ld events, %ld determinate, %ld agree, %ld fail, %ld without data\n", dir.c_str(), tot, det, agree,
           bad, nodata);
    return bad || nodata ? 1 : 0;
}
