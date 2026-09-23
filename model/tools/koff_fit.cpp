// koff_fit.cpp -- standalone AEG replay of the tests/aeg_koff captures (cases/aeg_koff.c): which key-event rules
// reproduce every sample of every cycle?  Integer arithmetic only, no link to the production model (the AEG law is
// re-implemented from NOTES.md "Amplitude envelope" / "Envelope clock": clock on even MDEC_CT, cnt = K - MDEC_CT/2,
// the eg_inc rows with {b,2b,b,b,b,2b,b,b} at rows 5/9/13, the R < 48 counter offset -1, level =
// 16 * floor(s * (127 - (a & 63)) >> (7 + (a >> 6))) with s = 0x7FFF or the ramp sample 8*CA - 0x4000).
//
// The key-event samples are PINNED by the capture itself (see the case header): the test slots' fresh key-on shows
// as level 496 (a = 0x280) on the key-on sample E_A; the witness slot (stream 3, R 63) keyed by the same KYONEX
// write as the test slots' key-off jumps to full level (520176) on the key-off sample E_B.  In kon_rel the witness
// is keyed on (fresh) by the write that keys the test slots on during their release: it pins that sample E_C.
//
// koff_d1 (decay 1 -> release; F1 measured decay 2, F2 the attack): the same H0 / H1 / H3 question on the missing segment.
// Hypotheses at a key-off sample that is a clock (Q1 / Q2):
//   H0  a += inc_release                 (the new segment's increment)
//   H1  a += inc_prev  (linear)          (the previous segment's increment; the model's S3 rule, also in an attack)
//   H2  previous segment's formula       (attack: a += (~a * inc_att) >> 4, decay: a += inc_prev)
//   H3  no step on the key-off sample
// A key-off on an odd sample: all four coincide (the next clock uses the release rate) -> uninformative.
// Hypotheses at a key-on during a release (Q3):
//   L0  a = 0x280 on the key-on sample, no step on it (S2, the model)
//   L1  a = 0x280 on the key-on sample and a step on it when it is a clock
//   L2  a = 0x280 on the next clock (the release level until then)
//   C0 / C1 (ramp streams): CA restarts on the key-on sample / on the first clock at or after it.
// The witness decays to "off" by itself after each key-on (D1R 31 to DL 31, D2R 31), so its key-ons are fresh ones
// (S2 / S5) and its stream carries no key-off information; the rate-0 key-off case (inc_prev 0) is koff_d2b stream 2
// (D2R 0, RR 26).  The witness section of the output only appears when the witness is still sounding at E_A.
//
//   koff_fit <capture prefix> <c0 hex> <K> <kind> [aeg_koff.txt] [-v] [-synth H0|H2|H3|L1|L2|C1 ...]
// kind: koff_d2 | koff_d2b | koff_d1 | koff_att | kon_rel (selects the built-in stream table = cases/aeg_koff.c; with the
// case's text output given, the stream lines of that run are parsed instead, and c0 too when 0 is passed: the c0 of
// the cap_start line between that run's "<run> stream k:" lines and its "<run>: N samples" line, matched as ", c0 " --
// a bare "c0 " also matches inside a ring address such as "at a9c0 (n 679)", which is how the hw koff_d2b run once got
// c0 0 from the file).  -v dumps
// the levels around every key event.  -synth regenerates the streams from the pinned events under another rule
// (self-test: shows what a console with that rule would print).
// Build: make -C tools koff_fit (-> build/tools/koff_fit; single file, no model link).  Run from caique-rtl/model.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>
#include <algorithm>
#include "filt_capture.h"

static const uint8_t eg_inc[17][8] = {
    {0, 1, 0, 1, 0, 1, 0, 1}, {0, 1, 0, 1, 1, 1, 0, 1}, {0, 1, 1, 1, 0, 1, 1, 1}, {0, 1, 1, 1, 1, 1, 1, 1},
    {1, 1, 1, 1, 1, 1, 1, 1}, {1, 2, 1, 1, 1, 2, 1, 1}, {1, 2, 1, 2, 1, 2, 1, 2}, {1, 2, 2, 2, 1, 2, 2, 2},
    {2, 2, 2, 2, 2, 2, 2, 2}, {2, 4, 2, 2, 2, 4, 2, 2}, {2, 4, 2, 4, 2, 4, 2, 4}, {2, 4, 4, 4, 2, 4, 4, 4},
    {4, 4, 4, 4, 4, 4, 4, 4}, {4, 8, 4, 4, 4, 8, 4, 4}, {4, 8, 4, 8, 4, 8, 4, 8}, {4, 8, 8, 8, 4, 8, 8, 8},
    {8, 8, 8, 8, 8, 8, 8, 8}};
static inline uint32_t eg_increment(uint32_t R, uint32_t cnt, uint32_t slow_off) {
    if (R == 0) return 0;
    if (R < 48) {
        cnt += slow_off;
        uint32_t shift = 11 - (R >> 2);
        if (cnt & ((1u << shift) - 1)) return 0;
        return eg_inc[R & 3][(cnt >> shift) & 7];
    }
    return eg_inc[R >= 60 ? 16 : 4 + (R - 48)][cnt & 7];
}
struct Stream { int AR = 31, D1R = 0, DL = 0, D2R = 0, RR = 31, KRS = 15, OCT = 0, FNS = 0; bool ramp = false, witness = false; int slot = 0; };
static uint32_t eff_rate(const Stream &st, int re) {
    if (re == 0) return 0;
    int s = 0;
    if (st.KRS != 15) {
        int k = st.KRS + ((st.OCT & 8) ? st.OCT - 16 : st.OCT);
        s = k < 0 ? 0 : 2 * std::min(k, 15) + ((st.FNS >> 9) & 1);
    }
    return (uint32_t)std::min(63, 2 * re + s);
}
static inline int32_t level_of(int a, int32_t s) {
    int M = 127 - (a & 63), k = a >> 6;
    return 16 * (int32_t)(((int64_t)s * M) >> (7 + k));
}
static inline int32_t ramp_s(uint32_t CA) { return (int32_t)(8 * (int32_t)(CA & 4095) - 0x4000); }
static int inv_level(int32_t lv) { /* smallest a with level_of(a, 0x7FFF) == lv, -1 if none (0 = off) */
    if (lv == 0) return -1;
    for (int a = 0; a < 0x3FF; a++) if (level_of(a, 0x7FFF) == lv) return a;
    return -1;
}
static int inv_level_hi(int32_t lv) { int r = -1; for (int a = 0; a < 0x3FF; a++) if (level_of(a, 0x7FFF) == lv) r = a; return r; }
enum { ATT = 0, D1 = 1, D2 = 2, REL = 3 };
static const char *sname(int s) { return s == ATT ? "att" : s == D1 ? "d1" : s == D2 ? "d2" : "rel"; }
struct Hyp { int K = 1, L = 0, C = 0; };
struct EG { int a = 0x3FF, state = REL; bool off = true, playing = false, pending = false, ca_pending = false; uint32_t CA = 0; };
struct Ev { int i; bool on; };
struct Ctx { Capture c; uint32_t c0 = 0, K = 6491; std::vector<Stream> st; std::vector<std::pair<int, int>> marks; bool verbose = false;
             int synthK = -1, synthL = -1, synthC = -1; /* -synth: regenerate the streams under this hypothesis (self-test) */ };
static inline uint32_t mdct(const Ctx &x, int i) { return (x.c0 - x.c.first - (uint32_t)i) & 0xFFFF; }
static inline const char *par(const Ctx &x, int i) { return (mdct(x, i) & 1) ? "odd" : "even"; }
static inline bool is_clock(const Ctx &x, int i) { return (mdct(x, i) & 1) == 0; }

static void load(EG &e, const Stream &st) {
    e.a = eff_rate(st, st.AR) >= 63 ? 0 : 0x280;
    e.state = ATT; e.off = false; e.playing = true;
}
/* simulate stream k over [i0, i1) from state e with the key events evs under hypothesis h.  Returns the number of
 * consecutive samples matching the capture (i1 - i0 = all).  pred: the predicted levels; a_at: a before the output of
 * sample a_at_i (for the report). */
struct SimOut { std::vector<int32_t> *pred = nullptr; int a_at_i = -1; int a_at = -1; int state_at = -1; };
static int simulate(const Ctx &x, int k, EG e, int i0, int i1, const std::vector<Ev> &evs, Hyp h, SimOut *out = nullptr) {
    const Stream &st = x.st[k];
    int matched = -1;
    for (int i = i0; i < i1; i++) {
        uint32_t md = mdct(x, i);
        bool clock = (md & 1) == 0;
        uint32_t cnt = (x.K - (md >> 1)) & 0x3FFF;
        bool keyed = false, koffed = false;
        int prev = e.state;
        for (auto &ev : evs) {
            if (ev.i != i) continue;
            if (ev.on) {
                if (e.state != REL) continue; /* key-on outside a release: ignored */
                /* a fresh key-on (slot "off") follows S2 (load on the key-on sample, no step, CA 0: tests/eg_lock keys,
                 * sgc_loop); the L / C hypotheses are about a key-on during a release that has not reached "off" */
                Hyp hk = h;
                if (e.off) hk.L = hk.C = 0;
                if (hk.L == 2 && !clock) {     /* load on the next clock; CA per C rule */
                    e.pending = true;
                    if (hk.C == 0) e.CA = 0; else e.ca_pending = true;
                } else {
                    load(e, st);
                    keyed = hk.L != 1;
                    if (hk.C == 0 || clock) e.CA = 0; else e.ca_pending = true;
                }
            } else if (e.state != REL) { prev = e.state; e.state = REL; koffed = true; }
        }
        if (clock && e.pending) { load(e, st); keyed = true; e.pending = false; }
        if (clock && e.ca_pending) { e.CA = 0; e.ca_pending = false; }
        if (out && i == out->a_at_i) { out->a_at = e.off ? 0x3FF : e.a; out->state_at = koffed ? prev : e.state; }
        if (clock && !e.off && e.playing && !keyed && !(koffed && h.K == 3)) {
            int rs = koffed ? (h.K == 0 ? REL : prev) : e.state;
            int rate = rs == ATT ? st.AR : rs == D1 ? st.D1R : rs == D2 ? st.D2R : st.RR;
            uint32_t inc = eg_increment(eff_rate(st, rate), cnt, (uint32_t)-1);
            bool was_d1 = e.state == D1;
            if (inc) {
                bool attf = (!koffed && e.state == ATT) || (koffed && h.K == 2 && prev == ATT);
                if (attf) {
                    e.a += ((~e.a) * (int)inc) >> 4;
                    if (e.a <= 0) { e.a = 0; if (e.state == ATT) e.state = D1; }
                } else {
                    e.a += (int)inc;
                    if (e.a > 0x3FF) { e.a = 0x3FF; e.off = true; e.playing = false; e.CA = 0; }
                }
            }
            if (was_d1 && !e.off && (e.a >> 5) == st.DL) e.state = D2;
        }
        int32_t lv = (e.off || !e.playing) ? 0 : level_of(e.a, st.ramp ? ramp_s(e.CA) : 0x7FFF);
        if (out && out->pred) out->pred->push_back(lv);
        if (matched < 0 && lv != x.c.v[(size_t)i * x.c.ns + k]) {
            matched = i - i0;
            if (!(out && out->pred)) return matched;
        }
        if (e.playing && !e.off) e.CA = (e.CA + 1) & 4095; /* pitch 1.0, loop [0, 4096) */
    }
    return matched < 0 ? i1 - i0 : matched;
}
static EG init_state(const Ctx &x, int k, int i) { /* state before sample i from the level of sample i-1 */
    EG e;
    const Stream &st = x.st[k];
    int32_t lv = i > 0 ? x.c.v[(size_t)(i - 1) * x.c.ns + k] : 0;
    if (lv == 0 || st.ramp) return e; /* off (a ramp stream is only started from off) */
    int a = inv_level(lv);
    if (a < 0) return e;
    e.a = a; e.off = false; e.playing = true;
    e.state = st.witness ? D2 : REL; /* witness: rate-0 sustain; test slot: a held release */
    return e;
}
static std::string setstr(const std::vector<int> &v, int ref) {
    std::string s = "{";
    for (size_t i = 0; i < v.size(); i++) { char b[32]; snprintf(b, sizeof b, "%s%+d", i ? "," : "", v[i] - ref); s += b; }
    return s + "}";
}
/* FULL, or the first mismatch relative to a reference sample (ref = its offset from the simulation start) */
static const char *res(int m, int full, int ref = 0, const char *refname = "") { static char b[16][40]; static int n; char *p = b[n++ & 15]; if (m >= full) snprintf(p, 40, "FULL"); else snprintf(p, 40, "fail@%s%+d", refname, m - ref); return p; }
static int find_496_onset(const Ctx &x, int k, int lo, int hi) { /* fresh key-on: 496 (a 0x280) then a rising attack */
    lo = std::max(lo, 1); hi = std::min(hi, (int)x.c.n - 3);
    for (int i = lo; i <= hi; i++) {
        const int32_t *v = &x.c.v[(size_t)i * x.c.ns + k];
        int ns = x.c.ns;
        if (v[0] == 496 && v[-ns] != 496 && (v[ns] == 496 || v[ns] > 496) && (v[2 * ns] > 496)) return i;
    }
    return -1;
}
static int find_full_onset(const Ctx &x, int k, int lo, int hi) { /* witness key-on: jump to 520176 (a = 0), held */
    lo = std::max(lo, 1); hi = std::min(hi, (int)x.c.n - 2);
    for (int i = lo; i <= hi; i++)
        if (x.c.v[(size_t)i * x.c.ns + k] == 520176 && x.c.v[(size_t)(i - 1) * x.c.ns + k] != 520176 && x.c.v[(size_t)(i + 1) * x.c.ns + k] == 520176) return i;
    return -1;
}
static void dump(const Ctx &x, int k, int from, int to, int ref);
static void diag_window(const Ctx &x, int k, int lo, int hi, const char *what) { /* an event was not found: show the first change */
    lo = std::max(lo, 1); hi = std::min(hi, (int)x.c.n - 1);
    int ch = -1;
    for (int i = lo; i <= hi && ch < 0; i++) if (x.c.v[(size_t)i * x.c.ns + k] != x.c.v[(size_t)(i - 1) * x.c.ns + k]) ch = i;
    if (ch < 0) { printf("      %s: stream %d constant (%d) over samples %d..%d\n", what, k, x.c.v[(size_t)lo * x.c.ns + k], lo, hi); return; }
    printf("      %s: stream %d first changes at %d (%s):\n", what, k, ch, par(x, ch));
    dump(x, k, ch - 3, ch + 8, ch);
}
static void first_mismatch(const Ctx &x, int k, int i0, const std::vector<int32_t> &pred, int m, const char *tag) {
    if (m >= (int)pred.size()) return;
    int i = i0 + m;
    printf("        %s first mismatch at %d (%+d, %s): captured %d, predicted %d\n", tag, i, m, par(x, i), x.c.v[(size_t)i * x.c.ns + k], pred[m]);
}
static void dump(const Ctx &x, int k, int from, int to, int ref) {
    printf("      s%d levels from %+d:", k, from - ref);
    for (int i = std::max(from, 0); i <= to && i < (int)x.c.n; i++) {
        int32_t lv = x.c.v[(size_t)i * x.c.ns + k];
        int a = x.st[k].ramp ? -2 : inv_level(lv), ah = x.st[k].ramp ? -2 : inv_level_hi(lv);
        if (a == -2) printf(" %d%s", lv, is_clock(x, i) ? "*" : "");
        else if (a < 0) printf(" %d(off)%s", lv, is_clock(x, i) ? "*" : "");
        else if (a == ah) printf(" %d(a %03x)%s", lv, a, is_clock(x, i) ? "*" : "");
        else printf(" %d(a %03x..%03x)%s", lv, a, ah, is_clock(x, i) ? "*" : "");
    }
    printf("   (* = clock sample)\n");
}

struct Tally { int rep[4] = {0}, inf[4] = {0}, none[4] = {0}; };   /* per hypothesis: reproduced / informative stream-cycles; none: no hypothesis fits */

/* ---- key-on / key-off cycles (koff_d2, koff_d2b, koff_att) ---- */
static void regen(Ctx &x, int k, int i0, int i1, const std::vector<Ev> &evs, Hyp h) { /* -synth: replace the captured stream by the simulation */
    EG e0 = init_state(x, k, i0);
    std::vector<int32_t> pred;
    SimOut o; o.pred = &pred;
    simulate(x, k, e0, i0, i1, evs, h, &o);
    for (int i = i0; i < i1; i++) x.c.v[(size_t)i * x.c.ns + k] = pred[i - i0];
}
static void koff_run(Ctx &x, Tally &T, Tally &TW, int &ncyc_out) {
    const int W = 300;
    std::vector<int> mA, mB;
    for (auto &m : x.marks) { if (m.first == 1) mA.push_back(m.second); if (m.first == 3) mB.push_back(m.second); }
    int ncyc = (int)std::min(mA.size(), mB.size());
    std::vector<int> EA(ncyc, -1), EB(ncyc, -1);
    for (int c = 0; c < ncyc; c++) {
        EA[c] = find_496_onset(x, 0, mA[c] - W, mA[c] + W);
        if (EA[c] < 0) continue;
        EB[c] = find_full_onset(x, 3, std::max(EA[c] + 1, mB[c] - W), mB[c] + W);
    }
    ncyc_out = ncyc;
    if (x.synthK >= 0 || x.synthL >= 0) {   /* self-test: regenerate every stream from the pinned events under the synthetic rule */
        Hyp hs; hs.K = x.synthK >= 0 ? x.synthK : 1; hs.L = x.synthL >= 0 ? x.synthL : 0;
        for (int c = 0; c < ncyc; c++) {
            if (EA[c] < 0 || EB[c] < 0) continue;
            int end = c + 1 < ncyc && EA[c + 1] > 0 ? EA[c + 1] : (int)x.c.n;
            for (int k = 0; k < 4; k++) {
                std::vector<Ev> evs = k < 3 ? std::vector<Ev>{{EA[c], true}, {EB[c], false}} : std::vector<Ev>{{EA[c], false}, {EB[c], true}};
                regen(x, k, EA[c], end, evs, hs);
            }
        }
        printf("SYNTHETIC: streams regenerated under H%d / L%d from the pinned events (self-test of the fitter)\n", hs.K, hs.L);
    }
    int nEBodd = 0, nEBeven = 0;
    for (int c = 0; c < ncyc; c++) {
        if (EA[c] < 0 || EB[c] < 0) {
            printf("cycle %2d: key-on %s / key-off %s NOT FOUND (marks %d %d)\n", c, EA[c] < 0 ? "496 onset" : "found", EB[c] < 0 ? "witness onset" : "found", mA[c], mB[c]);
            if (EA[c] < 0) diag_window(x, 0, mA[c] - W, mA[c] + W, "key-on window");
            else diag_window(x, 3, std::max(EA[c] + 1, mB[c] - W), mB[c] + W, "key-off window");
            continue;
        }
        int end = c + 1 < ncyc && EA[c + 1] > 0 ? EA[c + 1] : (int)x.c.n;
        int ea = EA[c], eb = EB[c];
        if (is_clock(x, eb)) nEBeven++; else nEBodd++;
        printf("cycle %2d: key-on E_A %d (%s, marks %d..%d) key-off E_B %d (%s, on %d samples, marks %d..%d)\n", c, ea, par(x, ea), mA[c], mB[c] - 1, eb, par(x, eb), eb - ea, mB[c], end - 1);
        /* the other test streams must show the fresh key-on on E_A too */
        for (int k = 1; k < 3; k++) {
            int32_t lv = x.c.v[(size_t)ea * x.c.ns + k];
            if (lv != 496) printf("    s%d: level %d on E_A (not 496: a %d)%s\n", k, lv, inv_level(lv), x.st[k].RR == 0 ? " -- held release slot (Q3)" : "");
        }
        /* witness: first release step after its key-off on E_A (rate-0 segment -> H0 vs the rest) */
        if (c > 0 && x.c.v[(size_t)(ea - 1) * x.c.ns + 3] == 520176) {
            int d = ea; while (d < eb && x.c.v[(size_t)d * x.c.ns + 3] == 520176) d++;
            int pH0 = is_clock(x, ea) ? 0 : 1, pH1 = is_clock(x, ea) ? 2 : 1;
            printf("    W : key-off on E_A (%s), first release step at E_A%+d (H0 predicts +%d, H1/H2/H3 +%d)%s\n", par(x, ea), d - ea, pH0, pH1,
                   is_clock(x, ea) ? (d - ea == pH0 ? "  -> H0" : d - ea == pH1 ? "  -> H1/H2/H3" : "  -> NEITHER") : "  (odd: uninformative)");
            EG e0 = init_state(x, 3, ea);
            std::vector<Ev> evs = {{ea, false}, {eb, true}};
            std::vector<std::vector<int32_t>> preds(4);
            int m[4];
            for (int h = 0; h < 4; h++) { Hyp hy; hy.K = h; SimOut o; o.pred = &preds[h]; m[h] = simulate(x, 3, e0, ea, end, evs, hy, &o); }
            bool ident = preds[0] == preds[1] && preds[0] == preds[2] && preds[0] == preds[3];
            bool inf = is_clock(x, ea) && !ident;
            printf("    W : H0 %s H1 %s H2 %s H3 %s%s\n", res(m[0], end - ea, 0, "E_A"), res(m[1], end - ea, 0, "E_A"), res(m[2], end - ea, 0, "E_A"), res(m[3], end - ea, 0, "E_A"), inf ? "" : "  (uninformative)");
            for (int h = 0; h < 4; h++) if (inf) { TW.inf[h]++; TW.rep[h] += m[h] >= end - ea; }
            bool none = m[0] < end - ea && m[1] < end - ea && m[2] < end - ea && m[3] < end - ea;
            if (x.verbose || none) { for (int h = 0; h < 4; h++) { char t[8]; snprintf(t, sizeof t, "H%d", h); first_mismatch(x, 3, ea, preds[h], m[h], t); } dump(x, 3, ea - 2, ea + 6, ea); }
        }
        for (int k = 0; k < 3; k++) {
            const Stream &st = x.st[k];
            EG e0 = init_state(x, k, ea);
            std::vector<Ev> evs = {{ea, true}, {eb, false}};
            std::vector<std::vector<int32_t>> preds(4);
            int m[4]; int a_before = -1, st_before = -1;
            for (int h = 0; h < 4; h++) {
                Hyp hy; hy.K = h;
                SimOut o; o.pred = &preds[h]; o.a_at_i = eb;
                m[h] = simulate(x, k, e0, ea, end, evs, hy, &o);
                if (h == 1) { a_before = o.a_at; st_before = o.state_at; }
            }
            /* which L reproduces the key-on segment (matters for the held-release slot) */
            int Lok = -1;
            for (int L = 0; L < 3 && Lok < 0; L++) { Hyp hy; hy.K = 1; hy.L = L; if (simulate(x, k, e0, ea, eb, evs, hy) >= eb - ea) Lok = L; }
            uint32_t cnt = (x.K - (mdct(x, eb) >> 1)) & 0x3FFF;
            int rate_prev = st_before == ATT ? st.AR : st_before == D1 ? st.D1R : st_before == D2 ? st.D2R : st.RR;
            int inc_prev = (int)eg_increment(eff_rate(st, rate_prev), cnt, (uint32_t)-1), inc_rel = (int)eg_increment(eff_rate(st, st.RR), cnt, (uint32_t)-1);
            bool ident = preds[0] == preds[1] && preds[0] == preds[2] && preds[0] == preds[3];
            bool inf = is_clock(x, eb) && !ident;
            /* free key-off search: which samples near E_B reproduce under each hypothesis (shows the +-1 ambiguity) */
            std::vector<int> ks[4];
            for (int h = 0; h < 4; h++)
                for (int ko = eb - 3; ko <= eb + 3; ko++) {
                    if (ko <= ea) continue;
                    Hyp hy; hy.K = h;
                    std::vector<Ev> e2 = {{ea, true}, {ko, false}};
                    if (simulate(x, k, e0, ea, end, e2, hy) >= end - ea) ks[h].push_back(ko);
                }
            printf("    s%d: %s at E_B a 0x%03x, inc_prev %d inc_rel %d, key-on %s:  H0 %s  H1 %s  H2 %s  H3 %s%s   key-off samples reproducing: H0%s H1%s H2%s H3%s\n",
                   k, sname(st_before), a_before, inc_prev, inc_rel, Lok < 0 ? "no L fits" : Lok == 0 ? "L0" : Lok == 1 ? "L1" : "L2",
                   res(m[0], end - ea, eb - ea, "E_B"), res(m[1], end - ea, eb - ea, "E_B"), res(m[2], end - ea, eb - ea, "E_B"), res(m[3], end - ea, eb - ea, "E_B"),
                   inf ? "" : (is_clock(x, eb) ? "  (uninformative: identical predictions)" : "  (odd key-off: uninformative)"),
                   setstr(ks[0], eb).c_str(), setstr(ks[1], eb).c_str(), setstr(ks[2], eb).c_str(), setstr(ks[3], eb).c_str());
            if (inf) for (int h = 0; h < 4; h++) { T.inf[h]++; T.rep[h] += m[h] >= end - ea; }
            /* observed step on E_B from the decoded attenuation */
            if (inf) {
                int a0 = inv_level(x.c.v[(size_t)(eb - 1) * x.c.ns + k]), a1 = inv_level(x.c.v[(size_t)eb * x.c.ns + k]), a1h = inv_level_hi(x.c.v[(size_t)eb * x.c.ns + k]);
                int a2 = inv_level(x.c.v[(size_t)(eb + 2) * x.c.ns + k]);
                if (a0 >= 0 && a1 >= 0) printf("        observed: a %03x -> %03x%s on E_B (%+d), then %03x on E_B+2\n", a0, a1, a1 != a1h ? "(ambiguous)" : "", a1 - a0, a2);
            }
            bool none = m[1] < end - ea && m[0] < end - ea && m[2] < end - ea && m[3] < end - ea;
            if (x.verbose || none) { for (int h = 0; h < 4; h++) { char t[8]; snprintf(t, sizeof t, "H%d", h); first_mismatch(x, k, ea, preds[h], m[h], t); } dump(x, k, eb - 3, eb + 6, eb); }
            if (none) for (int h = 0; h < 4; h++) T.none[h]++;
        }
    }
    printf("witness key-off samples (E_B) on even MDEC_CT: %d, odd: %d\n", nEBeven, nEBodd);
}

/* ---- kon_rel: on, off, on during the release (pinned by the witness), off ---- */
static void kon_run(Ctx &x, Tally &TL, Tally &TC, int &ncyc_out) {
    const int W = 300;
    std::vector<int> mA, mB, mC, mD;
    for (auto &m : x.marks) { if (m.first == 1) mA.push_back(m.second); if (m.first == 3) mB.push_back(m.second); if (m.first == 5) mC.push_back(m.second); if (m.first == 7) mD.push_back(m.second); }
    int ncyc = (int)std::min(std::min(mA.size(), mB.size()), std::min(mC.size(), mD.size()));
    ncyc_out = ncyc;
    std::vector<int> EA(ncyc, -1), EC(ncyc, -1);
    for (int c = 0; c < ncyc; c++) {
        EA[c] = find_496_onset(x, 0, mA[c] - W, mA[c] + W);
        if (EA[c] < 0) continue;
        EC[c] = find_full_onset(x, 3, std::max(EA[c] + 1, mC[c] - W), mC[c] + W);
    }
    int nECodd = 0, nECeven = 0;
    for (int c = 0; c < ncyc; c++) {
        if (EA[c] < 0 || EC[c] < 0) {
            printf("cycle %2d: key-on %s / second key-on %s NOT FOUND (marks %d %d %d %d)\n", c, EA[c] < 0 ? "496 onset" : "found", EC[c] < 0 ? "witness onset" : "found", mA[c], mB[c], mC[c], mD[c]);
            if (EA[c] < 0) diag_window(x, 0, mA[c] - W, mA[c] + W, "key-on window");
            else { diag_window(x, 3, std::max(EA[c] + 1, mC[c] - W), mC[c] + W, "second key-on window (witness)"); diag_window(x, 0, std::max(EA[c] + 1, mC[c] - W), mC[c] + W, "second key-on window (stream 0)"); }
            continue;
        }
        int ea = EA[c], ec = EC[c];
        int end = c + 1 < ncyc && EA[c + 1] > 0 ? EA[c + 1] : (int)x.c.n;
        if (is_clock(x, ec)) nECeven++; else nECodd++;
        /* first key-off E_B: not pinned; the samples in the mark window that reproduce stream 0 up to E_C - 1 under
         * H1 (the +-1 ambiguity of a key-off from the level alone) */
        EG e0 = init_state(x, 0, ea);
        std::vector<int> ebs[2];
        for (int h = 0; h < 2; h++)
            for (int ko = std::max(ea + 1, mB[c] - W); ko <= std::min(mB[c] + W, ec - 1); ko++) {
                Hyp hy; hy.K = h;
                std::vector<Ev> evs = {{ea, true}, {ko, false}};
                if (simulate(x, 0, e0, ea, ec, evs, hy) >= ec - ea) ebs[h].push_back(ko);
            }
        if (ebs[1].empty()) {
            int best = -1, bm = -1;
            for (int ko = std::max(ea + 1, mB[c] - W); ko <= std::min(mB[c] + W, ec - 1); ko++) { Hyp hy; std::vector<Ev> evs = {{ea, true}, {ko, false}}; int m = simulate(x, 0, e0, ea, ec, evs, hy); if (m > bm) { bm = m; best = ko; } }
            printf("cycle %2d: E_A %d (%s), E_C %d (%s): no key-off sample reproduces stream 0 up to E_C under H1 (best %d with key-off %d)\n", c, ea, par(x, ea), ec, par(x, ec), bm, best);
            dump(x, 0, best - 3, best + 6, best);
            continue;
        }
        int eb = ebs[1][0];
        /* the a at E_C - 1 (release level) for the report */
        SimOut o; o.a_at_i = ec - 1; { Hyp hy; std::vector<Ev> evs = {{ea, true}, {eb, false}}; simulate(x, 0, e0, ea, ec, evs, hy, &o); }
        printf("cycle %2d: E_A %d (%s); first key-off (not pinned) reproducing stream 0: H1 at %d%s, H0 at %d%s; second key-on E_C %d (%s, %d samples after, release a 0x%03x); marks %d %d %d %d\n",
               c, ea, par(x, ea), eb, setstr(ebs[1], eb).c_str(), ebs[0].empty() ? -1 : ebs[0][0], ebs[0].empty() ? "" : setstr(ebs[0], ebs[0][0]).c_str(), ec, par(x, ec), ec - eb, o.a_at, mA[c], mB[c], mC[c], mD[c]);
        /* second key-off E_D: searched under H1 with L0/C0 on stream 0 so the cycle can be checked to its end */
        std::vector<int> eds;
        for (int ko = std::max(ec + 1, mD[c] - W); ko <= std::min(mD[c] + W, end - 1); ko++) {
            Hyp hy;
            std::vector<Ev> evs = {{ea, true}, {eb, false}, {ec, true}, {ko, false}};
            if (simulate(x, 0, e0, ea, end, evs, hy) >= end - ea) eds.push_back(ko);
        }
        int ed = eds.empty() ? -1 : eds[0];
        if (x.synthL >= 0 || x.synthC >= 0) {   /* self-test: regenerate the test streams under the synthetic key-on rule */
            Hyp hs; hs.K = 1; hs.L = x.synthL >= 0 ? x.synthL : 0; hs.C = x.synthC >= 0 ? x.synthC : 0;
            std::vector<Ev> evs = {{ea, true}, {eb, false}, {ec, true}};
            if (ed > 0) evs.push_back({ed, false});
            for (int k = 0; k < 3; k++) regen(x, k, ea, end, evs, hs);
            if (c == 0) printf("SYNTHETIC: test streams regenerated under L%d / C%d from the pinned events (self-test of the fitter)\n", hs.L, hs.C);
        }
        for (int k = 0; k < 3; k++) {
            const Stream &st = x.st[k];
            EG ek = init_state(x, k, ea);
            std::vector<Ev> evs = {{ea, true}, {eb, false}, {ec, true}};
            if (ed > 0) evs.push_back({ed, false});
            int lim = ed > 0 ? end : (int)std::min<int>(end, mD[c] - W); /* without E_D compare up to the E_D window */
            int nC = st.ramp ? 2 : 1;
            std::vector<std::vector<int32_t>> preds(6);
            int m[6];
            for (int L = 0; L < 3; L++)
                for (int C = 0; C < nC; C++) {
                    Hyp hy; hy.K = 1; hy.L = L; hy.C = C;
                    SimOut so; so.pred = &preds[L * 2 + C];
                    m[L * 2 + C] = simulate(x, k, ek, ea, lim, evs, hy, &so);
                }
            /* the L verdict per stream uses the best CA rule for each L (ramp streams), so the two questions stay separate */
            int mL[3], bestCofL[3];
            for (int L = 0; L < 3; L++) { mL[L] = m[L * 2]; bestCofL[L] = 0; if (nC > 1 && m[L * 2 + 1] > mL[L]) { mL[L] = m[L * 2 + 1]; bestCofL[L] = 1; } }
            bool identL = preds[0] == preds[2] && preds[0] == preds[4];
            printf("    s%d (%s%s): pre-E_C %s;  L0 %s  L1 %s  L2 %s%s", k, st.ramp ? "ramp" : "const", st.AR == 31 ? " R62" : " R48",
                   res(std::min(m[0], ec - ea), ec - ea, ec - ea, "E_C"), res(mL[0], lim - ea, ec - ea, "E_C"), res(mL[1], lim - ea, ec - ea, "E_C"), res(mL[2], lim - ea, ec - ea, "E_C"), identL ? "  (uninformative)" : "");
            if (!identL) for (int L = 0; L < 3; L++) { TL.inf[L]++; TL.rep[L] += mL[L] >= lim - ea; }
            int bestL = 0; for (int L = 0; L < 3; L++) if (mL[L] > mL[bestL]) bestL = L;
            if (st.ramp) {
                /* CA hypotheses under the best L */
                bool identC = preds[bestL * 2] == preds[bestL * 2 + 1];
                printf("  | CA (under L%d): C0 %s  C1 %s%s", bestL, res(m[bestL * 2], lim - ea, ec - ea, "E_C"), res(m[bestL * 2 + 1], lim - ea, ec - ea, "E_C"), identC ? "  (even E_C: uninformative)" : "");
                if (!identC) for (int C = 0; C < 2; C++) { TC.inf[C]++; TC.rep[C] += m[bestL * 2 + C] >= lim - ea; }
            }
            printf("%s\n", ed < 0 ? "  [E_D not fitted: compared up to the E_D window]" : "");
            bool none = mL[0] < lim - ea && mL[1] < lim - ea && mL[2] < lim - ea;
            (void)bestCofL;
            if (x.verbose || none) {
                for (int L = 0; L < 3; L++) for (int C = 0; C < nC; C++) { char t[8]; snprintf(t, sizeof t, "L%dC%d", L, C); first_mismatch(x, k, ea, preds[L * 2 + C], m[L * 2 + C], t); }
                dump(x, k, ec - 3, ec + 5, ec);
            }
            if (none) for (int L = 0; L < 3; L++) TL.none[L]++;
        }
    }
    printf("second key-on samples (E_C) on even MDEC_CT: %d, odd: %d\n", nECeven, nECodd);
}

static bool parse_txt(const char *path, const char *kind, Ctx &x, bool &c0_found) {
    FILE *f = fopen(path, "r");
    if (!f) return false;
    char line[512];
    bool in_run = false;
    while (fgets(line, sizeof line, f)) {
        char name[64]; int k, slot, AR, D1R, DL, D2R, RR, KRS, OCT, FNS, ramp; char role[16];
        if (sscanf(line, "%63s stream %d: slot %d AR %d D1R %d DL %d D2R %d RR %d KRS %d OCT %d FNS %x ramp %d role %15s",
                   name, &k, &slot, &AR, &D1R, &DL, &D2R, &RR, &KRS, &OCT, &FNS, &ramp, role) == 13) {
            if (strcmp(name, kind)) { in_run = false; continue; }
            in_run = true;
            if (k >= (int)x.st.size()) x.st.resize(k + 1);
            Stream &s = x.st[k];
            s.slot = slot; s.AR = AR; s.D1R = D1R; s.DL = DL; s.D2R = D2R; s.RR = RR; s.KRS = KRS; s.OCT = OCT; s.FNS = FNS; s.ramp = ramp != 0; s.witness = !strcmp(role, "witness");
            continue;
        }
        /* the run ends at its "<run>: N samples" line, so a later run's cap_start line is never taken */
        if (in_run && !strncmp(line, kind, strlen(kind)) && line[strlen(kind)] == ':') { in_run = false; continue; }
        /* ", c0 " (with the comma): the bare "c0 " also matches inside the ring address ("at a9c0 (n 679)" in the hw koff_d2b run) */
        unsigned c0;
        const char *p = strstr(line, ", c0 ");
        if (in_run && !c0_found && strstr(line, "cap_start:") && p && sscanf(p, ", c0 %x", &c0) == 1) { x.c0 = c0; c0_found = true; }
    }
    fclose(f);
    return !x.st.empty();
}

int main(int argc, char **argv) {
    if (argc < 5) { fprintf(stderr, "usage: koff_fit <capture prefix> <c0 hex> <K> <koff_d2|koff_d2b|koff_d1|koff_att|kon_rel> [aeg_koff.txt] [-v] [-synth H0|H2|H3|L1|L2|C1]\n"); return 2; }
    Ctx x;
    std::string prefix = argv[1];
    x.c0 = strtoul(argv[2], nullptr, 16);
    x.K = (uint32_t)atol(argv[3]);
    std::string kind = argv[4];
    const char *txt = nullptr;
    for (int i = 5; i < argc; i++) {
        if (!strcmp(argv[i], "-v")) x.verbose = true;
        else if (!strcmp(argv[i], "-synth") && i + 1 < argc) {   /* -synth H0|H1|H2|H3|L0|L1|L2|C0|C1 (combinable, repeat the option) */
            const char *s = argv[++i];
            int v = atoi(s + 1);
            if (s[0] == 'H') x.synthK = v; else if (s[0] == 'L') x.synthL = v; else if (s[0] == 'C') x.synthC = v;
            else { fprintf(stderr, "bad -synth %s\n", s); return 2; }
        } else txt = argv[i];
    }
    /* stream table = cases/aeg_koff.c */
    auto S = [](int AR, int D1R, int DL, int D2R, int RR, int KRS = 15, int OCT = 0, int FNS = 0, bool ramp = false, bool wit = false) {
        Stream s; s.AR = AR; s.D1R = D1R; s.DL = DL; s.D2R = D2R; s.RR = RR; s.KRS = KRS; s.OCT = OCT; s.FNS = FNS; s.ramp = ramp; s.witness = wit; return s; };
    Stream wit = S(31, 31, 31, 31, 31, 1, 0, 0, false, true);   /* KRS 1: R 63 at pitch 1.0; decays to off by itself */
    if (kind == "koff_d2") x.st = {S(31, 31, 2, 26, 24), S(31, 31, 2, 24, 28), S(31, 31, 2, 28, 0), wit};
    else if (kind == "koff_d2b") x.st = {S(31, 31, 2, 30, 24), S(31, 31, 2, 26, 30), S(31, 31, 2, 0, 26), wit};
    else if (kind == "koff_d1") x.st = {S(31, 26, 31, 0, 24), S(31, 24, 31, 0, 28), S(31, 28, 31, 0, 26), wit};   /* decay 1 +2/+1/+4 vs release +1/+4/+2 */
    else if (kind == "koff_att") x.st = {S(24, 0, 0, 0, 28), S(26, 0, 0, 0, 24), S(28, 0, 0, 0, 26), wit};
    else if (kind == "kon_rel") x.st = {S(24, 0, 0, 0, 24), S(24, 0, 0, 0, 24, 15, 0, 0, true), S(31, 0, 0, 0, 24, 15, 0, 0, true), wit};
    else { fprintf(stderr, "unknown kind %s\n", kind.c_str()); return 2; }
    bool c0_from_txt = false;
    if (txt) {
        std::vector<Stream> keep = x.st;
        x.st.clear();
        if (!parse_txt(txt, kind.c_str(), x, c0_from_txt)) { fprintf(stderr, "no stream lines for %s in %s\n", kind.c_str(), txt); return 2; }
        if (x.st.size() != 4) { fprintf(stderr, "expected 4 streams\n"); return 2; }
        if (strtoul(argv[2], nullptr, 16) != 0) x.c0 = strtoul(argv[2], nullptr, 16);   /* c0 argument wins; pass 0 to take it from the file */
    }
    x.c = cap(prefix);
    {   /* marks */
        FILE *f = fopen((prefix + ".hdr").c_str(), "rb");
        uint32_t h[16 + 2 * 64] = {0};
        size_t nh = f ? fread(h, 4, sizeof h / 4, f) : 0;
        if (f) fclose(f);
        for (size_t i = 11; i + 1 < nh && i < 11 + 2 * h[6]; i += 2) x.marks.push_back({(int)h[i], (int)(h[i + 1] - x.c.first)});
    }
    printf("%s (%s): %u samples x %u streams, n_first %u, c0 %04x -> MDEC_CT of sample i = (%04x - i) & 0xFFFF, K %u, %zu marks\n", prefix.c_str(), kind.c_str(), x.c.n, x.c.ns, x.c.first, x.c0, (x.c0 - x.c.first) & 0xFFFF, x.K, x.marks.size());
    for (size_t k = 0; k < x.st.size(); k++) {
        const Stream &s = x.st[k];
        printf("  stream %zu: AR %d D1R %d DL %d D2R %d RR %d KRS %d OCT %d FNS %03x%s%s  (R %u/%u/%u/%u, clock increments %u/%u/%u/%u)\n", k, s.AR, s.D1R, s.DL, s.D2R, s.RR, s.KRS, s.OCT, s.FNS,
               s.ramp ? " ramp" : "", s.witness ? " WITNESS" : "", eff_rate(s, s.AR), eff_rate(s, s.D1R), eff_rate(s, s.D2R), eff_rate(s, s.RR),
               eg_increment(eff_rate(s, s.AR), 0, (uint32_t)-1), eg_increment(eff_rate(s, s.D1R), 0, (uint32_t)-1), eg_increment(eff_rate(s, s.D2R), 0, (uint32_t)-1), eg_increment(eff_rate(s, s.RR), 0, (uint32_t)-1));
    }
    int ncyc = 0;
    if (kind == "kon_rel") {
        Tally TL, TC;
        kon_run(x, TL, TC, ncyc);
        printf("SUMMARY %s (%d cycles): key-on during release, reproduced / informative stream-cycles:", kind.c_str(), ncyc);
        for (int L = 0; L < 3; L++) printf("  L%d %d/%d", L, TL.rep[L], TL.inf[L]);
        printf("  |  CA restart (ramp streams, odd E_C):  C0 %d/%d  C1 %d/%d", TC.rep[0], TC.inf[0], TC.rep[1], TC.inf[1]);
        printf("  |  stream-cycles no L fits: %d\n", TL.none[0]);
        printf("  (L0 and L2 coincide on an even E_C, L0 and L1 on an odd one: a hypothesis is refuted when reproduced < informative)\n");
    } else {
        Tally T, TW;
        koff_run(x, T, TW, ncyc);
        printf("SUMMARY %s (%d cycles): key-off on a clock, reproduced / informative stream-cycles:", kind.c_str(), ncyc);
        for (int h = 0; h < 4; h++) printf("  H%d %d/%d", h, T.rep[h], T.inf[h]);
        if (TW.inf[0]) { printf("  |  witness still sounding at its key-off (rate-0 segment):"); for (int h = 0; h < 4; h++) printf("  H%d %d/%d", h, TW.rep[h], TW.inf[h]); }
        printf("  |  stream-cycles no H fits: %d\n", T.none[0]);
        printf("  (H1 = H2 for a key-off from a decay; H1 = H3 when inc_prev = 0; H0 = H3 when inc_rel = 0: a hypothesis is refuted when reproduced < informative)\n");
    }
    return 0;
}
