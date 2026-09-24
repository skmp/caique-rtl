// timer_check.cpp -- tests/timer_phase (cases/timer_phase.c): where the timers tick and where the one-sample interval
// bit sets, from the DSP frame logger's rings (tp_<batch>.bin, cases/flog.h; no MIXS streams: pairs 1..63 are markers).
//   timer_check [DIR]        (default tests/timer_phase/hw9; run from caique-rtl/model)
// Each event: marker m1, the write under test (MCIRE bit 10, or TIMx = P << 8 | 0xFF), marker m2 in one burst, then the
// SH4 polls MCIPD until the bit is set and writes marker m3.  A marker's X0 lies after the last frame read without it
// and before the first with it (clocks 2 mod 4); the write's X0 lies between m1's and m2's.  Hypothesis (phi, phase):
// the event happens at clock phi of every sample (phi = ph, 0..511) whose MDEC_CT at ph 0 is = phase mod 2^P; the
// first such clock after the write's X0 is T, and m3's X0 - T is the detection latency (a poll read, the SH4's reaction,
// the write).  The right hypothesis gives every event of a group the same latency up to the poll period; groups: the
// interval bit, and each prescale P (timers A/B/C together, and apart).  Printed: the best hypotheses by latency spread.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <climits>

struct Ev { int b, e, kind, x, P; uint32_t m1, m2, m3; };
struct Win { int64_t lo, hi; };   // X0 in [lo, hi]
struct Pt { int kind, x, P; Win w, m3; std::vector<uint32_t> mdec_of; int64_t n0; };   // mdec_of[n - n0]

int main(int argc, char **argv) {
    std::string dir = argc > 1 ? argv[1] : "tests/timer_phase/hw9";
    std::vector<Ev> evs;
    {
        FILE *f = fopen((dir + "/timer_phase.txt").c_str(), "r");
        if (!f) { perror("timer_phase.txt"); return 2; }
        char line[256];
        while (fgets(line, sizeof line, f)) {
            Ev e; unsigned m1, m2, m3;
            if (sscanf(line, "E %d %d %d %d %d %u %u %u", &e.b, &e.e, &e.kind, &e.x, &e.P, &m1, &m2, &m3) == 8) { e.m1 = m1; e.m2 = m2; e.m3 = m3; evs.push_back(e); }
        }
        fclose(f);
    }
    std::vector<Pt> pts;
    int lost = 0;
    std::map<int, std::vector<Ev>> byb;
    for (auto &e : evs) byb[e.b].push_back(e);
    for (auto &kv : byb) {
        std::vector<uint16_t> ring(65536);
        char name[64];
        snprintf(name, sizeof name, "/tp_%d.bin", kv.first);
        FILE *rf = fopen((dir + name).c_str(), "rb");
        if (!rf || fread(ring.data(), 2, 65536, rf) != 65536) { if (rf) fclose(rf); lost += (int)kv.second.size(); continue; }
        fclose(rf);
        // counter (region 0): word -(n + 1) at address m; n + m is one constant for every word the logger wrote
        std::map<uint32_t, int> cnt;
        for (uint32_t m = 0; m < 65536; m++) cnt[(uint32_t)((-(int32_t)(int16_t)ring[m] - 1) + m) & 0xFFFF]++;
        uint32_t C = 0; int best = -1;
        for (auto &c : cnt) if (c.second > best) { best = c.second; C = c.first; }
        uint32_t mlo = UINT32_MAX, mhi = 0;
        for (auto &e : kv.second) { mlo = std::min(mlo, e.m1 - 1); mhi = std::max(mhi, e.m3); }
        auto marker = [&](int p, uint32_t m) { return (uint32_t)(uint16_t)(-(int32_t)(int16_t)ring[(1024 * p + m) & 0xFFFF]); };
        std::vector<std::pair<uint32_t, uint32_t>> raw;   // (n16, m)
        int64_t nref = -1;
        for (uint32_t m = 0; m < 65536; m++) {
            const uint32_t n16 = (uint32_t)(-(int32_t)(int16_t)ring[m] - 1) & 0xFFFF;
            if (((n16 + m) & 0xFFFF) != C) continue;
            bool inb = true, last = true;
            for (int p = 1; p < 64 && inb; p++) { const uint32_t mk = marker(p, m); inb = mk >= mlo && mk <= mhi; last &= mk == mhi; }
            if (!inb) continue;
            raw.push_back({n16, m});
            if (last) nref = n16;
        }
        if (nref < 0) { lost += (int)kv.second.size(); continue; }
        struct Rd { int64_t t; uint32_t mk; };
        std::vector<Rd> rd;
        std::map<int64_t, uint32_t> mdec;   // DSP sample n (starts at ph 64 of SGC sample n) -> MDEC_CT
        for (auto &s : raw) {
            const int64_t n = 100000 + (int16_t)(uint16_t)(s.first - nref);
            mdec[n] = s.second;
            for (int p = 1; p < 64; p++) rd.push_back({512 * n + 65 + 8 * p, marker(p, s.second)});
        }
        std::sort(rd.begin(), rd.end(), [](const Rd &a, const Rd &b) { return a.t < b.t; });
        auto window = [&](uint32_t mk, Win &w) {   // X0 of marker mk (reads show markers in order: >= mk counts)
            for (size_t i = 0; i < rd.size(); i++)
                if (rd[i].mk >= mk && rd[i].mk <= mhi) {
                    if (i == 0) return false;
                    int64_t lo = rd[i - 1].t, hi = rd[i].t;
                    while ((lo & 3) != 2) lo++;
                    while ((hi & 3) != 2) hi--;
                    w = {lo, hi};
                    return lo <= hi;
                }
            return false;
        };
        const int64_t n0 = mdec.begin()->first, n1 = mdec.rbegin()->first;
        for (auto &e : kv.second) {
            Win a, b, c;
            if (!window(e.m1, a) || !window(e.m2, b) || !window(e.m3, c)) { lost++; continue; }
            Pt pt{e.kind, e.x, e.P, {a.lo, b.hi}, c, {}, n0};
            for (int64_t n = n0; n <= n1; n++) pt.mdec_of.push_back(mdec.count(n) ? mdec[n] : 0x10000);
            pts.push_back(pt);
        }
    }
    printf("timer_check %s: %zu events, %zu decoded, %d lost\n", dir.c_str(), evs.size(), pts.size(), lost);
    // latency of one event under (phi, phase, P): [min, max] over the write's and m3's X0 windows
    auto lat = [&](const Pt &p, int phi, uint32_t phase, int P, int64_t &lmin, int64_t &lmax) {
        const uint32_t mask = (1u << P) - 1;
        auto first_T = [&](int64_t x0) -> int64_t {
            int64_t n = x0 / 512;
            for (int k = 0; k < (2 << P) + 2; k++, n++) {
                const int64_t T = 512 * n + phi;
                if (T <= x0) continue;
                const int64_t idx = n - p.n0;
                if (idx < 0 || idx >= (int64_t)p.mdec_of.size() || p.mdec_of[idx] > 0xFFFF) return INT64_MIN;
                if ((p.mdec_of[idx] & mask) == phase) return T;
            }
            return INT64_MIN;
        };
        const int64_t t_lo = first_T(p.w.lo), t_hi = first_T(p.w.hi);
        if (t_lo == INT64_MIN || t_hi == INT64_MIN) return false;
        lmin = p.m3.lo - t_hi; lmax = p.m3.hi - t_lo;
        return true;
    };
    struct Group { const char *name; int kind, x, P; };
    std::vector<Group> groups = {{"interval bit 10", 0, -1, 0}};
    static char names[32][48];
    int ng = 0;
    for (int P : {0, 1, 2, 3, 5}) { snprintf(names[ng], 48, "timers A/B/C, P %d", P); groups.push_back({names[ng++], 1, -1, P}); }
    for (int x = 0; x < 3; x++) for (int P : {0, 3}) { snprintf(names[ng], 48, "timer %c, P %d", 'A' + x, P); groups.push_back({names[ng++], 1, x, P}); }
    for (auto &g : groups) {
        std::vector<const Pt *> gp;
        for (auto &p : pts) if (p.kind == g.kind && (g.x < 0 || p.x == g.x) && p.P == g.P) gp.push_back(&p);
        if (gp.empty()) continue;
        struct H { int phi; uint32_t phase; int64_t lo, hi; };
        std::vector<H> hs;
        for (uint32_t phase = 0; phase < (1u << g.P); phase++)
            for (int phi = 0; phi < 512; phi++) {
                // every event allows a latency interval [a, b]; a band meeting all of them needs width max(a) - min(b)
                int64_t maxmin = INT64_MIN, minmax = INT64_MAX; bool ok = true;
                for (auto *p : gp) { int64_t a, b; if (!lat(*p, phi, phase, g.P, a, b)) { ok = false; break; } maxmin = std::max(maxmin, a); minmax = std::min(minmax, b); }
                if (!ok) continue;
                hs.push_back({phi, phase, minmax, maxmin});
            }
        std::sort(hs.begin(), hs.end(), [](const H &a, const H &b) { return a.hi - a.lo < b.hi - b.lo; });
        printf("%-20s %3zu events (phi = ph of the event, phase = its sample's MDEC_CT mod %u; need = the latency band every event\n"
               "                     allows must be this wide, negative: they share a range):\n", g.name, gp.size(), 1u << g.P);
        for (size_t i = 0; i < hs.size() && i < 4; i++)
            printf("    phi %3d (frame %2d c%d) phase %2u: need %4lld  (latencies from %lld to %lld)\n", hs[i].phi, hs[i].phi / 8,
                   hs[i].phi % 8, hs[i].phase, (long long)(hs[i].hi - hs[i].lo), (long long)std::min(hs[i].lo, hs[i].hi),
                   (long long)std::max(hs[i].lo, hs[i].hi));
    }
    return 0;
}
