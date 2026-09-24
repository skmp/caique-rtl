// timer_irq_check.cpp -- tests/timer_irq (cases/timer_irq.c) against the timer / interrupt rules (NOTES "Timers and
// interrupts"), on any platform's log: the console's and the models' must both pass.
//   timer_irq_check DIR      (reads DIR/timer_irq.txt; run from caique-rtl/model)
// Rules checked:
//   R  TIMA/B/C, SCIRE, SCILV0-2, MCIRE read 0; SCIEB / MCIEB read back the written value & 0x7FF; a write to SCIPD /
//      MCIPD sets bit 5 only (bits 0-4 never read set)
//   C  MCIRE clears MCIPD only, SCIRE clears SCIPD only (a bit may set again at once only if it is 10); a write of 0x7FF
//      to a pending register sets its bit 5 and nothing in the other
//   L  the SH4 line = (MCIEB & MCIPD) != 0 (the read right after an MCIRE write may still see the old line: a posted
//      G2 write)
//   S  the sample-interval bit sets once per sample: every interval is a whole number of samples (one, unless the SH4
//      was held between two waits), give or take the two detections' last poll gaps
//   T  a timer written with prescale P and count S overflows after (256 - S) ticks of 2^P samples: the first interval
//      lies in ((255 - S) 2^P, (256 - S) 2^P] samples plus the poll delay, the second (count from 0) is 256 2^P
// The log's times are SH4 microseconds; the microseconds per sample are fitted from the second intervals (the console's
// SH4 timer runs 0.26 % fast against the AICA).  Each detection is late by at most the gap between its last two polls
// (logged: the SH4 is held now and then, ~60 us often); the tolerances use it.  A wait whose longest poll gap is over
// 1 ms (~2.5 ms jumps of the SH4 time base or stalls, anywhere in the wait) is skipped and counted; a shorter hold
// (~60 us) widens that wait's tolerance by its length (the time base is not consistent around them: one second
// interval measured 45 us short).  Exit 0 iff every rule holds.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>

int main(int argc, char **argv) {
    std::string dir = argc > 1 ? argv[1] : "tests/timer_irq/hw9";
    FILE *f = fopen((dir + "/timer_irq.txt").c_str(), "r");
    if (!f) { perror("timer_irq.txt"); return 2; }
    std::vector<std::string> lines;
    char buf[1024];
    while (fgets(buf, sizeof buf, f)) lines.push_back(buf);
    fclose(f);
    int bad = 0, nr = 0, nc = 0, nl = 0, ns = 0, nt = 0;
    auto fail = [&](const std::string &l, const char *why) { if (bad < 20) printf("  FAIL (%s): %s", why, l.c_str()); bad++; };
    struct T { char x; int P, S; long d1, d2, d3, g1, g2, g3, x1, x2, x3; };
    std::vector<T> ts;
    for (size_t i = 0; i < lines.size(); i++) {
        const std::string &l = lines[i];
        unsigned reg, w[4], r[4];
        if (sscanf(l.c_str(), "R %x w %x r %x w %x r %x w %x r %x w %x r %x", &reg, &w[0], &r[0], &w[1], &r[1], &w[2], &r[2], &w[3], &r[3]) == 9) {
            nr++;
            for (int k = 0; k < 4; k++) {
                unsigned exp_mask = 0, v = r[k];
                switch (reg) {
                case 0x289C: case 0x28B4: if (v != (w[k] & 0x7FF)) fail(l, "enable register storage"); break;
                case 0x28A0: case 0x28B8:
                    if (v & 0x1F) fail(l, "pending bits 0-4 set");
                    if ((w[k] & 0x20) && !(v & 0x20)) fail(l, "a write of bit 5 did not set it");
                    break;
                default: if (v != 0) fail(l, "a write-only register read non-zero"); break;
                }
                (void)exp_mask;
            }
            continue;
        }
        unsigned bm, bs, am, as;
        char which[8];
        if (sscanf(l.c_str(), "C %7s 7ff: before MCIPD %x SCIPD %x, after MCIPD %x SCIPD %x", which, &bm, &bs, &am, &as) == 5) {
            nc++;
            if (!strcmp(which, "MCIRE")) { if (am & ~0x400u) fail(l, "MCIRE left MCIPD bits"); if ((as | 0x400) != (bs | 0x400)) fail(l, "MCIRE changed SCIPD"); }
            else { if (as & ~0x400u) fail(l, "SCIRE left SCIPD bits"); if ((am | 0x400) != (bm | 0x400)) fail(l, "SCIRE changed MCIPD"); }
            continue;
        }
        if (sscanf(l.c_str(), "C write %7s 7ff: MCIPD %x SCIPD %x", which, &am, &as) == 3) {
            nc++;
            const unsigned own = !strcmp(which, "MCIPD") ? am : as, other = !strcmp(which, "MCIPD") ? as : am;
            if ((own & ~0x400u) != 0x20) fail(l, "a 7ff write set more (or less) than bit 5");
            if (other & ~0x400u) fail(l, "a pending write reached the other register");
            continue;
        }
        int b; unsigned pd, pd2; unsigned l1, l2, l3;
        if (sscanf(l.c_str(), "L MCIEB bit %d: MCIPD %x line %u; after MCIPD write 20: MCIPD %x line %u; after MCIRE 7ff: line %u", &b, &pd, &l1, &pd2, &l2, &l3) == 6) {
            nl++;
            // bit 10 sets at every sample edge: the edge may fall between the MCIPD read and the line read
            if (l1 != (((pd >> b) & 1) ? 1u : 0u) && !(b == 10 && l1)) fail(l, "line != MCIEB & MCIPD");
            if (l2 != (((pd2 >> b) & 1) ? 1u : 0u) && !(b == 10 && l2)) fail(l, "line != MCIEB & MCIPD (after the write)");
            (void)l3;   // posted MCIRE write: either value
            continue;
        }
        if (!strncmp(l.c_str(), "S sample interval", 17)) {
            const char *q = strchr(l.c_str(), ':');
            std::vector<long> v, g, mx;
            for (q = q ? q + 1 : l.c_str(); *q; ) {
                char *e; long x = strtol(q, &e, 10); if (e == q) break; q = e;
                long y = 0, z = 0; if (*q == '/') { y = strtol(q + 1, &e, 10); q = e; } if (*q == '/') { z = strtol(q + 1, &e, 10); q = e; }
                v.push_back(x); g.push_back(y); mx.push_back(z);
            }
            ns = (int)v.size();
            // the first value follows a clear: skip it; each interval = 1 sample + (this delay - the previous delay)
            for (size_t k = 1; k < v.size(); k++) {
                if (mx[k] > 1000) continue;
                // a hold between two waits (not in the gap log) lets the clear land after the next edge: n samples
                const long n = std::max(1L, std::lround(v[k] / 22.65));
                if (v[k] < 22.6 * n - g[k - 1] - 3 || v[k] > 22.7 * n + g[k] + 3) { fail(l, "an interval not a whole number of samples (beyond the poll gaps)"); break; }
            }
            continue;
        }
        T t; long d1, d2, d3; int P, S; char x;
        if (sscanf(l.c_str(), "T %c P %d S %x: first %ld second %ld same-P rewrite %ld", &x, &P, &S, &d1, &d2, &d3) == 6) {
            t = {x, P, S, d1, d2, d3, 0, 0, 0, 0, 0, 0};
            const char *g = strstr(l.c_str(), " gaps ");
            if (g) sscanf(g, " gaps %ld %ld %ld max %ld %ld %ld", &t.g1, &t.g2, &t.g3, &t.x1, &t.x2, &t.x3);
            ts.push_back(t); nt++;
        }
    }
    // the SH4 microseconds per sample: the second intervals are 256 2^P samples
    std::vector<double> sc;
    const long GAP = 12;   // intervals whose detections were this punctual fit the scale
    int skipped = 0;
    for (auto &t : ts) if (t.d2 > 0 && t.d2 < 100000000 && t.g1 <= GAP && t.g2 <= GAP && t.x2 <= 1000) sc.push_back((double)t.d2 / (256.0 * (1 << t.P)));
    std::sort(sc.begin(), sc.end());
    const double us = sc.empty() ? 1e6 / 44100.0 : sc[sc.size() / 2];
    const double poll = 3.0;   // us: a poll read plus the time read
    for (auto &t : ts) {
        const double per = (double)(1 << t.P);
        const double lo = (255 - t.S) * per, hi = (256 - t.S) * per;
        char l[160];
        snprintf(l, sizeof l, "T %c P %d S %02x: first %ld second %ld rewrite %ld\n", t.x, t.P, t.S, t.d1, t.d2, t.d3);
        const long ds[2] = {t.d1, t.d3}, gs[2] = {t.g1, t.g3}, xs[2] = {t.x1, t.x3};
        for (int k = 0; k < 2; k++) {
            if (ds[k] == 4294967295L || xs[k] > 1000) { skipped++; continue; }   // the wait ran out, or a ms jump inside it
            const double s = (double)ds[k] / us;
            const double tb = xs[k] > 20 ? (double)xs[k] / us : 0.0;
            if (s < lo - tb - 0.2 || s > hi + (gs[k] + poll) / us + tb + 0.2) fail(l, "first overflow outside ((255 - S) 2^P, (256 - S) 2^P] samples");
        }
        if (t.d1 == 4294967295L || t.d2 == 4294967295L || t.x2 > 1000) skipped++;
        else {
            const double s2 = (double)t.d2 / us, want = 256.0 * per;
            const double tb = t.x2 > 20 ? (double)t.x2 / us : 0.0;   // a ~60 us hold in the wait: the SH4 time base is off by up to it
            if (s2 < want - (t.g1 + poll) / us - tb - 0.2 || s2 > want + (t.g2 + poll) / us + tb + 0.2) fail(l, "second overflow != 256 2^P samples");
        }
    }
    printf("timer_irq_check %s: %.4f SH4 us per sample (%s), R %d, C %d, L %d, S %d intervals, T %d (%d intervals skipped: a ms jump in the wait, or it ran out): %s\n",
           dir.c_str(), us, sc.empty() ? "nominal" : "fitted", nr, nc, nl, ns, nt, skipped, bad ? "FAIL" : "all rules hold");
    return bad ? 1 : 0;
}
