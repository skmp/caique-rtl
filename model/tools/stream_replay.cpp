// stream_replay.cpp -- replay captured streams through the cycle model, sample by sample, ring-locked (TODO 5).
//   stream_replay [-v] [-w N] [-end M] [-lfo] DIR TEXT CAPTURE [stream ...]
// DIR is tests/<case>/<platform>, TEXT the case's log in it, CAPTURE a capture prefix (CAPTURE.hdr / .bin in DIR).
// The case log must carry:
//   "slotregs CAPTURE <stream> <slot> r00 .. r44"   the slot's register image (cases/aica_io.h slot_log); stream k is
//                                                    captured from the bus the image's ISEL names
//   "capture CAPTURE c0 X first F samples N head n H t_head_us T"   (cases/cap.h cap_save)
//   "ramfile <addr hex> <file>"                      wave RAM contents (files in DIR), loaded before anything runs
// and DIR the run's replay parameters (replay.txt, replay_log.txt: tools/replay_fit).  Per stream: a fresh cycle model,
// RAM loaded, MDEC_CT / LFSR / K set so that its samples carry the capture's MDEC_CT labels (the LFSR from replay.txt,
// with the number of MDEC_CT wraps between the preamble's sync and the capture taken from the two SH4 times), the
// slot's registers written (0x1C with LFORE clear), then KYONB + KYONEX at every sample boundary of a window around the
// stream's onset (the first non-zero sample; -w N: N samples either side, default 4); the candidate with the longest
// matching prefix is reported (ties listed).  Compared: the model's bus value after each sweep (AicaModel::MIXS, what
// the next DSP sample reads) against the capture sample with the same MDEC_CT, from the onset to the key-off mark
// (-end M, default 3) minus 400 samples, or the end.  -lfo: for a stream with ALFOS or PLFOS set that does not match,
// also try every LFO counter value at the key-on (1..1024; the model reloads the counter on the 0x1C write and counts
// only while the slot plays) at the best key-on boundary, and list the values that match in full.  Exit 0 iff every
// stream matches in full.
// Build: make -C tools stream_replay (links cycle-model/aica_model.cpp).  Run from caique-rtl/model.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <cmath>
#include "aica_model.h"
#include "filt_capture.h"
using namespace caique;

static uint32_t lfsr_step(uint32_t l) { return (l >> 1) | ((((l >> 0) ^ (l >> 5)) & 1) << 16); }
static uint32_t lfsr_steps(uint32_t l, uint64_t n) { n %= 131071; for (uint64_t i = 0; i < n; i++) l = lfsr_step(l); return l; }

struct SlotImg { int stream, slot; uint16_t r[18]; };
struct RamFile { uint32_t addr; std::string file; };

static bool read_marks(const std::string &path, uint32_t first, std::map<int, int> &marks) {
    FILE *f = fopen((path + ".hdr").c_str(), "rb");
    if (!f) return false;
    uint32_t h[16 + 128] = {0};
    size_t nh = fread(h, 4, sizeof h / 4, f);
    fclose(f);
    for (size_t i = 11; i + 1 < nh && i < 11 + 2 * h[6]; i += 2)
        if (!marks.count((int)h[i])) marks[(int)h[i]] = (int)(h[i + 1] - first);
    return true;
}

int main(int argc, char **argv) {
    bool verbose = false, lfo = false; int win = 4, end_mark = 3;
    int k_alfo_noff = 2, k_plfo_noff = -67, k_tri_a = 126, k_tri_b = -128, k_plfo_nxor = 0x80;   // model experiment knobs (-knob name=value; defaults: the rules)
    std::vector<std::string> pos;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-v")) verbose = true;
        else if (!strcmp(argv[i], "-w") && i + 1 < argc) win = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-end") && i + 1 < argc) end_mark = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-lfo")) lfo = true;
        else if (!strcmp(argv[i], "-knob") && i + 1 < argc) {
            char nm[32]; int v;
            if (sscanf(argv[++i], "%31[^=]=%d", nm, &v) == 2) {
                if (!strcmp(nm, "alfo_noff")) k_alfo_noff = v; else if (!strcmp(nm, "plfo_noff")) k_plfo_noff = v;
                else if (!strcmp(nm, "tri_a")) k_tri_a = v; else if (!strcmp(nm, "tri_b")) k_tri_b = v;
                else if (!strcmp(nm, "plfo_nxor")) k_plfo_nxor = v;
            }
        }
        else pos.push_back(argv[i]);
    }
    if (pos.size() < 3) { fprintf(stderr, "usage: stream_replay [-v] [-w N] DIR TEXT CAPTURE [stream ...]\n"); return 2; }
    const std::string dir = pos[0], text = pos[1], capname = pos[2];
    std::vector<int> only;
    for (size_t i = 3; i < pos.size(); i++) only.push_back(atoi(pos[i].c_str()));

    // ---- the case log ----
    std::vector<SlotImg> imgs; std::vector<RamFile> rams;
    bool have_cap = false; unsigned c0 = 0; unsigned long long t_head = 0; unsigned long n_head = 0;
    {
        std::string p = dir + "/" + text;
        FILE *f = fopen(p.c_str(), "r");
        if (!f) { perror(p.c_str()); return 2; }
        char line[1024];
        while (fgets(line, sizeof line, f)) {
            char nm[128]; int k, slot, pos2 = 0; unsigned a;
            if (sscanf(line, "slotregs %127s %d %d%n", nm, &k, &slot, &pos2) == 3 && capname == nm) {
                SlotImg im{k, slot, {}};
                const char *q = line + pos2;
                for (int i = 0; i < 18; i++) { unsigned v; int n = 0; if (sscanf(q, " %x%n", &v, &n) != 1) break; im.r[i] = (uint16_t)v; q += n; }
                imgs.push_back(im);
            } else if (sscanf(line, "capture %127s c0 %x", nm, &a) == 2 && capname == nm) {
                c0 = a; have_cap = true;
                const char *h = strstr(line, " head n "); if (h) n_head = strtoul(h + 8, nullptr, 10);
                const char *t = strstr(line, "t_head_us "); if (t) t_head = strtoull(t + 10, nullptr, 10);
            } else {
                char fn[256];
                if (sscanf(line, "ramfile %x %255s", &a, fn) == 2) rams.push_back({a, fn});
            }
        }
        fclose(f);
        if (!have_cap || imgs.empty()) { fprintf(stderr, "%s: no capture line or no slotregs for %s\n", p.c_str(), capname.c_str()); return 2; }
    }
    // ---- replay parameters ----
    unsigned rp_x = 0, rp_l = 0, rp_k = 0, rp_par = 0; unsigned long long t_sync = 0; bool have_rp = false, have_sync = false;
    {
        FILE *f = fopen((dir + "/replay.txt").c_str(), "r");
        char line[256];
        if (f) {
            while (fgets(line, sizeof line, f))
                if (sscanf(line, "mdec %x lfsr %x K %u", &rp_x, &rp_l, &rp_k) == 3) { have_rp = true; const char *q = strstr(line, " par "); rp_par = q ? (unsigned)atoi(q + 5) & 1 : 0; }
            fclose(f);
        }
        f = fopen((dir + "/replay_log.txt").c_str(), "r");
        if (f) { while (fgets(line, sizeof line, f)) { const char *t = strstr(line, "replay sync:"); const char *u = t ? strstr(t, "t_sync_us ") : nullptr; if (u) { t_sync = strtoull(u + 10, nullptr, 10); have_sync = true; } } fclose(f); }
        if (!have_rp || !have_sync) { fprintf(stderr, "%s: no replay.txt / replay_log.txt sync line (run tools/replay_fit)\n", dir.c_str()); return 2; }
    }
    Capture c;
    try { c = cap(dir + "/" + capname); } catch (std::exception &e) { fprintf(stderr, "%s: %s\n", capname.c_str(), e.what()); return 2; }
    std::map<int, int> marks;
    read_marks(dir + "/" + capname, c.first, marks);
    const int end_all = marks.count(end_mark) ? std::min<int>(c.n, marks[end_mark] - 400) : (int)c.n;
    auto X = [&](int i) { return (c0 - c.first - (uint32_t)i) & 0xFFFF; };   // MDEC_CT of capture sample i
    // f(X) = the LFSR at the ph 0 with MDEC_CT X: replay.txt gives f(rp_x - 1); D samples later with D from the SH4 time
    auto f_of = [&](int i) {
        const double t_i = (double)t_head + ((double)i + c.first - (double)n_head) * (1e6 / 44100.0);
        const double d_time = (t_i - (double)t_sync) * 0.0441;
        const uint32_t d_mod = ((rp_x - 1) - X(i)) & 0xFFFF;
        const long long w = std::llround((d_time - d_mod) / 65536.0);
        const long long D = (long long)d_mod + 65536 * w;
        return lfsr_steps(rp_l, D >= 0 ? 64ull * (uint64_t)D : 64ull * (uint64_t)(D % 131071 + 131071));
    };
    std::vector<uint8_t> ramimg(AicaModel::RAM_SIZE, 0);
    for (auto &rf : rams) {
        std::string p = dir + "/" + rf.file;
        FILE *f = fopen(p.c_str(), "rb");
        if (!f) { perror(p.c_str()); return 2; }
        size_t n = fread(ramimg.data() + (rf.addr & (AicaModel::RAM_SIZE - 1)), 1, AicaModel::RAM_SIZE - (rf.addr & (AicaModel::RAM_SIZE - 1)), f);
        fclose(f);
        if (verbose) printf("ram %06x <- %s (%zu bytes)\n", rf.addr, rf.file.c_str(), n);
    }
    printf("stream_replay %s %s: %u samples, c0 %04x, K %u, compared up to sample %d%s\n", dir.c_str(), capname.c_str(), c.n, c0, rp_k,
           end_all, marks.count(end_mark) ? " (key-off mark - 400)" : "");

    int bad = 0;
    for (const SlotImg &im : imgs) {
        if (!only.empty() && std::find(only.begin(), only.end(), im.stream) == only.end()) continue;
        const int k = im.stream, bus = im.r[8] & 0xF;
        if (k < 0 || k >= (int)c.ns) continue;
        int onset = 0;
        while (onset < end_all && !c.v[(size_t)onset * c.ns + k]) onset++;
        if (onset >= end_all) { printf("  stream %d (slot %d): silent\n", k, im.slot); continue; }
        const int i0 = std::max(0, onset - win - 8);
        struct Res { int j, m, first_bad; int32_t hw, md; };
        auto sim = [&](int j, int lfoc, int lfos = 0) {   // j: the capture sample at whose boundary KYONEX is written; lfoc > 0: the
                                            // slot's LFO counter set there (state 0)
            AicaModel M;
            memcpy(M.ram, ramimg.data(), AicaModel::RAM_SIZE);
            M.MDEC_CT = (X(i0) + 1) & 0xFFFF;   // after the first sweep MDEC_CT = X(i0): the capture sample it feeds
            M.lfsr = lfsr_steps(f_of(i0), 64ull * 131070);   // f(X(i0) + 1): one sample (64 steps) before i0's
            M.eg_K = rp_k;
            M.eg_par = rp_par;
            M.x_alfo_noff = k_alfo_noff; M.x_plfo_noff = k_plfo_noff; M.x_tri_a = k_tri_a; M.x_tri_b = k_tri_b; M.x_plfo_nxor = k_plfo_nxor;
            for (int r = 1; r < 18; r++) M.write(0x80 * im.slot + 4 * r, r == 7 ? (im.r[r] & 0x7FFF) : im.r[r]);
            M.write(0x80 * im.slot, im.r[0]);
            int i = i0 - 1, m = 0, first_bad = -1; int32_t hwv = 0, mdv = 0;
            for (;;) {
                const int next = i + 1;   // the capture sample this sweep feeds (MDEC_CT labels repeat every 65536 samples: count)
                if (next == j) {
                    M.write(0x80 * im.slot, im.r[0] | 0x4000); M.write(0x80 * im.slot, im.r[0] | 0x4000 | 0x8000);
                    if (lfoc > 0) { M.slot[im.slot].lfo.counter = (uint32_t)lfoc; M.slot[im.slot].lfo.state = (uint8_t)lfos; }
                }
                M.step();
                i = next;
                if (i >= end_all) break;
                if (i < onset - win) continue;   /* the samples before the onset must be silent too */
                const int32_t hw = c.v[(size_t)i * c.ns + k], md = M.MIXS[bus];
                if (hw != md) { first_bad = i; hwv = hw; mdv = md; break; }
                m++;
            }
            return Res{j, m, first_bad, hwv, mdv};
        };
        std::vector<Res> res;
        for (int j = onset - win; j <= onset + win; j++) if (j > i0) res.push_back(sim(j, 0));
        std::sort(res.begin(), res.end(), [](const Res &a, const Res &b) { return a.m > b.m; });
        const int total = end_all - std::max(0, onset - win);
        const Res &b = res[0];
        printf("  stream %d (slot %d, bus %d, r00 %04x r18 %04x r1c %04x r28 %04x): onset %d, key-on boundary %+d: %d/%d%s", k, im.slot, bus,
               im.r[0], im.r[6], im.r[7], im.r[10], onset, b.j - onset, b.m, total, b.m == total ? " FULL" : "");
        if (b.first_bad >= 0) printf(", first mismatch at %d (+%d): hw %d model %d", b.first_bad, b.first_bad - onset, b.hw, b.md);
        int ties = 0;
        for (size_t t = 1; t < res.size(); t++) if (res[t].m == b.m) ties++;
        if (ties) printf(" (%d tied candidates)", ties);
        printf("\n");
        if (verbose) for (auto &r : res) printf("    boundary %+d: %d\n", r.j - onset, r.m);
        bool full = b.m == total;
        if (lfo && !full && ((im.r[7] & 7) || ((im.r[7] >> 5) & 7))) {
            // the LFO state and counter at the key-on: counter 1..period (LFOF), state 0 first, then 1..255
            const uint32_t n = (im.r[7] >> 10) & 0x1F, S = n >> 2, Mm = (~n) & 3, G = 128u >> S, per = ((G - 1) << 2) + G * (Mm + 1);
            std::vector<std::pair<int, int>> fulls; int bestm = -1, bestc = 1, bests = 0; Res bestr{};
            for (int st = 0; st < 256 && fulls.empty(); st++) {
                std::vector<Res> lr(per);
#pragma omp parallel for schedule(dynamic)
                for (int cc = 1; cc <= (int)per; cc++) lr[cc - 1] = sim(b.j, cc, st);
                for (int cc = 1; cc <= (int)per; cc++) {
                    if (lr[cc - 1].m == total) fulls.push_back({st, cc});
                    if (lr[cc - 1].m > bestm) { bestm = lr[cc - 1].m; bestc = cc; bests = st; bestr = lr[cc - 1]; }
                }
            }
            printf("    -lfo: LFO (state, counter) at the key-on (period %u): ", per);
            if (fulls.empty()) printf("none in full; best %d/%d at (%d, %d), first mismatch at %d: hw %d model %d\n", bestm, total, bests, bestc,
                                      bestr.first_bad, bestr.hw, bestr.md);
            else { printf("%zu in full:", fulls.size()); for (size_t q = 0; q < fulls.size() && q < 8; q++) printf(" (%d, %d)", fulls[q].first, fulls[q].second); printf("\n"); full = true; }
        }
        if (!full) bad++;
    }
    printf("stream_replay %s: %s\n", capname.c_str(), bad ? "MISMATCH" : "all streams FULL");
    return bad ? 1 : 0;
}
