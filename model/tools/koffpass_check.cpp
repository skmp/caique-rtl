// koffpass_check.cpp -- analyse the captures of cases/feg_koffpass.c: the FEG key-off clock when the OLD segment has
// already PASSED its target (an attack / decay 1 that crossed on clock N, keyed off on clock N+1, before the next
// segment stepped).  Per run (kp_a, kp_b): 64 key-on / key-off cycles of three FEG slots (full-scale random input,
// Q 4, VOFF 1, LPOFF 0, the FEG recovered through the bit-exact filter as u = v >> 1) plus the AEG witness on stream 3,
// keyed ON by the KYONEX that keys the FEG slots OFF: its 520176 pins the key-off sample E of every cycle.
//   per cycle: E from the witness; the key-on sample from the tracker (the filter state is carried through the previous
//   cycle's release -- the filter is never cleared -- and the key-on hypothesis "CA restarts at sig[0], the FEG reloads
//   FLV0" is tested on every candidate in [E-64, E-6] until one tracks 100 samples on all three streams); the FEG law
//   (tools/feg_law.h tables: clock on even MDEC_CT, eg_cnt = K - MDEC_CT/2, no step on the key-on sample) simulated from
//   the key-on to E classifies the cycle: N+1 (passed set at E), N (E is the crossing clock), pre (inside the segment before
//   its crossing), post (keyed off from the following segment), odd (E not a clock; "odd*" when the passed flag was pending).
//   Readings of the N+1 clock (all coincide elsewhere: the established "one more step of the old segment" rule, tests/feg_koffdir
//   / feg_koffatt, hold check vs FLV4):
//     A oldStep  one more step of the segment that just passed (its increment, its direction) -- the model's rule (feg_prev / feg_prev_dir)
//     B nextSeg  the step a normal clock N+1 takes: the next segment's rate and direction toward its own target, then the release
//     C noStep   nothing on E, the release steps from the next clock (the AEG attack's rule)
//     D relStep  the release increment toward FLV4 already on E
//   Each reading is simulated from the key-on to the next key-on and compared with the tracked u wherever it is unambiguous.
//   koffpass_check <dir> [-K kc] [-q]   dir holds feg_koffpass.txt, kp_a/kp_b.hdr/.bin, input.bin (console: tests/feg_koffpass/hw,
//                                       model: tests/feg_koffpass/model); kc = the boot's clock constant (default 6491); -q prints
//                                       only the informative (N+1) cycles and the summary
// Build: make -C tools koffpass_check (single file, no model link).  Run from caique-rtl/model.
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <map>
#include <tuple>
#include <algorithm>
#include "filt_capture.h"
#include "feg_law.h"
using I = int64_t;
static const int NSIG = 8192;
static const int WITNESS_A0 = 520176;   /* the witness level at a = 0 (constant 0x7FFF, TL 0) */
static const int qm[32] = {192, 176, 160, 144, 128, 120, 112, 104, 96, 88, 80, 72, 64, 60, 56, 52, 48, 44, 40, 36, 32, 30, 28, 26, 24, 22, 20, 18, 16, 15, 14, 13};
static I ceilshr(I n, int s) { return -((-n) >> s); }
struct St { I L, B; int u; bool operator<(const St &o) const { return std::tie(L, B, u) < std::tie(o.L, o.B, o.u); } bool operator==(const St &o) const { return L == o.L && B == o.B && u == o.u; } };
static void fstep(I &L, I &B, I x, int u, int Q) { int v = u << 1, k = v >= 0x1ffe ? 512 : 256 + (u & 255), s = 24 - (v >> 9); I d = 2 * ceilshr(qm[Q] * B, 8); B += (k * (x - L - d)) >> s; L += ceilshr(k * B, s); }
static inline int32_t outv(I L) { return (int32_t)std::clamp<I>(-2 * L, -524288, 524287); }
static const size_t SETMAX = 400000;

// ---- tracker: one FEG stream through the whole capture -----------------------------------------------------------
// the filter input is the known signal, x(n) = 8 * sig[(n - on) mod NSIG] (pitch 1.0, CA restarts at the key-on); the
// FEG value u moves at most 4 per sample; the state set carries every (L, B, u) consistent with the outputs so far
struct Tracker {
    const Capture *c; int k, Q; const std::vector<int16_t> *sig;
    std::vector<St> S;         /* states after sample `pos` */
    int pos = -1;              /* last sample absorbed */
    int on = 0;                /* key-on sample of the current cycle (x phase) */
    std::vector<int32_t> u;    /* tracked u per sample (-1 unknown / untracked) */
    size_t maxset = 0;
    Tracker(const Capture *cc, int kk, int q, const std::vector<int16_t> *s) : c(cc), k(kk), Q(q), sig(s), u(cc->n, -1) {}
    I x_at(int n, int onset) const { return 8 * I((*sig)[((n - onset) % NSIG + NSIG) % NSIG]); }
    /* one sample under the continuation rule (u +- 4, x from `onset`); returns the surviving set */
    std::vector<St> step_set(const std::vector<St> &S0, int n, int onset, int force_u = -1) const {
        std::vector<St> T; I x = x_at(n, onset); int32_t want = c->v[(size_t)n * c->ns + k];
        for (auto &s : S0) {
            int lo = force_u >= 0 ? force_u : s.u - 4, hi = force_u >= 0 ? force_u : s.u + 4;
            for (int nu = lo; nu <= hi; nu++) {
                if (nu < 0 || nu > 4095) continue;
                I L = s.L, B = s.B; fstep(L, B, x, nu, Q);
                if (outv(L) == want) T.push_back({L, B, nu});
            }
        }
        std::sort(T.begin(), T.end()); T.erase(std::unique(T.begin(), T.end()), T.end());
        if (T.size() > SETMAX) T.resize(SETMAX);
        return T;
    }
    static int common_u(const std::vector<St> &T) { if (T.empty()) return -1; for (auto &s : T) if (s.u != T[0].u) return -1; return T[0].u; }
    /* absorb samples pos+1 .. n (continuation); stops (S empty) when the stream is lost */
    void advance_to(int n) {
        while (pos < n && pos + 1 < (int)c->n) {
            if (S.empty()) { pos++; continue; }
            S = step_set(S, pos + 1, on); pos++;
            maxset = std::max(maxset, S.size());
            u[pos] = common_u(S);
        }
    }
    /* the rest prior before the first key-on: L from the last output, B in [-256, 256] (a silent VOFF 1 slot rests on its bus) */
    void rest_prior(int i) { S.clear(); for (I b0 = -256; b0 <= 256; b0++) S.push_back({-I(c->v[(size_t)(i - 1) * c->ns + k]) / 2, b0, 0}); pos = i - 1; }
    /* key-on hypothesis at sample i from the state set S (which must be at pos = i - 1): hyp 0: x restarts, u = flv0 >> 1 on i;
     * hyp 1: x restarts on i, u continues on i and is flv0 >> 1 from i + 1; hyp 2: x restarts, u continues (no FLV0 reload).
     * Returns the surviving set after `ncheck` samples (empty = refuted). */
    std::vector<St> try_keyon(int i, int flv0, int hyp, int ncheck) const {
        if (pos != i - 1 || S.empty()) return {};
        std::vector<St> T = step_set(S, i, i, hyp == 0 ? (flv0 >> 1) : -1);
        for (int n = i + 1; n < i + ncheck && n < (int)c->n && !T.empty(); n++) T = step_set(T, n, i, (hyp == 1 && n == i + 1) ? (flv0 >> 1) : -1);
        return T;
    }
    /* commit the key-on at i under hyp: re-run from S (at i - 1) recording u from i to i + ncheck - 1 */
    void commit_keyon(int i, int flv0, int hyp, int ncheck) {
        std::vector<St> T = step_set(S, i, i, hyp == 0 ? (flv0 >> 1) : -1);
        on = i; pos = i; u[i] = common_u(T); S = T;
        for (int n = i + 1; n < i + ncheck && n < (int)c->n && !S.empty(); n++) { S = step_set(S, n, i, (hyp == 1 && n == i + 1) ? (flv0 >> 1) : -1); pos = n; u[n] = common_u(S); maxset = std::max(maxset, S.size()); }
    }
};

// ---- the FEG law with the four readings of the N+1 key-off clock --------------------------------------------------
struct FS { int state, v, dir; bool passed; };
enum Rd { RD_A = 0, RD_B, RD_C, RD_D, NRD };
static const char *rd_name[NRD] = {"A oldStep", "B nextSeg", "C noStep", "D relStep"};
static inline int inc_of(const Prog &p, int st, uint32_t cnt) { return (int)eg_increment(eff_rate(p, p.rate[st]), cnt); }
static inline void fs_step(FS &f, int inc, int dir, int target, bool hold) {
    if (!inc) return;
    bool C = f.v >= target;
    int nv = f.v + dir * inc; nv = nv < 0 ? 0 : nv > 0x1FFF ? 0x1FFF : nv;
    if (hold) { if ((nv >= target) == C) f.v = nv; }
    else { f.v = nv; if ((nv >= target) != C) f.passed = true; }
}
static inline void fs_key_on(FS &f, const Prog &p) { f.state = 0; f.v = p.flv[0]; f.dir = f.v >= p.flv[1] ? -1 : 1; f.passed = false; }
/* a normal clock: a passed attack / decay 1 advances first (direction toward the new target), then the segment steps */
static inline void fs_clock(FS &f, const Prog &p, uint32_t cnt) {
    if (f.passed && f.state < 2) { f.state++; f.dir = f.v >= p.flv[f.state + 1] ? -1 : 1; f.passed = false; }
    fs_step(f, inc_of(p, f.state, cnt), f.dir, p.flv[f.state + 1], f.state >= 2);
}
/* the key-off sample: state -> release on the sample; when it is a clock, the established rule is one more step of the
 * old segment (increment and direction) with the hold check against FLV4; with the old segment's passed flag set the four
 * readings apply */
static inline void fs_key_off(FS &f, const Prog &p, bool clock, uint32_t cnt, Rd r) {
    FS o = f;
    f.state = 3; f.dir = f.v >= p.flv[4] ? -1 : 1; f.passed = false;
    if (!clock) return;
    if (!(o.passed && o.state < 2)) { fs_step(f, inc_of(p, o.state, cnt), o.dir, p.flv[4], true); return; }
    switch (r) {
    case RD_A: fs_step(f, inc_of(p, o.state, cnt), o.dir, p.flv[4], true); break;
    case RD_B: { FS t = o; fs_clock(t, p, cnt); f.v = t.v; break; }
    case RD_C: break;
    case RD_D: fs_step(f, inc_of(p, 3, cnt), f.dir, p.flv[4], true); break;
    default: break;
    }
}
struct Clock { uint32_t c0, first, Kc; bool is_clock(int i) const { return ((c0 - first - (uint32_t)i) & 1) == 0; } uint32_t cnt(int i) const { return (Kc - (((c0 - first - (uint32_t)i) & 0xFFFF) >> 1)) & 0x3FFF; } uint32_t md(int i) const { return (c0 - first - (uint32_t)i) & 0xFFFF; } };
/* simulate stream k from the key-on `on` to `end` (exclusive) with the key-off on E under reading r; compares with the tracked
 * u where known; returns the first mismatching sample (or end), fills pred[0..14] with u at E-4..E+10 */
static int simulate(const Prog &p, const Clock &ck, int on, int E, int end, Rd r, const std::vector<int32_t> &u, int *pred, int *first_bad_pred) {
    FS f; fs_key_on(f, p);
    int bad = end;
    for (int i = on; i < end; i++) {
        if (i > on) {
            if (i == E) fs_key_off(f, p, ck.is_clock(i), ck.cnt(i), r);
            else if (ck.is_clock(i)) fs_clock(f, p, ck.cnt(i));
        }
        if (pred && i >= E - 4 && i <= E + 10) pred[i - (E - 4)] = f.v >> 1;
        if (bad == end && u[i] >= 0 && u[i] != (f.v >> 1)) { bad = i; if (first_bad_pred) *first_bad_pred = f.v >> 1; }
    }
    return bad;
}
/* the FEG state just before sample E under the established law (reading A everywhere before E) */
static FS state_before(const Prog &p, const Clock &ck, int on, int E) {
    FS f; fs_key_on(f, p);
    for (int i = on + 1; i < E; i++) if (ck.is_clock(i)) fs_clock(f, p, ck.cnt(i));
    return f;
}
enum Phase { PH_N1 = 0, PH_N, PH_PRE, PH_POST, PH_ODD_PEND, PH_ODD, NPH };
static const char *ph_name[NPH] = {"N+1", "N", "pre", "post", "odd*", "odd"};
/* the tested crossing: the last clock (within the first 64 after the key-on) on which the normal law sets `passed`
 * (slot 0 / 1: the attack; slot 2: decay 1, its 1-clock attack passes on clock 1) */
static int crossing_clock(const Prog &p, const Clock &ck, int on, int nmax) {
    FS f; fs_key_on(f, p); int nclk = 0, sN = -1;
    for (int i = on + 1; i < nmax && nclk < 64; i++) if (ck.is_clock(i)) { fs_clock(f, p, ck.cnt(i)); nclk++; if (f.passed) sN = i; }
    return sN;
}
static Phase classify(const Prog &p, const Clock &ck, int on, int E, FS &o, int &sN) {
    o = state_before(p, ck, on, E);
    sN = crossing_clock(p, ck, on, E + 200);
    if (!ck.is_clock(E)) return (o.passed && o.state < 2) ? PH_ODD_PEND : PH_ODD;
    if (o.passed && o.state < 2) return PH_N1;
    if (E == sN) return PH_N;
    return sN < 0 || E < sN ? PH_PRE : PH_POST;
}
static const char *seg_name[4] = {"attack", "decay1", "decay2", "release"};

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: koffpass_check <dir> [-K kc] [-q]\n"); return 2; }
    std::string dir = argv[1]; uint32_t Kc = 6491; bool quiet = false;
    for (int i = 2; i < argc; i++) { if (!strcmp(argv[i], "-K") && i + 1 < argc) Kc = (uint32_t)atoi(argv[++i]); else if (!strcmp(argv[i], "-q")) quiet = true; }
    /* runs, programs, c0 from the text output */
    struct Run { std::string name; Prog prog[3]; int Q = 4; uint32_t c0 = 0; bool have_c0 = false; int ncross = 0; };
    std::vector<Run> runs;
    { FILE *f = fopen((dir + "/feg_koffpass.txt").c_str(), "r"); if (!f) { perror("feg_koffpass.txt"); return 2; } char line[512];
      while (fgets(line, sizeof line, f)) {
          char nm[64]; int k, slot, krs, flv[5], rt[4], Q, lpoff, voff;
          if (sscanf(line, "%63s stream %d: slot %d role feg KRS %d FLV %x %x %x %x %x FAR %d FD1R %d FD2R %d FRR %d Q %d lpoff %d voff %d",
                     nm, &k, &slot, &krs, &flv[0], &flv[1], &flv[2], &flv[3], &flv[4], &rt[0], &rt[1], &rt[2], &rt[3], &Q, &lpoff, &voff) == 16 && k >= 0 && k < 3) {
              if (runs.empty() || runs.back().name != nm) { runs.push_back(Run()); runs.back().name = nm; }
              Prog &P = runs.back().prog[k]; for (int j = 0; j < 5; j++) P.flv[j] = flv[j]; for (int j = 0; j < 4; j++) P.rate[j] = rt[j]; P.krs = krs; P.oct = 0; P.fns = 0; runs.back().Q = Q;
              continue;
          }
          int nc; if (sscanf(line, "%63s crossing clock N %d", nm, &nc) == 2 && !runs.empty() && runs.back().name + ":" == nm) { runs.back().ncross = nc; continue; }
          const char *p = strstr(line, ", c0 ");
          if (strstr(line, "cap_start") && p && !runs.empty() && !runs.back().have_c0) { unsigned c0; if (sscanf(p, ", c0 %x", &c0) == 1) { runs.back().c0 = c0; runs.back().have_c0 = true; } }
      }
      fclose(f); }
    if (runs.empty()) { fprintf(stderr, "no runs found in %s/feg_koffpass.txt\n", dir.c_str()); return 2; }
    /* the input signal: regenerated from the case's LCG, checked against input.bin when present */
    std::vector<int16_t> sig(NSIG);
    { uint32_t seed = 4242; for (int i = 0; i < NSIG; i++) { seed = seed * 1103515245u + 12345u; sig[i] = (int16_t)(seed >> 16); if (!sig[i]) sig[i] = 1; }
      try { std::vector<int16_t> in = input(dir + "/input.bin"); if (in != sig) { fprintf(stderr, "input.bin differs from the regenerated signal\n"); return 2; } }
      catch (std::exception &) { printf("(no input.bin in %s: using the regenerated signal)\n", dir.c_str()); } }
    printf("koffpass_check %s: %zu runs, clock constant K %u\n", dir.c_str(), runs.size(), Kc);
    printf("readings of the key-off clock E when the old segment had PASSED its target on the previous clock (v = the value at E-1):\n"
           "  A oldStep = one more step of the passed segment (its increment, its direction; the model)   B nextSeg = the next segment's step (what clock N+1 does)\n"
           "  C noStep = hold on E, release from the next clock   D relStep = the release increment on E.   Elsewhere every reading is the established old-step rule.\n");
    /* totals: [run][phase][reading] FULL counts per stream and all-3 */
    int tot_cyc[NPH] = {0}, tot_full3[NPH][NRD] = {{0}}, tot_full[3][NPH][NRD] = {{{0}}}, tot_inf[3][NPH] = {{0}}, tot_none[NPH] = {0}, tot_multi[NPH] = {0};
    std::vector<std::string> summary;
    for (auto &R : runs) {
        std::string path = dir + "/" + R.name;
        Capture c; try { c = cap(path); } catch (std::exception &e) { printf("== %s: %s\n", R.name.c_str(), e.what()); continue; }
        if (!R.have_c0) { printf("== %s: no cap_start c0 line\n", R.name.c_str()); continue; }
        Clock ck{R.c0, c.first, Kc};
        FILE *hf = fopen((path + ".hdr").c_str(), "rb"); uint32_t h[300] = {0}; size_t nh = fread(h, 4, 300, hf); fclose(hf);
        std::vector<int> marks; for (size_t i = 11; i + 1 < nh && i < 11 + 2 * h[6]; i += 2) marks.push_back((int)(h[i + 1] - c.first));
        /* key-off samples: the witness onsets */
        std::vector<int> Es;
        for (unsigned i = 1; i < c.n; i++) if (c.v[i * c.ns + 3] == WITNESS_A0 && c.v[(i - 1) * c.ns + 3] != WITNESS_A0) Es.push_back((int)i);
        printf("\n==== %s: %u samples, first %u, c0 %04x, %zu marks, %zu witness onsets (key-off samples); programs:\n", R.name.c_str(), c.n, c.first, R.c0, marks.size(), Es.size());
        for (int k = 0; k < 3; k++) {
            const Prog &p = R.prog[k];
            printf("     s%d FLV %04x %04x %04x %04x %04x rates FAR %d FD1R %d FD2R %d FRR %d = R %u %u %u %u (inc %u %u %u %u at cnt 0)\n", k, p.flv[0], p.flv[1], p.flv[2], p.flv[3], p.flv[4],
                   p.rate[0], p.rate[1], p.rate[2], p.rate[3], eff_rate(p, p.rate[0]), eff_rate(p, p.rate[1]), eff_rate(p, p.rate[2]), eff_rate(p, p.rate[3]),
                   eg_increment(eff_rate(p, p.rate[0]), 0), eg_increment(eff_rate(p, p.rate[1]), 0), eg_increment(eff_rate(p, p.rate[2]), 0), eg_increment(eff_rate(p, p.rate[3]), 0));
        }
        Tracker tr[3] = {Tracker(&c, 0, R.Q, &sig), Tracker(&c, 1, R.Q, &sig), Tracker(&c, 2, R.Q, &sig)};
        std::vector<int> ons(Es.size(), -1), hyps(Es.size(), -1);
        int n_cyc[NPH] = {0}, full3[NPH][NRD] = {{0}}, full[3][NPH][NRD] = {{{0}}}, inf[3][NPH] = {{0}};
        std::map<int, int> d_hist;
        int lost[3] = {0, 0, 0};
        for (size_t j = 0; j < Es.size(); j++) {
            int E = Es[j];
            int lo = std::max(1, E - 64), hi = E - 6;
            if (j > 0) lo = std::max(lo, Es[j - 1] + 300);
            /* the key-on sample: the first candidate whose key-on hypothesis tracks 100 samples on all three streams */
            int on = -1, hyp = -1;
            for (int i = lo; i <= hi && on < 0; i++) {
                bool ready = true;
                for (int k = 0; k < 3; k++) { if (j == 0) tr[k].rest_prior(i); else tr[k].advance_to(i - 1); ready &= !tr[k].S.empty(); }
                if (!ready) continue;
                for (int hy = 0; hy < 3 && on < 0; hy++) {
                    bool ok = true;
                    for (int k = 0; k < 3 && ok; k++) ok = !tr[k].try_keyon(i, R.prog[k].flv[0], hy, 100).empty();
                    if (ok) { on = i; hyp = hy; }
                }
            }
            if (on < 0) {
                printf("\n== %s cycle %zu: E %d (MDEC_CT %04x): KEY-ON NOT FOUND in [%d, %d] (no candidate tracks 100 samples under any hypothesis; tracker sets at E-64: %zu %zu %zu)\n",
                       R.name.c_str(), j, E, ck.md(E), lo, hi, tr[0].S.size(), tr[1].S.size(), tr[2].S.size());
                for (int k = 0; k < 3; k++) { if (j == 0) tr[k].rest_prior(E); tr[k].S.clear(); }
                continue;
            }
            ons[j] = on; hyps[j] = hyp;
            for (int k = 0; k < 3; k++) tr[k].commit_keyon(on, R.prog[k].flv[0], hyp, 100);
            /* track to the end of the cycle (the next witness onset - 70, or the end) */
            int end = j + 1 < Es.size() ? Es[j + 1] - 70 : (int)c.n;
            for (int k = 0; k < 3; k++) tr[k].advance_to(end - 1);
            int d = E - on; d_hist[d]++;
            bool even = ck.is_clock(E);
            /* classify per stream, simulate the readings */
            Phase ph[3]; FS before[3]; int sN[3]; int bad[3][NRD], badp[3][NRD], pred[3][NRD][15];
            int nfull[3] = {0, 0, 0};
            for (int k = 0; k < 3; k++) {
                ph[k] = classify(R.prog[k], ck, on, E, before[k], sN[k]);
                for (int r = 0; r < NRD; r++) { badp[k][r] = -1; bad[k][r] = simulate(R.prog[k], ck, on, E, end, (Rd)r, tr[k].u, pred[k][r], &badp[k][r]); nfull[k] += bad[k][r] >= end; }
            }
            Phase ph0 = ph[0];
            bool same_phase = ph[1] == ph0 && ph[2] == ph0;
            n_cyc[ph0]++;
            int known_after_E[3] = {0, 0, 0}; for (int k = 0; k < 3; k++) for (int i = E; i < std::min(end, E + 12); i++) known_after_E[k] += tr[k].u[i] >= 0;
            bool tracked_ok[3]; for (int k = 0; k < 3; k++) { tracked_ok[k] = tr[k].pos >= end - 1 && !tr[k].S.empty() && known_after_E[k] >= 3; if (!tracked_ok[k]) lost[k]++; }
            bool all_tracked = tracked_ok[0] && tracked_ok[1] && tracked_ok[2];
            for (int r = 0; r < NRD; r++) {
                bool f3 = all_tracked;
                for (int k = 0; k < 3; k++) { if (tracked_ok[k]) { inf[k][ph[k]] += r == 0; if (bad[k][r] >= end) full[k][ph[k]][r]++; else f3 = false; } }
                if (f3 && same_phase) full3[ph0][r]++;
            }
            int nfull_all = 0; for (int r = 0; r < NRD; r++) { bool f3 = all_tracked; for (int k = 0; k < 3; k++) f3 &= bad[k][r] >= end; nfull_all += f3; }
            if (ph0 == PH_N1 && all_tracked) { if (nfull_all == 0) tot_none[PH_N1]++; if (nfull_all > 1) tot_multi[PH_N1]++; }
            bool show = !quiet || ph0 == PH_N1 || !same_phase || !all_tracked || nfull_all == 0;   /* -q: the informative cycles and anything anomalous */
            if (show) {
                printf("\n== %s cycle %zu: key-on %d (MDEC_CT %04x %s%s), E %d (MDEC_CT %04x %s), d = E - key-on = %d, phase %s", R.name.c_str(), j, on, ck.md(on), ck.is_clock(on) ? "even" : "odd",
                       hyp == 0 ? "" : hyp == 1 ? "; FLV0 loaded one sample AFTER the key-on" : "; FLV0 NOT reloaded", E, ck.md(E), even ? "EVEN = a clock" : "odd: indifferent", d, ph_name[ph0]);
                if (!same_phase) printf(" [per stream: %s %s %s]", ph_name[ph[0]], ph_name[ph[1]], ph_name[ph[2]]);
                printf("; witness E-1..E+2: %d %d %d %d\n", c.v[(size_t)(E - 1) * c.ns + 3], c.v[(size_t)E * c.ns + 3], c.v[(size_t)(E + 1) * c.ns + 3], c.v[(size_t)(E + 2) * c.ns + 3]);
                for (int k = 0; k < 3; k++)
                    printf("   s%d before E: %s v %04x dir %+d passed %d%s; crossing clock at %d (E%+d); tracked to %d/%d (set %zu, max %zu)%s\n", k, seg_name[before[k].state], before[k].v, before[k].dir, before[k].passed,
                           ph[k] == PH_N ? " (E is its crossing clock)" : "", sN[k], sN[k] - E, tr[k].pos + 1, end, tr[k].S.size(), tr[k].maxset, tracked_ok[k] ? "" : "  <-- TRACKER LOST / too few known samples after E: no verdict");
                printf("   %-14s", "u at E-4..E+10:"); for (int i = E - 4; i <= E + 10; i++) printf(" %s%+3d", i == E ? "*" : " ", i - E); printf("\n");
                for (int k = 0; k < 3; k++) {
                    printf("   s%d observed   ", k);
                    for (int i = E - 4; i <= E + 10; i++) { int x = i < (int)c.n ? tr[k].u[i] : -1; if (x < 0) printf("    ?"); else printf("  %03x", x); }
                    printf("\n");
                    for (int r = 0; r < NRD; r++) {
                        printf("   %s %-10s", r == 0 ? "  " : "  ", rd_name[r]);
                        for (int i = 0; i < 15; i++) printf("  %03x", pred[k][r][i]);
                        if (!tracked_ok[k]) printf("   (no verdict)");
                        else if (bad[k][r] >= end) printf("   FULL (matches every known sample to %d)", end);
                        else printf("   fail@E%+d (observed %03x, predicted %03x)%s", bad[k][r] - E, tr[k].u[bad[k][r]], badp[k][r], bad[k][r] >= E + 8 ? " [matches through E+8]" : "");
                        printf("\n");
                    }
                }
                if (ph0 == PH_N1) { printf("   N+1 verdict (all 3 streams FULL):"); for (int r = 0; r < NRD; r++) { bool f3 = all_tracked; for (int k = 0; k < 3; k++) f3 &= bad[k][r] >= end; printf("  %s %s", rd_name[r], f3 ? "FULL" : "refuted"); } printf("\n"); }
            }
            char s[256]; snprintf(s, sizeof s, "%s cyc %2zu  on %6d  E %6d  d %2d  %-4s  %-4s  A %d/3  B %d/3  C %d/3  D %d/3%s%s", R.name.c_str(), j, on, E, d, even ? "EVEN" : "odd", ph_name[ph0],
                     (bad[0][0] >= end) + (bad[1][0] >= end) + (bad[2][0] >= end), (bad[0][1] >= end) + (bad[1][1] >= end) + (bad[2][1] >= end),
                     (bad[0][2] >= end) + (bad[1][2] >= end) + (bad[2][2] >= end), (bad[0][3] >= end) + (bad[1][3] >= end) + (bad[2][3] >= end),
                     all_tracked ? "" : "  (untracked stream)", same_phase ? "" : "  (phases differ)");
            summary.push_back(s);
        }
        printf("\n-- %s: d = E - key-on histogram:", R.name.c_str()); for (auto &kv : d_hist) printf(" %d:%d", kv.first, kv.second); printf("\n");
        printf("-- %s: cycles per phase:", R.name.c_str()); for (int p = 0; p < NPH; p++) printf(" %s %d", ph_name[p], n_cyc[p]); printf(";  untracked stream-cycles: %d %d %d\n", lost[0], lost[1], lost[2]);
        printf("-- %s: FULL per reading (cycles with all 3 streams FULL / cycles of the phase, then per stream):\n", R.name.c_str());
        for (int p = 0; p < NPH; p++) {
            if (!n_cyc[p]) continue;
            printf("     %-5s", ph_name[p]);
            for (int r = 0; r < NRD; r++) printf("  %s %d/%d", rd_name[r], full3[p][r], n_cyc[p]);
            printf("   |");
            for (int k = 0; k < 3; k++) { printf("  s%d:", k); for (int r = 0; r < NRD; r++) printf(" %d", full[k][p][r]); printf("/%d", inf[k][p]); }
            printf("\n");
            tot_cyc[p] += n_cyc[p]; for (int r = 0; r < NRD; r++) { tot_full3[p][r] += full3[p][r]; for (int k = 0; k < 3; k++) tot_full[k][p][r] += full[k][p][r]; } for (int k = 0; k < 3; k++) tot_inf[k][p] += inf[k][p];
        }
    }
    printf("\nSUMMARY per cycle (A/B/C/D = streams reproduced to the end of the cycle; readings differ only on N+1 cycles)\n");
    for (auto &s : summary) printf("  %s\n", s.c_str());
    printf("\nTOTAL over the runs (cycles with all 3 streams FULL / cycles of the phase | per stream FULL / tracked):\n");
    for (int p = 0; p < NPH; p++) {
        if (!tot_cyc[p]) continue;
        printf("  %-5s", ph_name[p]);
        for (int r = 0; r < NRD; r++) printf("  %s %d/%d", rd_name[r], tot_full3[p][r], tot_cyc[p]);
        printf("   |");
        for (int k = 0; k < 3; k++) { printf("  s%d:", k); for (int r = 0; r < NRD; r++) printf(" %d", tot_full[k][p][r]); printf("/%d", tot_inf[k][p]); }
        printf("\n");
    }
    printf("  N+1 cycles (all streams tracked) where NO reading is FULL: %d; where MORE THAN ONE reading is FULL (not separated): %d\n", tot_none[PH_N1], tot_multi[PH_N1]);
    printf("  (N / pre / post / odd cycles re-test the established rule -- every reading coincides there; odd* = the passed flag was pending on an odd key-off sample:\n"
           "   the prediction is the release from the next clock.)\n");
    return 0;
}
