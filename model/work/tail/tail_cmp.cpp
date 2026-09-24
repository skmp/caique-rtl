// tail_cmp.cpp -- replay one tests/slot_tail run (cases/slot_tail.c) through the production AicaModel with the
// envelope clock locked to the capture's ring position (NOTES "Envelope clock"), the key-off and the later register
// writes searched inside their mark windows, and compare every sample of every stream.  Template: tools/eg_model.cpp.
//   tail_cmp [-v] <capture prefix> <c0 hex> <K> <kind a|b|c>
//     e.g. tail_cmp tests/slot_tail/model/tail_a e415 6491 a
// c0 is the "cap_start: ... c0 XXXX" value of that run in slot_tail.txt, K the boot constant (6491 for the model).
// input.bin / input2.bin are read from the capture's directory (input2 regenerated from seed 4243 if missing).
// The MDEC_CT of capture sample i is (c0 - n_first - i) & 0xFFFF.  The model is configured exactly as the case, run
// 16 samples, given the onset sample's MDEC_CT, keyed on (KYONB x4 + KYONEX: the next model sample is the onset,
// found as the first non-zero sample of the reference stream 3).  Then, stage by stage, an event sample is searched
// from a snapshot for the longest agreement of the affected streams (0..2), ties listed:
//   stage 0  the filter state each slot 0..2 (LPOFF 0) inherits from the previous program (never reset): with VOFF 1
//            the last pre-onset sample pins low (MIXS = -2 low) and the band is searched in [-65536, 65535]; with VOFF 0
//            low in [-16, 16] x band in [-64, 64]; scored on that stream alone up to the key-off window
//   stage 1  key-off of slots 0..2, ko in [mark3 - 100, mark4 + 400]         (kind c: compared up to mark5 - 100)
//   stage 2  (kind c) reg 0x14 = 0x001F at w00 - d (d 0/1) and reg 0x00 = 0 + KYONEX at w00, w00 in [mark5 - 100,
//            mark6 + 400], compared up to mark7 - 100 (only slot 0 still plays: its SA switch to wave RAM 0 is
//            visible at once, its AEG off 128 clocks after the RR rewrite)
//   stage 3  (kind c) reg 0x20 = 0 (IMXL 0) on slots 0..2 at w2 in [mark7 - 100, mark8 + 400], to the end (the bus
//            then retains its last value; unobservable when the tail is already at rest -- ties are expected)
// Output per stream: matched prefix / total, first mismatch (sample, MDEC_CT parity, hw vs model, model slot state),
// number of mismatching samples, the console's last 24 distinct values (index of first appearance) and the model's,
// the model's "off" sample per slot and the values it predicts after it.  Exit 0 when every stream is FULL.
// Build: g++ -O2 -std=c++17 -o build/work/tail_cmp work/tail/tail_cmp.cpp sample-model/aica_model.cpp   (from model/)
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include "../../sample-model/aica_model.h"
#include "../../tools/filt_capture.h"
using namespace caique;

struct SlotCfg { int slot, ISEL, VOFF, LPOFF, Q; int FLV[5]; int FAR, FD1R, FD2R, FRR, KRS, OCT, FNS, AR, RR; };
static std::vector<SlotCfg> configs(char kind) {
    std::vector<SlotCfg> v;
    auto mk = [&](int k, int VOFF, int LPOFF, int Q, const int flv[5], int FAR, int FD1R, int FD2R, int FRR, int KRS, int OCT, int FNS, int AR, int RR) {
        SlotCfg c{k, k, VOFF, LPOFF, Q, {flv[0], flv[1], flv[2], flv[3], flv[4]}, FAR, FD1R, FD2R, FRR, KRS, OCT, FNS, AR, RR};
        v.push_back(c);
    };
    const int f1b00[5] = {0x1B00, 0x1B00, 0x1B00, 0x1B00, 0x1B00}, f1ffe[5] = {0x1FFE, 0x1FFE, 0x1FFE, 0x1FFE, 0x1FFE};
    const int progA[5] = {0x1800, 0x1C00, 0x1800, 0x1A00, 0x1C00};
    switch (kind) {
    case 'a':
        mk(0, 1, 0, 4, f1b00, 0, 0, 0, 0, 15, 0, 0, 31, 31);
        mk(1, 1, 0, 4, f1b00, 0, 0, 0, 0, 15, 0, 0, 31, 24);
        mk(2, 1, 1, 4, f1b00, 0, 0, 0, 0, 15, 0, 0, 31, 31);
        mk(3, 1, 1, 4, f1ffe, 0, 0, 0, 0, 15, 0, 0, 31, 0);
        break;
    case 'b':
        mk(0, 0, 0, 4, f1b00, 0, 0, 0, 0, 15, 0, 0, 31, 31);
        mk(1, 0, 1, 4, f1b00, 0, 0, 0, 0, 15, 0, 0, 31, 31);
        mk(2, 1, 0, 0, f1ffe, 0, 0, 0, 0, 15, 0, 0, 31, 31);
        mk(3, 1, 1, 4, f1ffe, 0, 0, 0, 0, 15, 0, 0, 31, 0);
        break;
    default:
        mk(0, 1, 0, 4, progA, 24, 26, 28, 22, 0, 0, 0x200, 31, 0);
        mk(1, 1, 0, 4, progA, 24, 26, 28, 22, 0, 0, 0x200, 31, 31);
        mk(2, 1, 0, 4, progA, 24, 26, 28, 22, 0, 0, 0x200, 31, 24);
        mk(3, 1, 1, 4, f1ffe, 0, 0, 0, 0, 15, 0, 0x200, 31, 0);
        break;
    }
    return v;
}
static void write_slot(AicaModel &m, const SlotCfg &c) {
    int ch = c.slot;
    auto aw = [&](uint32_t o, uint32_t v) { m.write(0x80 * ch + o, v); };
    const uint32_t SA = 0x20000, LSA = 0, LEA = 8192, LPCTL = 1;
    aw(0x04, SA & 0xFFFF); aw(0x08, LSA); aw(0x0C, LEA);
    aw(0x10, (0 << 11) | (0 << 6) | c.AR);                  /* D2R 0, D1R 0 */
    aw(0x14, (0 << 14) | (c.KRS << 10) | (0 << 5) | c.RR);  /* LPSLNK 0, DL 0 */
    aw(0x18, (c.OCT << 11) | c.FNS);
    aw(0x1C, 0);
    aw(0x20, (15 << 4) | c.ISEL);
    aw(0x24, 0);
    aw(0x28, (0 << 8) | (c.VOFF << 6) | (c.LPOFF << 5) | c.Q);
    for (int i = 0; i < 5; i++) aw(0x2C + 4 * i, c.FLV[i]);
    aw(0x40, (c.FAR << 8) | c.FD1R);
    aw(0x44, (c.FD2R << 8) | c.FRR);
    aw(0x00, (LPCTL << 9) | ((SA >> 16) & 0x7F));
}
static void load_ram(AicaModel &m, const std::vector<int16_t> &sig, uint32_t at) {
    for (size_t i = 0; i < sig.size(); i++) { m.ram[at + 2 * i] = (uint8_t)sig[i]; m.ram[at + 2 * i + 1] = (uint8_t)(sig[i] >> 8); }
}

/* snapshot of the model (RAM is never written here: no DSP program, no CPU RAM writes after the setup) */
struct Snap { std::vector<uint8_t> b; Snap() : b(sizeof(AicaModel)) {} };
static void snap_take(Snap &s, const AicaModel &m) { memcpy(s.b.data(), &m, sizeof m); }
static void snap_restore(AicaModel &m, const Snap &s) { uint8_t *ram = m.ram; memcpy((void *)&m, s.b.data(), sizeof m); m.ram = ram; }

struct Events { int ko = -1, w14 = -1, w00 = -1, w2 = -1; };
static void apply_events(AicaModel &m, const Events &e, int n) {
    if (n == e.ko) {   /* KYONB 0 on slots 0..2, one KYONEX: the release starts on this sample */
        for (int k = 0; k < 3; k++) m.write(0x80 * k, m.chr(k, 0) & 0x3FFF);
        m.write(0, m.chr(0, 0) | 0x8000);
    }
    if (n == e.w14) for (int k = 2; k >= 0; k--) m.write(0x80 * k + 0x14, 0x001F);   /* RR 31, KRS 0, DL 0 */
    if (n == e.w00) { for (int k = 2; k >= 0; k--) m.write(0x80 * k, 0); m.write(0, 0x8000); }   /* KYONB 0, SA 0, LPCTL 0; KYONEX */
    if (n == e.w2) for (int k = 0; k < 3; k++) m.write(0x80 * k + 0x20, 0);   /* IMXL 0 */
}

struct SlotState { int a, off, state, CA, fegv, fegstate, low, band, enabled; };
static SlotState grab(const AicaModel &m, int k) {
    const Slot &s = m.slot[k];
    return SlotState{s.AEG.a, s.AEG.off, s.AEG.state, (int)s.CA, s.FEG.v, s.FEG.state, s.lpf_low, s.lpf_band, s.enabled};
}
static const char *stname(int s) { return s == 0 ? "att" : s == 1 ? "d1" : s == 2 ? "d2" : "rel"; }

struct Ctx {
    const Capture *cp; int ns, n, on; uint32_t c0ring;
    std::vector<int32_t> rec[4];      /* model values, index i - on */
    SlotState mm_state[4];            /* slot state at the stream's first mismatch */
    int off_at[4] = {-1, -1, -1, -1}; /* first sample where the slot's AEG is off after its key-on */
    bool seen_on[4] = {false, false, false, false};
    int ca_zero_at[4] = {-1, -1, -1, -1};
};
static inline uint32_t mdec(const Ctx &c, int i) { return (c.c0ring - c.cp->first - (uint32_t)i) & 0xFFFF; }

/* run samples [lo, end) with the events; first_bad[k] carried (-1 = none yet).  record: keep values/states.
 * early: stop once every affected stream has mismatched (search mode). */
static void run_range(AicaModel &m, Ctx &c, int lo, int end, const Events &e, int first_bad[4], const bool affected[4], bool early, bool record) {
    for (int i = lo; i < end; i++) {
        apply_events(m, e, i);
        m.step();
        bool all_bad = true;
        for (int k = 0; k < 4; k++) {
            int32_t v = m.MIXS[k], hw = c.cp->v[(size_t)i * c.ns + k];
            if (record) {
                c.rec[k].push_back(v);
                if (!m.slot[k].AEG.off) c.seen_on[k] = true;
                if (c.seen_on[k] && c.off_at[k] < 0 && m.slot[k].AEG.off) c.off_at[k] = i;
                if (c.seen_on[k] && c.ca_zero_at[k] < 0 && !m.slot[k].enabled) c.ca_zero_at[k] = i;
            }
            if (first_bad[k] < 0 && v != hw) { first_bad[k] = i; if (record) c.mm_state[k] = grab(m, k); }
            if (affected[k] && first_bad[k] < 0) all_bad = false;
        }
        if (early && all_bad) break;
    }
}
static long score_of(const int first_bad[4], const bool affected[4], int end) {
    long s = 0;
    for (int k = 0; k < 4; k++) if (affected[k]) s += first_bad[k] < 0 ? end : first_bad[k];
    return s;
}
/* search one stage: the model is at sample lo; returns the best event set (ties in `ties`), model left at lo */
static Events search_stage(AicaModel &m, Ctx &c, int lo, int end, const std::vector<Events> &cands, const int first_bad_in[4], const bool affected[4], std::vector<Events> &ties, long &best_score) {
    Snap s; snap_take(s, m);
    Events best; best_score = -1; ties.clear();
    for (const Events &e : cands) {
        snap_restore(m, s);
        int fb[4]; memcpy(fb, first_bad_in, sizeof fb);
        run_range(m, c, lo, end, e, fb, affected, true, false);
        long sc = score_of(fb, affected, end);
        if (sc > best_score) { best_score = sc; best = e; ties.clear(); ties.push_back(e); }
        else if (sc == best_score) ties.push_back(e);
    }
    snap_restore(m, s);
    return best;
}
template <class F> static void print_tail(const char *label, F v, int from, int n) {   /* last 24 distinct runs; v(i) = value of sample i */
    std::vector<std::pair<int, int32_t>> runs;
    int i = n - 1;
    while (i >= from && runs.size() < 24) {
        int32_t val = v(i);
        int start = i;
        while (start - 1 >= from && v(start - 1) == val) start--;
        runs.push_back({start, val});
        i = start - 1;
    }
    std::reverse(runs.begin(), runs.end());
    printf("    %s tail (%zu distinct values, index:value):", label, runs.size());
    for (auto &r : runs) printf(" %d:%d", r.first, r.second);
    printf("\n");
}
static std::string ev_str(const Ctx &c, const Events &e, char kind) {
    char b[160];
    auto par = [&](int i) { return i < 0 ? "-" : (mdec(c, i) & 1) ? "odd" : "even"; };
    if (kind == 'c') snprintf(b, sizeof b, "ko %d(%s) w14 %d(%s) w00 %d(%s) w2 %d(%s)", e.ko, par(e.ko), e.w14, par(e.w14), e.w00, par(e.w00), e.w2, par(e.w2));
    else snprintf(b, sizeof b, "ko %d(%s)", e.ko, par(e.ko));
    return b;
}

int main(int argc, char **argv) {
    bool verbose = false;
    std::vector<std::string> pos;
    for (int i = 1; i < argc; i++) { if (!strcmp(argv[i], "-v")) verbose = true; else pos.push_back(argv[i]); }
    if (pos.size() != 4) { fprintf(stderr, "usage: tail_cmp [-v] <capture prefix> <c0 hex> <K> <kind a|b|c>\n"); return 2; }
    std::string path = pos[0];
    uint32_t c0ring = (uint32_t)strtoul(pos[1].c_str(), nullptr, 16), K = (uint32_t)strtoul(pos[2].c_str(), nullptr, 10);
    char kind = pos[3][0];
    if (kind != 'a' && kind != 'b' && kind != 'c') { fprintf(stderr, "kind must be a, b or c\n"); return 2; }
    std::string dir = path.substr(0, path.find_last_of('/') == std::string::npos ? 0 : path.find_last_of('/') + 1);

    Capture cp = cap(path);
    FILE *hf = fopen((path + ".hdr").c_str(), "rb");
    uint32_t h[300] = {0}; size_t nh = fread(h, 4, 300, hf); fclose(hf);
    int mark[9]; for (int i = 0; i < 9; i++) mark[i] = -1;
    for (size_t i = 11; i + 1 < nh && i < 11 + 2 * h[6]; i += 2) if (h[i] < 9 && mark[h[i]] < 0) mark[h[i]] = (int)(h[i + 1] - cp.first);
    const int n = (int)cp.n, ns = (int)cp.ns;
    if (ns != 4) { fprintf(stderr, "expected 4 streams\n"); return 2; }

    std::vector<int16_t> sig = input(dir + "input.bin"), sig2;
    bool have2 = true;
    try { sig2 = input(dir + "input2.bin"); } catch (...) {
        have2 = false; sig2.resize(8192); uint32_t seed = 4243;
        for (int i = 0; i < 8192; i++) { seed = seed * 1103515245u + 12345u; int16_t s = (int16_t)(seed >> 16); sig2[i] = s ? s : 1; }
    }
    auto cfg = configs(kind);

    /* onset: first non-zero sample of the reference stream 3 (VOFF, plays from the key-on sample) */
    int on = 1;
    while (on < n && !cp.v[(size_t)on * ns + 3]) on++;
    if (on >= n) { fprintf(stderr, "no onset in stream 3\n"); return 2; }
    int pre_nz[4] = {0, 0, 0, 0};
    for (int i = 0; i < on; i++) for (int k = 0; k < 4; k++) pre_nz[k] += cp.v[(size_t)i * ns + k] != 0;

    Ctx c; c.cp = &cp; c.ns = ns; c.n = n; c.on = on; c.c0ring = c0ring;
    AicaModel m;
    m.eg_K = K;
    load_ram(m, sig, 0x20000);
    load_ram(m, sig2, 0x0);
    for (auto &s : cfg) write_slot(m, s);
    for (int i = 0; i < 16; i++) m.step();
    uint32_t md_on = (c0ring - cp.first - (uint32_t)on) & 0xFFFF;
    m.MDEC_CT = md_on;
    for (auto &s : cfg) m.write(0x80 * s.slot, m.chr(s.slot, 0) | 0x4000);
    m.write(0, m.chr(0, 0) | 0x8000);

    printf("%s: kind %c, %d samples x %d streams, n_first %u, c0 %04x, K %u, marks", path.c_str(), kind, n, ns, cp.first, c0ring, K);
    for (int i = 1; i < 9; i++) if (mark[i] >= 0) printf(" %d@%d", i, mark[i]);
    printf("\n  onset %d (MDEC_CT %04x %s), pre-onset non-zero samples per stream %d %d %d %d%s\n", on, md_on, (md_on & 1) ? "odd" : "even",
           pre_nz[0], pre_nz[1], pre_nz[2], pre_nz[3], have2 ? "" : " (input2.bin missing: regenerated from seed 4243)");

    int first_bad[4] = {-1, -1, -1, -1};
    const bool aff012[4] = {true, true, true, false};
    Events ev;
    auto clampw = [&](int lo_, int hi_, int &lo, int &hi) { lo = std::max(on + 1, lo_); hi = std::min(n - 1, hi_); };

    /* stage 1: the key-off */
    int lo1, hi1;
    if (mark[3] < 0 || mark[4] < 0) { lo1 = n; hi1 = n - 1; printf("  no key-off marks: replay without a key-off\n"); }
    else clampw(mark[3] - 100, mark[4] + 400, lo1, hi1);
    int end1 = n;
    if (kind == 'c' && mark[5] >= 0) end1 = std::min(n, std::max(lo1 + 1, mark[5] - 100));

    /* stage 0: the filter state each slot inherits from the previous program (never reset: NOTES "Slot filter").  With
     * VOFF 1 and LPOFF 0 a silent slot outputs -2 * low every sample, so the last pre-onset sample pins the low
     * integrator entering the onset; the band is searched.  With VOFF 0 the pre-onset output is 0 (off slot) and
     * both integrators are searched over small ranges.  Scored on that stream alone up to the key-off window. */
    {
        int end0 = std::min(lo1, n);
        Snap s0; snap_take(s0, m);
        for (int k = 0; k < 3; k++) {
            if (cfg[k].LPOFF) continue;
            std::vector<std::pair<int, int>> cands;   /* (low, band) */
            int32_t vpre = on >= 1 ? cp.v[(size_t)(on - 1) * ns + k] : 0;
            bool pinned = cfg[k].VOFF && on >= 1 && (vpre & 1) == 0 && vpre > -524288 && vpre < 524287;
            if (pinned) for (int b = -65536; b <= 65535; b++) cands.push_back({-vpre / 2, b});
            else for (int l = -16; l <= 16; l++) for (int b = -64; b <= 64; b++) cands.push_back({l, b});
            bool aff[4] = {false, false, false, false}; aff[k] = true;
            long best = -1; std::pair<int, int> bs{0, 0}; int nties = 0;
            for (auto &cand : cands) {
                snap_restore(m, s0);
                m.slot[k].lpf_low = cand.first; m.slot[k].lpf_band = cand.second;
                int fb[4] = {-1, -1, -1, -1};
                run_range(m, c, on, end0, ev, fb, aff, true, false);
                long sc = fb[k] < 0 ? end0 : fb[k];
                if (sc > best) { best = sc; bs = cand; nties = 1; }
                else if (sc == best) nties++;
            }
            snap_restore(m, s0);
            m.slot[k].lpf_low = bs.first; m.slot[k].lpf_band = bs.second;
            snap_take(s0, m);
            printf("  stage 0 slot %d inherited filter state: pre-onset value %d -> low %d%s, band %d (%s to %d, %d tie(s) of %zu)\n", k, vpre, bs.first,
                   pinned ? " (pinned)" : " (searched)", bs.second, best >= end0 ? "matches" : "best prefix", best >= end0 ? end0 : (int)best, nties, cands.size());
        }
        c.rec[0].clear(); c.rec[1].clear(); c.rec[2].clear(); c.rec[3].clear();
    }
    run_range(m, c, on, std::min(lo1, n), ev, first_bad, aff012, false, true);   /* onset .. stage-1 window */
    if (lo1 <= hi1) {
        std::vector<Events> cands;
        for (int ko = lo1; ko <= hi1; ko++) { Events e; e.ko = ko; cands.push_back(e); }
        std::vector<Events> ties; long sc;
        ev = search_stage(m, c, lo1, end1, cands, first_bad, aff012, ties, sc);
        printf("  stage 1 key-off: window [%d, %d], compared to %d: best ko %d (MDEC_CT %04x %s), score %ld/%ld, %zu tie(s):", lo1, hi1, end1, ev.ko, mdec(c, ev.ko),
               (mdec(c, ev.ko) & 1) ? "odd" : "even", sc, 3L * end1, ties.size());
        for (size_t i = 0; i < ties.size() && i < 8; i++) printf(" %d(%s)", ties[i].ko, (mdec(c, ties[i].ko) & 1) ? "odd" : "even");
        if (ties.size() > 8) printf(" ...");
        printf("\n");
        run_range(m, c, lo1, end1, ev, first_bad, aff012, false, true);
    }
    if (kind == 'c') {
        /* stage 2: reg 0x14 (w00 - d) and reg 0x00 + KYONEX (w00) */
        int lo2 = end1, hi2 = n - 1, end2 = n;
        if (mark[5] >= 0 && mark[6] >= 0) { hi2 = std::min(n - 1, mark[6] + 400); }
        if (mark[7] >= 0) end2 = std::min(n, std::max(lo2 + 1, mark[7] - 100));
        std::vector<Events> cands;
        for (int w = lo2; w <= hi2; w++) for (int d = 0; d < 2; d++) { if (w - d < lo2) continue; Events e = ev; e.w00 = w; e.w14 = w - d; cands.push_back(e); }
        std::vector<Events> ties; long sc;
        ev = search_stage(m, c, lo2, end2, cands, first_bad, aff012, ties, sc);
        printf("  stage 2 RR/KYONB rewrite: window [%d, %d], compared to %d: best w14 %d w00 %d (MDEC_CT %04x %s, d %d), score %ld/%ld, %zu tie(s):", lo2, hi2, end2, ev.w14, ev.w00,
               mdec(c, ev.w00), (mdec(c, ev.w00) & 1) ? "odd" : "even", ev.w00 - ev.w14, sc, 3L * end2, ties.size());
        for (size_t i = 0; i < ties.size() && i < 8; i++) printf(" %d/%d", ties[i].w14, ties[i].w00);
        if (ties.size() > 8) printf(" ...");
        printf("\n");
        run_range(m, c, lo2, end2, ev, first_bad, aff012, false, true);
        /* stage 3: IMXL 0 */
        int lo3 = end2, hi3 = n - 1;
        if (mark[8] >= 0) hi3 = std::min(n - 1, mark[8] + 400);
        cands.clear();
        for (int w = lo3; w <= hi3; w++) { Events e = ev; e.w2 = w; cands.push_back(e); }
        ev = search_stage(m, c, lo3, n, cands, first_bad, aff012, ties, sc);
        printf("  stage 3 IMXL 0: window [%d, %d], compared to %d: best w2 %d (MDEC_CT %04x %s), score %ld/%ld, %zu tie(s)%s\n", lo3, hi3, n, ev.w2, mdec(c, ev.w2),
               (mdec(c, ev.w2) & 1) ? "odd" : "even", sc, 3L * n, ties.size(), ties.size() > 1 ? " (the freeze is invisible while the tail is at rest)" : "");
        run_range(m, c, lo3, n, ev, first_bad, aff012, false, true);
    }
    if ((int)c.rec[0].size() != n - on) { fprintf(stderr, "internal: recorded %zu of %d samples\n", c.rec[0].size(), n - on); return 3; }

    /* report */
    printf("  events: %s\n", ev_str(c, ev, kind).c_str());
    int nfull = 0;
    for (int k = 0; k < 4; k++) {
        const SlotCfg &s = cfg[k];
        int fb = first_bad[k];
        int nbad = 0;
        for (int i = on; i < n; i++) nbad += c.rec[k][i - on] != cp.v[(size_t)i * ns + k];
        printf("  stream %d (slot %d VOFF %d LPOFF %d Q %d FLV0 %04x AR %d RR %d KRS %d FNS %03x):", k, s.slot, s.VOFF, s.LPOFF, s.Q, s.FLV[0], s.AR, s.RR, s.KRS, s.FNS);
        if (fb < 0) { nfull++; printf(" FULL %d/%d", n - on, n - on); }
        else {
            const SlotState &st = c.mm_state[k];
            printf(" %d/%d, first mismatch at %d (+%d, MDEC_CT %04x %s): hw %d model %d; model slot a %03x %s%s CA %d FEG.v %04x %s low %d band %d, %d mismatching samples",
                   fb - on, n - on, fb, fb - on, mdec(c, fb), (mdec(c, fb) & 1) ? "odd" : "even", cp.v[(size_t)fb * ns + k], c.rec[k][fb - on],
                   st.a, stname(st.state), st.off ? " OFF" : "", st.CA, st.fegv, stname(st.fegstate), st.low, st.band, nbad);
        }
        if (c.off_at[k] >= 0) {
            printf("; model off at %d (+%d after the onset%s)", c.off_at[k], c.off_at[k] - on, ev.ko >= 0 ? (std::string(", +") + std::to_string(c.off_at[k] - ev.ko) + " after the key-off").c_str() : "");
            if (kind == 'c' && ev.w14 >= 0 && c.off_at[k] > ev.w14) printf(" = +%d after the RR rewrite", c.off_at[k] - ev.w14);
            int i0 = c.off_at[k];
            printf("; model values from off: ");
            int shown = 0; int32_t prev = 0x7FFFFFFF;
            for (int i = i0; i < n && shown < 12; i++) { int32_t v = c.rec[k][i - on]; if (v != prev) { printf("%s%d:%d", shown ? " " : "", i, v); shown++; prev = v; } }
            if (shown == 12) printf(" ...");
        }
        printf("; final value hw %d model %d\n", cp.v[(size_t)(n - 1) * ns + k], c.rec[k][n - 1 - on]);
        if (fb >= 0) {
            printf("    around the first mismatch (i: hw/model):");
            for (int i = std::max(on, fb - 2); i < std::min(n, fb + 10); i++) printf(" %d:%d/%d", i, cp.v[(size_t)i * ns + k], c.rec[k][i - on]);
            printf("\n");
        }
        print_tail("hw   ", [&](int i) { return cp.v[(size_t)i * ns + k]; }, on, n);
        print_tail("model", [&](int i) { return c.rec[k][i - on]; }, on, n);
    }
    if (kind == 'c') {
        printf("  retained bus values at the end (IMXL 0 from %d): hw %d %d %d | model %d %d %d;  stream 3 hw %d model %d\n", ev.w2,
               cp.v[(size_t)(n - 1) * ns + 0], cp.v[(size_t)(n - 1) * ns + 1], cp.v[(size_t)(n - 1) * ns + 2], c.rec[0][n - 1 - on], c.rec[1][n - 1 - on], c.rec[2][n - 1 - on],
               cp.v[(size_t)(n - 1) * ns + 3], c.rec[3][n - 1 - on]);
        /* the value each bus held just before the IMXL 0 write and the last change before it */
        for (int k = 0; k < 3; k++) {
            int last_change = -1;
            for (int i = ev.w2 - 1; i > on; i--) if (cp.v[(size_t)i * ns + k] != cp.v[(size_t)(i - 1) * ns + k]) { last_change = i; break; }
            printf("    hw bus %d: value before the IMXL 0 write %d, last change at %d (%d samples before w2)\n", k, cp.v[(size_t)(ev.w2 - 1) * ns + k], last_change, ev.w2 - last_change);
        }
    }
    if (verbose) {
        for (int k = 0; k < 4; k++) if (first_bad[k] >= 0) {
            printf("  stream %d, 16 samples from the first mismatch (i MDEC_CT: hw model):\n", k);
            for (int i = first_bad[k]; i < std::min(n, first_bad[k] + 16); i++)
                printf("    %d %04x %s: %d %d\n", i, mdec(c, i), (mdec(c, i) & 1) ? "odd " : "EVEN", cp.v[(size_t)i * ns + k], c.rec[k][i - on]);
        }
    }
    printf("RESULT %s: %d/4 streams FULL, events %s\n", path.c_str(), nfull, ev_str(c, ev, kind).c_str());
    return nfull == 4 ? 0 : 1;
}
