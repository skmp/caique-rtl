// koffatt_check.cpp -- analyse the captures of cases/feg_koffatt.c (FEG key-off on a clock FROM AN ATTACK, key-off
// sample pinned by an AEG witness on stream 3): recover u = v >> 1 per sample through the bit-exact filter (the
// tools/feg_track.cpp algorithm as in tools/koffdir_check.cpp, but the filter input comes from the known signal,
// x(i) = 8 * sig[(i - onset) mod 8192], since pitch 1.0 and CA restarts at the key-on: there is no reference stream), pin
// the key-off sample E from the witness onset (first sample at level 520176 = a 0 after the key-on), and compare the u
// sequence of every FEG stream with the four readings of the key-off clock (tools/feg_law.h):
//   oldStep  one more step of the OLD segment: old increment and old direction, hold check against the release target
//            (feg_law M_PEND_EVEN = "oldDir"; what F4 measured for decay 2)
//   noStep   no step on E (what F2 measured for the AEG attack; feg_law M_NOSTEP)
//   S3       old increment toward the RELEASE target (src/aica_model.cpp feg_clock before F4; feg_law M_S3)
//   relInc   the release increment already on E (feg_law M_NOS3)
// An odd E takes no step under any reading (the release steps from E+1): the batch is indifferent.
//   koffatt_check <dir> [-K kc] [-u <prefix>]   dir holds feg_koffatt.txt, ka_<b>.hdr/.bin and input.bin (console: tests/feg_koffatt/hw;
//                                   model: tests/feg_koffatt/model); kc = the boot's clock constant (default 6491); -u work/eg/ka_ writes the
//                                   recovered u per stream to work/eg/ka_<b>_<k>.u (the fk_*.u / kd_*.u format: int32 per sample from
//                                   the onset, -1 unknown) for tools/eg_model's ka_ runs
// The FEG programs are parsed from the "ka_<b> stream k: slot k role feg ..." lines (fallback: the table in the case);
// c0 per batch from the ", c0 XXXX" field of the cap_start lines (", c0 " with the comma: a bare "c0 " also matches inside
// a ring address such as "at a9c0 (").
// Build: make -C tools koffatt_check (-> build/tools/koffatt_check; single file, no model link).  Run from caique-rtl/model.
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <set>
#include <map>
#include <tuple>
#include <algorithm>
#include "filt_capture.h"
#include "feg_law.h"
using I = int64_t;
static const int NSIG = 8192;
static const int qm[32] = {192, 176, 160, 144, 128, 120, 112, 104, 96, 88, 80, 72, 64, 60, 56, 52, 48, 44, 40, 36, 32, 30, 28, 26, 24, 22, 20, 18, 16, 15, 14, 13};
static I ceilshr(I n, int s) { return -((-n) >> s); }
struct St { I L, B; int u; bool operator<(const St &o) const { return std::tie(L, B, u) < std::tie(o.L, o.B, o.u); } bool operator==(const St &o) const { return L == o.L && B == o.B && u == o.u; } };
static void fstep(I &L, I &B, I x, int u, int Q) { int v = u << 1, k = v >= 0x1ffe ? 512 : 256 + (u & 255), s = 24 - (v >> 9); I d = 2 * ceilshr(qm[Q] * B, 8); B += (k * (x - L - d)) >> s; L += ceilshr(k * B, s); }
// recover u(n) from the onset for stream k; input x(n) = 8 * sig[(n - on) mod NSIG] (MIXS of an unfiltered VOFF 1 slot is
// 16 * sample, and the tracker's x is that >> 1); FLV0 known
// nmax > 0 limits the tracked length (onset search).  The filter state before the onset is whatever the slot left from
// the previous batch (a stopped VOFF 1 slot keeps a rest value on its bus, tests/slot_tail): L from the last output
// before the onset, B in [-256, 256].
static std::vector<int32_t> track(const Capture &c, unsigned on, int k, int flv0, int Q, const std::vector<int16_t> &sig, size_t &maxset, unsigned nmax = 0) {
    std::vector<St> S, T;
    for (I b0 = -256; b0 <= 256; b0++) S.push_back({-I(c.v[(on - 1) * c.ns + k]) / 2, b0, flv0 >> 1});
    unsigned end = nmax && on + nmax < c.n ? on + nmax : c.n;
    std::vector<int32_t> u(end - on, -1);
    maxset = 0;
    for (unsigned n = on; n < end; n++) {
        I x = 8 * I(sig[(n - on) % NSIG]);
        T.clear();
        for (auto &s : S)
            for (int du = -4; du <= 4; du++) {
                int nu = s.u + du; if (nu < 0 || nu > 4095) continue;
                I L = s.L, B = s.B; fstep(L, B, x, nu, Q);
                if (std::clamp<I>(-2 * L, -524288, 524287) == c.v[n * c.ns + k]) T.push_back({L, B, nu});
            }
        std::sort(T.begin(), T.end()); T.erase(std::unique(T.begin(), T.end()), T.end());
        if (T.empty()) { u.resize(n - on); break; }
        if (T.size() > 400000) T.resize(400000);
        maxset = std::max(maxset, T.size());
        bool one = true; for (auto &s : T) one &= s.u == T[0].u;
        u[n - on] = one ? T[0].u : -1;
        std::swap(S, T);
    }
    return u;
}
static const Prog def_progs[3] = {   /* = cases/feg_koffatt.c */
    {{0x1C00, 0x1800, 0x1800, 0x1800, 0x1C00}, {26, 31, 31, 28}, 15, 0, 0},
    {{0x1800, 0x1C00, 0x1C00, 0x1C00, 0x1800}, {26, 31, 31, 28}, 15, 0, 0},
    {{0x1800, 0x1C00, 0x1C00, 0x1C00, 0x1C00}, {26, 31, 31, 28}, 15, 0, 0},
};
// simulate stream k from the onset with the key-off on sample K under mechanism m; returns the number of matched samples
// (u.size() = all), the first mismatch in *bad_n / *bad_v, and (u_at) the simulated u at samples [E-6, E+8]
static int sim(const Prog &p, const std::vector<int32_t> &u, uint32_t c0, uint32_t first, int on, Mech m, int K, uint32_t Kc, int *bad_n, int *bad_v,
               int *u_at = nullptr, int E = -1) {
    Feg f{}; feg_key_on(f, p);
    int end = on + (int)u.size();
    int res = -1;
    for (int i = on; i < end; i++) {
        uint32_t md = (c0 - first - (uint32_t)i) & 0xFFFF;
        bool koff_now = false;
        if (i == K) { feg_key_off(f, p); koff_now = true; }
        int inc = -1, rs = -1;
        if ((md & 1) == 0 && i > on) feg_clock_step(f, p, (Kc - (md >> 1)) & 0x3FFF, m, koff_now, inc, rs);
        int n = i - on;
        if (u_at && i >= E - 6 && i <= E + 8) u_at[i - (E - 6)] = f.v >> 1;
        if (res < 0 && u[n] >= 0 && u[n] != (f.v >> 1)) { if (bad_n) { *bad_n = n; *bad_v = f.v; } res = n; if (!u_at) return n; }
    }
    return res < 0 ? (int)u.size() : res;
}
static const Mech mechs[4] = {M_PEND_EVEN, M_NOSTEP, M_S3, M_NOS3};
static const char *mnames[4] = {"oldStep", "noStep", "S3(oldInc->rel)", "relInc"};
struct Verdict { int matched[3][4]; int full; };   /* per stream x mech */
int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: koffatt_check <dir> [-K kc] [-u <prefix>]\n  -u work/eg/ka_  writes the recovered u per stream to <prefix><b>_<k>.u (int32 per sample from the onset, -1 unknown; the format of work/eg/fk_*.u, read by tools/eg_model)\n"); return 2; }
    std::string dir = argv[1]; uint32_t Kc = 6491; const char *uprefix = nullptr;
    for (int i = 2; i + 1 < argc; i++) { if (!strcmp(argv[i], "-K")) Kc = (uint32_t)atoi(argv[++i]); else if (!strcmp(argv[i], "-u")) uprefix = argv[++i]; }
    /* c0 per batch and the FEG programs from the text output */
    std::vector<uint32_t> c0s;
    std::map<int, Prog[3]> progs_of; std::map<int, int> q_of;
    { FILE *f = fopen((dir + "/feg_koffatt.txt").c_str(), "r"); if (!f) { perror("feg_koffatt.txt"); return 2; } char line[512];
      while (fgets(line, sizeof line, f)) {
          const char *p = strstr(line, ", c0 ");
          if (strstr(line, "cap_start") && p) { unsigned c0; if (sscanf(p, ", c0 %x", &c0) == 1) c0s.push_back(c0); continue; }
          unsigned b; int k, slot, krs, flv[5], rt[4], Q, lpoff, voff;
          if (sscanf(line, "ka_%u stream %d: slot %d role feg KRS %d FLV %x %x %x %x %x FAR %d FD1R %d FD2R %d FRR %d Q %d lpoff %d voff %d",
                     &b, &k, &slot, &krs, &flv[0], &flv[1], &flv[2], &flv[3], &flv[4], &rt[0], &rt[1], &rt[2], &rt[3], &Q, &lpoff, &voff) == 16 && k >= 0 && k < 3) {
              Prog &P = progs_of[(int)b][k]; for (int j = 0; j < 5; j++) P.flv[j] = flv[j]; for (int j = 0; j < 4; j++) P.rate[j] = rt[j]; P.krs = krs; P.oct = 0; P.fns = 0; q_of[(int)b] = Q;
          }
      }
      fclose(f); }
    /* the input signal: regenerated from the case's LCG, checked against input.bin when present */
    std::vector<int16_t> sig(NSIG);
    { uint32_t seed = 4242; for (int i = 0; i < NSIG; i++) { seed = seed * 1103515245u + 12345u; sig[i] = (int16_t)(seed >> 16); if (!sig[i]) sig[i] = 1; }
      try { std::vector<int16_t> in = input(dir + "/input.bin"); if (in != sig) { fprintf(stderr, "input.bin differs from the regenerated signal\n"); return 2; } }
      catch (std::exception &) { printf("(no input.bin in %s: using the regenerated signal)\n", dir.c_str()); } }
    printf("koffatt_check %s: %zu batches, clock constant K %u\n", dir.c_str(), c0s.size(), Kc);
    printf("readings of an even key-off sample E (v = the FEG value before E): oldStep = old increment, old direction; noStep = hold on E;\n"
           "S3 = old increment toward the release target (the model before F4); relInc = release increment on E.  Odd E: all coincide.\n");
    std::vector<std::string> summary;
    int n_even = 0, n_odd = 0, full_even[4] = {0, 0, 0, 0}, full_even_streams[3][4] = {{0}}, inf_even_streams[3][4] = {{0}};
    for (unsigned b = 0; b < c0s.size(); b++) {
        std::string path = dir + "/ka_" + std::to_string(b);
        Capture c; try { c = cap(path); } catch (std::exception &e) { printf("== ka_%u: %s\n", b, e.what()); continue; }
        FILE *hf = fopen((path + ".hdr").c_str(), "rb"); uint32_t h[300] = {0}; size_t nh = fread(h, 4, 300, hf); fclose(hf);
        std::map<int, int> mark;
        for (size_t i = 11; i + 1 < nh && i < 11 + 2 * h[6]; i += 2) if (!mark.count((int)h[i])) mark[(int)h[i]] = (int)(h[i + 1] - c.first);
        const Prog *progs = progs_of.count((int)b) ? progs_of[(int)b] : def_progs;
        int Q = q_of.count((int)b) ? q_of[(int)b] : 4;
        /* FEG onset (= the key-on sample): the first sample in the mark-1 window where a FEG stream changes (before it
         * the streams carry the rest value the filters kept from the previous batch, 0 in the first one), verified by
         * the tracker (the first candidate it can follow for 300 samples) */
        int m1 = mark.count(1) ? mark[1] : 200;
        unsigned on = 0, on_first = 0;
        for (int i = std::max(1, m1 - 400); i < std::min((int)c.n - 300, m1 + 400) && !on; i++) {
            bool ch = false; for (int k = 0; k < 3; k++) ch |= c.v[i * c.ns + k] != c.v[(i - 1) * c.ns + k];
            if (!ch) continue;
            if (!on_first) on_first = i;
            size_t ms; auto u = track(c, i, 0, progs[0].flv[0], Q, sig, ms, 300);
            if (u.size() >= 300) on = i;
        }
        if (!on) { printf("== ka_%u: FEG onset NOT FOUND (no sample in the mark-1 window [%d, %d] starts a trackable FEG stream; first change at %u, marks:", b, m1 - 400, m1 + 400, on_first); for (auto &m : mark) printf(" %d@%d", m.first, m.second); printf(")\n"); continue; }
        /* key-off sample E: the witness (stream 3) jumps to 520176 (a = 0) on its key-on sample */
        int E = -1;
        for (unsigned i = on + 1; i + 1 < c.n; i++) if (c.v[i * c.ns + 3] == 520176 && c.v[(i - 1) * c.ns + 3] != 520176) { E = (int)i; break; }
        uint32_t md_on = (c0s[b] - c.first - on) & 0xFFFF;
        printf("== ka_%u: %u samples, first %u, c0 %04x, FEG onset %u (MDEC_CT %04x %s%s; rest values before it: %d %d %d), marks:", b, c.n, c.first, c0s[b], on, md_on, (md_on & 1) ? "odd" : "even",
               on != on_first ? ", not the first change" : "", c.v[(on - 1) * c.ns], c.v[(on - 1) * c.ns + 1], c.v[(on - 1) * c.ns + 2]);
        for (auto &m : mark) printf(" %d@%d", m.first, m.second);
        if (E < 0) { printf("\n   witness onset (520176) NOT FOUND after the FEG onset: stream 3 levels around mark 3:"); int m3 = mark.count(3) ? mark[3] : (int)on + 100; for (int i = std::max(0, m3 - 3); i < std::min((int)c.n, m3 + 12); i++) printf(" %d", c.v[i * c.ns + 3]); printf("\n"); continue; }
        uint32_t md_E = (c0s[b] - c.first - (uint32_t)E) & 0xFFFF;
        bool even = (md_E & 1) == 0;
        printf("\n   key-off sample E %d (witness onset; MDEC_CT %04x %s, %d samples after the key-on), witness levels E-1..E+3: %d %d %d %d %d\n", E, md_E, even ? "EVEN = a clock: informative" : "odd: indifferent", E - (int)on,
               c.v[(E - 1) * c.ns + 3], c.v[E * c.ns + 3], c.v[(E + 1) * c.ns + 3], c.v[(E + 2) * c.ns + 3], c.v[(E + 3) * c.ns + 3]);
        if (even) n_even++; else n_odd++;
        std::vector<int32_t> us[3];
        for (int k = 0; k < 3; k++) { size_t ms; us[k] = track(c, on, k, progs[k].flv[0], Q, sig, ms); int amb = 0; for (auto x : us[k]) amb += x < 0;
            printf("   stream %d: FLV %04x->%04x rel %04x FAR %d FRR %d (R %u/%u, inc %u/%u): tracked %zu/%u samples, %d ambiguous, max set %zu%s", k, progs[k].flv[0], progs[k].flv[1], progs[k].flv[4], progs[k].rate[0], progs[k].rate[3],
                   eff_rate(progs[k], progs[k].rate[0]), eff_rate(progs[k], progs[k].rate[3]), eg_increment(eff_rate(progs[k], progs[k].rate[0]), 0), eg_increment(eff_rate(progs[k], progs[k].rate[3]), 0),
                   us[k].size(), c.n - on, amb, ms, us[k].size() < c.n - on ? "  <-- TRACKER LOST THE FEG (wrong onset / input / FLV0?)" : "");
            if (uprefix) {   /* u file for tools/eg_model (one int32 per sample from the onset, -1 = unknown), as koffdir_check -u */
                std::string uf = std::string(uprefix) + std::to_string(b) + "_" + std::to_string(k) + ".u";
                FILE *f = fopen(uf.c_str(), "wb");
                if (f) { fwrite(us[k].data(), 4, us[k].size(), f); fclose(f); printf(" -> %s", uf.c_str()); } else printf(" (cannot write %s)", uf.c_str());
            }
            printf("\n"); }
        /* a stream the tracker lost before E + 200 gets no verdict (a vacuous match on a short prefix is not FULL) */
        bool usable[3]; int n_usable = 0;
        for (int k = 0; k < 3; k++) { usable[k] = (int)us[k].size() >= E - (int)on + 200; n_usable += usable[k]; }
        if (n_usable < 3) printf("   %d stream(s) untracked past E + 200: excluded from the verdicts\n", 3 - n_usable);
        /* per mechanism and stream: matched samples with the pinned E; free search of the key-off sample in [E-3, E+3] */
        int matched[3][4]; int uat[3][4][15]; memset(uat, 0, sizeof uat);
        int full_here[4] = {0, 0, 0, 0};
        for (int mi = 0; mi < 4; mi++) {
            std::set<int> inter; bool firstk = true; std::string per;
            for (int k = 0; k < 3; k++) {
                int bn = -1, bv = 0;
                if (!usable[k]) { matched[k][mi] = -1; char buf[64]; snprintf(buf, sizeof buf, "  s%d UNTRACKED", k); per += buf; continue; }
                matched[k][mi] = sim(progs[k], us[k], c0s[b], c.first, (int)on, mechs[mi], E, Kc, &bn, &bv, uat[k][mi], E);
                std::set<int> ks;
                for (int K = E - 3; K <= E + 3; K++) if (K > (int)on && sim(progs[k], us[k], c0s[b], c.first, (int)on, mechs[mi], K, Kc, nullptr, nullptr) == (int)us[k].size()) ks.insert(K);
                char buf[200];
                if (matched[k][mi] >= (int)us[k].size()) { snprintf(buf, sizeof buf, "  s%d FULL", k); full_here[mi]++; }
                else snprintf(buf, sizeof buf, "  s%d fail@E%+d (hw u %03x, predicted %03x)", k, matched[k][mi] + (int)on - E, us[k][matched[k][mi]], bv >> 1);
                per += buf;
                std::string t; for (int K : ks) t += " " + std::to_string(K - E) + (((c0s[b] - c.first - K) & 1) ? "o" : "e"); if (ks.empty()) t = " none";
                per += " [key-off samples reproducing, rel. E:" + t + "]";
                if (firstk) { inter = ks; firstk = false; } else { std::set<int> in; std::set_intersection(inter.begin(), inter.end(), ks.begin(), ks.end(), std::inserter(in, in.begin())); inter = in; }
                if (even) { inf_even_streams[k][mi]++; if (matched[k][mi] >= (int)us[k].size()) full_even_streams[k][mi]++; }
            }
            printf("   %-16s%s  -> shared key-off sample:", mnames[mi], per.c_str());
            if (inter.empty()) printf(" NONE"); else for (int K : inter) printf(" E%+d(%s)", K - E, ((c0s[b] - c.first - K) & 1) ? "odd" : "even");
            printf("%s\n", n_usable == 3 && full_here[mi] == 3 ? "   <== fits the whole capture on all 3 streams" : "");
            if (even && n_usable == 3 && full_here[mi] == 3) full_even[mi]++;
        }
        /* the window: observed u per stream, then each reading's prediction */
        printf("   window E-6..E+8 (u = v >> 1 per sample; observed, then the predictions; ? = ambiguous):\n");
        printf("     %-22s", "i (rel. E)  MDEC_CT"); for (int k = 0; k < 3; k++) printf("  s%d   ", k); printf("\n");
        for (int i = E - 6; i <= E + 8; i++) {
            int n = i - (int)on; if (n < 0) continue;
            uint32_t md = (c0s[b] - c.first - (uint32_t)i) & 0xFFFF;
            printf("     %6d (%+3d) %04x %s%s", i, i - E, md, (md & 1) ? "odd " : "EVEN", i == E ? " *" : "  ");
            for (int k = 0; k < 3; k++) { int x = n < (int)us[k].size() ? us[k][n] : -2; if (x < 0) printf("    ?  "); else printf("  %03x  ", x); }
            printf("   observed\n");
        }
        for (int mi = 0; mi < 4; mi++) {
            printf("     %-22s", mnames[mi]);
            for (int k = 0; k < 3; k++) { printf("  E:%03x", uat[k][mi][6]); }
            printf("   E+2:"); for (int k = 0; k < 3; k++) printf(" %03x", uat[k][mi][8]);
            printf("   E+4:"); for (int k = 0; k < 3; k++) printf(" %03x", uat[k][mi][10]);
            printf("   (%s)\n", full_here[mi] == n_usable && n_usable ? "FULL on every tracked stream" : full_here[mi] ? "partial" : "refuted");
        }
        char s[300]; snprintf(s, sizeof s, "ka_%u  E %6d  %-4s  oldStep %d/%d  noStep %d/%d  S3 %d/%d  relInc %d/%d%s", b, E, even ? "EVEN" : "odd", full_here[0], n_usable, full_here[1], n_usable, full_here[2], n_usable, full_here[3], n_usable,
                 n_usable < 3 ? "  (untracked stream(s))" : "");
        summary.push_back(s);
    }
    printf("\nSUMMARY (streams reproduced to the end of the capture per reading; an odd E is indifferent: all four coincide there)\n");
    for (auto &s : summary) printf("  %s\n", s.c_str());
    printf("  even key-off samples: %d, odd: %d\n", n_even, n_odd);
    printf("  batches with an EVEN E fitted on all 3 streams:  oldStep %d/%d  noStep %d/%d  S3(oldInc->rel) %d/%d  relInc %d/%d\n", full_even[0], n_even, full_even[1], n_even, full_even[2], n_even, full_even[3], n_even);
    printf("  per stream (even E, FULL / batches):");
    for (int k = 0; k < 3; k++) { printf("  s%d:", k); for (int mi = 0; mi < 4; mi++) printf(" %s %d/%d", mnames[mi], full_even_streams[k][mi], inf_even_streams[k][mi]); }
    printf("\n  (s2 releases in the attack's direction: oldStep = S3 there; s0 / s1 separate them.  A reading is refuted when FULL < batches.)\n");
    return 0;
}
