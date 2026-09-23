// eg_model.cpp -- replay captured slot programs through the production AicaModel with the envelope clock locked
// to the capture's ring position (NOTES.md "Envelope clock"), and compare every sample.
// For each run: the slots are configured like the case, the model's MDEC_CT is set to the capture's MDEC_CT of the
// onset sample (c0 - n_first - onset; c0 from the case's cap_start line), eg_K is the boot constant, the key-on
// lands on the onset sample, and the key-off sample is searched between mark 3 - 64 and mark 4 + 400 (marks are
// head estimates) from a snapshot.  AEG runs compare MIXS; FEG runs compare FEG.v >> 1 with the tracked u(n) file.
//   eg_model [-K k] [name...]        (run from caique-rtl/model; default: every run)
// Build: make -C tools eg_model (-> build/tools/eg_model; links src/aica_model.cpp)
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <functional>
#include "../src/aica_model.h"
#include "filt_capture.h"
using namespace caique;

struct SlotCfg { int slot, ISEL; int AR = 31, D1R = 0, DL = 0, D2R = 0, RR = 31, KRS = 15, OCT = 0, FNS = 0, LSA = 0, LEA = 32, LPCTL = 1, LPSLNK = 0;
                 int VOFF = 0, LPOFF = 1, Q = 0, FLV[5] = {0x1FF8, 0x1FF8, 0x1FF8, 0x1FF8, 0x1FF8}, FAR = 31, FD1R = 31, FD2R = 31, FRR = 31; uint32_t SA = 0x10000; };
struct Run { std::string name, path; uint32_t c0ring, K; int ram_kind; std::vector<SlotCfg> slots; int ref = -1; std::string ufile; };
// ram_kind: 0 = 32 x 0x7FFF at 0x10000; 1 = sgc_loop ramp at 0x20000; 2 = feg_track random signal at 0x20000 (8192)

static void load_ram(AicaModel &m, int kind) {
    if (kind == 0) for (int i = 0; i < 32; i++) { m.ram[0x10000 + 2 * i] = 0xFF; m.ram[0x10000 + 2 * i + 1] = 0x7F; }
    if (kind == 1) for (int i = 0; i < 4096; i++) { int16_t s = (int16_t)(8 * i - 0x4000); m.ram[0x20000 + 2 * i] = (uint8_t)s; m.ram[0x20000 + 2 * i + 1] = (uint8_t)(s >> 8); }
    if (kind == 2) { uint32_t seed = 4242; for (int i = 0; i < 8192; i++) { seed = seed * 1103515245u + 12345u; int16_t s = (int16_t)(seed >> 16); if (!s) s = 1; m.ram[0x20000 + 2 * i] = (uint8_t)s; m.ram[0x20000 + 2 * i + 1] = (uint8_t)(s >> 8); } }
}
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
struct Snap { AicaModel m; uint8_t *ram; };
static void snap_take(Snap &s, const AicaModel &m) { memcpy((void *)&s.m, &m, sizeof m); memcpy(s.ram, m.ram, AicaModel::RAM_SIZE); }
static void snap_restore(AicaModel &m, const Snap &s) { uint8_t *ram = m.ram; memcpy((void *)&m, &s.m, sizeof m); m.ram = ram; memcpy(ram, s.ram, AicaModel::RAM_SIZE); }

int main(int argc, char **argv) {
    long Kopt = -1; bool verbose = false;
    std::vector<std::string> want;
    for (int i = 1; i < argc; i++) { if (!strcmp(argv[i], "-K")) Kopt = atol(argv[++i]); else if (!strcmp(argv[i], "-v")) verbose = true; else want.push_back(argv[i]); }
    std::vector<Run> runs;
    auto aeg = [&](const char *name, const char *path, uint32_t c0, uint32_t K, int slots[4], int AR[4], int D1R[4], int DL[4], int D2R[4], int RR[4], int KRS, int OCT, int FNS) {
        Run r{name, path, c0, K, 0, {}};
        for (int k = 0; k < 4; k++) { SlotCfg c; c.slot = slots[k]; c.ISEL = k; c.AR = AR[k]; c.D1R = D1R[k]; c.DL = DL[k]; c.D2R = D2R[k]; c.RR = RR[k]; c.KRS = KRS; c.OCT = OCT; c.FNS = FNS; r.slots.push_back(c); }
        runs.push_back(r);
    };
    int s0123[4] = {0, 1, 2, 3}, s5[4] = {5, 17, 40, 63}, rr31[4] = {31, 31, 31, 31}, z[4] = {0, 0, 0, 0};
    { int a[4] = {31, 31, 31, 20}, d[4] = {31, 20, 10, 31}, d2[4] = {0, 0, 0, 10}; aeg("aeg_dl0", "tests/aeg_dl0/hw/dl0", 0x87bb, 6491, s0123, a, d, z, d2, rr31, 15, 0, 0); }
    { int a[4] = {6, 4, 2, 1}; aeg("att_slow", "tests/eg_lock/hw/att_slow", 0x44c9, 6491, s0123, a, z, z, z, rr31, 15, 0, 0); }
    { int a[4] = {22, 20, 18, 16}; aeg("att_mid", "tests/eg_lock/hw/att_mid", 0x3f0e, 6491, s0123, a, z, z, z, rr31, 15, 0, 0); }
    { int a[4] = {22, 24, 26, 28}; aeg("odd_att", "tests/eg_lock/hw/odd_att", 0xafdf, 6491, s0123, a, z, z, z, rr31, 0, 0, 0x200); }
    { int a[4] = {23, 25, 27, 29}; aeg("odd_att3", "tests/eg_lock/hw/odd_att3", 0x7d3a, 6491, s0123, a, z, z, z, rr31, 0, 0, 0x200); }
    { int a[4] = {24, 24, 24, 24}; aeg("odd_same", "tests/eg_lock/hw/odd_same", 0x4a7b, 6491, s0123, a, z, z, z, rr31, 0, 0, 0x200); }
    { int a[4] = {24, 24, 24, 24}; aeg("odd_same2", "tests/eg_lock/hw/odd_same2", 0x744b, 6491, s5, a, z, z, z, rr31, 0, 0, 0x200); }
    { int d[4] = {24, 26, 28, 22}, dl[4] = {31, 31, 31, 31}; aeg("odd_dec", "tests/eg_lock/hw/odd_dec", 0x9df5, 6491, s0123, rr31, d, dl, z, rr31, 0, 0, 0x200); }
    {   // sgc_loop lo_2: ramp; streams 2/3 have the AEG (VOFF 0); 0/1 are VOFF 1 loops (also checked)
        Run r{"lo_2", "tests/sgc_loop/hw/lo_2", 0x2d40, 165, 1, {}};
        int lsa[4] = {200, 0, 2000, 1000}, lea[4] = {100, 0, 3000, 3000}, ar[4] = {31, 31, 8, 8}, lps[4] = {0, 0, 1, 0};
        for (int k = 0; k < 4; k++) { SlotCfg c; c.slot = k; c.ISEL = k; c.SA = 0x20000; c.LSA = lsa[k]; c.LEA = lea[k]; c.AR = ar[k]; c.LPSLNK = lps[k]; c.D1R = lps[k] ? 20 : 0; c.VOFF = k < 2; r.slots.push_back(c); }
        runs.push_back(r);
    }
    {   // feg_odd: FEG through the filter, compare FEG.v >> 1 with work/eg/feg_odd_<k>.u
        int far[3] = {24, 25, 24}, fd1[3] = {26, 27, 26}, fd2[3] = {28, 29, 28}, frr[3] = {22, 23, 22};
        for (int k = 0; k < 3; k++) {
            Run r{std::string("feg_odd_s") + char('0' + k), "tests/eg_lock/hw/feg_odd", 0x0374, 6491, 2, {}};
            SlotCfg c; c.slot = k; c.ISEL = k; c.SA = 0x20000; c.LEA = 8192; c.VOFF = 1; c.LPOFF = 0; c.Q = 4; c.OCT = 0; c.FNS = 0x200; c.RR = 0; c.KRS = 0;
            int flv[5] = {0x1800, 0x1C00, 0x1800, 0x1A00, 0x1C00}; for (int j = 0; j < 5; j++) c.FLV[j] = flv[j];
            c.FAR = far[k]; c.FD1R = fd1[k]; c.FD2R = fd2[k]; c.FRR = frr[k];
            r.slots.push_back(c);
            SlotCfg ref; ref.slot = 3; ref.ISEL = 3; ref.SA = 0x20000; ref.LEA = 8192; ref.VOFF = 1; ref.LPOFF = 1; ref.Q = 4; ref.OCT = 0; ref.FNS = 0x200; ref.RR = 31;
            for (int j = 0; j < 5; j++) ref.FLV[j] = 0x1FFE;
            ref.FAR = ref.FD1R = ref.FD2R = ref.FRR = 0;
            r.slots.push_back(ref);
            r.ref = 3; r.ufile = "work/eg/feg_odd_" + std::to_string(k) + ".u";
            runs.push_back(r);
        }
    }
    {   // feg_krs: KRS on different slots / matched effective rates (tests/feg_krs), compare FEG.v >> 1 with work/eg/fk_<b>_<k>.u
        struct P { int far, fd1r, fd2r, frr, krs; };
        const P b0[3] = {{16, 20, 24, 22, 5}, {16, 20, 24, 22, 0}, {16, 20, 24, 22, 15}};
        const P b1[3] = {{24, 26, 28, 24, 15}, {22, 24, 26, 22, 2}, {19, 21, 23, 19, 5}};
        const P b3[3] = {{22, 24, 26, 22, 2}, {19, 21, 23, 19, 5}, {24, 26, 28, 24, 15}};
        const int slots[4][4] = {{0, 1, 2, 3}, {0, 1, 2, 3}, {5, 17, 40, 63}, {0, 1, 2, 3}};
        const uint32_t c0s[4] = {0x0c7d, 0xcb72, 0x9340, 0x5acf};
        for (int b = 0; b < 4; b++)
            for (int k = 0; k < 3; k++) {
                const P &p = b == 0 ? b0[k] : b == 3 ? b3[k] : b1[k];
                Run r{std::string("fk_") + char('0' + b) + "_s" + char('0' + k), "tests/feg_krs/hw/fk_" + std::to_string(b), c0s[b], 6491, 2, {}};
                SlotCfg c; c.slot = slots[b][k]; c.ISEL = k; c.SA = 0x20000; c.LEA = 8192; c.VOFF = 1; c.LPOFF = 0; c.Q = 4; c.OCT = b == 0 ? 3 : 0; c.FNS = b == 0 ? 0x200 : 0; c.RR = 0; c.KRS = p.krs;
                int flv[5] = {0x1800, 0x1C00, 0x1800, 0x1A00, 0x1C00}; for (int j = 0; j < 5; j++) c.FLV[j] = flv[j];
                c.FAR = p.far; c.FD1R = p.fd1r; c.FD2R = p.fd2r; c.FRR = p.frr;
                r.slots.push_back(c);
                SlotCfg ref; ref.slot = slots[b][3]; ref.ISEL = 3; ref.SA = 0x20000; ref.LEA = 8192; ref.VOFF = 1; ref.LPOFF = 1; ref.Q = 4; ref.OCT = c.OCT; ref.FNS = c.FNS; ref.RR = 31;
                for (int j = 0; j < 5; j++) ref.FLV[j] = 0x1FFE;
                ref.FAR = ref.FD1R = ref.FD2R = ref.FRR = 0;
                r.slots.push_back(ref);
                r.ref = 3; r.ufile = "work/eg/fk_" + std::to_string(b) + "_" + std::to_string(k) + ".u";
                runs.push_back(r);
            }
    }
    {   // feg_track batches (tools/feg_track.cpp u files work/feg/ft_<b>_<k>.u), the cases of tests/feg_track
        struct P { int flv[5], rate[4], krs; };
        const P bt[3][3] = {
            {{{0x1800, 0x1C05, 0x1A03, 0x1B00, 0x1900}, {28, 26, 30, 27}, 15}, {{0x1FF0, 0x1802, 0x1F01, 0x1E00, 0x1FFD}, {30, 29, 24, 30}, 15}, {{0x1C00, 0x1C80, 0x1C00, 0x1C40, 0x1BF0}, {22, 23, 21, 25}, 15}},
            {{{0x1800, 0x1C00, 0x1800, 0x1A00, 0x1C00}, {16, 20, 24, 22}, 15}, {{0x1800, 0x1C00, 0x1800, 0x1A00, 0x1C00}, {16, 20, 24, 22}, 0}, {{0x1800, 0x1C00, 0x1800, 0x1A00, 0x1C00}, {16, 20, 24, 22}, 5}},
            {{{0x1800, 0x1FF0, 0x1800, 0x1800, 0x1A00}, {24, 26, 0, 28}, 15}, {{0x1800, 0x1C00, 0x1900, 0x1A00, 0x1C00}, {18, 20, 22, 24}, 2}, {{0x1C00, 0x1D00, 0x1D00, 0x1E00, 0x1B00}, {26, 26, 0, 26}, 15}}};
        const int oct[3] = {0, 3, 13}, fns[3] = {0, 0x200, 0x155};
        const uint32_t c0s[3] = {0xe4be, 0xa802, 0x66ac};
        for (int b = 0; b < 3; b++)
            for (int k = 0; k < 3; k++) {
                Run r{std::string("ft_") + char('0' + b) + "_s" + char('0' + k), "tests/feg_track/hw/ft_" + std::to_string(b), c0s[b], 6491, 2, {}};
                SlotCfg c; c.slot = k; c.ISEL = k; c.SA = 0x20000; c.LEA = 8192; c.VOFF = 1; c.LPOFF = 0; c.Q = 4; c.OCT = oct[b]; c.FNS = fns[b]; c.RR = 0; c.KRS = bt[b][k].krs;
                for (int j = 0; j < 5; j++) c.FLV[j] = bt[b][k].flv[j];
                c.FAR = bt[b][k].rate[0]; c.FD1R = bt[b][k].rate[1]; c.FD2R = bt[b][k].rate[2]; c.FRR = bt[b][k].rate[3];
                r.slots.push_back(c);
                SlotCfg ref; ref.slot = 3; ref.ISEL = 3; ref.SA = 0x20000; ref.LEA = 8192; ref.VOFF = 1; ref.LPOFF = 1; ref.Q = 4; ref.OCT = oct[b]; ref.FNS = fns[b]; ref.RR = 31;
                for (int j = 0; j < 5; j++) ref.FLV[j] = 0x1FFE;
                ref.FAR = ref.FD1R = ref.FD2R = ref.FRR = 0;
                r.slots.push_back(ref);
                r.ref = 3; r.ufile = "work/feg/ft_" + std::to_string(b) + "_" + std::to_string(k) + ".u";
                runs.push_back(r);
            }
    }
    Snap *snap = (Snap *)calloc(1, sizeof(Snap));
    snap->ram = (uint8_t *)malloc(AicaModel::RAM_SIZE);
    int nfull = 0, ntotal = 0;
    for (auto &r : runs) {
        if (!want.empty()) { bool sel = false; for (auto &w : want) sel |= w == r.name; if (!sel) continue; }
        auto cp = cap(r.path);
        FILE *hf = fopen((r.path + ".hdr").c_str(), "rb");
        uint32_t h[300] = {0}; size_t nh = fread(h, 4, 300, hf); fclose(hf);
        int m2 = -1, m3 = -1, m4 = -1;
        for (size_t i = 11; i + 1 < nh && i < 11 + 2 * h[6]; i += 2) { if (h[i] == 2 && m2 < 0) m2 = (int)(h[i + 1] - cp.first); if (h[i] == 3 && m3 < 0) m3 = (int)(h[i + 1] - cp.first); if (h[i] == 4 && m4 < 0) m4 = (int)(h[i + 1] - cp.first); }
        if (m3 < 0 && m2 >= 0) { m3 = m2 - 192; m4 = m2; }   /* feg_track: mark 2 follows the key-off write (head estimates lag) */
        int onstream = r.ref >= 0 ? r.ref : 0;
        int on = 1;
        while (on < (int)cp.n && !cp.v[on * cp.ns + onstream]) on++;
        uint32_t md_on = (r.c0ring - cp.first - on) & 0xFFFF;
        std::vector<int> u;
        if (!r.ufile.empty()) { FILE *f = fopen(r.ufile.c_str(), "rb"); int32_t x; while (f && fread(&x, 4, 1, f) == 1) u.push_back(x); if (f) fclose(f); }
        int end = r.ufile.empty() ? (int)cp.n : on + (int)u.size();
        AicaModel m;
        m.eg_K = Kopt >= 0 ? (uint32_t)Kopt : r.K;
        load_ram(m, r.ram_kind);
        for (auto &c : r.slots) write_slot(m, c);
        for (int i = 0; i < 16; i++) m.step();
        m.MDEC_CT = md_on;
        for (auto &c : r.slots) m.write(0x80 * c.slot, m.chr(c.slot, 0) | 0x4000);
        m.write(0x80 * r.slots[0].slot, m.chr(r.slots[0].slot, 0) | 0x8000);   /* KYONEX: the next sample is the onset */
        // compare function for sample i (after m.step())
        auto check = [&](int i) -> bool {
            if (!r.ufile.empty()) { int n = i - on; return u[n] < 0 || u[n] == (m.slot[r.slots[0].slot].FEG.v >> 1); }
            for (auto &c : r.slots) if (m.MIXS[c.ISEL] != cp.v[i * cp.ns + c.ISEL]) return false;
            return true;
        };
        int lo = m3 >= 0 ? m3 - 64 : end, hi = m3 >= 0 ? std::min(end - 1, m4 + 400) : end - 1;
        int bad = -1, i = on;
        for (; i < lo && i < end; i++) { m.step(); if (!check(i)) { bad = i; break; } }
        int bestko = -1, best = bad < 0 ? i : bad;
        std::vector<int> kos;
        if (bad < 0 && i < end) {
            snap_take(*snap, m);
            for (int ko = lo; ko <= hi; ko++) {
                snap_restore(m, *snap);
                int b2 = -1, got = lo;
                for (int n = lo; n < end; n++) {
                    if (n == ko) { for (auto &c : r.slots) m.write(0x80 * c.slot, m.chr(c.slot, 0) & 0x3FFF); m.write(0x80 * r.slots[0].slot, m.chr(r.slots[0].slot, 0) | 0x8000); }
                    m.step();
                    if (!check(n)) { b2 = n; break; }
                    got = n + 1;
                }
                if (got > best) { best = got; bestko = ko; bad = b2; }
                if (b2 < 0) { if (bad >= 0 || bestko < 0 || best < end) { best = end; bestko = ko; bad = -1; } kos.push_back(ko); }
            }
        }
        ntotal++; nfull += bad < 0;
        uint32_t md_ko = (r.c0ring - cp.first - bestko) & 0xFFFF;
        printf("%-10s onset %d (MDEC_CT %04x %s) K %u: %s %d/%d", r.name.c_str(), on, md_on, md_on & 1 ? "odd" : "even", m.eg_K, bad < 0 ? "FULL" : "fail", (bad < 0 ? end : bad) - on, end - on);
        if (bestko >= 0) { printf(", key-off samples"); size_t shown = 0; for (int ko : kos) { if (shown++ >= 6) break; printf(" %d(%s)", ko, ((r.c0ring - cp.first - ko) & 1) ? "odd" : "even"); } if (kos.size() > 6) printf(" ... (%zu work)", kos.size()); (void)md_ko; }
        if (bad >= 0) {
            printf("; first mismatch at %d (+%d):", bad, bad - on);
            if (!r.ufile.empty()) printf(" hw u %03x model v %04x", u[bad - on], m.slot[r.slots[0].slot].FEG.v);
            else for (auto &c : r.slots) printf(" s%d hw %d model %d", c.ISEL, cp.v[bad * cp.ns + c.ISEL], m.MIXS[c.ISEL]);
        }
        printf("\n");
        if (verbose && bad >= 0 && r.ufile.empty()) {   /* the next samples after the first mismatch */
            for (int t = 0; t < 12 && bad + t < end; t++) {
                if (t) m.step();
                int i2 = bad + t;
                uint32_t md = (r.c0ring - cp.first - i2) & 0xFFFF;
                printf("    i %d (+%d) MDEC_CT %04x %s:", i2, i2 - on, md, md & 1 ? "odd " : "EVEN");
                for (auto &c : r.slots) printf("  s%d hw %8d model %8d (a %03x %s CA %u)", c.ISEL, cp.v[i2 * cp.ns + c.ISEL], m.MIXS[c.ISEL], m.slot[c.slot].AEG.a, m.slot[c.slot].AEG.state == 0 ? "att" : m.slot[c.slot].AEG.state == 1 ? "d1" : m.slot[c.slot].AEG.state == 2 ? "d2" : "rel", m.slot[c.slot].CA);
                printf("\n");
            }
        }
    }
    printf("TOTAL full=%d/%d\n", nfull, ntotal);
    return nfull != ntotal;
}
