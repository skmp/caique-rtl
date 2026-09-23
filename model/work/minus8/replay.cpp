// replay.cpp -- what does a slot send after it stops?  Replays tests/eg_lock/hw/feg_odd slot 2 (program A, VOFF 1,
// filter Q 4, AR 31 RR 0: its AEG never releases) through the production AicaModel with the envelope clock locked to
// the capture (template tools/eg_model.cpp: MDEC_CT from c0/n_first/onset, K 6491, key-on on the onset sample,
// key-off on sample 6772), checks MIXS2/MIXS3 against the capture sample by sample, keeps stepping past the capture
// end, then applies the aica_quiet() sequence that eg_lock's mixs_run() ran on the console (reg 0x00 := 0 and
// reg 0x14 := 0x1F on every slot, one KYONEX, 20 ms) and prints slot 2's MIXS2 and state from that sample on.
//   replay [-gap N] [-post N] [-spread S] [-latched] [-o file] [-sweep N [-stride S]] [-K k]
//     -gap N     samples between the capture end and the quiet writes (default 4410 = 100 ms)
//     -post N    samples printed after the quiet KYONEX (default 1000)
//     -spread S  samples between slot 2's two register writes and the KYONEX (default 0: all in one gap)
//     -latched   keep SA / LPCTL / PCMS in reg 0x00 (only KYONB cleared): "the console latched them at key-on"
//     -sweep N   instead of one listing: try every gap in [0, N) with stride S (default 1), run the quiet sequence,
//                step 882 samples (20 ms) and tally the final MIXS2 / filter state (the landing distribution)
//     -o file    per-sample dump (t, MDEC_CT, MIXS2, a, state, off, enabled, CA, FEG.v, low, band)
// Build: g++ -O2 -std=c++17 -fopenmp -o build/work/minus8_replay work/minus8/replay.cpp src/aica_model.cpp
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <omp.h>
#include "../../src/aica_model.h"
#include "../../tools/filt_capture.h"
using namespace caique;

struct SlotCfg { int slot, ISEL; int AR = 31, D1R = 0, DL = 0, D2R = 0, RR = 31, KRS = 15, OCT = 0, FNS = 0, LSA = 0, LEA = 32, LPCTL = 1, LPSLNK = 0;
                 int VOFF = 0, LPOFF = 1, Q = 0, FLV[5] = {0x1FF8, 0x1FF8, 0x1FF8, 0x1FF8, 0x1FF8}, FAR = 31, FD1R = 31, FD2R = 31, FRR = 31; uint32_t SA = 0x10000; };
static void write_slot(AicaModel &m, const SlotCfg &c) {
    int ch = c.slot;
    auto aw = [&](uint32_t o, uint32_t v) { m.write(0x80 * ch + o, v); };
    aw(0x04, c.SA & 0xFFFF); aw(0x08, c.LSA); aw(0x0C, c.LEA);
    aw(0x10, (c.D2R << 11) | (c.D1R << 6) | c.AR);
    aw(0x14, (c.LPSLNK << 14) | (c.KRS << 10) | (c.DL << 5) | c.RR);
    aw(0x18, (c.OCT << 11) | c.FNS);
    aw(0x1C, 0);
    aw(0x20, (15 << 4) | c.ISEL);
    aw(0x24, 0);
    aw(0x28, (0 << 8) | (c.VOFF << 6) | (c.LPOFF << 5) | c.Q);
    for (int i = 0; i < 5; i++) aw(0x2C + 4 * i, c.FLV[i]);
    aw(0x40, (c.FAR << 8) | c.FD1R);
    aw(0x44, (c.FD2R << 8) | c.FRR);
    aw(0x00, (c.LPCTL << 9) | ((c.SA >> 16) & 0x7F));
}
static const char *stname(EgState s) { return s == EG_ATTACK ? "att" : s == EG_DECAY1 ? "d1" : s == EG_DECAY2 ? "d2" : "rel"; }
static void state_line(FILE *f, const AicaModel &m, long t, const char *tag) {
    const Slot &s = m.slot[2];
    uint32_t md = m.MDEC_CT;
    fprintf(f, "  t %5ld MDEC_CT %04x %s MIXS2 %7d | AEG a %03x %s off %d enabled %d CA %5u | FEG v %04x %s | low %d band %d%s%s\n", t, (md + 1) & 0xFFFF,
            ((md + 1) & 1) ? "odd " : "EVEN", m.MIXS[2], s.AEG.a, stname(s.AEG.state), s.AEG.off, s.enabled, s.CA, s.FEG.v, stname(s.FEG.state), s.lpf_low, s.lpf_band,
            tag ? "  " : "", tag ? tag : "");
}
// the quiet sequence of cases/aica_io.h aica_quiet() as the model sees it (ARMRST is not a slot register)
static void quiet_writes(AicaModel &m, bool latched) {
    for (int c = 0; c < 64; c++) {
        m.write(0x80 * c + 0x00, latched ? (m.chr(c, 0x00) & 0x3FFF) : 0);
        m.write(0x80 * c + 0x14, 0x1F);
    }
}
static void quiet_kyonex(AicaModel &m, bool latched) { m.write(0x00, latched ? ((m.chr(0, 0x00) & 0x3FFF) | 0x8000) : 0x8000); }

struct Land { int mixs2, low, band; int stop_t; const char *how; int last_change; bool settled; };
// run the quiet sequence on m (already at the chosen gap) and step `post` samples; returns the landing
static Land run_quiet(AicaModel &m, bool latched, int spread, int post, FILE *dump, bool print, uint32_t md_quiet) {
    Land L{}; L.stop_t = -1; L.how = "-"; L.last_change = -1;
    quiet_writes(m, latched);
    for (int i = 0; i < spread; i++) m.step();
    quiet_kyonex(m, latched);
    int prev = m.MIXS[2]; bool was_enabled = m.slot[2].enabled;
    int run_start = 0; int run_val = 0; bool first = true;
    std::string rle;
    for (int t = 0; t < post; t++) {
        uint32_t md = m.MDEC_CT;
        uint16_t a_prev = m.slot[2].AEG.a;
        m.step();
        const Slot &s = m.slot[2];
        if (was_enabled && !s.enabled) { L.stop_t = t; L.how = a_prev >= 0x3F8 ? "AEG off (release +8 past 0x3FF)" : "one-shot end (LPCTL 0, CA >= LEA)"; was_enabled = false;
            if (print) state_line(stdout, m, t, "<- slot 2 stops here"); }
        if (dump) fprintf(dump, "%d %04x %d %03x %d %d %d %u %04x %d %d\n", t, md, m.MIXS[2], s.AEG.a, s.AEG.state, s.AEG.off, s.enabled, s.CA, s.FEG.v, s.lpf_low, s.lpf_band);
        if (first) { run_start = t; run_val = m.MIXS[2]; first = false; }
        else if (m.MIXS[2] != run_val) { char b[64]; snprintf(b, sizeof b, " %d:%d*%d", run_start, run_val, t - run_start); rle += b; run_start = t; run_val = m.MIXS[2]; }
        if (m.MIXS[2] != prev) L.last_change = t;
        if (print && (t % 50 == 0 || (!s.enabled && m.MIXS[2] != prev))) state_line(stdout, m, t, nullptr);
        prev = m.MIXS[2];
    }
    { char b[64]; snprintf(b, sizeof b, " %d:%d*%d", run_start, run_val, post - run_start); rle += b; }
    if (print) {
        printf("  MIXS2 after the stop as RLE runs (t:value*len; t 0 = the sample after the KYONEX, MDEC_CT %04x %s):\n", md_quiet, (md_quiet & 1) ? "odd" : "even");
        // print only from the stop on (the pre-stop part is the live signal)
        printf("   %s\n", rle.c_str());
    }
    L.mixs2 = m.MIXS[2]; L.low = m.slot[2].lpf_low; L.band = m.slot[2].lpf_band;
    // settled: the state is a fixed point or short cycle -> check that another 64 steps do not change MIXS2 (fixed)
    L.settled = L.last_change >= 0 && L.last_change < post - 64;
    return L;
}

int main(int argc, char **argv) {
    long gap = 4410, post = 1000, sweep = -1, stride = 1, Kopt = 6491; int spread = 0; bool latched = false; const char *dumpf = nullptr;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-gap")) gap = atol(argv[++i]);
        else if (!strcmp(argv[i], "-post")) post = atol(argv[++i]);
        else if (!strcmp(argv[i], "-spread")) spread = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-latched")) latched = true;
        else if (!strcmp(argv[i], "-o")) dumpf = argv[++i];
        else if (!strcmp(argv[i], "-sweep")) sweep = atol(argv[++i]);
        else if (!strcmp(argv[i], "-stride")) stride = atol(argv[++i]);
        else if (!strcmp(argv[i], "-K")) Kopt = atol(argv[++i]);
        else { fprintf(stderr, "unknown arg %s\n", argv[i]); return 2; }
    }
    const char *path = "tests/eg_lock/hw/feg_odd";
    const uint32_t c0ring = 0x0374;
    auto cp = cap(path);
    // programs: A on slots 0 and 2, B on slot 1, reference (no filter) on slot 3 -- cases/eg_lock.c feg_run()
    int far[3] = {24, 25, 24}, fd1[3] = {26, 27, 26}, fd2[3] = {28, 29, 28}, frr[3] = {22, 23, 22};
    std::vector<SlotCfg> slots;
    for (int k = 0; k < 3; k++) {
        SlotCfg c; c.slot = k; c.ISEL = k; c.SA = 0x20000; c.LEA = 8192; c.VOFF = 1; c.LPOFF = 0; c.Q = 4; c.OCT = 0; c.FNS = 0x200; c.RR = 0; c.KRS = 0;
        int flv[5] = {0x1800, 0x1C00, 0x1800, 0x1A00, 0x1C00}; for (int j = 0; j < 5; j++) c.FLV[j] = flv[j];
        c.FAR = far[k]; c.FD1R = fd1[k]; c.FD2R = fd2[k]; c.FRR = frr[k];
        slots.push_back(c);
    }
    { SlotCfg ref; ref.slot = 3; ref.ISEL = 3; ref.SA = 0x20000; ref.LEA = 8192; ref.VOFF = 1; ref.LPOFF = 1; ref.Q = 4; ref.OCT = 0; ref.FNS = 0x200; ref.RR = 31; ref.KRS = 15;
      for (int j = 0; j < 5; j++) ref.FLV[j] = 0x1FFE; ref.FAR = ref.FD1R = ref.FD2R = ref.FRR = 0; slots.push_back(ref); }
    int on = 1; while (on < (int)cp.n && !cp.v[on * cp.ns + 3]) on++;
    uint32_t md_on = (c0ring - cp.first - on) & 0xFFFF;
    const int keyoff = 6772;   // work/verify/expected/eg_model_all.txt: feg_odd key-off samples 6772(odd)
    AicaModel *mp = new AicaModel; AicaModel &m = *mp;
    m.eg_K = (uint32_t)Kopt;
    { uint32_t seed = 4242; for (int i = 0; i < 8192; i++) { seed = seed * 1103515245u + 12345u; int16_t s = (int16_t)(seed >> 16); if (!s) s = 1; m.ram[0x20000 + 2 * i] = (uint8_t)s; m.ram[0x20000 + 2 * i + 1] = (uint8_t)(s >> 8); } }
    for (auto &c : slots) write_slot(m, c);
    for (int i = 0; i < 16; i++) m.step();
    m.MDEC_CT = md_on;
    for (auto &c : slots) m.write(0x80 * c.slot, m.chr(c.slot, 0) | 0x4000);
    m.write(0, m.chr(0, 0) | 0x8000);
    long mism2 = 0, mism3 = 0; int first2 = -1, first3 = -1, last2 = -1; int v_1bff_at = -1; uint16_t vprev = 0;
    for (int i = on; i < (int)cp.n; i++) {
        if (i == keyoff) { for (int k = 0; k < 3; k++) m.write(0x80 * k, m.chr(k, 0) & 0x3FFF); m.write(0, m.chr(0, 0) | 0x8000); }
        m.step();
        if (m.MIXS[2] != cp.v[i * cp.ns + 2]) { mism2++; if (first2 < 0) first2 = i; last2 = i; }
        if (m.MIXS[3] != cp.v[i * cp.ns + 3]) { mism3++; if (first3 < 0) first3 = i; }
        if (m.slot[2].FEG.v != vprev) { vprev = m.slot[2].FEG.v; if (i > keyoff && vprev == 0x1BFF && v_1bff_at < 0) v_1bff_at = i; }
    }
    printf("feg_odd replay: n %u n_first %u onset %d (MDEC_CT %04x %s) K %u key-off %d; MIXS2 mismatches %ld (first %d, last %d: the console's inherited filter state, model starts from 0), MIXS3 mismatches %ld (first %d)\n",
           cp.n, cp.first, on, md_on, (md_on & 1) ? "odd" : "even", m.eg_K, keyoff, mism2, first2, last2, mism3, first3);
    state_line(stdout, m, (long)cp.n - 1, "<- capture end");
    printf("  FEG of slot 2 (release from FLV3 0x1A00 toward FLV4 0x1C00 at R 45) reached its hold value 0x1BFF at capture sample %d\n", v_1bff_at);
    // snapshot at the capture end (RAM is not written during the replay: the DSP program is empty)
    AicaModel *snap = (AicaModel *)malloc(sizeof(AicaModel)); memcpy((void *)snap, &m, sizeof m);
    if (sweep < 0) {
        for (long i = 0; i < gap; i++) m.step();
        printf("after %ld more samples (%.1f ms):\n", gap, gap / 44.1);
        state_line(stdout, m, (long)cp.n - 1 + gap, "<- just before the quiet writes");
        uint32_t md_quiet = (m.MDEC_CT - (uint32_t)spread) & 0xFFFF;
        printf("quiet sequence (%s): reg 0x00 := %s, reg 0x14 := 0x1F on all slots, %d samples, KYONEX; then %ld samples:\n", latched ? "SA/LPCTL kept" : "exact aica_quiet",
               latched ? "KYONB cleared only" : "0 (SA -> 0, LPCTL -> 0)", spread, post);
        FILE *df = dumpf ? fopen(dumpf, "w") : nullptr;
        if (df) fprintf(df, "# t MDEC_CT MIXS2 a state off enabled CA FEGv low band\n");
        Land L = run_quiet(m, latched, spread, (int)post, df, true, md_quiet);
        if (df) fclose(df);
        printf("landing: MIXS2 %d (low %d band %d), slot stopped at t %d (%s), last output change at t %d%s\n", L.mixs2, L.low, L.band, L.stop_t, L.how, L.last_change,
               L.settled ? "" : " (NOT settled within the window)");
    } else {
        // sweep the gap: landing distribution (OpenMP over gaps, each thread its own model copy sharing the RAM image)
        // each thread owns a contiguous range of gaps: a base model advanced one stride at a time (shared RAM image,
        // read-only: the DSP program is empty) and a scratch copy for the quiet run
        std::vector<Land> lands((sweep + stride - 1) / stride);
        std::vector<uint32_t> mds(lands.size());
        std::vector<uint32_t> cas(lands.size());
        #pragma omp parallel
        {
            AicaModel *base = new AicaModel, *scr = new AicaModel;
            memcpy(base->ram, m.ram, AicaModel::RAM_SIZE);
            uint8_t *ram = base->ram, *ram2 = scr->ram;
            free(ram2); scr->ram = ram;
            memcpy((void *)base, snap, sizeof *base); base->ram = ram;
            int nt = omp_get_num_threads(), id = omp_get_thread_num();
            long n = (long)lands.size(), lo = n * id / nt, hi = n * (id + 1) / nt;
            for (long i = 0; i < lo * stride; i++) base->step();
            for (long gi = lo; gi < hi; gi++) {
                memcpy((void *)scr, base, sizeof *scr); scr->ram = ram;
                mds[gi] = scr->MDEC_CT; cas[gi] = scr->slot[2].CA;
                lands[gi] = run_quiet(*scr, latched, spread, 882, nullptr, false, 0);
                for (long i = 0; i < stride; i++) base->step();
            }
            scr->ram = (uint8_t *)calloc(1, 1); delete scr;   /* its ram was the shared image */
            delete base;
        }
        std::map<int, long> hist; std::map<std::pair<int, int>, long> states; std::map<std::pair<int, long>, long> byparity; std::map<std::string, long> hows;
        long unsettled = 0, never = 0; long stop_min = 1 << 30, stop_max = -1; long tail_sum = 0, tail_max = 0;
        for (size_t gi = 0; gi < lands.size(); gi++) {
            auto &L = lands[gi];
            hist[L.mixs2]++; states[{L.low, L.band}]++;
            byparity[{(int)(mds[gi] & 1), L.mixs2}]++;
            if (!L.settled) unsettled++;
            if (L.stop_t >= 0) { hows[L.how]++; stop_min = std::min<long>(stop_min, L.stop_t); stop_max = std::max<long>(stop_max, L.stop_t); tail_sum += L.last_change - L.stop_t; tail_max = std::max<long>(tail_max, L.last_change - L.stop_t); }
            else never++;
        }
        printf("sweep: %zu gaps in [0, %ld) stride %ld after the capture end, %s, spread %d; 882 samples after the quiet KYONEX:\n", lands.size(), sweep, stride,
               latched ? "SA/LPCTL kept" : "exact aica_quiet (SA -> 0, LPCTL -> 0)", spread);
        printf("  slot 2 stops at t %ld..%ld after the KYONEX sample (never stopped: %ld);", stop_min, stop_max, never);
        for (auto &h : hows) printf(" %s: %ld;", h.first.c_str(), h.second);
        printf("\n  output settles %.1f samples after the stop on average, max %ld; unsettled within 882 samples: %ld\n",
               lands.empty() ? 0.0 : (double)tail_sum / (double)(lands.size() - never), tail_max, unsettled);
        printf("  final MIXS2 histogram:");
        for (auto &h : hist) printf(" %d:%ld(%.1f%%)", h.first, h.second, 100.0 * h.second / lands.size());
        printf("\n  final (low, band) states:");
        for (auto &s : states) printf(" (%d,%d):%ld", s.first.first, s.first.second, s.second);
        printf("\n  by parity of the quiet-write sample (MDEC_CT&1 : MIXS2 : count):");
        for (auto &p : byparity) printf(" %d:%ld:%ld", p.first.first, p.first.second, p.second);
        printf("\n");
    }
    return 0;
}
