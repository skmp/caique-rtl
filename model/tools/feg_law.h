// feg_law.h -- the FEG law (NOTES "Filter envelope (FEG)", sample-model/aica_model.cpp feg_clock) as a standalone simulator
// with the key-off mechanisms compared in work/verify/s5/S3alt.md.  Envelope clock on even MDEC_CT, eg_cnt = K - MDEC_CT/2,
// R < 48 rows one step behind.  Shared by tools/koffdir_check.cpp, tools/koffatt_check.cpp and the scratch
// work/verify/s5/s3alt.cpp (existing captures; build it with -I tools).
#pragma once
#include <cstdint>
#include <cstring>
#include <algorithm>

static const uint8_t eg_inc[17][8] = {
    {0, 1, 0, 1, 0, 1, 0, 1}, {0, 1, 0, 1, 1, 1, 0, 1}, {0, 1, 1, 1, 0, 1, 1, 1}, {0, 1, 1, 1, 1, 1, 1, 1},
    {1, 1, 1, 1, 1, 1, 1, 1}, {1, 2, 1, 1, 1, 2, 1, 1}, {1, 2, 1, 2, 1, 2, 1, 2}, {1, 2, 2, 2, 1, 2, 2, 2},
    {2, 2, 2, 2, 2, 2, 2, 2}, {2, 4, 2, 2, 2, 4, 2, 2}, {2, 4, 2, 4, 2, 4, 2, 4}, {2, 4, 4, 4, 2, 4, 4, 4},
    {4, 4, 4, 4, 4, 4, 4, 4}, {4, 8, 4, 4, 4, 8, 4, 4}, {4, 8, 4, 8, 4, 8, 4, 8}, {4, 8, 8, 8, 4, 8, 8, 8},
    {8, 8, 8, 8, 8, 8, 8, 8}};
static inline uint32_t eg_increment(uint32_t R, uint32_t cnt) {
    if (R == 0) return 0;
    if (R < 48) { cnt -= 1; uint32_t sh = 11 - (R >> 2); if (cnt & ((1u << sh) - 1)) return 0; return eg_inc[R & 3][(cnt >> sh) & 7]; }
    return eg_inc[R >= 60 ? 16 : 4 + (R - 48)][cnt & 7];
}
struct Prog { int flv[5]; int rate[4]; int krs, oct, fns; };
static inline uint32_t eff_rate(const Prog &p, int re) {
    if (re == 0) return 0;
    int s = 0;
    if (p.krs != 15) { int k = p.krs + ((p.oct & 8) ? p.oct - 16 : p.oct); s = k < 0 ? 0 : 2 * std::min(k, 15) + ((p.fns >> 9) & 1); }
    return (uint32_t)std::min(63, 2 * re + s);
}
// ---- mechanisms -------------------------------------------------------------------------------------------------
enum Mech {
    M_S3 = 0,      // claim: key-off sample K: state/target/dir -> release; a clock on K takes the OLD segment's increment
    M_NOS3,        // no lag: a clock on K steps with the release increment (eg_phase koff_same 1)
    M_NOSTEP,      // no step on K at all (key-on-like), release steps from the next clock
    M_LAGCLK,      // (iii) rate at every clock = rate of the state at the previous clock (all transitions lag one clock)
    M_LAGCLK_KO,   // (iii) restricted to key-offs: the first clock AFTER a key-off (any parity of K) uses the old increment
    M_PEND,        // (iv) pending step: increment AND direction of the previous clock, applied at the first clock after the key-off (any parity), hold check against the new target
    M_PEND_EVEN,   // (iv) variant = "old direction": old increment and old direction, but only when K is a clock (else release)
    M_S3_2CLK,     // rate lag of two clocks (3-4 samples): the old increment on K and on the next clock too
    M_V_NOS3,      // (v) state change only on even samples (write on an even sample lands one sample later), release rate on that clock
    M_V_NOSTEP,    // (v) with no step on the (even) state-change sample
    M_KYONB,       // (vi) target/direction follow the KYONB clear (sample T), state/rate follow KYONEX (sample K), no rate lag
    M_COUNT
};
static const char *mech_name[M_COUNT] = {"S3", "noS3", "noStep", "lagClk(iii)", "lagClkKO", "pend(iv)", "oldDir", "S3_2clk", "v_noS3", "v_noStep", "KYONB(vi)"};

struct Feg { int state, v, dir; bool passed; int prev; int last_rs; int last_dir; bool koff_done; int koff_clocks; int tgt_override; };

// one envelope clock (key events of the sample already applied); inc_used / rs_used report the increment and the
// segment whose rate was used
static inline void feg_clock_step(Feg &f, const Prog &p, uint32_t cnt, Mech m, bool koff_now, int &inc_used, int &rs_used) {
    if (f.passed && f.state < 2) { f.state++; f.dir = f.v >= p.flv[f.state + 1] ? -1 : 1; f.passed = false; }
    int rs = f.state, dir = f.dir;
    switch (m) {
    case M_S3: if (koff_now) rs = f.prev; break;
    case M_KYONB: break;   /* no rate lag: the target follows KYONB (T), the state/rate follow KYONEX (K) */
    case M_NOS3: case M_V_NOS3: break;
    case M_NOSTEP: case M_V_NOSTEP: if (koff_now) { inc_used = 0; rs_used = rs; f.last_rs = f.state; f.last_dir = f.dir; return; } break;
    case M_LAGCLK: rs = f.last_rs; break;
    case M_LAGCLK_KO: if (f.koff_done && f.koff_clocks == 0) rs = f.prev; break;
    case M_PEND: if (f.koff_done && f.koff_clocks == 0) { rs = f.prev; dir = f.last_dir; } break;
    case M_PEND_EVEN: if (koff_now) { rs = f.prev; dir = f.last_dir; } break;
    case M_S3_2CLK: if (f.koff_done && f.koff_clocks <= 1) rs = f.prev; break;
    default: break;
    }
    int target = f.tgt_override >= 0 ? f.tgt_override : p.flv[f.state + 1];
    uint32_t inc = eg_increment(eff_rate(p, p.rate[rs]), cnt);
    inc_used = (int)inc; rs_used = rs;
    f.last_rs = f.state; f.last_dir = f.dir;
    if (f.koff_done) f.koff_clocks++;
    if (!inc || f.passed) return;
    bool C = f.v >= target;
    int nv = f.v + dir * (int)inc;
    nv = nv < 0 ? 0 : nv > 0x1FFF ? 0x1FFF : nv;
    if (f.state >= 2) { if ((nv >= target) == C) f.v = nv; }
    else { f.v = nv; if ((nv >= target) != C) f.passed = true; }
}
static inline void feg_key_on(Feg &f, const Prog &p) { f.state = 0; f.v = p.flv[0]; f.dir = f.v >= p.flv[1] ? -1 : 1; f.passed = false; f.prev = 0; f.last_rs = 0; f.last_dir = f.dir; f.koff_done = false; f.koff_clocks = 0; f.tgt_override = -1; }
static inline void feg_key_off(Feg &f, const Prog &p) { f.prev = f.state; f.state = 3; f.dir = f.v >= p.flv[4] ? -1 : 1; f.passed = false; f.koff_done = true; f.koff_clocks = 0; f.tgt_override = -1; }
// KYONB-only clear (M_KYONB): the target and direction switch to FLV4, the state and rate stay
static inline void feg_kyonb_clear(Feg &f, const Prog &p) { f.tgt_override = p.flv[4]; f.dir = f.v >= p.flv[4] ? -1 : 1; f.passed = false; }
