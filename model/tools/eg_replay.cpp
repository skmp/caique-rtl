// eg_replay.cpp -- replay a MULTI-CYCLE AEG capture through the production AicaModel with the envelope clock locked to
// the capture's ring position (NOTES.md "Envelope clock") and the key events (KYONB masks + one KYONEX) at given
// samples, and compare every sample of every stream.  Template: tools/eg_model.cpp (one cycle, key-off searched);
// the tests/aeg_koff event derivation follows work/koff/koff_fit.cpp (the samples are pinned by the capture itself).
//
//   eg_replay [-v] [-K k] [-c0 hex] -case <aeg_koff.txt> <run> [<capture prefix>]
//       tests/aeg_koff runs (cases/aeg_koff.c: koff_d2, koff_d2b, koff_d1, koff_att, kon_rel, ...): the stream table
//       is read from the run's "<run> stream k: slot s AR .. role test|witness" lines of the case's text output and c0
//       from the ", c0 XXXX" of the cap_start line that follows them; the capture is <dir of the txt>/<run> unless a
//       prefix is given.  The events are derived from the capture (below).
//   eg_replay [-v] <capture prefix> <c0 hex> <K> (-txt <aeg_koff.txt> <run> | -slot <spec> ...) -ev <sample>:<on>:<off> ...
//       generic: explicit events.  <on> / <off> are hex masks of the SLOT NUMBERS whose KYONB is set / cleared by that
//       write; one KYONEX then applies every slot's KYONB (as on the chip), a slot in neither mask keeps its KYONB.
//       -slot k:ISEL:AR:D1R:DL:D2R:RR:KRS:OCT:FNS:ramp   (ramp 1 = the 4096-sample ramp at 0x20000, else the constant)
//
// Capture frame: MDEC_CT of sample i = (c0 - n_first - i) & 0xFFFF (n_first = header word 3); the envelope clock is the
// even-MDEC_CT samples.  Model side: the slots are configured as the case (cases/aica_io.h slot_cfg_default: PCM16,
// LPCTL 1, TL 0, VOFF 0, LPOFF 1, IMXL 15, ISEL k), RAM = 32 x 0x7FFF at 0x10000 and the ramp 8 i - 0x4000 at
// 0x20000, 16 warm-up samples, MDEC_CT set to the capture's MDEC_CT of sample 0, then for every sample: that sample's
// KYONB writes and one KYONEX BEFORE the step() that produces it (the write lands on the next step(): eg_model.cpp), and
// MIXS[ISEL] compared with the capture.  Samples before the first event are compared too (the buses read 0).
//
// Event derivation for tests/aeg_koff (kind 0 = key-on / key-off cycles, marks 1 / 3; kind 1 = kon_rel, marks 1/3/5/7):
//   E_A  the test slots' fresh key-on: the first 496 (a 0x280) after a silent gap on stream 0 within mark 1 +- 300
//        (koff_fit find_496_onset); the write keyx(TEST): KYONB on for the test slots, off for the witness.
//   E_B  kind 0: the witness onset (stream 3 jumps to 520176 and holds it) within mark 3 +- 300: keyx(WIT).
//   kon_rel: E_C = the witness onset within mark 5 +- 300 (keyx(TEST|WIT): all on).  E_B (keyx(0): all off) is NOT
//        pinned: searched in [E_A + 100, E_C - 50] for the longest match of all streams up to E_C (snapshot replay,
//        ties listed, the first tie taken).  E_D (all off) is searched in mark 7 +- 300 (after E_C, before the next
//        E_A) for the longest match up to the next E_A.
// Output: the events per cycle; per cycle and stream the first mismatch relative to the nearest preceding event; per
// stream the matched prefix / total, the number of mismatching samples and the first mismatch (sample, MDEC_CT parity,
// hw / model, model a / state / CA); a TOTAL line.  -v dumps 12 samples from every stream's first mismatch.
// Exit 0 iff every stream is FULL.
// Build: make -C tools eg_replay (-> build/tools/eg_replay; links src/aica_model.cpp).  Run from caique-rtl/model.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include "../src/aica_model.h"
#include "filt_capture.h"
using namespace caique;

struct Stream { int slot = 0, ISEL = 0, AR = 31, D1R = 0, DL = 0, D2R = 0, RR = 31, KRS = 15, OCT = 0, FNS = 0; bool ramp = false, witness = false; };
struct Ev { uint64_t on = 0, off = 0; std::string name; };
struct Cyc { int EA = -1, EB = -1, EC = -1, ED = -1; };

static const char *stname(int s) { return s == 0 ? "att" : s == 1 ? "d1" : s == 2 ? "d2" : "rel"; }

static void load_ram(AicaModel &m) {
    for (int i = 0; i < 32; i++) { m.ram[0x10000 + 2 * i] = 0xFF; m.ram[0x10000 + 2 * i + 1] = 0x7F; }
    for (int i = 0; i < 4096; i++) { int16_t s = (int16_t)(8 * i - 0x4000); m.ram[0x20000 + 2 * i] = (uint8_t)s; m.ram[0x20000 + 2 * i + 1] = (uint8_t)(s >> 8); }
}
static void write_slot(AicaModel &m, const Stream &c) {
    int ch = c.slot;
    auto aw = [&](uint32_t o, uint32_t v) { m.write(0x80 * ch + o, v); };
    uint32_t SA = c.ramp ? 0x20000 : 0x10000, LEA = c.ramp ? 4096 : 32;
    aw(0x04, SA & 0xFFFF); aw(0x08, 0); aw(0x0C, LEA);
    aw(0x10, (c.D2R << 11) | (c.D1R << 6) | c.AR);
    aw(0x14, (c.KRS << 10) | (c.DL << 5) | c.RR);
    aw(0x18, (c.OCT << 11) | c.FNS);
    aw(0x1C, 0);
    aw(0x20, (15 << 4) | c.ISEL);
    aw(0x24, 0);
    aw(0x28, (0 << 8) | (0 << 6) | (1 << 5) | 0);   /* TL 0, VOFF 0, LPOFF 1, Q 0 */
    for (int i = 0; i < 5; i++) aw(0x2C + 4 * i, 0x1FF8);
    aw(0x40, (31 << 8) | 31);
    aw(0x44, (31 << 8) | 31);
    aw(0x00, (1 << 9) | ((SA >> 16) & 0x7F));
}
/* the KYONB writes of one event and the KYONEX (through the lowest slot written, as the case does through slot 0) */
static void apply_event(AicaModel &m, const Ev &e) {
    int kx = -1;
    for (int s = 0; s < 64; s++) {
        bool on = (e.on >> s) & 1, off = (e.off >> s) & 1;
        if (!on && !off) continue;
        if (kx < 0) kx = s;
        m.write(0x80 * s, (m.chr(s, 0) & 0x3FFF) | (on ? 0x4000 : 0));
    }
    if (kx < 0) kx = 0;
    m.write(0x80 * kx, m.chr(kx, 0) | 0x8000);
}
/* snapshot without the RAM (never written after the setup) */
struct Snap { std::vector<uint8_t> b; Snap() : b(sizeof(AicaModel)) {} };
static void snap_take(Snap &s, const AicaModel &m) { memcpy(s.b.data(), &m, sizeof m); }
static void snap_restore(AicaModel &m, const Snap &s) { uint8_t *ram = m.ram; memcpy((void *)&m, s.b.data(), sizeof m); m.ram = ram; }

struct Ctx {
    Capture cp; uint32_t c0 = 0, K = 6491; std::vector<Stream> st; int n = 0, ns = 0;
    std::map<int, Ev> evs;                 /* fixed events by sample */
    std::vector<int32_t> rec[4];           /* model MIXS per sample */
    std::vector<uint16_t> rec_a[4], rec_ca[4];
    std::vector<uint8_t> rec_st[4];        /* state | off << 2 */
    int pos = 0;                           /* next sample the model produces */
    int first_bad[4] = {-1, -1, -1, -1}; long nbad[4] = {0, 0, 0, 0};
    bool verbose = false;
};
static inline uint32_t mdec(const Ctx &c, int i) { return (c.c0 - c.cp.first - (uint32_t)i) & 0xFFFF; }
static inline const char *par(const Ctx &c, int i) { return (mdec(c, i) & 1) ? "odd" : "even"; }
static inline int32_t hw(const Ctx &c, int i, int k) { return c.cp.v[(size_t)i * c.ns + k]; }

/* run the model over [pos, to) with the fixed events, recording and comparing */
static void advance(AicaModel &m, Ctx &c, int to) {
    for (int i = c.pos; i < to; i++) {
        auto it = c.evs.find(i + 1);   /* the event's writes land one boundary before its effect sample */
        if (it != c.evs.end()) apply_event(m, it->second);
        m.step();
        for (int k = 0; k < c.ns; k++) {
            const Slot &s = m.slot[c.st[k].slot];
            int32_t v = m.MIXS[c.st[k].ISEL];
            c.rec[k][i] = v; c.rec_a[k][i] = s.AEG.a; c.rec_ca[k][i] = (uint16_t)s.CA; c.rec_st[k][i] = (uint8_t)(s.AEG.state | (s.AEG.off ? 4 : 0));
            if (v != hw(c, i, k)) { c.nbad[k]++; if (c.first_bad[k] < 0) c.first_bad[k] = i; }
        }
    }
    c.pos = std::max(c.pos, to);
}
/* search the sample of one unpinned event in [lo, hi] from a snapshot at lo (the model is advanced to lo first): the
 * score of a candidate is the SUM over the streams of their matched prefix over [lo, cmp_end) (tail_cmp's score: a
 * stream the model gets wrong for another reason -- e.g. the witness under a wrong R 63 rule -- costs a constant and the
 * other streams still pin the event).  The first full match (or the best score) is fixed and returned. */
static int search_event(AicaModel &m, Ctx &c, int lo, int hi, int cmp_end, const Ev &templ, int cyc) {
    lo = std::max(lo, c.pos); hi = std::min(hi, cmp_end - 1);
    if (lo > hi) { printf("  cycle %2d: %s search window empty ([%d, %d])\n", cyc, templ.name.c_str(), lo, hi); return -1; }
    advance(m, c, lo);
    Snap s; snap_take(s, m);
    long best = -1, full = (long)c.ns * (cmp_end - lo); int bestko = -1; std::vector<int> ties;
    for (int ko = lo; ko <= hi; ko++) {
        snap_restore(m, s);
        int fb[4] = {-1, -1, -1, -1};
        for (int i = lo; i < cmp_end; i++) {
            auto it = c.evs.find(i + 1);
            if (it != c.evs.end()) apply_event(m, it->second);
            if (i + 1 == ko) apply_event(m, templ);
            m.step();
            bool all_bad = true;
            for (int k = 0; k < c.ns; k++) {
                if (fb[k] < 0 && m.MIXS[c.st[k].ISEL] != hw(c, i, k)) fb[k] = i;
                if (fb[k] < 0) all_bad = false;
            }
            if (all_bad) break;
        }
        long sc = 0;
        for (int k = 0; k < c.ns; k++) sc += (fb[k] < 0 ? cmp_end : fb[k]) - lo;
        if (sc > best) { best = sc; bestko = ko; ties.clear(); }
        if (sc == best) ties.push_back(ko);
    }
    snap_restore(m, s);
    int chosen = bestko;
    printf("  cycle %2d: %s searched in [%d, %d] (compared to %d): ", cyc, templ.name.c_str(), lo, hi, cmp_end);
    printf("%s, score %ld/%ld, %zu candidate(s):", best == full ? "full match" : "NO full match", best, full, ties.size());
    for (size_t i = 0; i < ties.size() && i < 6; i++) printf(" %d(%s)", ties[i], par(c, ties[i]));
    if (ties.size() > 6) printf(" ...");
    printf("; taking %d\n", chosen);
    Ev &e = c.evs[chosen];
    e.on |= templ.on; e.off |= templ.off; if (e.name.empty()) e.name = templ.name; else if (e.name != templ.name) e.name += "+" + templ.name;
    return chosen;
}

/* ---- the pinned samples (koff_fit.cpp) ---- */
static int find_496_onset(const Ctx &c, int k, int lo, int hi) { /* fresh key-on: 496 (a 0x280), then a rising attack */
    lo = std::max(lo, 1); hi = std::min(hi, c.n - 3);
    for (int i = lo; i <= hi; i++)
        if (hw(c, i, k) == 496 && hw(c, i - 1, k) != 496 && hw(c, i + 1, k) >= 496 && hw(c, i + 2, k) > 496) return i;
    return -1;
}
static int find_full_onset(const Ctx &c, int k, int lo, int hi) { /* witness key-on: the jump to 520176 (a = 0), held one more sample */
    lo = std::max(lo, 1); hi = std::min(hi, c.n - 2);
    for (int i = lo; i <= hi; i++)
        if (hw(c, i, k) == 520176 && hw(c, i - 1, k) != 520176 && hw(c, i + 1, k) == 520176) return i;
    return -1;
}

/* the run's stream lines and c0 from the case's text output */
static bool parse_txt(const char *path, const std::string &run, std::vector<Stream> &st, uint32_t &c0, bool &c0_found) {
    FILE *f = fopen(path, "r");
    if (!f) { perror(path); return false; }
    char line[512];
    bool in_run = false;
    st.clear(); c0_found = false;
    while (fgets(line, sizeof line, f)) {
        char name[64], role[16]; int k, slot, AR, D1R, DL, D2R, RR, KRS, OCT, FNS, ramp;
        if (sscanf(line, "%63s stream %d: slot %d AR %d D1R %d DL %d D2R %d RR %d KRS %d OCT %d FNS %x ramp %d role %15s",
                   name, &k, &slot, &AR, &D1R, &DL, &D2R, &RR, &KRS, &OCT, &FNS, &ramp, role) == 13) {
            if (name != run) { in_run = false; continue; }
            in_run = true;
            if (k >= (int)st.size()) st.resize(k + 1);
            Stream &s = st[k];
            s.slot = slot; s.ISEL = k; s.AR = AR; s.D1R = D1R; s.DL = DL; s.D2R = D2R; s.RR = RR; s.KRS = KRS; s.OCT = OCT; s.FNS = FNS; s.ramp = ramp != 0; s.witness = !strcmp(role, "witness");
            continue;
        }
        /* ", c0 XXXX": a bare "c0 " also matches inside a ring address such as "at a9c0 (n 679)" */
        const char *p = strstr(line, ", c0 ");
        unsigned v;
        if (in_run && !c0_found && strstr(line, "cap_start:") && p && sscanf(p + 5, "%x", &v) == 1) { c0 = v; c0_found = true; }
        if (in_run && !strncmp(line, run.c_str(), run.size()) && line[run.size()] == ':') in_run = false;   /* "<run>: N samples" ends the run */
    }
    fclose(f);
    return !st.empty();
}
static std::string dir_of(const std::string &p) { size_t s = p.find_last_of('/'); return s == std::string::npos ? std::string() : p.substr(0, s + 1); }

static void describe_mismatch(const Ctx &c, int k, int i, const std::vector<std::pair<int, std::string>> &evlist) {
    /* nearest preceding event */
    std::string rel;
    for (auto &e : evlist) if (e.first <= i) { char b[64]; snprintf(b, sizeof b, "%s %d%+d", e.second.c_str(), e.first, i - e.first); rel = b; }
    if (rel.empty()) rel = "before the first event";
    printf("first mismatch at %d (%s, MDEC_CT %04x %s): hw %d model %d; model a 0x%03x %s%s CA %u", i, rel.c_str(), mdec(c, i), par(c, i), hw(c, i, k), c.rec[k][i],
           c.rec_a[k][i], stname(c.rec_st[k][i] & 3), (c.rec_st[k][i] & 4) ? " OFF" : "", c.rec_ca[k][i]);
}

int main(int argc, char **argv) {
    bool verbose = false; long Kopt = -1; long c0opt = -1;
    std::string txt, run, prefix; std::vector<std::string> pos; std::vector<std::string> slotspecs, evspecs; bool casemode = false;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-v")) verbose = true;
        else if (!strcmp(argv[i], "-K") && i + 1 < argc) Kopt = atol(argv[++i]);
        else if (!strcmp(argv[i], "-c0") && i + 1 < argc) c0opt = strtol(argv[++i], nullptr, 16);
        else if (!strcmp(argv[i], "-case") && i + 2 < argc) { casemode = true; txt = argv[++i]; run = argv[++i]; }
        else if (!strcmp(argv[i], "-txt") && i + 2 < argc) { txt = argv[++i]; run = argv[++i]; }
        else if (!strcmp(argv[i], "-slot") && i + 1 < argc) slotspecs.push_back(argv[++i]);
        else if (!strcmp(argv[i], "-ev") && i + 1 < argc) evspecs.push_back(argv[++i]);
        else pos.push_back(argv[i]);
    }
    Ctx c;
    c.verbose = verbose;
    bool c0_found = false;
    if (casemode) {
        if (!parse_txt(txt.c_str(), run, c.st, c.c0, c0_found)) { fprintf(stderr, "no stream lines for %s in %s\n", run.c_str(), txt.c_str()); return 2; }
        prefix = pos.empty() ? dir_of(txt) + run : pos[0];
        if (!c0_found && c0opt < 0) { fprintf(stderr, "no cap_start c0 for %s in %s (pass -c0)\n", run.c_str(), txt.c_str()); return 2; }
    } else {
        if (pos.size() < 3) { fprintf(stderr, "usage: eg_replay [-v] [-K k] [-c0 hex] -case <aeg_koff.txt> <run> [<prefix>]\n       eg_replay [-v] <prefix> <c0 hex> <K> (-txt <aeg_koff.txt> <run> | -slot k:ISEL:AR:D1R:DL:D2R:RR:KRS:OCT:FNS:ramp ...) -ev <sample>:<on hex>:<off hex> ...\n"); return 2; }
        prefix = pos[0]; c.c0 = (uint32_t)strtoul(pos[1].c_str(), nullptr, 16); c0_found = true; c.K = (uint32_t)atol(pos[2].c_str());
        if (!txt.empty()) { if (!parse_txt(txt.c_str(), run, c.st, c.c0, c0_found)) { fprintf(stderr, "no stream lines for %s in %s\n", run.c_str(), txt.c_str()); return 2; } c.c0 = (uint32_t)strtoul(pos[1].c_str(), nullptr, 16); }
        for (auto &sp : slotspecs) {
            Stream s; int ramp = 0;
            if (sscanf(sp.c_str(), "%d:%d:%d:%d:%d:%d:%d:%d:%d:%i:%d", &s.slot, &s.ISEL, &s.AR, &s.D1R, &s.DL, &s.D2R, &s.RR, &s.KRS, &s.OCT, &s.FNS, &ramp) != 11) { fprintf(stderr, "bad -slot %s\n", sp.c_str()); return 2; }
            s.ramp = ramp != 0;
            c.st.push_back(s);
        }
        if (c.st.empty()) { fprintf(stderr, "no slots (-txt or -slot)\n"); return 2; }
        if (evspecs.empty()) fprintf(stderr, "warning: no -ev events\n");
    }
    if (c0opt >= 0) c.c0 = (uint32_t)c0opt;
    if (Kopt >= 0) c.K = (uint32_t)Kopt;
    c.cp = cap(prefix);
    c.n = (int)c.cp.n; c.ns = (int)c.cp.ns;
    if ((int)c.st.size() < c.ns) { fprintf(stderr, "%d streams configured for a %d-stream capture\n", (int)c.st.size(), c.ns); return 2; }
    if (c.ns > 4) { fprintf(stderr, "at most 4 streams\n"); return 2; }
    for (int k = 0; k < c.ns; k++) { c.rec[k].assign(c.n, 0); c.rec_a[k].assign(c.n, 0); c.rec_ca[k].assign(c.n, 0); c.rec_st[k].assign(c.n, 0); }
    /* marks */
    std::vector<std::vector<int>> marks(16);
    {
        FILE *f = fopen((prefix + ".hdr").c_str(), "rb");
        uint32_t h[16 + 2 * 64] = {0};
        size_t nh = f ? fread(h, 4, sizeof h / 4, f) : 0;
        if (f) fclose(f);
        for (size_t i = 11; i + 1 < nh && i < 11 + 2 * h[6]; i += 2) if (h[i] < 16) marks[h[i]].push_back((int)(h[i + 1] - c.cp.first));
    }
    printf("%s (%s): %d samples x %d streams, n_first %u, c0 %04x (MDEC_CT of sample i = (%04x - i) & 0xFFFF), K %u\n", prefix.c_str(), casemode ? run.c_str() : "generic", c.n, c.ns, c.cp.first, c.c0, (c.c0 - c.cp.first) & 0xFFFF, c.K);
    for (int k = 0; k < c.ns; k++) {
        const Stream &s = c.st[k];
        printf("  stream %d: slot %d AR %d D1R %d DL %d D2R %d RR %d KRS %d OCT %d FNS %03x %s%s\n", k, s.slot, s.AR, s.D1R, s.DL, s.D2R, s.RR, s.KRS, s.OCT, s.FNS, s.ramp ? "ramp" : "const", s.witness ? " WITNESS" : "");
    }

    /* model */
    AicaModel m;
    m.eg_K = c.K;
    load_ram(m);
    for (int k = 0; k < c.ns; k++) write_slot(m, c.st[k]);
    for (int i = 0; i < 16; i++) m.step();
    m.MDEC_CT = mdec(c, 0);

    std::vector<Cyc> cycs;
    int kind = 0;
    if (casemode) {
        const int W = 300;
        uint64_t TEST = 0, WIT = 0;
        for (int k = 0; k < c.ns; k++) { if (c.st[k].witness) WIT |= 1ull << c.st[k].slot; else TEST |= 1ull << c.st[k].slot; }
        int wk = -1; for (int k = 0; k < c.ns; k++) if (c.st[k].witness) wk = k;
        kind = marks[5].empty() ? 0 : 1;
        auto &mA = marks[1], &mB = marks[3], &mC = marks[5], &mD = marks[7];
        int ncyc = kind == 0 ? (int)std::min(mA.size(), mB.size()) : (int)std::min(std::min(mA.size(), mB.size()), std::min(mC.size(), mD.size()));
        cycs.resize(ncyc);
        for (int cy = 0; cy < ncyc; cy++) cycs[cy].EA = find_496_onset(c, 0, mA[cy] - W, mA[cy] + W);
        printf("  kind %d (%s), %d cycles, test slots mask %02llx, witness mask %02llx\n", kind, kind == 0 ? "key-on / key-off, marks 1 / 3" : "kon_rel: on, off, on during the release, off; marks 1 / 3 / 5 / 7", ncyc, (unsigned long long)TEST, (unsigned long long)WIT);
        for (int cy = 0; cy < ncyc; cy++) {
            Cyc &y = cycs[cy];
            int nextEA = c.n;
            for (int z = cy + 1; z < ncyc; z++) if (cycs[z].EA > 0) { nextEA = cycs[z].EA; break; }
            if (y.EA < 0) { printf("  cycle %2d: key-on (496 onset on stream 0) NOT FOUND in [%d, %d]: cycle skipped\n", cy, mA[cy] - W, mA[cy] + W); continue; }
            c.evs[y.EA] = Ev{TEST, WIT, "E_A"};
            if (kind == 0) {
                y.EB = wk < 0 ? -1 : find_full_onset(c, wk, std::max(y.EA + 1, mB[cy] - W), mB[cy] + W);
                if (y.EB < 0) { printf("  cycle %2d: E_A %d (%s); key-off (witness onset) NOT FOUND in [%d, %d]\n", cy, y.EA, par(c, y.EA), std::max(y.EA + 1, mB[cy] - W), mB[cy] + W); continue; }
                c.evs[y.EB] = Ev{WIT, TEST, "E_B"};
                printf("  cycle %2d: E_A %d (%s) E_B %d (%s, %d samples on)\n", cy, y.EA, par(c, y.EA), y.EB, par(c, y.EB), y.EB - y.EA);
            } else {
                y.EC = wk < 0 ? -1 : find_full_onset(c, wk, std::max(y.EA + 1, mC[cy] - W), mC[cy] + W);
                if (y.EC < 0) { printf("  cycle %2d: E_A %d (%s); second key-on (witness onset) NOT FOUND in [%d, %d]\n", cy, y.EA, par(c, y.EA), std::max(y.EA + 1, mC[cy] - W), mC[cy] + W); continue; }
                c.evs[y.EC] = Ev{TEST | WIT, 0, "E_C"};
                printf("  cycle %2d: E_A %d (%s) E_C %d (%s)\n", cy, y.EA, par(c, y.EA), y.EC, par(c, y.EC));
                y.EB = search_event(m, c, y.EA + 100, y.EC - 50, y.EC, Ev{0, TEST | WIT, "E_B"}, cy);
                int lo = std::max(y.EC + 1, mD[cy] - W), hi = std::min(mD[cy] + W, nextEA - 1);
                y.ED = search_event(m, c, lo, hi, nextEA, Ev{0, TEST | WIT, "E_D"}, cy);
            }
        }
    } else {
        for (auto &sp : evspecs) {
            int s; unsigned long long on, off;
            if (sscanf(sp.c_str(), "%d:%llx:%llx", &s, &on, &off) != 3 || s < 0 || s >= c.n) { fprintf(stderr, "bad -ev %s\n", sp.c_str()); return 2; }
            Ev &e = c.evs[s]; e.on |= on; e.off |= off; e.name = "ev";
        }
        printf("  %zu events:", c.evs.size());
        for (auto &e : c.evs) printf(" %d(%s):on %llx:off %llx", e.first, par(c, e.first), (unsigned long long)e.second.on, (unsigned long long)e.second.off);
        printf("\n");
    }
    advance(m, c, c.n);

    /* report */
    std::vector<std::pair<int, std::string>> evlist;
    for (auto &e : c.evs) evlist.push_back({e.first, e.second.name});
    int clean = 0, ncyc_ok = 0;
    if (casemode) {
        for (size_t cy = 0; cy < cycs.size(); cy++) {
            const Cyc &y = cycs[cy];
            if (y.EA < 0) continue;
            ncyc_ok++;
            int end = c.n;
            for (size_t z = cy + 1; z < cycs.size(); z++) if (cycs[z].EA > 0) { end = cycs[z].EA; break; }
            printf("  cycle %2zu [%d, %d):", cy, y.EA, end);
            bool allok = true;
            for (int k = 0; k < c.ns; k++) {
                int fb = -1;
                for (int i = y.EA; i < end; i++) if (c.rec[k][i] != hw(c, i, k)) { fb = i; break; }
                if (fb < 0) { printf("  s%d ok", k); continue; }
                allok = false;
                std::string rel;
                for (auto &e : evlist) if (e.first <= fb && e.first >= y.EA) { char b[64]; snprintf(b, sizeof b, "%s%+d", e.second.c_str(), fb - e.first); rel = b; }
                printf("  s%d FAIL at %d (%s, %s: hw %d model %d, a 0x%03x %s%s)", k, fb, rel.c_str(), par(c, fb), hw(c, fb, k), c.rec[k][fb], c.rec_a[k][fb], stname(c.rec_st[k][fb] & 3), (c.rec_st[k][fb] & 4) ? " OFF" : "");
            }
            printf("\n");
            clean += allok;
        }
    }
    int nfull = 0; long matched = 0;
    for (int k = 0; k < c.ns; k++) {
        const Stream &s = c.st[k];
        printf("  stream %d (slot %d%s): ", k, s.slot, s.witness ? " witness" : "");
        if (c.first_bad[k] < 0) { nfull++; printf("FULL %d/%d", c.n, c.n); }
        else { printf("prefix %d/%d, %ld mismatching samples, ", c.first_bad[k], c.n, c.nbad[k]); describe_mismatch(c, k, c.first_bad[k], evlist); }
        printf("\n");
        matched += c.n - c.nbad[k];
        if (verbose && c.first_bad[k] >= 0) {
            for (int t = 0; t < 12 && c.first_bad[k] + t < c.n; t++) {
                int i = c.first_bad[k] + t;
                printf("      i %d MDEC_CT %04x %s: hw %8d model %8d (a 0x%03x %s%s CA %u)\n", i, mdec(c, i), (mdec(c, i) & 1) ? "odd " : "EVEN", hw(c, i, k), c.rec[k][i], c.rec_a[k][i], stname(c.rec_st[k][i] & 3), (c.rec_st[k][i] & 4) ? " OFF" : "", c.rec_ca[k][i]);
            }
        }
    }
    printf("TOTAL %s: streams FULL %d/%d, samples matched %ld/%ld", casemode ? run.c_str() : prefix.c_str(), nfull, c.ns, matched, (long)c.n * c.ns);
    if (casemode) printf(", clean cycles %d/%d", clean, ncyc_ok);
    printf("\n");
    return nfull == c.ns ? 0 : 1;
}
