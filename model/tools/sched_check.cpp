// sched_check.cpp -- analyse tests/eg_sched: for every write group the effect sample of the two register writes (slots A, B)
// and the key-on samples of the two witnesses (wl low slot, wh high slot) keyed on by the group's KYONEX, as offsets from
// wl's onset E.  Fetch runs (f_): the effect is the output toggle 262144 <-> 131072; envelope runs (e_): the first level
// change of the held release (quantised to the envelope clock, so the offsets are reported per parity of E).
//   sched_check <dir>          (dir holds eg_sched.txt and <run>.hdr/.bin; c0 from the cap_start lines)
// Build: make -C tools sched_check
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <map>
#include "filt_capture.h"
struct Ev { int cyc, j, order, base; long t_us; };
struct Run { std::string name; char kind; int A, B, wl, wh, cycles; uint32_t c0 = 0; std::vector<Ev> evs; };
static std::vector<std::pair<int,int>> marks(const std::string &p) {
    std::vector<std::pair<int,int>> m; FILE *f = fopen((p + ".hdr").c_str(), "rb"); if (!f) return m;
    uint32_t h[300] = {0}; size_t n = fread(h, 4, 300, f); fclose(f);
    for (size_t i = 11; i + 1 < n && i < 11 + 2 * h[6]; i += 2) m.push_back({(int)h[i], (int)h[i + 1]});
    return m;
}
int main(int argc, char **argv) {
    std::string dir = argc > 1 ? argv[1] : "tests/eg_sched/hw";
    std::vector<Run> runs; FILE *f = fopen((dir + "/eg_sched.txt").c_str(), "r"); if (!f) { perror("eg_sched.txt"); return 1; }
    char line[512], name[64]; char kind; int A, B, wl, wh, cyc, ev, order, base, cycles; long t; unsigned c0;
    int pending_c0 = 0;
    while (fgets(line, sizeof line, f)) {
        if (sscanf(line, "%63s run: kind %c A %d B %d wl %d wh %d cycles %d", name, &kind, &A, &B, &wl, &wh, &cycles) == 7) { runs.push_back({name, kind, A, B, wl, wh, cycles}); pending_c0 = 1; continue; }
        const char *p = strstr(line, ", c0 ");
        if (pending_c0 && strstr(line, "cap_start:") && p && sscanf(p + 5, "%x", &c0) == 1) { runs.back().c0 = c0; pending_c0 = 0; continue; }
        if (sscanf(line, "%63s cyc %d ev %d order %d base %d t_us %ld", name, &cyc, &ev, &order, &base, &t) == 6 && !runs.empty() && runs.back().name == name) runs.back().evs.push_back({cyc, ev, order, base, t});
    }
    fclose(f);
    printf("sched_check %s: %zu runs.  Per event: E = wl's key-on sample (MDEC_CT parity), dh = wh onset - E, dA / dB = register effect sample - E.\n", dir.c_str(), runs.size());
    for (auto &r : runs) {
        Capture c; try { c = cap(dir + "/" + r.name); } catch (std::exception &e) { printf("%s: %s\n", r.name.c_str(), e.what()); continue; }
        auto mk = marks(dir + "/" + r.name);
        auto md = [&](int i) { return (r.c0 - c.first - (uint32_t)i) & 0xFFFF; };
        auto v = [&](int i, int k) { return c.v[(size_t)i * c.ns + k]; };
        printf("== %s (kind %c, A %d B %d wl %d wh %d, c0 %04x, %u samples, %zu events, %zu marks)\n", r.name.c_str(), r.kind, r.A, r.B, r.wl, r.wh, r.c0, c.n, r.evs.size(), mk.size());
        // tallies: key hA[order][d+2], hB, hh; envelope: per parity of E
        std::map<std::string, std::map<int,int>> H;
        int nbad = 0, scan = 1;
        for (size_t e = 0; e < r.evs.size(); e++) {
            // the marks are head estimates (hundreds of samples off on the console): events are located by scanning for wl onsets
            int El = -1, Eh = -1, SA = -1, SB = -1;
            for (int i = scan; i < (int)c.n; i++) if (v(i, 2) >= 200000 && v(i - 1, 2) < 100000) { El = i; break; }
            if (El < 0) { nbad++; printf("  cyc %2d ev %d: no more wl onsets after %d\n", r.evs[e].cyc, r.evs[e].j, scan); continue; }
            scan = El + 100;
            int lo = El - 60, hi = El + (r.kind == 'f' ? 60 : 400); if (lo < 1) lo = 1; if (hi >= (int)c.n) hi = c.n - 1;
            for (int i = lo; i < hi; i++) if (v(i, 3) >= 200000 && v(i - 1, 3) < 100000) { Eh = i; break; }
            for (int i = lo + 1; i < hi; i++) { if (SA < 0 && v(i, 0) != v(lo, 0)) SA = i; if (SB < 0 && v(i, 1) != v(lo, 1)) SB = i; }
            const Ev &ev = r.evs[e];
            if (El < 0 || Eh < 0 || SA < 0 || SB < 0) { nbad++; printf("  cyc %2d ev %d order %d: NOT FOUND (El %d Eh %d SA %d SB %d, window %d..%d)\n", ev.cyc, ev.j, ev.order, El, Eh, SA, SB, lo, hi); continue; }
            int par = md(El) & 1, dh = Eh - El, dA = SA - El, dB = SB - El;
            printf("  cyc %2d ev %d order %d: E %6d (%s)  dh %+d  dA %+d  dB %+d\n", ev.cyc, ev.j, ev.order, El, par ? "odd " : "EVEN", dh, dA, dB);
            char key[64];
            snprintf(key, sizeof key, "wh-wl (slot %d - slot %d) order %d", r.wh, r.wl, ev.order); H[key][dh]++;
            if (r.kind == 'f') {
                snprintf(key, sizeof key, "fetch slot %2d order %d", r.A, ev.order); H[key][dA]++;
                snprintf(key, sizeof key, "fetch slot %2d order %d", r.B, ev.order); H[key][dB]++;
            } else {
                snprintf(key, sizeof key, "envelope slot %2d order %d E %s", r.A, ev.order, par ? "odd " : "even"); H[key][dA]++;
                snprintf(key, sizeof key, "envelope slot %2d order %d E %s", r.B, ev.order, par ? "odd " : "even"); H[key][dB]++;
            }
        }
        printf("  summary (%d events not found):\n", nbad);
        for (auto &kv : H) { printf("    %-40s", kv.first.c_str()); for (auto &dv : kv.second) printf("  d%+d:%d", dv.first, dv.second); printf("\n"); }
    }
    return 0;
}
