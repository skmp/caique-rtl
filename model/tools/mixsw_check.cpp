// mixsw_check.cpp -- verdicts for the tests/mixs_write captures (cases/mixs_write.c; report
// work/verify/s5/case_mixs_write.md).  For every probe capture P*.hdr/.bin in DIR: the CPU-written values and their
// buses come from the marks (0x1BVVVVV before the write, 0x2BVVVVV after; B = bus, V = 20-bit value), so nothing is
// parsed from the text log.  Per written bus: the RLE of the captured values around the write, how many samples carry
// the CPU value (exact 20 bits / the 16-bit high field only, in case the two register writes straddled a sample and
// landed in different banks), and the verdict
//   LOST   the CPU value shows on <= 2 samples: the SGC rewrites this bus every sample
//   KEPT   the CPU value persists to the end of the capture on every other sample (one bank), the samples in between
//          hold one constant value (the other bank's old value): nobody rewrites this bus
//   OTHER  anything else (printed in full; e.g. the value persists on both banks, or disappears after a while)
// Then a probe x bus table with the prediction of every candidate rule, and which rules survive.
// build: make -C tools mixsw_check (-> build/tools/mixsw_check; single file, no model link)
// run:   build/tools/mixsw_check tests/mixs_write/hw     (or tests/mixs_write/model; -w N widens the RLE window; from caique-rtl/model)
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <dirent.h>

struct Cap {
    std::string name;
    uint32_t ns = 0, n = 0, first = 0, errors = 0, first_err = 0, nev = 0;
    int mixs[4] = {0, 0, 0, 0};
    std::vector<std::pair<uint32_t, uint32_t>> ev;   // id, absolute sample n
    std::vector<int32_t> v;                          // n x ns
    int32_t at(uint32_t i, int k) const { return v[(size_t)i * ns + k]; }
    int stream(int bus) const { for (uint32_t k = 0; k < ns; k++) if (mixs[k] == bus) return (int)k; return -1; }
};

static bool load(const std::string &dir, const std::string &name, Cap &c) {
    c.name = name;
    FILE *f = fopen((dir + "/" + name + ".hdr").c_str(), "rb");
    if (!f) return false;
    uint32_t h[16 + 2 * 64];
    size_t nh = fread(h, 4, sizeof h / 4, f);
    fclose(f);
    if (nh < 11 || h[0] != 0x31504143) return false;
    c.ns = h[1]; c.n = h[2]; c.first = h[3]; c.errors = h[4]; c.first_err = h[5]; c.nev = h[6];
    for (int k = 0; k < 4; k++) c.mixs[k] = (int)h[7 + k];
    for (uint32_t e = 0; e < c.nev && 11 + 2 * e + 1 < nh; e++) c.ev.push_back({h[11 + 2 * e], h[12 + 2 * e]});
    if (c.ns < 1 || c.ns > 4) return false;
    c.v.resize((size_t)c.n * c.ns);
    f = fopen((dir + "/" + name + ".bin").c_str(), "rb");
    if (!f) return false;
    size_t got = fread(c.v.data(), 4, c.v.size(), f);
    fclose(f);
    if (got != c.v.size()) { fprintf(stderr, "%s: short .bin (%zu of %zu values)\n", name.c_str(), got, c.v.size()); c.n = (uint32_t)(got / c.ns); }
    return true;
}

// candidate rules, in the order of the prediction strings below
static const char *HYP[] = {"H_M", "H_G", "H_B", "H_V", "H_F", "H_O", "H_S", "H_0"};
static const char *HYP_DESC[] = {
    "H_M  IMXL 0 never writes; a bus is rewritten iff a slot with IMXL != 0 has ISEL = bus (the model)",
    "H_G  every slot writes its ISEL bus every sample (IMXL is a gain, 0 -> 0); a bus retains iff nobody points at it",
    "H_B  slot 0 always writes its ISEL bus (0 when IMXL 0); other IMXL-0 slots never write",
    "H_V  an IMXL-0 slot writes 0 iff VOFF 0 (H_D: IMXL 0 = gain 0, VOFF 1 + IMXL 0 = no send)",
    "H_F  an IMXL-0 slot writes 0 iff LPOFF 0 (filter on)",
    "H_O  an IMXL-0 slot writes 0 iff the slot is off (not playing)",
    "H_S  an IMXL-0 slot writes 0 iff SA == 0",
    "H_0  bus 0 is rewritten every sample whoever points at it; other buses as H_M",
};
enum { NHYP = 8 };
struct Row { const char *probe; int bus; const char *desc; const char *pred; };   // pred[i]: K(EPT) / L(OST) under HYP[i]
static const Row ROWS[] = {
    {"P0",  0,  "64 zeroed slots (ISEL 0 IMXL 0 VOFF 0 LPOFF 0 SA 0, off)",      "KLLLLLLL"},
    {"P0",  1,  "nobody points at it",                                           "KKKKKKKK"},
    {"P1",  1,  "slot 0 alone (reg 0x20 = 0x01, else 0, off)",                   "KLLLLLLK"},
    {"P1",  0,  "slots 1..63 zeroed (ISEL 0)",                                   "KLKLLLLL"},
    {"P1b", 0,  "nobody (slot 0 at 1, slots 1..63 at 15)",                       "KKKKKKKL"},
    {"P1b", 1,  "slot 0 alone (ISEL 1, else 0, off)",                            "KLLLLLLK"},
    {"P1b", 15, "slots 1..63 (reg 0x20 = 0x0F, else 0, off)",                    "KLKLLLLK"},
    {"P2",  2,  "slot 7 alone (reg 0x20 = 0x02, else 0, off)",                   "KLKLLLLK"},
    {"P2",  3,  "slot 8 alone, IMXL 15 (0xF3), off: LOST control",               "LLLLLLLL"},
    {"P2",  4,  "slot 9 alone (ISEL 4, IMXL 0), SA 0x10000, never keyed",        "KLKLLLKK"},
    {"P3",  2,  "slot 7 alone, IMXL 0, VOFF 1, LPOFF 0, off",                    "KLKKLLLK"},
    {"P3",  3,  "slot 8 alone, IMXL 0, VOFF 0, LPOFF 1, off",                    "KLKLKLLK"},
    {"P3",  4,  "slot 9 alone, IMXL 0, VOFF 1, LPOFF 1, off",                    "KLKKKLLK"},
    {"P5",  2,  "slot 7 alone, PLAYING 0x7FFF, IMXL 0, VOFF 0, LPOFF 1",         "KLKLKKKK"},
    {"P5",  3,  "slot 8 alone, PLAYING 0x7FFF, IMXL 0, VOFF 1, LPOFF 1",         "KLKKKKKK"},
    {"P5",  4,  "slot 9 alone, PLAYING 0x7FFF, IMXL 0, VOFF 0, LPOFF 0",         "KLKLLKKK"},
    {"P7",  3,  "slot 0 alone, IMXL 0, VOFF 1, LPOFF 0, off",                    "KLLKLLLK"},
    {"P7",  15, "slots 1..63 (reg 0x20 = 0x0F, else 0, off)",                    "KLKLLLLK"},
    {"P7",  0,  "nobody (slot 0 at 3, slots 1..63 at 15)",                       "KKKKKKKL"},
    {"P8",  4,  "slot 63 alone (reg 0x20 = 0x04, else 0, off)",                  "KLKLLLLK"},
    {"P8",  0,  "slots 0..62 zeroed (ISEL 0)",                                   "KLLLLLLL"},
};
static const Row *find_row(const std::string &probe, int bus) {
    for (const Row &r : ROWS) if (probe == r.probe && r.bus == bus) return &r;
    return nullptr;
}

static void rle(const Cap &c, int k, uint32_t a, uint32_t b, int maxruns) {
    if (b > c.n) b = c.n;
    if (a >= b) { printf("   (empty)\n"); return; }
    int32_t prev = c.at(a, k);
    uint32_t start = a;
    int printed = 0;
    printf("  ");
    for (uint32_t i = a + 1; i <= b; i++) {
        int32_t cur = i < b ? c.at(i, k) : INT32_MIN;
        if (cur != prev || i == b) {
            printf(" %u:%d*%u", start, prev, i - start);
            if (++printed % 8 == 0 && i < b) printf("\n  ");
            if (printed >= maxruns && i < b) { printf(" ... (%u more samples to %u)", b - i, b); break; }
            prev = cur; start = i;
        }
    }
    printf("\n");
}
// most frequent values of stream k in [a, b), as "v*count" (top 3)
static std::string top_values(const Cap &c, int k, uint32_t a, uint32_t b, int32_t skip, bool use_skip) {
    std::map<int32_t, uint32_t> h;
    for (uint32_t i = a; i < b && i < c.n; i++) { int32_t x = c.at(i, k); if (use_skip && x == skip) continue; h[x]++; }
    std::vector<std::pair<uint32_t, int32_t>> s;
    for (auto &p : h) s.push_back({p.second, p.first});
    std::sort(s.rbegin(), s.rend());
    std::string out;
    char buf[64];
    for (size_t i = 0; i < s.size() && i < 3; i++) { snprintf(buf, sizeof buf, "%s%d*%u", i ? " " : "", s[i].second, s[i].first); out += buf; }
    if (s.size() > 3) { snprintf(buf, sizeof buf, " (+%zu other values)", s.size() - 3); out += buf; }
    if (s.empty()) out = "-";
    return out;
}

struct Verdict { std::string probe; int bus; uint32_t value; std::string verdict, detail; uint32_t exact, himatch; int first_off; };

static void analyse(const Cap &c, int win, std::vector<Verdict> &out) {
    printf("== %s: %u streams (MIXS", c.name.c_str(), c.ns);
    for (uint32_t k = 0; k < c.ns; k++) printf(" %d", c.mixs[k]);
    printf("), %u samples, n_first %u, errors %u", c.n, c.first, c.errors);
    if (c.errors) printf(" (first at n %u)", c.first_err);
    printf(", %u marks\n", c.nev);
    // writes: pairs of marks 0x1... / 0x2... with the same bus and value
    struct W { int bus; uint32_t value; uint32_t before, after; bool has_after; };
    std::vector<W> writes;
    for (auto &e : c.ev) {
        uint32_t kind = e.first >> 28, bus = (e.first >> 20) & 0xF, val = e.first & 0xFFFFF;
        uint32_t idx = e.second >= c.first ? e.second - c.first : 0;
        if (kind == 1) writes.push_back({(int)bus, val, idx, idx, false});
        else if (kind == 2) {
            for (auto &w : writes) if (w.bus == (int)bus && w.value == val && !w.has_after) { w.after = idx; w.has_after = true; break; }
        } else printf("   mark %08x at sample %u (not a write mark)\n", e.first, idx);
    }
    std::vector<bool> written(c.ns, false);
    for (size_t wi = 0; wi < writes.size(); wi++) {
        const W &w = writes[wi];
        int k = c.stream(w.bus);
        printf("-- write %05x to MIXS%d at sample %u..%u", w.value, w.bus, w.before, w.after);
        if (k < 0) { printf(": bus not captured\n"); continue; }
        written[k] = true;
        // the post-write window ends at the next write to the same bus, else at the capture end
        uint32_t end = c.n;
        for (size_t wj = wi + 1; wj < writes.size(); wj++) if (writes[wj].bus == w.bus) { end = std::min(end, writes[wj].before); break; }
        uint32_t from = w.before >= 6 ? w.before - 6 : 0;   // the mark estimate can be a few samples late or early
        const uint32_t hi16 = w.value >> 4;
        auto hit_hi = [&](uint32_t i) { return ((uint32_t)c.at(i, k) & 0xFFFF0u) >> 4 == hi16; };
        auto hit_ex = [&](uint32_t i) { return ((uint32_t)c.at(i, k) & 0xFFFFFu) == w.value; };
        uint32_t exact = 0, him = 0, first_hit = UINT32_MAX, last_hit = 0;
        for (uint32_t i = from; i < end; i++) {
            if (hit_ex(i)) exact++;
            if (hit_hi(i)) { him++; if (first_hit == UINT32_MAX) first_hit = i; last_hit = i; }
        }
        printf(" (window %u..%u): value seen on %u samples exact, %u with the high 16 bits", from, end, exact, him);
        if (him) printf(", first at %u (%+d from the mark), last at %u", first_hit, (int)first_hit - (int)w.before, last_hit);
        printf("\n");
        printf("   RLE around the write [%u, %u):\n", from, std::min(end, w.before + (uint32_t)win));
        rle(c, k, from, std::min(end, w.before + (uint32_t)win), 48);
        Verdict v{c.name, w.bus, w.value, "", "", exact, him, him ? (int)first_hit - (int)w.before : 0};
        if (him <= 2) {
            v.verdict = "LOST";
            v.detail = "bus afterwards: " + top_values(c, k, w.before, end, 0, false);
        } else {
            // alternation: hits on one parity from the first hit to the end, one constant value on the other parity
            uint32_t par = first_hit & 1, same_tot = 0, same_hit = 0, other_hit = 0;
            for (uint32_t i = first_hit; i < end; i++) {
                if ((i & 1) == par) { same_tot++; if (hit_hi(i)) same_hit++; }
                else if (hit_hi(i)) other_hit++;
            }
            std::map<int32_t, uint32_t> others;
            for (uint32_t i = first_hit; i < end; i++) if (!hit_hi(i)) others[c.at(i, k)]++;
            bool to_end = last_hit + 3 >= end;
            bool one_bank = same_hit * 10 >= same_tot * 9 && other_hit * 50 <= same_tot;   // every other sample
            bool both_banks = same_hit * 10 >= same_tot * 9 && other_hit * 10 >= same_tot * 9; // every sample
            char buf[256];
            std::string ob;   // the non-CPU values after the first appearance (the other bank's old value)
            for (auto &p : others) { snprintf(buf, sizeof buf, "%s%d*%u", ob.empty() ? "" : " ", p.first, p.second); ob += buf; }
            if (to_end && one_bank && others.size() <= 2) {
                v.verdict = "KEPT";
                snprintf(buf, sizeof buf, "alternating to the end (%u/%u on its parity, %u on the other); other bank: %s",
                         same_hit, same_tot, other_hit, ob.empty() ? "-" : ob.c_str());
                v.detail = buf;
            } else if (to_end && both_banks) {
                v.verdict = "KEPT";
                snprintf(buf, sizeof buf, "on BOTH banks to the end (%u/%u on its parity, %u on the other): the CPU write reached both banks",
                         same_hit, same_tot, other_hit);
                v.detail = buf;
            } else {
                v.verdict = "OTHER";
                snprintf(buf, sizeof buf, "%s%s; %u/%u on its parity, %u on the other; non-CPU values: %s",
                         to_end ? "persists to the end" : "disappears before the end", one_bank ? "" : ", not alternating",
                         same_hit, same_tot, other_hit, top_values(c, k, first_hit, end, 0, false).c_str());
                v.detail = buf;
            }
        }
        printf("   VERDICT %s: %s\n", v.verdict.c_str(), v.detail.c_str());
        out.push_back(v);
    }
    for (uint32_t k = 0; k < c.ns; k++)
        if (!written[k]) printf("-- MIXS%d not written (reference stream): %s\n", c.mixs[k], top_values(c, k, 0, c.n, 0, false).c_str());
    printf("\n");
}

int main(int argc, char **argv) {
    std::string dir;
    int win = 40;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-w") && i + 1 < argc) win = atoi(argv[++i]);
        else dir = argv[i];
    }
    if (dir.empty()) { fprintf(stderr, "usage: mixsw_check [-w WINDOW] DIR   (DIR = tests/mixs_write/hw or .../model)\n"); return 2; }
    std::vector<std::string> names;
    if (DIR *d = opendir(dir.c_str())) {
        while (dirent *e = readdir(d)) {
            std::string s = e->d_name;
            if (s.size() > 4 && s.compare(s.size() - 4, 4, ".hdr") == 0 && s[0] == 'P') names.push_back(s.substr(0, s.size() - 4));
        }
        closedir(d);
    } else { fprintf(stderr, "cannot open %s\n", dir.c_str()); return 2; }
    // probe order as in the case (P0 P1 P1b P2 P3 P5 P7 P8): by the probe index encoded in the written value is not
    // available before loading, so sort by name length then name (P1 < P1b) after a numeric key
    std::sort(names.begin(), names.end(), [](const std::string &a, const std::string &b) {
        int na = atoi(a.c_str() + 1), nb = atoi(b.c_str() + 1);
        return na != nb ? na < nb : a < b;
    });
    if (names.empty()) { fprintf(stderr, "no P*.hdr in %s\n", dir.c_str()); return 2; }
    std::vector<Verdict> all;
    for (auto &nm : names) {
        Cap c;
        if (!load(dir, nm, c)) { printf("== %s: cannot load\n\n", nm.c_str()); continue; }
        analyse(c, win, all);
    }
    // summary
    printf("==== summary: %s\n", dir.c_str());
    printf("%-4s %-3s %-58s %-6s %5s %5s %6s  %s\n", "probe", "bus", "who points at the bus", "result", "exact", "hi16", "first", "detail");
    for (auto &v : all) {
        const Row *r = find_row(v.probe, v.bus);
        printf("%-5s %-3d %-58s %-6s %5u %5u %+6d  %s\n", v.probe.c_str(), v.bus, r ? r->desc : "(no table entry)", v.verdict.c_str(),
               v.exact, v.himatch, v.first_off, v.detail.c_str());
    }
    // hypotheses
    printf("\n==== candidate rules (prediction per row: K = KEPT, L = LOST)\n");
    for (int h = 0; h < NHYP; h++) printf("  %s\n", HYP_DESC[h]);
    printf("\n%-5s %-3s %-6s", "probe", "bus", "result");
    for (int h = 0; h < NHYP; h++) printf(" %s", HYP[h]);
    printf("\n");
    int ok[NHYP] = {0}, bad[NHYP] = {0}, undecided = 0;
    std::string mism[NHYP];
    for (auto &v : all) {
        const Row *r = find_row(v.probe, v.bus);
        printf("%-5s %-3d %-6s", v.probe.c_str(), v.bus, v.verdict.c_str());
        for (int h = 0; h < NHYP; h++) {
            char p = r ? r->pred[h] : '?';
            char got = v.verdict == "KEPT" ? 'K' : v.verdict == "LOST" ? 'L' : '?';
            const char *mark = (p == '?' || got == '?') ? " " : p == got ? "=" : "X";
            printf("  %c%s ", p, mark);
            if (p != '?' && got != '?') { if (p == got) ok[h]++; else { bad[h]++; mism[h] += " " + v.probe + ":" + std::to_string(v.bus); } }
        }
        if (v.verdict == "OTHER") undecided++;
        printf("\n");
    }
    printf("\n==== rules vs the verdicts (%zu rows%s)\n", all.size(), undecided ? ", OTHER rows not counted" : "");
    for (int h = 0; h < NHYP; h++)
        printf("  %s  %2d agree  %2d disagree  %s%s\n", HYP[h], ok[h], bad[h], bad[h] ? "REFUTED by" : "consistent", mism[h].c_str());
    return 0;
}
