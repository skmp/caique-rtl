// eg_model.cpp -- replay captured slot programs through the production AicaModel with the envelope clock locked
// to the capture's ring position (NOTES.md "Envelope clock"), and compare every sample.
// For each run: the slots are configured like the case, the model's MDEC_CT is set to the capture's MDEC_CT of the
// onset sample (c0 - n_first - onset; c0 from the case's cap_start line), eg_K is the boot constant, the key-on
// lands on the onset sample, and the key-off sample is searched between mark 3 - 64 and mark 4 + 400 (marks are
// head estimates) from a snapshot.  AEG runs compare MIXS; FEG runs compare FEG.v >> 1 with the tracked u(n) file.
// Runs: the 33 session-4 runs (aeg_dl0, eg_lock x 8, sgc_loop lo_2, feg_odd x 3, feg_krs x 12, feg_track x 9), plus
// session 5: kp_p0..kp_p7 (tests/eg_kprobe, AEG, R 63 attack on even onsets = F7), kd_<b>_s<k> (tests/feg_koffdir,
// FEG key-off clock with opposite directions = F4; u files work/eg/kd_*.u from tools/koffdir_check -u work/eg/kd_) and
// ka_<b>_s<k> (tests/feg_koffatt, FEG key-off clock from an ATTACK, the key-off sample pinned by an AEG witness slot that
// the same KYONEX keys ON; u files work/eg/ka_*.u from tools/koffatt_check -u work/eg/ka_; the witness's MIXS is compared
// too).  The c0 of those runs is read from the case text outputs under tests/<case>/hw/ (runs skipped when absent).
//   eg_model [-K k] [-v] [name...]   (run from caique-rtl/model; default: every run; GROUPS line = per-group counts)
// Build: make -C tools eg_model (-> build/tools/eg_model; links src/aica_model.cpp)
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <functional>
#include <algorithm>
#include "../src/aica_model.h"
#include "filt_capture.h"
using namespace caique;

struct SlotCfg { int slot, ISEL; int AR = 31, D1R = 0, DL = 0, D2R = 0, RR = 31, KRS = 15, OCT = 0, FNS = 0, LSA = 0, LEA = 32, LPCTL = 1, LPSLNK = 0;
                 int VOFF = 0, LPOFF = 1, Q = 0, FLV[5] = {0x1FF8, 0x1FF8, 0x1FF8, 0x1FF8, 0x1FF8}, FAR = 31, FD1R = 31, FD2R = 31, FRR = 31; uint32_t SA = 0x10000; };
struct Run { std::string name, path; uint32_t c0ring, K; int ram_kind; std::vector<SlotCfg> slots; int ref = -1; std::string ufile;
             int ko_lo_margin = 64;      /* key-off search window: [mark_lo - ko_lo_margin, mark_hi + 400] */
             int mark_lo = 3, mark_hi = 4; /* the marks around the key-off write (7 / 8 for the KYONB-only feg_koffdir batches) */
             int group = 0;               /* 0 = the session-4 runs, 1 = eg_kprobe, 2 = feg_koffdir, 3 = feg_koffatt (the GROUPS line) */
             int witness = -1;            /* index in slots of an AEG witness slot: KYONB 0 at the key-on, KYONB 1 at the key-off (the
                                           * same KYONEX keys it ON, pinning the key-off sample); its MIXS is compared with the capture */
             bool onset_by_change = false; }; /* onset = the first sample in the mark-1 window where one of streams 0..2 CHANGES (the
                                           * buses carry the filters' rest values from the previous batch), not the first non-zero one */
// ram_kind: 0 = 32 x 0x7FFF at 0x10000; 1 = sgc_loop ramp at 0x20000; 2 = feg_track random signal at 0x20000 (8192);
//           3 = 48 x 0x7FFF at 0x10000 (eg_kprobe: the constant continues past LEA 32 for the pitch-1.5 interpolation);
//           4 = kind 2 AND 48 x 0x7FFF at 0x10000 (feg_koffatt: the random signal for the FEG slots, the constant for the witness)

static void load_ram(AicaModel &m, int kind) {
    if (kind == 0 || kind == 3 || kind == 4) for (int i = 0; i < (kind == 0 ? 32 : 48); i++) { m.ram[0x10000 + 2 * i] = 0xFF; m.ram[0x10000 + 2 * i + 1] = 0x7F; }
    if (kind == 1) for (int i = 0; i < 4096; i++) { int16_t s = (int16_t)(8 * i - 0x4000); m.ram[0x20000 + 2 * i] = (uint8_t)s; m.ram[0x20000 + 2 * i + 1] = (uint8_t)(s >> 8); }
    if (kind == 2 || kind == 4) { uint32_t seed = 4242; for (int i = 0; i < 8192; i++) { seed = seed * 1103515245u + 12345u; int16_t s = (int16_t)(seed >> 16); if (!s) s = 1; m.ram[0x20000 + 2 * i] = (uint8_t)s; m.ram[0x20000 + 2 * i + 1] = (uint8_t)(s >> 8); } }
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

/* c0 of every capture of a case from its text output.  The sync value is the ", c0 XXXX" of a "cap_start:" line (a bare
 * "c0 " also matches inside a ring address such as "at 0bc0 (n 512)"); eg_kprobe also prints "probe pN: c0 XXXX ...". */
static std::vector<uint32_t> capstart_c0s(const std::string &txt) {
    std::vector<uint32_t> v;
    FILE *f = fopen(txt.c_str(), "r");
    if (!f) return v;
    char line[512];
    while (fgets(line, sizeof line, f)) {
        const char *p = strstr(line, ", c0 ");
        unsigned c0;
        if (strstr(line, "cap_start:") && p && sscanf(p + 5, "%x", &c0) == 1) v.push_back(c0);
    }
    fclose(f);
    return v;
}
static std::vector<std::pair<std::string, uint32_t>> probe_c0s(const std::string &txt) {
    std::vector<std::pair<std::string, uint32_t>> v;
    FILE *f = fopen(txt.c_str(), "r");
    if (!f) return v;
    char line[512], name[64]; unsigned c0;
    while (fgets(line, sizeof line, f)) if (sscanf(line, "probe %63[^:]: c0 %x", name, &c0) == 2) v.push_back({name, c0});
    fclose(f);
    return v;
}

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
    {   // session 5 -- eg_kprobe probes p0..p7 (cases/eg_kprobe.c): AEG, R 63 attack on both onset parities (F7: the attack
        // leaves on the first clock at or after the key-on sample), decay 1 at R 3 / 13 / 29 / 45 from a = 0 (D1R 1/6/14/22,
        // KRS 0 OCT 0 FNS 0x200), DL 31, key-off RR 31 at ~1.5 s.  c0 per probe from the "probe pN: c0 XXXX" lines; the
        // key-off marks are head estimates up to ~100 samples off after 1.5 s, hence the 400-sample lower margin.
        auto probes = probe_c0s("tests/eg_kprobe/hw/eg_kprobe.txt");
        if (probes.empty()) fprintf(stderr, "eg_model: tests/eg_kprobe/hw/eg_kprobe.txt not found: the kp_ runs are skipped\n");
        const int d1r[4] = {1, 6, 14, 22};
        for (auto &p : probes) {
            Run r{"kp_" + p.first, "tests/eg_kprobe/hw/" + p.first, p.second, 6491, 3, {}};
            for (int k = 0; k < 4; k++) { SlotCfg c; c.slot = k; c.ISEL = k; c.AR = 31; c.D1R = d1r[k]; c.DL = 31; c.D2R = 0; c.RR = 31; c.KRS = 0; c.OCT = 0; c.FNS = 0x200; r.slots.push_back(c); }
            r.ko_lo_margin = 400; r.group = 1;
            runs.push_back(r);
        }
    }
    {   // session 5 -- feg_koffdir batches kd_0..kd_7 (cases/feg_koffdir.c): FEG key-off on a clock with the old segment and
        // the release moving in OPPOSITE directions (F4: one more step of the old segment, old increment AND old direction,
        // hold check against FLV4).  Programs on slots 0..2 (KRS 15, OCT 0, FNS 0, RR 0), reference slot 3; compare FEG.v >> 1
        // with work/eg/kd_<b>_<k>.u (written by work/verify/s5/koffdir_check.cpp -u work/eg/kd_).  Batches 6 / 7 clear KYONB
        // without KYONEX (no effect, marks 5 / 6) and key off with the KYONEX 100 ms later (marks 7 / 8).
        auto c0s = capstart_c0s("tests/feg_koffdir/hw/feg_koffdir.txt");
        if (c0s.empty()) fprintf(stderr, "eg_model: tests/feg_koffdir/hw/feg_koffdir.txt not found: the kd_ runs are skipped\n");
        struct P { int flv[5], rate[4]; };
        const P prog[3] = {{{0x1C00, 0x1800, 0x1C00, 0x1A02, 0x1C00}, {28, 26, 28, 24}},
                           {{0x1800, 0x1C00, 0x1800, 0x1A00, 0x1C00}, {24, 26, 28, 24}},
                           {{0x1800, 0x1C00, 0x1800, 0x1A00, 0x1800}, {28, 26, 28, 24}}};
        for (size_t b = 0; b < c0s.size() && b < 8; b++)
            for (int k = 0; k < 3; k++) {
                Run r{"kd_" + std::to_string(b) + "_s" + std::to_string(k), "tests/feg_koffdir/hw/kd_" + std::to_string(b), c0s[b], 6491, 2, {}};
                SlotCfg c; c.slot = k; c.ISEL = k; c.SA = 0x20000; c.LEA = 8192; c.VOFF = 1; c.LPOFF = 0; c.Q = 4; c.OCT = 0; c.FNS = 0; c.RR = 0; c.KRS = 15;
                for (int j = 0; j < 5; j++) c.FLV[j] = prog[k].flv[j];
                c.FAR = prog[k].rate[0]; c.FD1R = prog[k].rate[1]; c.FD2R = prog[k].rate[2]; c.FRR = prog[k].rate[3];
                r.slots.push_back(c);
                SlotCfg ref; ref.slot = 3; ref.ISEL = 3; ref.SA = 0x20000; ref.LEA = 8192; ref.VOFF = 1; ref.LPOFF = 1; ref.Q = 4; ref.OCT = 0; ref.FNS = 0; ref.RR = 31;
                for (int j = 0; j < 5; j++) ref.FLV[j] = 0x1FFE;
                ref.FAR = ref.FD1R = ref.FD2R = ref.FRR = 0;
                r.slots.push_back(ref);
                r.ref = 3; r.ufile = "work/eg/kd_" + std::to_string(b) + "_" + std::to_string(k) + ".u";
                if (b >= 6) { r.mark_lo = 7; r.mark_hi = 8; }
                r.group = 2;
                runs.push_back(r);
            }
    }
    {   // session 5 -- feg_koffatt batches ka_0..ka_7 (cases/feg_koffatt.c): FEG key-off on a clock FROM AN ATTACK, the release
        // going the other way on slots 0 / 1 (the F4 rule holds for the attack too: one more step of the old segment, old
        // increment AND old direction, hold check against FLV4; work/verify/s5/cases_ext.md).  Programs on slots 0..2 (KRS 15,
        // OCT 0, FNS 0, AR 31, D1R 0, RR 0: the released slot plays on).  No reference stream: stream 3 is an AEG WITNESS
        // (constant 0x7FFF at 0x10000, TL 0, VOFF 0, LPOFF 1, AR 31 D1R 31 DL 31 D2R 31 RR 31, KRS 1 -> R 63) that the key-off
        // KYONEX keys ON (KYONB 0 on slots 0..2, KYONB 1 on slot 3): its onset (520176 on that sample) pins the key-off sample,
        // so of the searched candidates only the true one is FULL.  FEG.v >> 1 is compared with work/eg/ka_<b>_<k>.u
        // (tools/koffatt_check tests/feg_koffatt/hw -u work/eg/ka_) and the witness's MIXS with the capture.  The FEG onset is
        // the first sample in the mark-1 window where a FEG stream changes (koffatt_check's rule; the buses carry the filters'
        // rest values from the previous batch, so "first non-zero" does not work).  RAM kind 4 = the signal AND the constant.
        auto c0s = capstart_c0s("tests/feg_koffatt/hw/feg_koffatt.txt");
        if (c0s.empty()) fprintf(stderr, "eg_model: tests/feg_koffatt/hw/feg_koffatt.txt not found: the ka_ runs are skipped\n");
        struct P { int flv[5], rate[4]; };
        const P prog[3] = {{{0x1C00, 0x1800, 0x1800, 0x1800, 0x1C00}, {26, 31, 31, 28}},
                           {{0x1800, 0x1C00, 0x1C00, 0x1C00, 0x1800}, {26, 31, 31, 28}},
                           {{0x1800, 0x1C00, 0x1C00, 0x1C00, 0x1C00}, {26, 31, 31, 28}}};
        for (size_t b = 0; b < c0s.size() && b < 8; b++)
            for (int k = 0; k < 3; k++) {
                Run r{"ka_" + std::to_string(b) + "_s" + std::to_string(k), "tests/feg_koffatt/hw/ka_" + std::to_string(b), c0s[b], 6491, 4, {}};
                SlotCfg c; c.slot = k; c.ISEL = k; c.SA = 0x20000; c.LEA = 8192; c.VOFF = 1; c.LPOFF = 0; c.Q = 4; c.OCT = 0; c.FNS = 0; c.RR = 0; c.KRS = 15;
                for (int j = 0; j < 5; j++) c.FLV[j] = prog[k].flv[j];
                c.FAR = prog[k].rate[0]; c.FD1R = prog[k].rate[1]; c.FD2R = prog[k].rate[2]; c.FRR = prog[k].rate[3];
                r.slots.push_back(c);
                SlotCfg w; w.slot = 3; w.ISEL = 3; w.SA = 0x10000; w.LEA = 32; w.AR = 31; w.D1R = 31; w.DL = 31; w.D2R = 31; w.RR = 31; w.KRS = 1; w.OCT = 0; w.FNS = 0; w.VOFF = 0; w.LPOFF = 1;
                r.slots.push_back(w);
                r.witness = 1; r.onset_by_change = true;
                r.ufile = "work/eg/ka_" + std::to_string(b) + "_" + std::to_string(k) + ".u";
                r.group = 3;
                runs.push_back(r);
            }
    }
    Snap *snap = (Snap *)calloc(1, sizeof(Snap));
    snap->ram = (uint8_t *)malloc(AicaModel::RAM_SIZE);
    int nfull = 0, ntotal = 0, gfull[4] = {0, 0, 0, 0}, gtotal[4] = {0, 0, 0, 0};
    for (auto &r : runs) {
        if (!want.empty()) { bool sel = false; for (auto &w : want) sel |= w == r.name; if (!sel) continue; }
        auto cp = cap(r.path);
        FILE *hf = fopen((r.path + ".hdr").c_str(), "rb");
        uint32_t h[300] = {0}; size_t nh = fread(h, 4, 300, hf); fclose(hf);
        int mk[16]; for (int i = 0; i < 16; i++) mk[i] = -1;   /* first occurrence of each mark id */
        for (size_t i = 11; i + 1 < nh && i < 11 + 2 * h[6]; i += 2) if (h[i] < 16 && mk[h[i]] < 0) mk[h[i]] = (int)(h[i + 1] - cp.first);
        int m2 = mk[2], m3 = mk[r.mark_lo], m4 = mk[r.mark_hi];
        if (m3 < 0 && m2 >= 0) { m3 = m2 - 192; m4 = m2; }   /* feg_track: mark 2 follows the key-off write (head estimates lag) */
        int onstream = r.ref >= 0 ? r.ref : 0;
        int on = 1;
        if (r.onset_by_change) {   /* the first sample in the mark-1 window where one of streams 0..2 changes (tools/koffatt_check) */
            int m1 = mk[1] >= 0 ? mk[1] : 200, lim = std::min((int)cp.n - 1, m1 + 400);
            for (on = std::max(1, m1 - 400); on < lim; on++) { bool ch = false; for (int k = 0; k < 3 && k < (int)cp.ns; k++) ch |= cp.v[on * cp.ns + k] != cp.v[(on - 1) * cp.ns + k]; if (ch) break; }
        } else while (on < (int)cp.n && !cp.v[on * cp.ns + onstream]) on++;
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
        /* the key-on write: KYONB 1 on every slot but the witness (KYONB 0, as the case writes it), then KYONEX through the
         * first slot (the case does it through slot 0); the key-off write: KYONB 0 on every slot, KYONB 1 on the witness */
        auto key_write = [&](bool on_event) {
            for (size_t si = 0; si < r.slots.size(); si++) { int s = r.slots[si].slot; bool kyonb = ((int)si == r.witness) != on_event; m.write(0x80 * s, kyonb ? (m.chr(s, 0) | 0x4000) : (m.chr(s, 0) & 0x3FFF)); }
            m.write(0x80 * r.slots[0].slot, m.chr(r.slots[0].slot, 0) | 0x8000);
        };
        key_write(true);   /* KYONEX: the next sample is the onset */
        // compare function for sample i (after m.step())
        auto check = [&](int i) -> bool {
            if (r.witness >= 0) { const SlotCfg &w = r.slots[r.witness]; if (m.MIXS[w.ISEL] != cp.v[i * cp.ns + w.ISEL]) return false; }
            if (!r.ufile.empty()) { int n = i - on; return u[n] < 0 || u[n] == (m.slot[r.slots[0].slot].FEG.v >> 1); }
            for (auto &c : r.slots) if (m.MIXS[c.ISEL] != cp.v[i * cp.ns + c.ISEL]) return false;
            return true;
        };
        int lo = m3 >= 0 ? m3 - r.ko_lo_margin : end, hi = m3 >= 0 ? std::min(end - 1, m4 + 400) : end - 1;
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
                    if (n == ko) key_write(false);
                    m.step();
                    if (!check(n)) { b2 = n; break; }
                    got = n + 1;
                }
                if (got > best) { best = got; bestko = ko; bad = b2; }
                if (b2 < 0) { if (bad >= 0 || bestko < 0 || best < end) { best = end; bestko = ko; bad = -1; } kos.push_back(ko); }
            }
            if (bad >= 0 && bestko >= 0) {   /* leave the model in the BEST candidate's state at its first mismatch (for the report) */
                snap_restore(m, *snap);
                for (int n = lo; n <= bad; n++) {
                    if (n == bestko) key_write(false);
                    m.step();
                }
            }
        }
        ntotal++; nfull += bad < 0; gtotal[r.group]++; gfull[r.group] += bad < 0;
        uint32_t md_ko = (r.c0ring - cp.first - bestko) & 0xFFFF;
        printf("%-10s onset %d (MDEC_CT %04x %s) K %u: %s %d/%d", r.name.c_str(), on, md_on, md_on & 1 ? "odd" : "even", m.eg_K, bad < 0 ? "FULL" : "fail", (bad < 0 ? end : bad) - on, end - on);
        if (bestko >= 0) { printf(", key-off samples"); size_t shown = 0; for (int ko : kos) { if (shown++ >= 6) break; printf(" %d(%s)", ko, ((r.c0ring - cp.first - ko) & 1) ? "odd" : "even"); } if (kos.size() > 6) printf(" ... (%zu work)", kos.size()); (void)md_ko; }
        if (bad >= 0) {
            if (bestko >= 0 && kos.empty()) printf(" (best key-off %d %s)", bestko, ((r.c0ring - cp.first - bestko) & 1) ? "odd" : "even");
            printf("; first mismatch at %d (+%d, MDEC_CT %s):", bad, bad - on, ((r.c0ring - cp.first - bad) & 1) ? "odd" : "even");
            if (!r.ufile.empty()) printf(" hw u %03x model v %04x", u[bad - on], m.slot[r.slots[0].slot].FEG.v);
            else for (auto &c : r.slots) printf(" s%d hw %d model %d", c.ISEL, cp.v[bad * cp.ns + c.ISEL], m.MIXS[c.ISEL]);
            if (r.witness >= 0) { const SlotCfg &w = r.slots[r.witness]; printf("; witness s%d hw %d model %d (a %03x)", w.ISEL, cp.v[bad * cp.ns + w.ISEL], m.MIXS[w.ISEL], m.slot[w.slot].AEG.a); }
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
    printf("GROUPS session4=%d/%d eg_kprobe=%d/%d feg_koffdir=%d/%d feg_koffatt=%d/%d\n", gfull[0], gtotal[0], gfull[1], gtotal[1], gfull[2], gtotal[2], gfull[3], gtotal[3]);
    printf("TOTAL full=%d/%d\n", nfull, ntotal);
    return nfull != ntotal;
}
