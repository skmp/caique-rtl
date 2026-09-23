// sched2_check.cpp -- tests/eg_sched2: absolute latencies.  Per write group M = the first sample on which bus 3 carries
// the group's CPU-written MIXS value (the boundary after the write), E = the witness key-on sample (bus 2), S_A / S_B =
// the register effect samples (buses 0 / 1: the SA toggle in f_ runs, the first level change in e_ runs).  Prints
// per event E-M, S_A-M, S_B-M and per (slot, order) histograms; e_ runs also split by the parity of M (the envelope
// clock).   sched2_check <dir>      Build: make -C tools sched2_check
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <map>
#include "filt_capture.h"
struct Ev { int cyc, j, order, mv; long t_us; };
struct Run { std::string name; char kind; int A, B, wl; uint32_t c0 = 0; std::vector<Ev> evs; };
int main(int argc, char **argv) {
    std::string dir = argc > 1 ? argv[1] : "tests/eg_sched2/hw";
    std::vector<Run> runs; FILE *f = fopen((dir + "/eg_sched2.txt").c_str(), "r"); if (!f) { perror("eg_sched2.txt"); return 1; }
    char line[512], name[64]; char kind; int A, B, wl, cyc, ev, order; unsigned mv, c0; long t; int pend = 0;
    while (fgets(line, sizeof line, f)) {
        if (sscanf(line, "%63s run: kind %c A %d B %d wl %d", name, &kind, &A, &B, &wl) == 5) { runs.push_back({name, kind, A, B, wl}); pend = 1; continue; }
        const char *p = strstr(line, ", c0 ");
        if (pend && strstr(line, "cap_start:") && p && sscanf(p + 5, "%x", &c0) == 1) { runs.back().c0 = c0; pend = 0; continue; }
        if (sscanf(line, "%63s cyc %d ev %d order %d mixs %x t_us %ld", name, &cyc, &ev, &order, &mv, &t) == 6 && !runs.empty() && runs.back().name == name) runs.back().evs.push_back({cyc, ev, order, (int)mv, t});
    }
    fclose(f);
    printf("sched2_check %s: %zu runs.  M = boundary after the group (MIXS3 anchor); E - M = witness key-on, S - M = register effect (fetch: SA toggle; envelope: first level change).\n", dir.c_str(), runs.size());
    for (auto &r : runs) {
        Capture c; try { c = cap(dir + "/" + r.name); } catch (std::exception &e) { printf("%s: %s\n", r.name.c_str(), e.what()); continue; }
        auto md = [&](int i) { return (r.c0 - c.first - (uint32_t)i) & 0xFFFF; };
        auto v = [&](int i, int k) { return c.v[(size_t)i * c.ns + k]; };
        printf("== %s (kind %c, A %d B %d wl %d, c0 %04x, %u samples, %zu events)\n", r.name.c_str(), r.kind, r.A, r.B, r.wl, r.c0, c.n, r.evs.size());
        std::map<std::string, std::map<int,int>> H; int nbad = 0, scan = 1;
        for (auto &e : r.evs) {
            int32_t want = (int32_t)(e.mv << 4);
            int M = -1, E = -1, SA = -1, SB = -1;
            for (int i = scan; i < (int)c.n; i++) if (v(i, 3) == want) { M = i; break; }
            if (M < 0) { nbad++; printf("  cyc %2d ev %d: anchor %03x not found after %d\n", e.cyc, e.j, e.mv, scan); continue; }
            scan = M + 50;
            int lo = M - 8, hi = M + (r.kind == 'f' ? 12 : 400); if (lo < 1) lo = 1; if (hi >= (int)c.n) hi = c.n - 1;
            for (int i = lo; i < hi; i++) if (v(i, 2) >= 200000 && v(i - 1, 2) < 100000) { E = i; break; }
            for (int i = lo + 1; i < hi; i++) { if (SA < 0 && v(i, 0) != v(lo, 0)) SA = i; if (SB < 0 && v(i, 1) != v(lo, 1)) SB = i; }
            if (E < 0 || SA < 0 || SB < 0) { nbad++; printf("  cyc %2d ev %d order %d: M %d, NOT FOUND (E %d SA %d SB %d)\n", e.cyc, e.j, e.order, M, E, SA, SB); continue; }
            int par = md(M) & 1;
            printf("  cyc %2d ev %d order %d: M %6d (%s)  E-M %+d  SA-M %+d  SB-M %+d\n", e.cyc, e.j, e.order, M, par ? "odd " : "EVEN", E - M, SA - M, SB - M);
            char key[80];
            snprintf(key, sizeof key, "key-on (slot %2d) order %d", r.wl, e.order); H[key][E - M]++;
            if (r.kind == 'f') { snprintf(key, sizeof key, "fetch slot %2d order %d", r.A, e.order); H[key][SA - M]++; snprintf(key, sizeof key, "fetch slot %2d order %d", r.B, e.order); H[key][SB - M]++; }
            else { snprintf(key, sizeof key, "envelope slot %2d order %d M %s", r.A, e.order, par ? "odd " : "even"); H[key][SA - M]++; snprintf(key, sizeof key, "envelope slot %2d order %d M %s", r.B, e.order, par ? "odd " : "even"); H[key][SB - M]++; }
        }
        printf("  summary (%d events not found):\n", nbad);
        for (auto &kv : H) { printf("    %-36s", kv.first.c_str()); for (auto &dv : kv.second) printf("  d%+d:%d", dv.first, dv.second); printf("\n"); }
    }
    return 0;
}
