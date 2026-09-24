// kfit_core.h -- the envelope rules of kfit.cpp (see its header), shared with replay_fit.cpp: eg_increment, eff_rate,
// level_of, sim (one stream from its onset at a given K), read_marks.  Header-only, no model link.
#ifndef CAIQUE_KFIT_CORE_H
#define CAIQUE_KFIT_CORE_H
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
static uint32_t k_par = 0;                 // the envelope clock's MDEC_CT parity (0: even, the rule so far; 1: odd, after an
                                           // odd jump of MDEC_CT against the envelope counter -- replay_fit searches both)

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
        bool clock = (md & 1) == k_par;
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
#endif
