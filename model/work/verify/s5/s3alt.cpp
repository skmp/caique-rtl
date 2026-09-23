// s3alt.cpp -- alternative readings of claim S3 (key-off on a clock sample steps with the previous segment's
// increment) tested against every tracked FEG stream: feg_krs fk_0..3 (12), feg_track ft_0..2 (9) and, as a bonus,
// eg_lock feg_odd (3).  Standalone FEG law (NOTES "Filter envelope (FEG)", src/aica_model.cpp feg_clock), envelope
// clock on even MDEC_CT, eg_cnt = K - MDEC_CT/2, R < 48 rows one step behind.
//   s3alt dump [stream...]      window koff-40..koff+40 of every stream under S3 (the eg_model key-off sample)
//   s3alt fit  [-w]             every mechanism x stream: key-off samples that reproduce the whole stream, and the
//                               per-batch intersection (one KYONEX keys off all three slots of a batch)
// Build: g++ -O2 -std=c++17 -o build/work/s3alt work/verify/s5/s3alt.cpp   (run from caique-rtl/model)
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <set>
#include <map>
#include <algorithm>
#include "../../../tools/filt_capture.h"

#include "feg_law.h"

struct Stream { std::string name, cappath, ufile; uint32_t c0; int batch, order; Prog p; };
static std::vector<Stream> streams() {
    std::vector<Stream> v;
    const int flvA[5] = {0x1800, 0x1C00, 0x1800, 0x1A00, 0x1C00};
    struct P { int far, fd1r, fd2r, frr, krs; };
    const P b0[3] = {{16, 20, 24, 22, 5}, {16, 20, 24, 22, 0}, {16, 20, 24, 22, 15}};
    const P b1[3] = {{24, 26, 28, 24, 15}, {22, 24, 26, 22, 2}, {19, 21, 23, 19, 5}};
    const P b3[3] = {{22, 24, 26, 22, 2}, {19, 21, 23, 19, 5}, {24, 26, 28, 24, 15}};
    const uint32_t c0fk[4] = {0x0c7d, 0xcb72, 0x9340, 0x5acf};
    for (int b = 0; b < 4; b++)
        for (int k = 0; k < 3; k++) {
            const P &q = b == 0 ? b0[k] : b == 3 ? b3[k] : b1[k];
            Stream s; s.name = "fk_" + std::to_string(b) + "_s" + std::to_string(k); s.cappath = "tests/feg_krs/hw/fk_" + std::to_string(b);
            s.ufile = "work/eg/fk_" + std::to_string(b) + "_" + std::to_string(k) + ".u"; s.c0 = c0fk[b]; s.batch = b; s.order = k;
            memcpy(s.p.flv, flvA, sizeof flvA); s.p.rate[0] = q.far; s.p.rate[1] = q.fd1r; s.p.rate[2] = q.fd2r; s.p.rate[3] = q.frr;
            s.p.krs = q.krs; s.p.oct = b == 0 ? 3 : 0; s.p.fns = b == 0 ? 0x200 : 0;
            v.push_back(s);
        }
    struct T { int flv[5], rate[4], krs; };
    const T bt[3][3] = {
        {{{0x1800, 0x1C05, 0x1A03, 0x1B00, 0x1900}, {28, 26, 30, 27}, 15}, {{0x1FF0, 0x1802, 0x1F01, 0x1E00, 0x1FFD}, {30, 29, 24, 30}, 15}, {{0x1C00, 0x1C80, 0x1C00, 0x1C40, 0x1BF0}, {22, 23, 21, 25}, 15}},
        {{{0x1800, 0x1C00, 0x1800, 0x1A00, 0x1C00}, {16, 20, 24, 22}, 15}, {{0x1800, 0x1C00, 0x1800, 0x1A00, 0x1C00}, {16, 20, 24, 22}, 0}, {{0x1800, 0x1C00, 0x1800, 0x1A00, 0x1C00}, {16, 20, 24, 22}, 5}},
        {{{0x1800, 0x1FF0, 0x1800, 0x1800, 0x1A00}, {24, 26, 0, 28}, 15}, {{0x1800, 0x1C00, 0x1900, 0x1A00, 0x1C00}, {18, 20, 22, 24}, 2}, {{0x1C00, 0x1D00, 0x1D00, 0x1E00, 0x1B00}, {26, 26, 0, 26}, 15}}};
    const int oct[3] = {0, 3, 13}, fns[3] = {0, 0x200, 0x155};
    const uint32_t c0ft[3] = {0xe4be, 0xa802, 0x66ac};
    for (int b = 0; b < 3; b++)
        for (int k = 0; k < 3; k++) {
            Stream s; s.name = "ft_" + std::to_string(b) + "_s" + std::to_string(k); s.cappath = "tests/feg_track/hw/ft_" + std::to_string(b);
            s.ufile = "work/feg/ft_" + std::to_string(b) + "_" + std::to_string(k) + ".u"; s.c0 = c0ft[b]; s.batch = 4 + b; s.order = k;
            memcpy(s.p.flv, bt[b][k].flv, sizeof s.p.flv); memcpy(s.p.rate, bt[b][k].rate, sizeof s.p.rate);
            s.p.krs = bt[b][k].krs; s.p.oct = oct[b]; s.p.fns = fns[b];
            v.push_back(s);
        }
    const int far[3] = {24, 25, 24}, fd1[3] = {26, 27, 26}, fd2[3] = {28, 29, 28}, frr[3] = {22, 23, 22};
    for (int k = 0; k < 3; k++) {
        Stream s; s.name = "feg_odd_s" + std::to_string(k); s.cappath = "tests/eg_lock/hw/feg_odd"; s.ufile = "work/eg/feg_odd_" + std::to_string(k) + ".u";
        s.c0 = 0x0374; s.batch = 7; s.order = k;
        memcpy(s.p.flv, flvA, sizeof flvA); s.p.rate[0] = far[k]; s.p.rate[1] = fd1[k]; s.p.rate[2] = fd2[k]; s.p.rate[3] = frr[k];
        s.p.krs = 0; s.p.oct = 0; s.p.fns = 0x200;
        v.push_back(s);
    }
    return v;
}
static const char *batch_name[8] = {"fk_0", "fk_1", "fk_2", "fk_3", "ft_0", "ft_1", "ft_2", "feg_odd"};
struct Trace { int n, i; uint32_t md, cnt; int u, v, state, inc, rs; bool clock; char note[24]; };

struct Data { Capture cp; std::vector<int32_t> u; int on, lo, hi; };
static Data load(const Stream &s) {
    Data d; d.cp = cap(s.cappath);
    FILE *hf = fopen((s.cappath + ".hdr").c_str(), "rb"); uint32_t h[300] = {0}; size_t nh = fread(h, 4, 300, hf); fclose(hf);
    int m2 = -1, m3 = -1, m4 = -1;
    for (size_t i = 11; i + 1 < nh && i < 11 + 2 * h[6]; i += 2) { if (h[i] == 2 && m2 < 0) m2 = (int)(h[i + 1] - d.cp.first); if (h[i] == 3 && m3 < 0) m3 = (int)(h[i + 1] - d.cp.first); if (h[i] == 4 && m4 < 0) m4 = (int)(h[i + 1] - d.cp.first); }
    if (m3 < 0 && m2 >= 0) { m3 = m2 - 192; m4 = m2; }
    d.lo = m3 - 64; d.hi = m4 + 400;
    d.on = 1; while (d.on < (int)d.cp.n && !d.cp.v[d.on * d.cp.ns + 3]) d.on++;
    FILE *f = fopen(s.ufile.c_str(), "rb"); int32_t x; while (f && fread(&x, 4, 1, f) == 1) d.u.push_back(x); if (f) fclose(f);
    if (d.hi > d.on + (int)d.u.size() - 1) d.hi = d.on + (int)d.u.size() - 1;
    return d;
}
// simulate; K = key-off sample (state change), T = target switch sample (M_KYONB only, else T = K); Kc = clock constant
// returns matched samples from the onset (== u.size() for FULL), first mismatch info in *bad
static int sim(const Stream &s, const Data &d, Mech m, int K, int T, uint32_t Kc, int *bad_n, int *bad_v, std::vector<Trace> *tr, int tr_lo, int tr_hi) {
    Feg f{}; feg_key_on(f, s.p);
    int end = d.on + (int)d.u.size();
    for (int i = d.on; i < end; i++) {
        uint32_t md = (s.c0 - d.cp.first - (uint32_t)i) & 0xFFFF;
        bool clock = (md & 1) == 0;
        bool koff_now = false;
        if (m == M_KYONB && i == T && i < K) feg_kyonb_clear(f, s.p);
        if (i == K) { feg_key_off(f, s.p); koff_now = true; }
        int inc = -1, rs = -1;
        uint32_t cnt = (Kc - (md >> 1)) & 0x3FFF;
        if (clock && i > d.on) feg_clock_step(f, s.p, cnt, m, koff_now, inc, rs);
        int n = i - d.on;
        if (tr && i >= tr_lo && i <= tr_hi) { Trace t; t.n = n; t.i = i; t.md = md; t.cnt = cnt; t.u = d.u[n]; t.v = f.v; t.state = f.state; t.inc = inc; t.rs = rs; t.clock = clock; t.note[0] = 0; if (i == K) strcpy(t.note, "KEY-OFF"); if (m == M_KYONB && i == T && T != K) strcpy(t.note, "KYONB-clear"); tr->push_back(t); }
        if (d.u[n] >= 0 && d.u[n] != (f.v >> 1)) { if (bad_n) { *bad_n = n; *bad_v = f.v; } return n; }
    }
    return (int)d.u.size();
}

int main(int argc, char **argv) {
    std::string mode = argc > 1 ? argv[1] : "fit";
    std::vector<std::string> want; bool wide = false;
    for (int i = 2; i < argc; i++) { if (!strcmp(argv[i], "-w")) wide = true; else want.push_back(argv[i]); }
    auto S = streams();
    // reference key-off samples under S3 from work/verify/expected/eg_model_all.txt (the shared one per batch)
    std::map<std::string, int> ref_koff = {{"fk_0", 6771}, {"fk_1", 4454}, {"fk_2", 4565}, {"fk_3", 4564}, {"ft_0", 5469}, {"ft_1", 6660}, {"ft_2", 803}, {"feg_odd", 6772}};
    if (mode == "dump") {
        for (auto &s : S) {
            if (!want.empty() && std::find(want.begin(), want.end(), s.name) == want.end()) continue;
            Data d = load(s);
            int K = ref_koff[batch_name[s.batch]];
            std::vector<Trace> tr; int bn = -1, bv = 0;
            int got = sim(s, d, M_S3, K, K, 6491, &bn, &bv, &tr, K - 40, K + 40);
            uint32_t R[4]; for (int j = 0; j < 4; j++) R[j] = eff_rate(s.p, s.p.rate[j]);
            printf("== %s  slot program FLV %04x %04x %04x %04x %04x  FAR %d FD1R %d FD2R %d FRR %d KRS %d OCT %d FNS %03x -> R %u/%u/%u/%u; onset %d, S3 key-off sample %d (MDEC_CT %04x %s): %s %d/%zu\n",
                   s.name.c_str(), s.p.flv[0], s.p.flv[1], s.p.flv[2], s.p.flv[3], s.p.flv[4], s.p.rate[0], s.p.rate[1], s.p.rate[2], s.p.rate[3], s.p.krs, s.p.oct, s.p.fns,
                   R[0], R[1], R[2], R[3], d.on, K, (s.c0 - d.cp.first - K) & 0xFFFF, ((s.c0 - d.cp.first - K) & 1) ? "odd" : "EVEN", got == (int)d.u.size() ? "FULL" : "fail", got, d.u.size());
            printf("   n(+onset)  i     MDEC_CT par  cnt   hw_u  model_u st  rate-seg inc  note\n");
            static const char *stn[4] = {"att", "d1 ", "d2 ", "rel"};
            for (auto &t : tr) {
                char hw[16]; if (t.u < 0) strcpy(hw, "  ?"); else snprintf(hw, sizeof hw, "%03x", t.u);
                if (t.clock) printf("   %6d %6d  %04x  EVEN %5u  %s  %03x     %s %s      %d   %s%s\n", t.n, t.i, t.md, t.cnt, hw, t.v >> 1, stn[t.state], t.rs >= 0 ? stn[t.rs] : "-  ", t.inc, t.note, t.u >= 0 && t.u != (t.v >> 1) ? " MISMATCH" : "");
                else printf("   %6d %6d  %04x  odd  %5s  %s  %03x     %s                %s%s\n", t.n, t.i, t.md, "", hw, t.v >> 1, stn[t.state], t.note, t.u >= 0 && t.u != (t.v >> 1) ? " MISMATCH" : "");
            }
        }
        return 0;
    }
    // fit: for each mechanism and stream, the set of key-off samples (and T for KYONB) reproducing the whole stream
    std::map<int, std::map<int, std::set<int>>> batch_sets;              // [mech][batch] -> intersection of K sets
    std::map<int, std::map<int, std::vector<std::set<std::pair<int,int>>>>> kyonb_sets; // [batch] -> per order set of (T,K)
    std::map<int, std::map<int, bool>> batch_all_full;
    for (int mi = 0; mi < M_COUNT; mi++) {
        Mech m = (Mech)mi;
        printf("---- mechanism %s\n", mech_name[mi]);
        for (auto &s : S) {
            if (!want.empty() && std::find(want.begin(), want.end(), s.name) == want.end()) continue;
            Data d = load(s);
            // key-on phase (no key-off) with Kc 6490..6492: which Kc reproduce the samples before the window?
            int bestKc = -1, bestpre = -1; int pre_bad_n = -1, pre_bad_v = 0;
            for (uint32_t Kc : {6491u, 6490u, 6492u}) {
                int bn = -1, bv = 0;
                int got = sim(s, d, m, 1 << 30, 1 << 30, Kc, &bn, &bv, nullptr, 0, -1);
                int pre = std::min(got, d.lo - d.on);
                if (pre > bestpre) { bestpre = pre; bestKc = (int)Kc; pre_bad_n = bn; pre_bad_v = bv; }
                if (pre >= d.lo - d.on) break;
            }
            std::set<int> ks; std::set<std::pair<int,int>> tks; int best = bestpre, bestK = -1, bad_n = -1, bad_v = 0;
            if (bestpre >= d.lo - d.on) {
                for (int K = d.lo; K <= d.hi; K++) {
                    if ((m == M_V_NOS3 || m == M_V_NOSTEP) && (((s.c0 - d.cp.first - K) & 1) != 0)) continue;
                    int Tlo = m == M_KYONB ? K - 2 : K, Thi = K;
                    for (int T = Tlo; T <= Thi; T++) {
                        int bn = -1, bv = 0;
                        int got = sim(s, d, m, K, T, (uint32_t)bestKc, &bn, &bv, nullptr, 0, -1);
                        if (got == (int)d.u.size()) { ks.insert(m == M_KYONB ? T : K); tks.insert({T, K}); }
                        if (got > best) { best = got; bestK = K; bad_n = bn; bad_v = bv; }
                    }
                }
            } else { bad_n = pre_bad_n; bad_v = pre_bad_v; }
            printf("  %-10s Kc %d: ", s.name.c_str(), bestKc);
            if (bestpre < d.lo - d.on) printf("FAILS BEFORE the key-off window at +%d (hw u %03x, model u %03x)", bad_n, d.u[bad_n], bad_v >> 1);
            else if (ks.empty()) printf("no key-off sample fits (window %d..%d); best %d/%zu at K %d, first mismatch +%d (hw u %03x, model u %03x)", d.lo, d.hi, best, d.u.size(), bestK, bad_n, bad_n >= 0 ? d.u[bad_n] : -1, bad_v >> 1);
            else {
                printf("FULL for %zu %s:", ks.size(), m == M_KYONB ? "(T,K) pairs, T" : "key-off samples");
                int shown = 0;
                if (m == M_KYONB) { for (auto &tk : tks) { if (shown++ >= 8) { printf(" ..."); break; } printf(" (%d,%d)", tk.first, tk.second); } }
                else for (int k : ks) { if (shown++ >= 8) { printf(" ..."); break; } printf(" %d(%s)", k, ((s.c0 - d.cp.first - k) & 1) ? "odd" : "even"); }
            }
            printf("\n");
            if (m == M_KYONB) { auto &v = kyonb_sets[s.batch][0]; if ((int)v.size() < 3) v.resize(3); v[s.order] = tks; }
            auto &bs = batch_sets[mi];
            if (!bs.count(s.batch)) { bs[s.batch] = ks; batch_all_full[mi][s.batch] = !ks.empty(); }
            else { std::set<int> in; std::set_intersection(bs[s.batch].begin(), bs[s.batch].end(), ks.begin(), ks.end(), std::inserter(in, in.begin())); bs[s.batch] = in; batch_all_full[mi][s.batch] = batch_all_full[mi][s.batch] && !ks.empty(); }
        }
    }
    printf("\n==== per batch: key-off samples shared by the three slots of the batch (one KYONEX write keys them all off)\n");
    printf("%-14s", "mechanism"); for (int b = 0; b < 8; b++) printf(" %-14s", batch_name[b]); printf("  verdict\n");
    for (int mi = 0; mi < M_COUNT; mi++) {
        if (mi == M_KYONB) continue;
        printf("%-14s", mech_name[mi]);
        int nb = 0, nfit = 0;
        for (int b = 0; b < 8; b++) {
            if (!batch_sets[mi].count(b)) { printf(" %-14s", "-"); continue; }
            nb++;
            auto &ks = batch_sets[mi][b];
            char buf[64] = "";
            if (ks.empty()) strcpy(buf, batch_all_full[mi][b] ? "no shared K" : "FAIL");
            else { nfit++; std::string t; for (int k : ks) t += (t.empty() ? "" : ",") + std::to_string(k); snprintf(buf, sizeof buf, "%s", t.c_str()); buf[14] = 0; }
            printf(" %-14s", buf);
        }
        printf("  %s\n", nfit == nb ? "fits all batches" : "REFUTED");
    }
    // KYONB: per batch, tuples (T0,T1,T2,K) with T0 <= T1 <= T2 <= K <= T0+2 (KYONB cleared in slot order, KYONEX last)
    printf("%-14s", mech_name[M_KYONB]);
    for (int b = 0; b < 8; b++) {
        if (!kyonb_sets.count(b)) { printf(" %-14s", "-"); continue; }
        auto &v = kyonb_sets[b][0];
        std::set<int> Ks; int ntup = 0; std::string ex;
        for (auto &a : v[0]) for (auto &c : v[1]) for (auto &e : v[2]) {
            int K = a.second; if (c.second != K || e.second != K) continue;
            if (!(a.first <= c.first && c.first <= e.first && e.first <= K && K <= a.first + 2)) continue;
            ntup++; Ks.insert(K); if (ex.empty()) ex = "(" + std::to_string(a.first) + "," + std::to_string(c.first) + "," + std::to_string(e.first) + ";" + std::to_string(K) + ")";
        }
        char buf[64]; snprintf(buf, sizeof buf, "%d tup e.g.%s", ntup, ex.c_str()); buf[14] = 0;
        printf(" %-14s", buf);
        if (wide) printf("\n    %s: %d (T0,T1,T2;K) tuples, first %s, K in {", batch_name[b], ntup, ex.c_str()), ({ for (int k : Ks) printf(" %d", k); printf(" }\n"); });
    }
    printf("  (see the wide listing)\n");
    return 0;
}
