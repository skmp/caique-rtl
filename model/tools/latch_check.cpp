// latch_check.cpp -- analyse the captures of cases/eg_latch.c: does a rewritten envelope register reach the envelope
// generator at the clock of the sample the write pair's key event takes effect on (E: "live", the fetch's timing) or
// one clock later (E + 2: "latched", the model's Slot::egreg)?  Standalone integer replay, no link to the production
// model: the AEG law is re-implemented from NOTES.md "Amplitude envelope" / "Envelope clock" (as tools/koff_fit.cpp),
// the FEG law comes from tools/feg_law.h and u = v >> 1 is recovered through the bit-exact filter with the
// tools/koffatt_check.cpp tracker (the input is the case's LCG signal from the cycle's key-on sample).
//
// Per run and cycle: E_A = the cycle's key-on sample (AEG runs: the 496 onset of stream 0; FEG runs: the first sample from
// which the tracker follows stream 0), then per event j the witness onset E_j on bus 3 (a jump of >= 450000 to ~520176
// in the window around mark 2 + j).  The test stream of the rewritten slot is simulated from E_A under three readings of
// the register image: none (the old registers throughout), live (the EG sees the new image from clock E), latched (from
// clock E + 2, i.e. the image switches on sample E + 1), and compared with the capture up to E + 600 samples (or the
// key-off).  A cycle-event is informative when E is even and the live / latched predictions differ.  The verdict is the
// reading that reproduces the stream; the summary counts them per write order (order 0: register write then KYONEX;
// order 1: KYONEX then register write -- a sample boundary between the two writes (~10 %) makes a latched register look
// live in order 0 and a live register look latched in order 1, so the order with the 100 % consistent verdict is the
// truthful one).
//   latch_check <dir> [-K kc] [-run name] [-v] [-txt eg_latch2.txt]   dir holds eg_latch.txt (or eg_latch2.txt: cases/eg_latch2.c), <run>.hdr/.bin, input.bin
// eg_latch2 runs (rr0, rr24, rr0_koff, rekoff24, d2r0): the same fits; "koff_before" runs have a key-off (KYONEX, not
// pinned) before the events -- its sample K is searched around koff_us on stream 0 under the old law (decay: one more
// old step on the key-off clock, then the release; koff_fit's H1) -- and "reg00 written" events carry the slot's own reg
// 0x00 write (a redundant key-off) in the write group.  Vc = E+1 there means "one clock late" (H_rate0 / H_rekoff).
// Build: make -C tools latch_check (-> build/tools/latch_check).  Run from caique-rtl/model.
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <tuple>
#include <algorithm>
#include "filt_capture.h"
#include "feg_law.h"   /* eg_inc, eg_increment (R < 48 rows one step behind), Prog, eff_rate, Feg, feg_clock_step, feg_key_on */

// ---- AEG law (NOTES "Amplitude envelope"; = tools/koff_fit.cpp) -----------------------------------------------------
struct Img { int AR = 31, D1R = 0, DL = 0, D2R = 0, RR = 31, KRS = 15, OCT = 0, FNS = 0; int flv[5] = {0x1FF8, 0x1FF8, 0x1FF8, 0x1FF8, 0x1FF8}; int FAR = 31, FD1R = 31, FD2R = 31, FRR = 31; };
static void apply_reg(Img &im, int reg, int v) {
    switch (reg) {
    case 0x10: im.AR = v & 0x1F; im.D1R = (v >> 6) & 0x1F; im.D2R = (v >> 11) & 0x1F; break;
    case 0x14: im.RR = v & 0x1F; im.DL = (v >> 5) & 0x1F; im.KRS = (v >> 10) & 0xF; break;
    case 0x18: im.FNS = v & 0x3FF; im.OCT = (v >> 11) & 0xF; break;
    case 0x40: im.FD1R = v & 0x1F; im.FAR = (v >> 8) & 0x1F; break;
    case 0x44: im.FRR = v & 0x1F; im.FD2R = (v >> 8) & 0x1F; break;
    default: if (reg >= 0x2C && reg <= 0x3C) im.flv[(reg - 0x2C) / 4] = v & 0x1FFF; break;
    }
}
static uint32_t aeg_R(const Img &im, int re) {
    if (re == 0) return 0;
    int s = 0;
    if (im.KRS != 15) { int k = im.KRS + ((im.OCT & 8) ? im.OCT - 16 : im.OCT); s = k < 0 ? 0 : 2 * std::min(k, 15) + ((im.FNS >> 9) & 1); }
    return (uint32_t)std::min(63, 2 * re + s);
}
static Prog prog_of(const Img &im) { Prog p; for (int i = 0; i < 5; i++) p.flv[i] = im.flv[i]; p.rate[0] = im.FAR; p.rate[1] = im.FD1R; p.rate[2] = im.FD2R; p.rate[3] = im.FRR; p.krs = im.KRS; p.oct = im.OCT; p.fns = im.FNS; return p; }
static inline int32_t level_of(int a) { int M = 127 - (a & 63), k = a >> 6; return 16 * (int32_t)(((int64_t)0x7FFF * M) >> (7 + k)); }
static int inv_level(int32_t lv) { if (lv == 0) return -1; for (int a = 0; a < 0x3FF; a++) if (level_of(a) == lv) return a; return -1; }
enum { ATT = 0, D1 = 1, D2 = 2, REL = 3 };

struct Ctx { Capture c; uint32_t c0 = 0, K = 6491; std::vector<std::pair<int, int>> marks; bool verbose = false; };
static inline uint32_t mdct(const Ctx &x, int i) { return (x.c0 - x.c.first - (uint32_t)i) & 0xFFFF; }
static inline bool is_clock(const Ctx &x, int i) { return (mdct(x, i) & 1) == 0; }
static inline uint32_t cnt_of(const Ctx &x, int i) { return (x.K - (mdct(x, i) >> 1)) & 0x3FFF; }
static inline const char *par(const Ctx &x, int i) { return is_clock(x, i) ? "EVEN" : "odd"; }

/* simulate test stream k (constant 0x7FFF, fresh key-on on ea) over [ea, end) with the register image old, switching
 * to nw from sample Vc on (Vc < 0: never).  Returns the matched prefix length; pred gets every predicted level. */
static int sim_aeg(const Ctx &x, int k, int ea, int end, const Img &old, const Img &nw, int Vc, std::vector<int32_t> &pred, int K = -1) {
    int a = aeg_R(old, old.AR) >= 63 ? 0 : 0x280, state = ATT;
    bool off = false;
    int matched = -1;
    pred.clear();
    for (int i = ea; i < end; i++) {
        const Img &im = (Vc >= 0 && i >= Vc) ? nw : old;
        int prev = state;
        bool koffed = false;
        if (i == K && state != REL) { state = REL; koffed = true; }   /* key-off (T3): decays take one more old step on this clock, the attack none */
        if (is_clock(x, i) && !off) {
            if (i == ea) {   /* the key-on sample takes no step; an R 63 attack leaves the attack on it (T7) */
                if (state == ATT && a == 0) state = D1;
            } else if (koffed && prev == ATT) {
            } else {
                bool was_d1 = state == D1;
                int rs = koffed ? prev : state;
                int rate = rs == ATT ? im.AR : rs == D1 ? im.D1R : rs == D2 ? im.D2R : im.RR;
                uint32_t inc = eg_increment(aeg_R(im, rate), cnt_of(x, i));
                if (inc) {
                    if (state == ATT) { a += ((~a) * (int)inc) >> 4; if (a <= 0) { a = 0; state = D1; } }
                    else { a += (int)inc; if (a > 0x3FF) { a = 0x3FF; off = true; } }
                }
                if (was_d1 && !off && (a >> 5) == im.DL) state = D2;   /* after the step, also without one (aeg_dl0) */
            }
        }
        int32_t lv = off ? 0 : level_of(a);
        pred.push_back(lv);
        if (matched < 0 && lv != x.c.v[(size_t)i * x.c.ns + k]) matched = i - ea;
    }
    return matched < 0 ? end - ea : matched;
}

// ---- FEG tracker (tools/koffatt_check.cpp) ---------------------------------------------------------------------------
using I = int64_t;
static const int NSIG = 8192;
static const int qm[32] = {192, 176, 160, 144, 128, 120, 112, 104, 96, 88, 80, 72, 64, 60, 56, 52, 48, 44, 40, 36, 32, 30, 28, 26, 24, 22, 20, 18, 16, 15, 14, 13};
static I ceilshr(I n, int s) { return -((-n) >> s); }
struct St { I L, B; int u; bool operator<(const St &o) const { return std::tie(L, B, u) < std::tie(o.L, o.B, o.u); } bool operator==(const St &o) const { return L == o.L && B == o.B && u == o.u; } };
static void fstep(I &L, I &B, I x, int u, int Q) { int v = u << 1, k = v >= 0x1ffe ? 512 : 256 + (u & 255), s = 24 - (v >> 9); I d = 2 * ceilshr(qm[Q] * B, 8); B += (k * (x - L - d)) >> s; L += ceilshr(k * B, s); }
/* u(n) from the onset (= key-on sample: CA restarts, x(i) = 8 * sig[(i - on) mod 8192]); the filter state before the
 * onset is the rest the stopped slot left (L from the last output, B in [-256, 256]); FLV0 known.  Stops at the first
 * sample no state reproduces (after the key-off the fetch stops and the input is no longer the signal). */
static std::vector<int32_t> track(const Capture &c, unsigned on, int k, int flv0, int Q, const std::vector<int16_t> &sig, unsigned nmax) {
    std::vector<St> S, T;
    for (I b0 = -256; b0 <= 256; b0++) S.push_back({-I(c.v[(on - 1) * c.ns + k]) / 2, b0, flv0 >> 1});
    unsigned end = std::min<unsigned>(c.n, on + nmax);
    std::vector<int32_t> u(end - on, -1);
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
        bool one = true; for (auto &s : T) one &= s.u == T[0].u;
        u[n - on] = one ? T[0].u : -1;
        std::swap(S, T);
    }
    return u;
}
/* FEG replay from the onset with the program old, switching to nw from sample Vc on (target and rates alike); compares
 * v >> 1 with the tracked u over [on, end) (unknown u skipped); returns the matched prefix length; pred = v per sample */
static int sim_feg(const Ctx &x, const std::vector<int32_t> &u, int on, int end, const Img &old, const Img &nw, int Vc, std::vector<int32_t> &pred) {
    Prog po = prog_of(old), pn = prog_of(nw);
    Feg f{}; feg_key_on(f, po);
    int matched = -1;
    pred.clear();
    for (int i = on; i < end; i++) {
        const Prog &p = (Vc >= 0 && i >= Vc) ? pn : po;
        int inc, rs;
        if (is_clock(x, i) && i > on) feg_clock_step(f, p, cnt_of(x, i), M_S3, false, inc, rs);
        pred.push_back(f.v);
        int n = i - on;
        if (matched < 0 && n < (int)u.size() && u[n] >= 0 && u[n] != (f.v >> 1)) matched = n;
    }
    return matched < 0 ? end - on : matched;
}

// ---- text output ---------------------------------------------------------------------------------------------------------
struct Ev { int cyc, j, slot, reg, oldv, newv, order, mon; bool missed; std::string field; unsigned fold = 0, fnew = 0; bool reg00 = false; int probe_reg = -1; unsigned probe_val = 0; long ca[3] = {-1, -1, -1}; };
struct Run { std::string name; bool feg = false; int koff_us = 0, cycle_us = 0, koff_before = 0, restore_us = 0; Img s[3]; int Q = 4; std::vector<Ev> evs; uint32_t c0 = 0; bool c0_found = false, ended = false; };
static bool parse_txt(const std::string &path, std::vector<Run> &runs) {
    FILE *f = fopen(path.c_str(), "r");
    if (!f) return false;
    char line[600];
    Run *cur = nullptr;
    auto find = [&](const char *name) -> Run * { for (auto &r : runs) if (r.name == name) return &r; return nullptr; };
    while (fgets(line, sizeof line, f)) {
        char name[64], role[16], mode[8], field[16];
        int k, slot, AR, D1R, DL, D2R, RR, KRS, OCT; unsigned FNS; int VOFF, LPOFF, Q; unsigned flv[5]; int FAR, FD1R, FD2R, FRR;
        if (sscanf(line, "%63s stream %d: slot %d role %15s AR %d D1R %d DL %d D2R %d RR %d KRS %d OCT %d FNS %x VOFF %d LPOFF %d Q %d FLV %x %x %x %x %x FAR %d FD1R %d FD2R %d FRR %d",
                   name, &k, &slot, role, &AR, &D1R, &DL, &D2R, &RR, &KRS, &OCT, &FNS, &VOFF, &LPOFF, &Q, &flv[0], &flv[1], &flv[2], &flv[3], &flv[4], &FAR, &FD1R, &FD2R, &FRR) == 24 && k >= 0 && k < 3) {
            Run *r = find(name);
            if (!r) { runs.push_back(Run()); r = &runs.back(); r->name = name; }
            cur = r;
            Img &im = r->s[k];
            im.AR = AR; im.D1R = D1R; im.DL = DL; im.D2R = D2R; im.RR = RR; im.KRS = KRS; im.OCT = OCT; im.FNS = (int)FNS;
            for (int i = 0; i < 5; i++) im.flv[i] = (int)flv[i];
            im.FAR = FAR; im.FD1R = FD1R; im.FD2R = FD2R; im.FRR = FRR; r->Q = Q; r->feg = LPOFF == 0;
            continue;
        }
        int ncyc; unsigned koff, cyc_us;
        if (sscanf(line, "%63s run: mode %7s cycles %d koff_us %u cycle_us %u", name, mode, &ncyc, &koff, &cyc_us) == 5) {
            Run *r = find(name); if (!r) continue;
            r->feg = !strcmp(mode, "feg"); r->koff_us = (int)koff; r->cycle_us = (int)cyc_us; cur = r;
            const char *kb = strstr(line, "koff_before "); unsigned ru;
            if (kb) sscanf(kb, "koff_before %d restore_us %u", &r->koff_before, &ru), r->restore_us = (int)ru;
            continue;
        }
        int cyc, j, order; unsigned reg, oldv, newv, mon, fold, fnew;
        if (sscanf(line, "%63s cyc %d ev %d: slot %d reg %x old %x new %x order %d mon %x field %15s %u->%u", name, &cyc, &j, &slot, &reg, &oldv, &newv, &order, &mon, field, &fold, &fnew) == 12) {
            Run *r = find(name); if (!r) continue;
            Ev e{cyc, j, slot, (int)reg, (int)oldv, (int)newv, order, (int)mon, strstr(line, "MISSED") != nullptr, field, fold, fnew, strstr(line, "reg00 written") != nullptr};
            const char *pp = strstr(line, " probe ");   /* eg_latch3: "probe <reg|none> <val> ca a b c" */
            if (pp) { char pr[16]; unsigned pv; if (sscanf(pp, " probe %15s %x", pr, &pv) == 2 && strcmp(pr, "none")) { e.probe_reg = (int)strtol(pr, nullptr, 16); e.probe_val = pv; } }
            const char *pc = strstr(line, " ca ");
            if (pc) sscanf(pc, " ca %ld %ld %ld", &e.ca[0], &e.ca[1], &e.ca[2]);
            r->evs.push_back(e);
            continue;
        }
        /* the run's cap_start line (", c0 " with the comma: a bare "c0 " also matches inside a ring address) */
        const char *p = strstr(line, ", c0 ");
        unsigned c0;
        if (cur && !cur->c0_found && !cur->ended && strstr(line, "cap_start:") && p && sscanf(p, ", c0 %x", &c0) == 1) { cur->c0 = c0; cur->c0_found = true; continue; }
        if (cur && !strncmp(line, cur->name.c_str(), cur->name.size()) && line[cur->name.size()] == ':') cur->ended = true;
    }
    fclose(f);
    return !runs.empty();
}

static int find_496_onset(const Ctx &x, int k, int lo, int hi) { /* fresh key-on: 496 (a 0x280) then a rising attack */
    lo = std::max(lo, 1); hi = std::min(hi, (int)x.c.n - 3);
    for (int i = lo; i <= hi; i++) {
        const int32_t *v = &x.c.v[(size_t)i * x.c.ns + k];
        int ns = x.c.ns;
        if (v[0] == 496 && v[-ns] != 496 && v[ns] >= 496 && v[2 * ns] > 496) return i;
    }
    return -1;
}
static int find_witness(const Ctx &x, int lo, int hi) { /* witness key-on on bus 3: a jump of >= 450000 (to 520176 + the previous witness's residual) */
    lo = std::max(lo, 1); hi = std::min(hi, (int)x.c.n - 2);
    for (int i = lo; i <= hi; i++)
        if (x.c.v[(size_t)i * x.c.ns + 3] - x.c.v[(size_t)(i - 1) * x.c.ns + 3] >= 450000) return i;
    return -1;
}
static const char *res(int m, int full, int ref) { static char b[16][40]; static int n; char *p = b[n++ & 15]; if (m >= full) snprintf(p, 40, "FULL"); else snprintf(p, 40, "fail@E%+d", m - ref); return p; }

struct Tally { int events = 0, found = 0, missed = 0, pre_fail = 0, odd = 0, ident = 0, inf[2] = {0, 0}, live[2] = {0, 0}, latched[2] = {0, 0}, neither[2] = {0, 0}, early[2] = {0, 0}, late[2] = {0, 0}, ca_restart = 0, ca_running = 0; };

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: latch_check <dir> [-K kc] [-run name] [-v]\n"); return 2; }
    std::string dir = argv[1], only, txt;
    uint32_t Kc = 6491; bool verbose = false;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "-K") && i + 1 < argc) Kc = (uint32_t)atoi(argv[++i]);
        else if (!strcmp(argv[i], "-run") && i + 1 < argc) only = argv[++i];
        else if (!strcmp(argv[i], "-txt") && i + 1 < argc) txt = argv[++i];
        else if (!strcmp(argv[i], "-v")) verbose = true;
    }
    std::vector<Run> runs;
    if (txt.empty()) for (const char *cand : {"eg_latch.txt", "eg_latch2.txt", "eg_latch3.txt"}) { FILE *f = fopen((dir + "/" + cand).c_str(), "r"); if (f) { fclose(f); txt = cand; break; } }
    if (txt.empty()) { fprintf(stderr, "no eg_latch*.txt in %s\n", dir.c_str()); return 2; }
    if (!parse_txt(dir + "/" + txt, runs)) { fprintf(stderr, "cannot read %s/%s\n", dir.c_str(), txt.c_str()); return 2; }
    /* the FEG input signal: the case's LCG, checked against input.bin when present */
    std::vector<int16_t> sig(NSIG);
    { uint32_t seed = 4242; for (int i = 0; i < NSIG; i++) { seed = seed * 1103515245u + 12345u; sig[i] = (int16_t)(seed >> 16); if (!sig[i]) sig[i] = 1; }
      try { std::vector<int16_t> in = input(dir + "/input.bin"); if (in != sig) { fprintf(stderr, "input.bin differs from the regenerated signal\n"); return 2; } }
      catch (std::exception &) { printf("(no input.bin in %s: using the regenerated signal)\n", dir.c_str()); } }
    printf("latch_check %s: %zu runs, clock constant K %u\n", dir.c_str(), runs.size(), Kc);
    printf("readings of a rewrite whose write pair's key event takes effect on sample E: live = the EG sees the new register at clock E;\n"
           "latched = at clock E + 2 (the model's egreg).  Only an EVEN E is informative.  Order 0 = register write then KYONEX, order 1 = KYONEX first.\n");
    std::vector<std::string> summary;
    for (auto &r : runs) {
        if (!only.empty() && r.name != only) continue;
        Ctx x; x.K = Kc; x.verbose = verbose;
        try { x.c = cap(dir + "/" + r.name); } catch (std::exception &e) { printf("== %s: %s\n", r.name.c_str(), e.what()); continue; }
        if (!r.c0_found) { printf("== %s: no cap_start c0 line in eg_latch.txt\n", r.name.c_str()); continue; }
        x.c0 = r.c0;
        { FILE *f = fopen((dir + "/" + r.name + ".hdr").c_str(), "rb"); uint32_t h[16 + 2 * 64] = {0}; size_t nh = f ? fread(h, 4, sizeof h / 4, f) : 0; if (f) fclose(f);
          for (size_t i = 11; i + 1 < nh && i < 11 + 2 * h[6]; i += 2) x.marks.push_back({(int)h[i], (int)(h[i + 1] - x.c.first)}); }
        std::vector<int> m1; std::vector<std::vector<int>> mj(3);
        for (auto &m : x.marks) { if (m.first == 1) m1.push_back(m.second); else if (m.first >= 2 && m.first <= 4) mj[m.first - 2].push_back(m.second); }
        int ncyc = (int)m1.size();
        int koff_samples = (int)(((int64_t)r.koff_us * 441) / 10000);
        int restore_samples = (int)(((int64_t)r.restore_us * 441) / 10000);
        printf("\n== %s (%s): %u samples x %u streams, n_first %u, c0 %04x (MDEC_CT of sample i = (%04x - i) & 0xFFFF), %d cycles, key-off at +%d us (%d samples)%s\n",
               r.name.c_str(), r.feg ? "FEG" : "AEG", x.c.n, x.c.ns, x.c.first, x.c0, (x.c0 - x.c.first) & 0xFFFF, ncyc, r.koff_us, koff_samples,
               r.koff_before ? " BEFORE the events (searched per cycle), registers restored at restore_us" : "");
        for (int k = 0; k < 3; k++) {
            const Img &s = r.s[k];
            if (r.feg) printf("   stream %d: FLV %04x %04x %04x %04x %04x FAR %d FD1R %d FD2R %d FRR %d (R %u/%u/%u/%u) Q %d\n", k, s.flv[0], s.flv[1], s.flv[2], s.flv[3], s.flv[4], s.FAR, s.FD1R, s.FD2R, s.FRR,
                              aeg_R(s, s.FAR), aeg_R(s, s.FD1R), aeg_R(s, s.FD2R), aeg_R(s, s.FRR), r.Q);
            else printf("   stream %d: AR %d D1R %d DL %d D2R %d RR %d KRS %d (R %u/%u/%u/%u)\n", k, s.AR, s.D1R, s.DL, s.D2R, s.RR, s.KRS, aeg_R(s, s.AR), aeg_R(s, s.D1R), aeg_R(s, s.D2R), aeg_R(s, s.RR));
        }
        Tally T; Tally Tk[3];
        const int W = 300;
        for (int c = 0; c < ncyc; c++) {
            /* the cycle's key-on sample */
            int ea = -1;
            std::vector<std::vector<int32_t>> us(3);
            if (!r.feg) {
                ea = find_496_onset(x, 0, m1[c] - W, m1[c] + W);
                if (ea < 0) { printf("cycle %2d: key-on (496 onset of stream 0) NOT FOUND around mark %d\n", c, m1[c]); continue; }
                for (int k = 1; k < 3; k++) if (x.c.v[(size_t)ea * x.c.ns + k] != 496) printf("cycle %2d: stream %d is %d on E_A (not 496)\n", c, k, x.c.v[(size_t)ea * x.c.ns + k]);
            } else {
                /* the input starts on the key-on sample: the first sizeable change of stream 0 in the mark window, then
                 * the earliest candidate near it from which the tracker follows stream 0 for 300 samples */
                int ch = -1;
                for (int i = std::max(1, m1[c] - W); i <= std::min((int)x.c.n - 400, m1[c] + W) && ch < 0; i++)
                    if (std::abs(x.c.v[(size_t)i * x.c.ns] - x.c.v[(size_t)(i - 1) * x.c.ns]) > 1000) ch = i;
                if (ch < 0) { printf("cycle %2d: FEG onset NOT FOUND (stream 0 flat around mark %d)\n", c, m1[c]); continue; }
                for (int i = std::max(1, ch - 6); i <= ch + 1 && ea < 0; i++) { auto u = track(x.c, i, 0, r.s[0].flv[0], r.Q, sig, 300); if (u.size() >= 300) ea = i; }
                if (ea < 0) { printf("cycle %2d: FEG onset NOT FOUND (stream 0 changes at %d but no candidate in [%d, %d] tracks 300 samples)\n", c, ch, ch - 6, ch + 1); continue; }
                unsigned nmax = (unsigned)(koff_samples + 400);
                for (int k = 0; k < 3; k++) us[k] = track(x.c, ea, k, r.s[k].flv[0], r.Q, sig, nmax);
            }
            printf("cycle %2d: key-on E_A %d (%s, mark %d)", c, ea, par(x, ea), m1[c]);
            if (r.feg) { printf(", tracked"); for (int k = 0; k < 3; k++) { int amb = 0; for (auto v : us[k]) amb += v < 0; printf(" s%d %zu (%d ambiguous)", k, us[k].size(), amb); } }
            printf("\n");
            int Kko = -1;   /* koff_before runs: the (unpinned) key-off sample, searched on stream 0 under the old law up to the first event */
            if (r.koff_before) {
                int e0 = -1;
                if (!mj[0].empty() && (int)mj[0].size() > c) e0 = find_witness(x, std::max(ea + 20, mj[0][c] - W), mj[0][c] + W);
                int lim = e0 > 0 ? e0 - 1 : std::min((int)x.c.n, ea + koff_samples + 2 * W);   /* up to E0 - 2: a straddled write may act at clock E0 - 1 */
                std::vector<int32_t> tmp; std::vector<int> ks;
                for (int K = std::max(ea + 1, ea + koff_samples - W); K <= std::min(lim - 1, ea + koff_samples + W); K++)
                    if (sim_aeg(x, 0, ea, lim, r.s[0], r.s[0], -1, tmp, K) >= lim - ea) ks.push_back(K);
                if (ks.empty()) { printf("    key-off: NO sample in [koff-%d, koff+%d] reproduces stream 0 up to the first event under the old law\n", W, W); continue; }
                Kko = ks[0];
                printf("    key-off sample: %zu candidates reproduce stream 0 (first %d = E_A%+d, %s%s); the events see the slots released\n", ks.size(), Kko, Kko - ea, par(x, Kko), ks.size() > 4 ? " -- RR 0: the key-off is invisible" : "");
            }
            int prevE = ea;
            for (int j = 0; j < 3; j++) {
                const Ev *ev = nullptr;
                for (auto &e : r.evs) if (e.cyc == c && e.j == j) ev = &e;
                if (!ev) { printf("    ev %d: no text line\n", j); continue; }
                T.events++; Tk[ev->slot].events++;
                if (ev->missed) { printf("    ev %d: slot %d MISSED (no burst)\n", j, ev->slot); T.missed++; continue; }
                if ((int)mj[j].size() <= c) { printf("    ev %d: no mark\n", j); continue; }
                int E = find_witness(x, std::max(prevE + 20, mj[j][c] - W), mj[j][c] + W);
                if (E < 0) { printf("    ev %d: slot %d witness onset NOT FOUND around mark %d\n", j, ev->slot, mj[j][c]); continue; }
                T.found++;
                prevE = E;
                int k = ev->slot;
                Img old = r.s[k], nw = r.s[k];
                apply_reg(old, ev->reg, ev->oldv); apply_reg(nw, ev->reg, ev->newv);
                int end = std::min({(int)x.c.n, E + 600, ea + (r.koff_before ? restore_samples : koff_samples) - 5});
                if (r.feg) end = std::min(end, ea + (int)us[k].size());
                /* four switch samples Vc: the EG sees the new image from clock Vc on.  E-1 = the register write landed in
                 * the sample BEFORE the key event's (order 0 straddle of a live register); E = live; E+1 = latched
                 * (also a live register whose write landed one sample late, order 1); E+2 = a latched register whose write
                 * landed one sample late (order 1 straddle).  Even E: {E-1, E} = the live pattern, {E+1, E+2} = the latched
                 * pattern.  Odd E: {E, E+1} coincide (clock E+1); E-1 alone ("EARLY") proves a live register, E+2 alone
                 * ("LATE") proves a latched one -- both only when a sample boundary fell inside the write pair. */
                std::vector<int32_t> p_none, p[4];
                int m_none, m[4];
                if (!r.feg) { m_none = sim_aeg(x, k, ea, end, old, nw, -1, p_none, Kko); for (int q = 0; q < 4; q++) m[q] = sim_aeg(x, k, ea, end, old, nw, E - 1 + q, p[q], Kko); }
                else { m_none = sim_feg(x, us[k], ea, end, old, nw, -1, p_none); for (int q = 0; q < 4; q++) m[q] = sim_feg(x, us[k], ea, end, old, nw, E - 1 + q, p[q]); }
                bool fit[4]; for (int q = 0; q < 4; q++) fit[q] = m[q] >= end - ea;
                int wv[5]; for (int q = 0; q < 5; q++) wv[q] = x.c.v[(size_t)(E - 1 + q) * x.c.ns + 3];
                char probe_s[160] = "";
                if (ev->probe_reg >= 0) snprintf(probe_s, sizeof probe_s, ", probe reg %02x := %04x", ev->probe_reg, ev->probe_val);
                if (ev->ca[0] >= 0) {   /* eg_latch3: CA monitor before the group, right after it, 250 us later */
                    bool restart = ev->ca[2] < 40 && ev->ca[0] > 100;
                    char b[100]; snprintf(b, sizeof b, "; CA %ld -> %ld -> %ld: %s", ev->ca[0], ev->ca[1], ev->ca[2], restart ? "RESTARTED" : ev->ca[2] > ev->ca[0] ? "running" : "?");
                    strncat(probe_s, b, sizeof probe_s - strlen(probe_s) - 1);
                    if (restart) { T.ca_restart++; Tk[k].ca_restart++; } else { T.ca_running++; Tk[k].ca_running++; }
                }
                printf("    ev %d: slot %d reg %02x %04x -> %04x (%s %u -> %u, order %d, mon %04x%s%s)  E %d (%s, E_A%+d, mark %d; witness %d %d %d %d %d)\n", j, k, ev->reg, ev->oldv, ev->newv, ev->field.c_str(),
                       ev->fold, ev->fnew, ev->order, ev->mon, ev->reg00 ? ", slot's reg 0x00 rewritten: redundant key-off" : "", probe_s, E, par(x, E), E - ea, mj[j][c], wv[0], wv[1], wv[2], wv[3], wv[4]);
                bool pre_ok = m_none >= E - 1 - ea;   /* the old law must hold up to E - 2 (a straddled write may act at clock E - 1) */
                bool even = is_clock(x, E);
                bool ident = p[1] == p[2];   /* live == latched: nothing to learn from this E (odd E, or a rewrite without effect) */
                int obs = m_none < end - ea ? m_none + ea - E : 9999;
                /* the flv run: was the written target inside (v_E, v_E + inc]?  v_E = the value before E's step under the old law */
                std::string extra;
                if (ev->reg == 0x38 && !p_none.empty()) {
                    int vE = p_none[std::max(0, E - 1 - ea)];
                    uint32_t inc = eg_increment(aeg_R(old, old.FD2R), cnt_of(x, E));
                    char b[160]; snprintf(b, sizeof b, "  v before E %04x, +%u: target %04x is %s", vE, inc, ev->newv, ev->newv > vE && ev->newv <= vE + (int)inc ? "IN (v_E, v_E+inc]: hold vs runaway" : ev->newv <= vE ? "at/below v_E (both run away)" : "above v_E+inc (both hold later)");
                    extra = b;
                }
                const char *verdict;
                int o = ev->order & 1;
                if (!pre_ok) { verdict = "PRE-EVENT MISMATCH (law / onset?)"; T.pre_fail++; Tk[k].pre_fail++; }
                else if (even) {
                    if (ident) { verdict = "identical predictions: uninformative"; T.ident++; Tk[k].ident++; }
                    else {
                        T.inf[o]++; Tk[k].inf[o]++;
                        if (fit[1] && !fit[2]) { verdict = "LIVE (clock E)"; T.live[o]++; Tk[k].live[o]++; }
                        else if (fit[2] && !fit[1]) { verdict = "LATCHED (clock E+2)"; T.latched[o]++; Tk[k].latched[o]++; }
                        else { verdict = "NEITHER"; T.neither[o]++; Tk[k].neither[o]++; }
                    }
                } else {
                    if (fit[1]) { verdict = "odd E: live = latched (clock E+1), no straddle"; T.odd++; Tk[k].odd++; }
                    else if (fit[0] && !fit[3]) { verdict = "odd E, EARLY: took effect at clock E-1 (write one sample before the key event: LIVE register)"; T.early[o]++; Tk[k].early[o]++; }
                    else if (fit[3] && !fit[0]) { verdict = "odd E, LATE: took effect at clock E+3 (write one sample after the key event: LATCHED register)"; T.late[o]++; Tk[k].late[o]++; }
                    else { verdict = "odd E: NEITHER"; T.neither[o]++; Tk[k].neither[o]++; }
                }
                printf("        %s: compared [E_A, E+%d); old law %s, observed change at E%+d;  Vc=E-1 %s  E %s  E+1 %s  E+2 %s  -> %s%s\n", r.feg ? "u" : "level", end - E, res(m_none, end - ea, E - ea),
                       obs, res(m[0], end - ea, E - ea), res(m[1], end - ea, E - ea), res(m[2], end - ea, E - ea), res(m[3], end - ea, E - ea), verdict, extra.c_str());
                bool none_fit = !fit[0] && !fit[1] && !fit[2] && !fit[3];
                if (x.verbose || !pre_ok || none_fit) {
                    printf("        window E-3..E+8 (i, MDEC_CT, captured%s, then the predictions none / Vc=E-1 / E / E+1 / E+2):\n", r.feg ? " u" : " level(a)");
                    for (int i = E - 3; i <= E + 8 && i < end; i++) {
                        int n = i - ea; if (n < 0) continue;
                        if (!r.feg) { int32_t lv = x.c.v[(size_t)i * x.c.ns + k]; int a = inv_level(lv);
                            printf("          %6d %04x%s  %7d(a %03x)   %7d / %7d %7d %7d %7d\n", i, mdct(x, i), is_clock(x, i) ? "*" : " ", lv, a < 0 ? 0x3FF : a, p_none[n], p[0][n], p[1][n], p[2][n], p[3][n]); }
                        else { int uu = n < (int)us[k].size() ? us[k][n] : -2;
                            printf("          %6d %04x%s  u %s%03x   v %04x / %04x %04x %04x %04x\n", i, mdct(x, i), is_clock(x, i) ? "*" : " ", uu < 0 ? "?" : "", uu < 0 ? 0 : uu, p_none[n], p[0][n], p[1][n], p[2][n], p[3][n]); }
                    }
                }
            }
        }
        char s[400];
        snprintf(s, sizeof s, "%-9s events %d found %d missed %d pre-fail %d odd-E plain %d identical %d | even E, order0 %d: live %d latched %d neither %d | order1 %d: live %d latched %d neither %d | odd E straddles: EARLY %d/%d LATE %d/%d (order0/order1)%s",
                 r.name.c_str(), T.events, T.found, T.missed, T.pre_fail, T.odd, T.ident, T.inf[0], T.live[0], T.latched[0], T.neither[0], T.inf[1], T.live[1], T.latched[1], T.neither[1], T.early[0], T.early[1], T.late[0], T.late[1],
                 (T.ca_restart + T.ca_running) ? (std::string(" | CA restarted ") + std::to_string(T.ca_restart) + "/" + std::to_string(T.ca_restart + T.ca_running)).c_str() : "");
        summary.push_back(s);
        printf("SUMMARY %s\n", s);
        for (int k = 0; k < 3; k++) {
            const Tally &t = Tk[k];
            printf("   stream %d: events %d pre-fail %d odd-plain %d identical %d | order0 inf %d live %d latched %d neither %d | order1 inf %d live %d latched %d neither %d | early %d/%d late %d/%d\n", k, t.events, t.pre_fail, t.odd, t.ident,
                   t.inf[0], t.live[0], t.latched[0], t.neither[0], t.inf[1], t.live[1], t.latched[1], t.neither[1], t.early[0], t.early[1], t.late[0], t.late[1]);
        }
    }
    printf("\nSUMMARY (even E: live = the new register acts at clock E, latched = at clock E+2; a reading is refuted where it reproduces < informative.\n"
           " A sample boundary inside the write pair (~10 %%) turns a LATCHED register into a 'live' verdict in order 0 (register first) and a LIVE one into\n"
           " 'latched' in order 1 (KYONEX first); on an odd E the same straddles show up as EARLY (effect at clock E-1: only a LIVE register, order 0)\n"
           " or LATE (effect at clock E+3: only a LATCHED register, order 1).  Expected: LIVE register -> order0 100 %% live, order1 ~90 %% live + ~10 %%\n"
           " latched, some EARLY, no LATE;  LATCHED register -> order1 100 %% latched, order0 ~90 %% latched + ~10 %% live, some LATE, no EARLY.)\n");
    for (auto &s : summary) printf("  %s\n", s.c_str());
    return 0;
}
