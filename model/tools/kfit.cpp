// kfit.cpp -- standalone integer K search for the eg_kprobe captures (cases/eg_kprobe.c): the envelope counter
// constant K of every probe, and whether it (or MDEC_CT) moved between probes.
//
// The rules (NOTES.md "Envelope clock", "Amplitude envelope"), re-implemented here without the model or eg_phase:
//   MDEC_CT of capture sample i = (c0 - n_first - i) & 0xFFFF; the envelope clock ticks on even MDEC_CT with
//   eg_cnt = (K - MDEC_CT/2) & 0x3FFF.  Key events take effect on the sample after the KYONEX write (any parity), the
//   key-on sample takes no increment step.  Key-on loads a = 0x280, or a = 0 when the attack's effective rate is 63:
//   then the envelope leaves the attack on the FIRST CLOCK AT OR AFTER THE KEY-ON SAMPLE without a step (F7, tests/
//   eg_kprobe p5/p6/p7: an even onset has decay 1 stepping at onset + 2), so decay 1 steps from the next clock.
//   -oldr63 is the session-4 rule as a control (the key-on clock takes no step at all, the attack leaves on the next
//   clock; identical for odd onsets, one clock late for even ones).
//   R = min(63, 2 rate + s), s = KRS == 15 ? 0 : (k < 0 ? 0 : 2 min(k, 15) + FNS[9]), k = KRS + OCT (signed).
//   R < 48: tick when (cnt - 1) & (2^(11-R/4) - 1) == 0 with row R&3 indexed by ((cnt - 1) >> (11 - R/4)) & 7 (the
//   -1 = the R < 48 counter offset; -noslow removes it as a control); R >= 48: every clock, row 4 + (R - 48)
//   (>= 60: 16) indexed by cnt & 7; rows 5/9/13 = {b,2b,b,b,b,2b,b,b}.  Attack a += (~a inc) >> 4, to 0 -> decay 1;
//   decay/release a += inc, past 0x3FF "off" (silent).  Decay 1 -> 2 when a[9:5] == DL, checked on every decay-1
//   clock after its step.  Key-off sample that is a clock: mode 1 (the model, S3) steps with the previous segment's
//   increment, mode 0 takes no step, mode 2 steps with the release increment.
//   Level (0x7FFF input, TL 0, VOFF 0, IMXL 15): MIXS = 16 floor(32767 (127 - (a & 63)) >> (7 + (a >> 6))).
// K is searched over the full 2^14 range per stream (the key-on phase up to the key-off mark window), the streams'
// K sets are intersected, then the key-off sample is searched between the marks for every mode.
//
//   kfit [-v] [-noslow] [-oldr63] [-expect K] -case tests/eg_kprobe/<hw|model>     (reads eg_kprobe.txt in that directory)
//   kfit [-v] [-noslow] [-oldr63] [-expect K] runs.txt
// runs.txt (eg_phase format plus optional head data): "<capture prefix> <c0 hex> [<n_head> <t_head_us> [action...]]"
// followed by 4 lines "AR D1R DL D2R RR KRS OCT FNS".  -expect K exits 1 unless every probe fits exactly K.
// Build: make -C tools kfit (-> build/tools/kfit; single file, no model link).  Run from caique-rtl/model.
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
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
static uint32_t slow_off = (uint32_t)-1;   // the R < 48 counter offset
static bool old_r63 = false;               // -oldr63: the session-4 R 63 attack rule (control), see sim()

static inline uint32_t eg_increment(uint32_t R, uint32_t cnt) {
    if (R == 0) return 0;
    if (R < 48) {
        cnt = (cnt + slow_off) & 0x3FFF;
        uint32_t shift = 11 - (R >> 2);
        if (cnt & ((1u << shift) - 1)) return 0;
        return eg_inc[R & 3][(cnt >> shift) & 7];
    }
    return eg_inc[R >= 60 ? 16 : 4 + (R - 48)][cnt & 7];
}
static uint32_t period_of(uint32_t R) {   // the counter period this rate can distinguish
    if (R == 0) return 1;
    if (R < 48) return 1u << (14 - (R >> 2));
    if (R >= 60) return 1;
    int row = 4 + (R - 48);
    return (row & 3) == 0 ? 1 : (row & 3) == 2 ? 2 : 4;
}
static inline int32_t level_of(int a, bool off) {
    if (off) return 0;
    int M = 127 - (a & 63), k = a >> 6;
    return 16 * (int32_t)((32767LL * M) >> (7 + k));
}
struct Stream { int AR, D1R, DL, D2R, RR, KRS, OCT, FNS; };
struct Run {
    std::string name, path, action;
    uint32_t c0 = 0;
    Stream s[4] = {};
    int ns = 4;
    bool have_head = false; uint32_t n_head = 0; uint64_t t_head = 0;
};
static uint32_t eff_rate(const Stream &st, int re) {
    if (re == 0) return 0;
    int s = 0;
    if (st.KRS != 15) {
        int k = st.KRS + ((st.OCT & 8) ? st.OCT - 16 : st.OCT);
        s = k < 0 ? 0 : 2 * std::min(k, 15) + ((st.FNS >> 9) & 1);
    }
    return std::min(63, 2 * re + s);
}
enum { ATT = 0, D1 = 1, D2 = 2, REL = 3 };
struct Mismatch { int i = -1, a = 0, state = 0; int32_t lv = 0; bool off = false; };
// simulate stream k of capture c from the onset sample 'on' to 'end' (exclusive); ko = key-off sample (>= end: none),
// mode = what the key-off sample does when it is a clock.  Returns the number of consecutive matching samples.
static int sim(const Capture &c, int k, const Stream &st, uint32_t c0, int on, int end, uint32_t K, int ko, int mode,
               Mismatch *mm = nullptr) {
    uint32_t rAR = eff_rate(st, st.AR), rD1 = eff_rate(st, st.D1R), rD2 = eff_rate(st, st.D2R), rRR = eff_rate(st, st.RR);
    int a = rAR >= 63 ? 0 : 0x280, state = ATT, prev = ATT;
    bool off = false;
    for (int i = on; i < end; i++) {
        uint32_t md = (c0 - c.first - (uint32_t)i) & 0xFFFF;
        bool clock = (md & 1) == 0;
        if (i == ko) { prev = state; state = REL; }
        if (clock && !off && state == ATT && rAR >= 63 && (i > on || !old_r63)) {
            // F7 (tests/eg_kprobe p5/p6/p7): the R 63 attack (a = 0 since the key-on) leaves the attack state on the
            // first clock at or after the key-on sample -- the key-on sample itself when it is a clock -- without an
            // increment step; decay 1 steps from the next clock.  Old rule (-oldr63): the key-on sample takes no
            // step at all and the first clock after it is the attack step landing at 0 (identical for odd onsets).
            state = D1;
        } else if (clock && i > on && !off && !(i == ko && mode == 0)) {
            uint32_t cnt = (K - (md >> 1)) & 0x3FFF;
            int rs = (i == ko && mode == 1) ? prev : state;
            uint32_t R = rs == ATT ? rAR : rs == D1 ? rD1 : rs == D2 ? rD2 : rRR;
            uint32_t inc = eg_increment(R, cnt);
            bool was_d1 = state == D1;
            if (inc) {
                if (state == ATT) { a += ((~a) * (int)inc) >> 4; if (a <= 0) { a = 0; state = D1; } }
                else { a += (int)inc; if (a > 0x3FF) { a = 0x3FF; off = true; } }
            }
            if (was_d1 && !off && (a >> 5) == st.DL) state = D2;
        }
        int32_t lv = level_of(a, off);
        if (lv != c.v[(size_t)i * c.ns + k]) {
            if (mm) { mm->i = i; mm->a = a; mm->state = state; mm->lv = lv; mm->off = off; }
            return i - on;
        }
    }
    return end - on;
}
static bool read_marks(const std::string &path, const Capture &c, int &m3, int &m4) {
    FILE *f = fopen((path + ".hdr").c_str(), "rb");
    if (!f) return false;
    uint32_t h[16 + 128] = {0};
    size_t nh = fread(h, 4, sizeof h / 4, f);
    fclose(f);
    m3 = m4 = -1;
    for (size_t i = 11; i + 1 < nh && i < 11 + 2 * h[6]; i += 2) {
        if (h[i] == 3 && m3 < 0) m3 = (int)(h[i + 1] - c.first);
        if (h[i] == 4 && m4 < 0) m4 = (int)(h[i + 1] - c.first);
    }
    return true;
}
static bool parse_runs(const char *file, std::vector<Run> &runs) {
    FILE *f = fopen(file, "r");
    if (!f) { perror(file); return false; }
    char line[1024];
    while (fgets(line, sizeof line, f)) {
        char p[512]; unsigned c0; unsigned long nh; unsigned long long th; int pos = 0;
        int got = sscanf(line, "%511s %x %lu %llu %n", p, &c0, &nh, &th, &pos);
        if (got < 2) continue;
        Run r; r.path = p; r.c0 = c0;
        size_t sl = r.path.rfind('/');
        r.name = sl == std::string::npos ? r.path : r.path.substr(sl + 1);
        if (got >= 4) { r.have_head = true; r.n_head = (uint32_t)nh; r.t_head = th; if (pos > 0) { r.action = line + pos; while (!r.action.empty() && (r.action.back() == '\n' || r.action.back() == '\r')) r.action.pop_back(); } }
        for (int k = 0; k < 4; k++) {
            if (!fgets(line, sizeof line, f)) { fclose(f); return false; }
            Stream &s = r.s[k];
            if (sscanf(line, "%d %d %d %d %d %d %d %d", &s.AR, &s.D1R, &s.DL, &s.D2R, &s.RR, &s.KRS, &s.OCT, &s.FNS) != 8) { fclose(f); return false; }
        }
        runs.push_back(r);
    }
    fclose(f);
    return true;
}
// -case <dir>: the case text (eg_kprobe.txt) carries the programs ("pN stream k: slot s AR ...") and the probe line
// ("probe pN: c0 XXXX head n N t_head_us T ... action ...")
static bool parse_case(const std::string &dir, std::vector<Run> &runs) {
    std::string txt = dir + "/eg_kprobe.txt";
    FILE *f = fopen(txt.c_str(), "r");
    if (!f) { perror(txt.c_str()); return false; }
    char line[1024];
    auto find = [&](const std::string &name) -> Run & {
        for (auto &r : runs) if (r.name == name) return r;
        Run r; r.name = name; r.path = dir + "/" + name; runs.push_back(r); return runs.back();
    };
    while (fgets(line, sizeof line, f)) {
        char name[64]; int k, slot; Stream s; unsigned c0; unsigned long nh; unsigned long th_s, th_us; int pos = 0;
        if (sscanf(line, "%63s stream %d: slot %d AR %d D1R %d DL %d D2R %d RR %d KRS %d OCT %d FNS %x", name, &k, &slot,
                   &s.AR, &s.D1R, &s.DL, &s.D2R, &s.RR, &s.KRS, &s.OCT, &s.FNS) == 11) {
            if (k >= 0 && k < 4) find(name).s[k] = s;
            continue;
        }
        if (sscanf(line, "probe %63[^:]: c0 %x head n %lu t_head_us %lu%n", name, &c0, &nh, &th_s, &pos) == 4) {
            Run &r = find(name);
            r.c0 = c0; r.have_head = true; r.n_head = (uint32_t)nh;
            // t_head_us was printed as seconds then 6 zero-padded digits: re-read the whole decimal
            const char *t = strstr(line, "t_head_us ");
            r.t_head = t ? strtoull(t + 10, nullptr, 10) : 0;
            (void)th_us;
            const char *a = strstr(line, " action ");
            if (a) { r.action = a + 8; while (!r.action.empty() && (r.action.back() == '\n' || r.action.back() == '\r')) r.action.pop_back(); }
        }
    }
    fclose(f);
    return !runs.empty();
}

int main(int argc, char **argv) {
    bool verbose = false; long expect = -1; const char *runfile = nullptr; std::string casedir;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-v")) verbose = true;
        else if (!strcmp(argv[i], "-noslow")) slow_off = 0;
        else if (!strcmp(argv[i], "-oldr63")) old_r63 = true;
        else if (!strcmp(argv[i], "-expect") && i + 1 < argc) expect = atol(argv[++i]);
        else if (!strcmp(argv[i], "-case") && i + 1 < argc) casedir = argv[++i];
        else runfile = argv[i];
    }
    std::vector<Run> runs;
    if (!casedir.empty()) { if (!parse_case(casedir, runs)) return 2; }
    else if (runfile) { if (!parse_runs(runfile, runs)) return 2; }
    else { fprintf(stderr, "usage: kfit [-v] [-noslow] [-expect K] (-case <dir> | runs.txt)\n"); return 2; }
    printf("kfit: %zu probes, R < 48 counter offset %s, R 63 attack leaves the attack %s\n", runs.size(), slow_off ? "-1" : "0 (control)",
           old_r63 ? "on the first clock AFTER the key-on sample (session-4 rule, control)" : "on the first clock at or after the key-on sample (F7)");

    struct Result { std::string name, action; long K = -1; size_t nK = 0; bool have_head; uint32_t mdec_head; uint64_t t_head; };
    std::vector<Result> res;
    int bad = 0;
    for (auto &r : runs) {
        Capture c;
        try { c = cap(r.path); } catch (std::exception &e) { printf("%s: cannot load (%s)\n", r.path.c_str(), e.what()); bad++; continue; }
        int m3 = -1, m4 = -1;
        read_marks(r.path, c, m3, m4);
        // onset per stream (first non-zero sample); all four are keyed by one KYONEX
        int on[4];
        for (int k = 0; k < (int)c.ns; k++) { on[k] = 1; while (on[k] < (int)c.n && !c.v[(size_t)on[k] * c.ns + k]) on[k]++; }
        uint32_t md_on = (r.c0 - c.first - (uint32_t)on[0]) & 0xFFFF;
        printf("%s: n %u first %u c0 %04x onset %d/%d/%d/%d (MDEC_CT %04x %s) key-off marks %d..%d%s%s\n", r.path.c_str(), c.n, c.first,
               r.c0, on[0], on[1], on[2], on[3], md_on, (md_on & 1) ? "odd" : "even", m3, m4, r.action.empty() ? "" : "  action: ", r.action.c_str());
        int on_end = m3 >= 0 ? std::min<int>(c.n, m3 - 400) : (int)c.n;   // marks are head estimates (+-100): stay clear
        std::vector<uint32_t> common; bool first = true;
        long Kfit = -1;
        for (int k = 0; k < (int)c.ns; k++) {
            const Stream &st = r.s[k];
            uint32_t P = 1;
            for (int re : {st.AR, st.D1R, st.D2R, st.RR}) P = std::max(P, period_of(eff_rate(st, re)));
            std::vector<uint32_t> full; int best = -1; uint32_t bestK = 0;
            for (uint32_t K = 0; K < 16384; K++) {
                int m = sim(c, k, st, r.c0, on[k], on_end, K, 1 << 30, 1);
                if (m > best) { best = m; bestK = K; }
                if (m == on_end - on[k]) full.push_back(K);
            }
            printf("  stream %d AR %2d D1R %2d DL %2d D2R %2d RR %2d KRS %2d OCT %d FNS %03x (R %u/%u/%u/%u, period %u): key-on phase %d samples, ",
                   k, st.AR, st.D1R, st.DL, st.D2R, st.RR, st.KRS, st.OCT, st.FNS, eff_rate(st, st.AR), eff_rate(st, st.D1R),
                   eff_rate(st, st.D2R), eff_rate(st, st.RR), P, on_end - on[k]);
            if (full.empty()) {
                Mismatch mm; sim(c, k, st, r.c0, on[k], on_end, bestK, 1 << 30, 1, &mm);
                printf("NO K fits: best %d/%d at K %u, first mismatch i %d (+%d) a %03x state %d level %d hw %d\n", best, on_end - on[k], bestK,
                       mm.i, mm.i - on[k], mm.a, mm.state, mm.lv, c.v[(size_t)mm.i * c.ns + k]);
                continue;
            }
            printf("%zu K fit (expected %u for period %u): K mod %u = %u%s\n", full.size(), 16384 / P, P, P, full[0] % P,
                   full.size() == 16384 / P ? "" : "  <-- unexpected count");
            if (verbose) { printf("    K:"); for (size_t i = 0; i < full.size() && i < 16; i++) printf(" %u", full[i]); if (full.size() > 16) printf(" ..."); printf("\n"); }
            if (first) { common = full; first = false; }
            else { std::vector<uint32_t> tmp; std::set_intersection(common.begin(), common.end(), full.begin(), full.end(), std::back_inserter(tmp)); common = tmp; }
        }
        Result rr{r.name, r.action, -1, common.size(), r.have_head, (uint32_t)((r.c0 - r.n_head) & 0xFFFF), r.t_head};
        if (common.size() == 1) { Kfit = common[0]; printf("  K = %ld (mod 8192: %ld, bit 13 = %ld)\n", Kfit, Kfit % 8192, Kfit >> 13); }
        else if (common.empty()) { printf("  K: the streams do not agree on any K\n"); bad++; }
        else { printf("  K ambiguous: %zu candidates:", common.size()); for (size_t i = 0; i < common.size() && i < 12; i++) printf(" %u", common[i]); printf("\n"); Kfit = common[0]; }
        rr.K = Kfit;
        // key-off: which samples (and which key-off-sample rule) reproduce the rest of the capture at the fitted K
        if (Kfit >= 0 && m3 >= 0) {
            int lo = std::max(on[0] + 1, m3 - 400), hi = std::min<int>(c.n - 1, m4 + 400);
            for (int k = 0; k < (int)c.ns; k++) {
                const Stream &st = r.s[k];
                bool silent = c.v[(size_t)lo * c.ns + k] == 0;
                printf("  stream %d key-off (window %d..%d)%s:", k, lo, hi, silent ? " [silent at the window start: uninformative]" : "");
                for (int mode = 0; mode < 3; mode++) {
                    std::vector<int> kos;
                    for (int ko = lo; ko <= hi; ko++)
                        if (sim(c, k, st, r.c0, on[k], (int)c.n, (uint32_t)Kfit, ko, mode) == (int)c.n - on[k]) kos.push_back(ko);
                    printf("  mode %d (%s):", mode, mode == 0 ? "no step" : mode == 1 ? "prev inc" : "rel inc");
                    if (kos.empty()) printf(" none");
                    for (size_t i = 0; i < kos.size() && i < 4; i++) printf(" %d(%s)", kos[i], ((r.c0 - c.first - kos[i]) & 1) ? "odd" : "even");
                    if (kos.size() > 4) printf(" ...(%zu)", kos.size());
                }
                if (verbose && !silent) {
                    Mismatch mm; int m = sim(c, k, st, r.c0, on[k], (int)c.n, (uint32_t)Kfit, 1 << 30, 1, &mm);
                    printf("  [no key-off: %d/%d, first mismatch i %d a %03x level %d hw %d]", m, (int)c.n - on[k], mm.i, mm.a, mm.lv, mm.i >= 0 ? c.v[(size_t)mm.i * c.ns + k] : 0);
                }
                printf("\n");
            }
        }
        res.push_back(rr);
    }
    // summary: K per probe, change against the first probe, and MDEC_CT continuity from the head measurements
    printf("\nsummary (K per probe; dK vs %s; MDEC_CT drift = measured - predicted from the SH4 clock at 44100 Hz):\n", res.empty() ? "-" : res[0].name.c_str());
    printf("  %-6s %6s %6s %5s %6s  %-9s %-11s %s\n", "probe", "K", "mod8k", "bit13", "dK", "mdec_head", "drift", "action");
    for (size_t i = 0; i < res.size(); i++) {
        const Result &x = res[i];
        char kbuf[32], dbuf[32], drift[32];
        if (x.K < 0) snprintf(kbuf, sizeof kbuf, "none"); else snprintf(kbuf, sizeof kbuf, "%ld%s", x.K, x.nK > 1 ? "?" : "");
        if (x.K < 0 || res[0].K < 0) snprintf(dbuf, sizeof dbuf, "-"); else snprintf(dbuf, sizeof dbuf, "%+ld", (long)(((x.K - res[0].K) % 16384 + 16384 + 8192) % 16384 - 8192));
        if (i > 0 && x.have_head && res[i - 1].have_head) {
            uint64_t dt = x.t_head - res[i - 1].t_head;                   // us
            uint32_t pred_n = (uint32_t)((dt * 441 + 5000) / 10000);        // samples at 44100 Hz
            uint32_t pred = (res[i - 1].mdec_head - pred_n) & 0xFFFF;
            int d = (int)((x.mdec_head - pred) & 0xFFFF); if (d >= 32768) d -= 65536;
            snprintf(drift, sizeof drift, "%+d", d);
        } else snprintf(drift, sizeof drift, "-");
        printf("  %-6s %6s %6ld %5ld %6s  %04x      %-11s %s\n", x.name.c_str(), kbuf, x.K < 0 ? -1L : x.K % 8192, x.K < 0 ? -1L : x.K >> 13, dbuf,
               x.mdec_head, drift, x.action.c_str());
        if (expect >= 0 && (x.K != expect || x.nK != 1)) bad++;
    }
    printf("reading: dK != 0 after an action => that action reset or offset a counter; a drift of thousands with it => MDEC_CT itself\n"
           "         jumped (the EG counter kept running); dK != 0 with a drift of a few samples => the EG counter was reset.\n");
    if (expect >= 0) printf("expect K = %ld: %s\n", expect, bad ? "FAIL" : "all probes match");
    return bad ? 1 : 0;
}
