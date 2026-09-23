// koffdir_check.cpp -- analyse the captures of cases/feg_koffdir.c (tests/feg_koffdir; proposed in work/verify/s5/S3alt.md):
// recover u = v >> 1 per sample through the bit-exact filter (the tools/feg_track.cpp algorithm), then fit the
// key-off sample under S3, oldDir, noS3, noStep and KYONB(vi) (tools/feg_law.h) and print the window.
//   koffdir_check <dir> [-K kc] [-u <prefix>]
//                                   dir holds feg_koffdir.txt and kd_<b>.hdr/.bin (console: tests/feg_koffdir/hw; model run:
//                                   work/verify/s5/feg_koffdir_model); kc = the boot's clock constant (default 6491; refit it
//                                   with eg_phase on the console's eg_lock att_slow if the console rebooted).
//                                   -u work/eg/kd_ writes the recovered u per stream to work/eg/kd_<b>_<k>.u (the fk_*.u
//                                   format: int32 per sample from the onset, -1 unknown) for tools/eg_model's kd_ runs.
// Build: make -C tools koffdir_check (-> build/tools/koffdir_check; single file, no model link).  Run from caique-rtl/model.
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <set>
#include <map>
#include <tuple>
#include <algorithm>
#include "filt_capture.h"
#include "feg_law.h"
using I = int64_t;
static const int qm[32] = {192, 176, 160, 144, 128, 120, 112, 104, 96, 88, 80, 72, 64, 60, 56, 52, 48, 44, 40, 36, 32, 30, 28, 26, 24, 22, 20, 18, 16, 15, 14, 13};
static I ceilshr(I n, int s) { return -((-n) >> s); }
struct St { I L, B; int u; bool operator<(const St &o) const { return std::tie(L, B, u) < std::tie(o.L, o.B, o.u); } bool operator==(const St &o) const { return L == o.L && B == o.B && u == o.u; } };
static void fstep(I &L, I &B, I x, int u, int Q) { int v = u << 1, k = v >= 0x1ffe ? 512 : 256 + (u & 255), s = 24 - (v >> 9); I d = 2 * ceilshr(qm[Q] * B, 8); B += (k * (x - L - d)) >> s; L += ceilshr(k * B, s); }
// recover u(n) from the onset for stream k (reference stream 3 = input), FLV0 known
static std::vector<int32_t> track(const Capture &c, unsigned on, int k, int flv0, size_t &maxset) {
    std::vector<St> S, T;
    for (I b0 = -256; b0 <= 256; b0++) S.push_back({-I(c.v[(on - 1) * 4 + k]) / 2, b0, flv0 >> 1});
    std::vector<int32_t> u(c.n - on, -1);
    maxset = 0;
    for (unsigned n = on; n < c.n; n++) {
        I x = I(c.v[n * 4 + 3]) >> 1;
        T.clear();
        for (auto &s : S)
            for (int du = -4; du <= 4; du++) {
                int nu = s.u + du; if (nu < 0 || nu > 4095) continue;
                I L = s.L, B = s.B; fstep(L, B, x, nu, 4);
                if (std::clamp<I>(-2 * L, -524288, 524287) == c.v[n * 4 + k]) T.push_back({L, B, nu});
            }
        std::sort(T.begin(), T.end()); T.erase(std::unique(T.begin(), T.end()), T.end());
        if (T.empty()) { u.resize(n - on); break; }
        if (T.size() > 400000) T.resize(400000);
        maxset = std::max(maxset, T.size());
        bool one = true; for (auto &s : T) one &= s.u == T[0].u;
        u[n - on] = one ? T[0].u : -1;
        std::swap(S, T);
    }
    return u;
}
static const Prog progs[3] = {
    {{0x1C00, 0x1800, 0x1C00, 0x1A02, 0x1C00}, {28, 26, 28, 24}, 15, 0, 0},
    {{0x1800, 0x1C00, 0x1800, 0x1A00, 0x1C00}, {24, 26, 28, 24}, 15, 0, 0},
    {{0x1800, 0x1C00, 0x1800, 0x1A00, 0x1800}, {28, 26, 28, 24}, 15, 0, 0},
};
// simulate stream k from the onset; K = key-off (state change) sample, T = KYONB-only clear sample (M_KYONB, T <= K); returns matched samples
static int sim(const Prog &p, const std::vector<int32_t> &u, uint32_t c0, uint32_t first, int on, Mech m, int K, int T, uint32_t Kc, int *bad_n, int *bad_v) {
    Feg f{}; feg_key_on(f, p);
    int end = on + (int)u.size();
    for (int i = on; i < end; i++) {
        uint32_t md = (c0 - first - (uint32_t)i) & 0xFFFF;
        bool koff_now = false;
        if (m == M_KYONB && i == T && i < K) feg_kyonb_clear(f, p);
        if (i == K) { feg_key_off(f, p); koff_now = true; }
        int inc = -1, rs = -1;
        if ((md & 1) == 0 && i > on) feg_clock_step(f, p, (Kc - (md >> 1)) & 0x3FFF, m, koff_now, inc, rs);
        int n = i - on;
        if (u[n] >= 0 && u[n] != (f.v >> 1)) { if (bad_n) { *bad_n = n; *bad_v = f.v; } return n; }
    }
    return (int)u.size();
}
int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: koffdir_check <dir> [-K kc] [-u <prefix>]\n  -u work/eg/kd_  writes the recovered u per stream to <prefix><b>_<k>.u (int32 per sample from the onset, -1 unknown; the format of work/eg/fk_*.u, read by tools/eg_model)\n"); return 2; }
    std::string dir = argv[1]; uint32_t Kc = 6491; const char *uprefix = nullptr;
    for (int i = 2; i + 1 < argc; i++) { if (!strcmp(argv[i], "-K")) Kc = (uint32_t)atoi(argv[++i]); else if (!strcmp(argv[i], "-u")) uprefix = argv[++i]; }
    // c0 per batch from the text output (cap_start lines in batch order); matched as ", c0 " -- a bare "c0 " also matches
    // inside a ring address such as "at 0bc0 (n 512)" (kd_7 on the console), which dropped that batch before
    std::vector<uint32_t> c0s;
    { FILE *f = fopen((dir + "/feg_koffdir.txt").c_str(), "r"); if (!f) { perror("feg_koffdir.txt"); return 2; } char line[512];
      while (fgets(line, sizeof line, f)) { const char *p = strstr(line, ", c0 "); if (strstr(line, "cap_start") && p) { unsigned c0; if (sscanf(p + 5, "%x", &c0) == 1) c0s.push_back(c0); } } fclose(f); }
    printf("koffdir_check %s: %zu batches, clock constant K %u\n", dir.c_str(), c0s.size(), Kc);
    const Mech mechs[5] = {M_S3, M_PEND_EVEN, M_NOS3, M_NOSTEP, M_KYONB};
    for (unsigned b = 0; b < c0s.size(); b++) {
        std::string path = dir + "/kd_" + std::to_string(b);
        Capture c; try { c = cap(path); } catch (std::exception &e) { printf("== kd_%u: %s\n", b, e.what()); continue; }
        FILE *hf = fopen((path + ".hdr").c_str(), "rb"); uint32_t h[300] = {0}; size_t nh = fread(h, 4, 300, hf); fclose(hf);
        std::map<int, int> mark;
        for (size_t i = 11; i + 1 < nh && i < 11 + 2 * h[6]; i += 2) if (!mark.count((int)h[i])) mark[(int)h[i]] = (int)(h[i + 1] - c.first);
        unsigned on = 1; while (on < c.n && !c.v[on * 4 + 3]) on++;
        uint32_t md_on = (c0s[b] - c.first - on) & 0xFFFF;
        bool kyonb_only = mark.count(5) > 0;
        printf("== kd_%u: %u samples, first %u, onset %u (MDEC_CT %04x %s), c0 %04x, marks:", b, c.n, c.first, on, md_on, (md_on & 1) ? "odd" : "even", c0s[b]);
        for (auto &m : mark) printf(" %d@%d", m.first, m.second); printf("%s\n", kyonb_only ? "  [KYONB-only batch]" : "");
        std::vector<int32_t> us[3];
        for (int k = 0; k < 3; k++) {
            size_t ms; us[k] = track(c, on, k, progs[k].flv[0], ms); int amb = 0; for (auto x : us[k]) amb += x < 0;
            printf("   stream %d: tracked %zu/%u samples, %d ambiguous, max set %zu", k, us[k].size(), c.n - on, amb, ms);
            if (uprefix) {   /* u file for tools/eg_model (one int32 per sample from the onset, -1 = unknown) */
                std::string uf = std::string(uprefix) + std::to_string(b) + "_" + std::to_string(k) + ".u";
                FILE *f = fopen(uf.c_str(), "wb");
                if (f) { fwrite(us[k].data(), 4, us[k].size(), f); fclose(f); printf(" -> %s", uf.c_str()); } else printf(" (cannot write %s)", uf.c_str());
            }
            printf("\n");
        }
        int lo, hi;
        if (!kyonb_only) { lo = mark[3] - 64; hi = mark[4] + 400; }
        else { lo = mark[7] - 64; hi = mark[8] + 400; }
        if (kyonb_only) {
            // did anything move between the KYONB clears (mark 6) and the KYONEX (mark 7)?
            for (int k = 0; k < 3; k++) {
                int a = mark[5] - 100 - (int)on, e = mark[7] - 100 - (int)on; int u0 = -1, umin = 1 << 30, umax = -1; int first_change = -1;
                for (int n = std::max(0, a); n < e && n < (int)us[k].size(); n++) { int x = us[k][n]; if (x < 0) continue; if (u0 < 0) u0 = x; umin = std::min(umin, x); umax = std::max(umax, x); if (first_change < 0 && x != u0) first_change = n + on; }
                printf("   stream %d between mark 5-100 and mark 7-100 (KYONB cleared, no KYONEX): u %03x, min %03x max %03x, first change %s%d  -> %s\n", k, u0, umin, umax, first_change < 0 ? "none" : "at sample ", first_change,
                       umax - umin > 2 ? "MOVED: KYONB-driven target" : "held: target follows the state (S3 reading stands)");
            }
        }
        // fit the key-off sample per mechanism and stream; per-batch intersection
        for (Mech m : mechs) {
            std::set<int> inter; bool firstk = true; std::string per;
            for (int k = 0; k < 3; k++) {
                std::set<int> ks; int best = -1, bestK = -1, bn = -1, bv = 0;
                for (int K = lo; K <= hi && K < (int)(on + us[k].size()); K++) {
                    int Tlo = m == M_KYONB ? (kyonb_only ? mark[5] - 64 : K - 2) : K, Thi = m == M_KYONB && kyonb_only ? mark[6] + 64 : K;
                    for (int T = Tlo; T <= Thi; T++) {
                        int b2 = -1, v2 = 0;
                        int got = sim(progs[k], us[k], c0s[b], c.first, (int)on, m, K, T, Kc, &b2, &v2);
                        if (got == (int)us[k].size()) ks.insert(K);
                        if (got > best) { best = got; bestK = K; bn = b2; bv = v2; }
                    }
                }
                char buf[160];
                if (ks.empty()) snprintf(buf, sizeof buf, " s%d: none (best %d/%zu at K %d, +%d hw %03x model %03x)", k, best, us[k].size(), bestK, bn, bn >= 0 && bn < (int)us[k].size() ? us[k][bn] : -1, bv >> 1);
                else { std::string t; int shown = 0; for (int K : ks) { if (shown++ >= 4) { t += " .."; break; } t += " " + std::to_string(K) + (((c0s[b] - c.first - K) & 1) ? "o" : "e"); } snprintf(buf, sizeof buf, " s%d:%s", k, t.c_str()); }
                per += buf;
                if (firstk) { inter = ks; firstk = false; } else { std::set<int> in; std::set_intersection(inter.begin(), inter.end(), ks.begin(), ks.end(), std::inserter(in, in.begin())); inter = in; }
            }
            printf("   %-10s%s  -> shared K:", mech_name[m], per.c_str());
            if (inter.empty()) printf(" NONE (refuted on this batch)"); else for (int K : inter) printf(" %d(%s)", K, ((c0s[b] - c.first - K) & 1) ? "odd" : "even");
            printf("\n");
        }
        // window around the S3 key-off (or the KYONEX) with u of the three streams
        {
            int Kref = -1; std::set<int> inter;
            for (int k = 0; k < 3; k++) { std::set<int> ks; for (int K = lo; K <= hi && K < (int)(on + us[k].size()); K++) if (sim(progs[k], us[k], c0s[b], c.first, (int)on, M_S3, K, K, Kc, nullptr, nullptr) == (int)us[k].size()) ks.insert(K);
                if (k == 0) inter = ks; else { std::set<int> in; std::set_intersection(inter.begin(), inter.end(), ks.begin(), ks.end(), std::inserter(in, in.begin())); inter = in; } }
            if (!inter.empty()) Kref = *inter.begin(); else Kref = kyonb_only ? mark[7] : mark[3] + 20;
            printf("   window around %s sample %d (u per sample, s0 s1 s2; . = unchanged, ? = ambiguous):\n", inter.empty() ? "estimated key-off" : "the S3 key-off", Kref);
            int last[3] = {-9, -9, -9};
            for (int i = Kref - 12; i <= Kref + 16; i++) {
                int n = i - (int)on; if (n < 0) continue;
                uint32_t md = (c0s[b] - c.first - (uint32_t)i) & 0xFFFF;
                printf("     i %6d MDEC_CT %04x %s%s:", i, md, (md & 1) ? "odd " : "EVEN", i == Kref ? " *" : "  ");
                for (int k = 0; k < 3; k++) { int x = n < (int)us[k].size() ? us[k][n] : -2; if (x < 0) printf("   ? "); else if (x == last[k]) printf("   . "); else printf(" %03x ", x); if (x >= 0) last[k] = x; }
                printf("\n");
            }
        }
    }
    return 0;
}
