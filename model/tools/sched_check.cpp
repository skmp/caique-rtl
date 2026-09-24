// sched_check.cpp -- tests/sub_sched (TODO 1.6): key events, the envelope pass and CPU MIXS accesses against the frame
// plan, console against the cycle model.
//   sched_check [DIR] [-v]        (default tests/sub_sched/hw; run from caique-rtl/model)
// cases/flog.h's ring (flog_<k>_<exp>_<batch>.bin, two streams: bus 1 = pair 1, bus 2 = pair 2) gives, per DSP sample n,
// the marker read in every frame (pair p >= 3 at clock t = 512 n + 65 + 8 p), the two buses, and n's MDEC_CT (the
// counter word's address).  Every access under test lies between two markers' X0 (X0 on clocks = 2 mod 4, a step apart).
// For every event the cycle model is set to the state before it (the slot as the case configures it; exp 1 with the
// console's MDEC_CT; exp 2 / 3 with the bus values the log shows) and run with the accesses at every clock combination
// the markers allow; the event is determinate when every combination predicts the same observation:
//   exp 0  (KYONEX burst, KYONB burst): the first DSP sample whose bus 1 shows the block, or none
//   exp 1  (RR 30 burst): the first DSP sample whose bus 1 leaves the held level
//   exp 2  (MIXS2 write burst, MIXS2 read): bus 2 in the DSP samples around the write, and the value read
//   exp 3  (MIXS1 write burst, MIXS1 read): bus 1 likewise (slot k sends to it)
#include "aica_model.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <algorithm>
#include <climits>
#include <array>

using caique::AicaModel;

enum { NEXP = 4, OBS = 6 };
static const char *exp_name[NEXP] = {"KYONB window", "envelope pass", "MIXS write+read", "MIXS write, bus with writer"};
struct Ev { int k, x, batch, e; uint32_t m[4]; uint32_t value, rd; };

static int32_t bus_word(int32_t mixs) {   // flog.h's stream word: SHIFTED[23:8] of clamp24(-(MIXS << 4))
    int32_t a = -(mixs * 16);
    a = a > 0x7FFFFF ? 0x7FFFFF : a < -0x800000 ? -0x800000 : a;
    return (int16_t)(a >> 8);
}

// cases/sub_sched.c setup(): slot k of experiment x (slot_cfg_default + LPOFF 1, ISEL 1, IMXL 15, VOFF 1 ...)
static void sched_setup(AicaModel &m, int k, int x) {
    const uint32_t ch = 0x80 * k;
    for (int i = 0; i < 32; i++) m.ram_write32(0x010000 + 4 * i, 0x08000800u);
    const int voff = x == 1 ? 0 : 1, rr = x == 1 ? 0 : 31, lpctl = x == 0 ? 0 : 1, lea = x == 0 ? 16 : 64;
    m.write(ch + 0x04, 0x0000); m.write(ch + 0x08, 0); m.write(ch + 0x0C, lea);
    m.write(ch + 0x10, 31); m.write(ch + 0x14, (15 << 10) | rr); m.write(ch + 0x18, 0); m.write(ch + 0x1C, 0);
    m.write(ch + 0x20, (15 << 4) | 1); m.write(ch + 0x24, 0); m.write(ch + 0x28, (voff << 6) | (1 << 5));
    for (int i = 0; i < 5; i++) m.write(ch + 0x2C + 4 * i, 0x1FF8);
    m.write(ch + 0x40, 0x1F1F); m.write(ch + 0x44, 0x1F1F);
    m.write(ch + 0x00, (lpctl << 9) | 1);
}

// an access for the simulation: at X0 clock t (relative to the simulation's sweep 0), a write or a read
struct Acc { int64_t t; bool we; uint32_t off, val; };
// run the model through the accesses and return the DSP's view of `bus` in sweeps 0..OBS-1 (the stream read of pair p
// at t2 of step 2p: ph 64 + 8 p + 2, i.e. the state after clock 64 + 8 p + 1) and the value of the read (if any)
static void simulate(const AicaModel &base, const std::vector<Acc> &acc, int bus, int32_t *words, uint32_t *rd, int k = 0,
                     bool *keyed_after = nullptr) {
    AicaModel *m = new AicaModel();
    uint8_t *own = m->ram;
    memcpy((void *)m, &base, sizeof *m);           // shares the base's wave RAM (nothing here writes it)
    const uint64_t c0 = base.clocks;              // clock 0 of sweep 0 (the base sits at ph 0)
    const uint32_t rph = 64 + 8 * bus + 2;
    size_t ai = 0;
    for (int s = 0; s < OBS; s++) {
        const uint64_t tr = c0 + 512 * (uint64_t)s + rph;
        while (m->clocks < tr) {
            while (ai < acc.size() && (int64_t)(m->clocks - c0) == acc[ai].t + (acc[ai].we ? 1 : 0)) {
                if (acc[ai].we) m->write(acc[ai].off, acc[ai].val);
                else *rd = m->read(acc[ai].off);
                ai++;
            }
            m->clock();
        }
        words[s] = bus_word(m->mixs[m->dsp_bank][bus]);
    }
    if (keyed_after) *keyed_after = m->slot[k].AEG.state != caique::EG_RELEASE;   // not released: a key-on is ignored
    m->ram = own;
    delete m;
}

int main(int argc, char **argv) {
    std::string dir = "tests/sub_sched/hw";
    bool verbose = false;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-v")) verbose = true;
        else dir = argv[i];
    }
    FILE *f = fopen((dir + "/sub_sched.txt").c_str(), "r");
    if (!f) f = fopen((dir + "/sub_env.txt").c_str(), "r");   /* cases/sub_env.c: experiment 1 only, four batches */
    if (!f) { perror((dir + "/sub_sched.txt").c_str()); return 2; }
    std::vector<Ev> evs;
    char line[256];
    while (fgets(line, sizeof line, f)) {
        Ev e; unsigned long v, r;
        if (sscanf(line, "E %d %d %d %d %u %u %u %u %lu %lu", &e.k, &e.x, &e.batch, &e.e, &e.m[0], &e.m[1], &e.m[2], &e.m[3], &v, &r) == 10) {
            e.value = (uint32_t)v; e.rd = (uint32_t)r; evs.push_back(e);
        }
    }
    fclose(f);
    long tot[NEXP] = {0}, det[NEXP] = {0}, agree[NEXP] = {0}, bad[NEXP] = {0}, nodata[NEXP] = {0};
    std::vector<std::string> out_lines;
    // one work item per ring file
    std::vector<std::array<int, 3>> files;
    for (int k = 0; k < 64; k++)
        for (int x = 0; x < NEXP; x++)
            for (int b = 0; b < 4; b++) files.push_back({k, x, b});   /* missing files are skipped */
#pragma omp parallel for schedule(dynamic)
    for (size_t fi = 0; fi < files.size(); fi++) {
        const int k = files[fi][0], x = files[fi][1], batch = files[fi][2];
        std::vector<uint16_t> ring(65536);
        char name[64];
        snprintf(name, sizeof name, "/flog_%d_%d_%d.bin", k, x, batch);
        FILE *rf = fopen((dir + name).c_str(), "rb");
        if (!rf || fread(ring.data(), 2, 65536, rf) != 65536) { if (rf) fclose(rf); continue; }
        fclose(rf);
        // counter (region 0): word -(n + 1) at address m; n + m is one constant C for every word the logger wrote
        std::map<uint32_t, int> cnt;
        for (uint32_t m = 0; m < 65536; m++) cnt[(uint32_t)((-(int32_t)(int16_t)ring[m] - 1) + m) & 0xFFFF]++;
        uint32_t C = 0; int best = -1;
        for (auto &kv : cnt) if (kv.second > best) { best = kv.second; C = kv.first; }
        std::vector<const Ev *> bev;
        uint32_t mlo = UINT32_MAX, mhi = 0;
        for (const Ev &ev : evs)
            if (ev.k == k && ev.x == x && ev.batch == batch) { bev.push_back(&ev); mlo = std::min(mlo, ev.m[0] - 1); mhi = std::max(mhi, ev.m[3]); }
        auto marker = [&](int p, uint32_t m) { return (uint32_t)(uint16_t)(-(int32_t)(int16_t)ring[(1024 * p + m) & 0xFFFF]); };
        struct Rd { int64_t t; uint32_t marker; };
        std::vector<Rd> rd;
        std::map<int64_t, std::array<int32_t, 3>> smp;   // DSP sample n -> bus 1 word, bus 2 word, MDEC_CT
        int64_t nref = -1;
        std::vector<std::pair<uint32_t, uint32_t>> raw;
        for (uint32_t m = 0; m < 65536; m++) {
            const uint32_t n16 = (uint32_t)(-(int32_t)(int16_t)ring[m] - 1) & 0xFFFF;
            if (((n16 + m) & 0xFFFF) != C) continue;
            bool inb = true, last = true;
            for (int p = 3; p < 64 && inb; p++) { const uint32_t mk = marker(p, m); inb = mk >= mlo && mk <= mhi; last &= mk == mhi; }
            if (!inb) continue;
            raw.push_back({n16, m});
            if (last) nref = n16;
        }
        if (nref < 0) continue;
        for (auto &s : raw) {
            const int64_t n = 100000 + (int16_t)(uint16_t)(s.first - nref);
            smp[n] = {(int16_t)ring[(1024 + s.second) & 0xFFFF], (int16_t)ring[(2048 + s.second) & 0xFFFF], (int32_t)s.second};
            for (int p = 3; p < 64; p++) rd.push_back({512 * n + 65 + 8 * p, marker(p, s.second)});
        }
        std::sort(rd.begin(), rd.end(), [](const Rd &a, const Rd &b) { return a.t < b.t; });
        // a marker's X0 candidates (2 mod 4): after the last read without it, before the first read with it
        auto window = [&](uint32_t mk, uint32_t next, std::vector<int64_t> &c) {
            for (size_t i = 0; i < rd.size(); i++)
                if (rd[i].marker == mk || rd[i].marker == next) {
                    if (i == 0) return false;
                    for (int64_t y = rd[i - 1].t; y < rd[i].t; y++) if ((y & 3) == 2) c.push_back(y);
                    return true;
                }
            return false;
        };
        // the base state of this batch's experiment: the slot as configured, 128 samples on (exp 1: held released)
        AicaModel *base = new AicaModel();
        sched_setup(*base, k, x);
        const uint32_t ch = 0x80 * k;
        if (x != 0) { base->write(ch, base->r(ch) | 0x4000); base->write(ch, base->r(ch) | 0xC000); }
        for (int i = 0; i < 128; i++) base->step();
        if (x == 1) {
            base->write(ch, base->r(ch) & 0x3FFF); base->write(ch, (base->r(ch) & 0x3FFF) | 0x8000);
            for (int i = 0; i < 64; i++) base->step();
        }
        const int bus = x == 2 ? 2 : 1;
        // exp 0: the slot is released (R) or keyed and stopped by its one-shot end, envelope running (N: a key-on is
        // ignored until a key-off; tests/oneshot).  The state carries from event to event: each event is simulated
        // from every state still possible, and the observation keeps the states it allows.
        AicaModel *baseN = nullptr;
        if (x == 0) {
            baseN = new AicaModel();
            uint8_t *o = baseN->ram;
            memcpy((void *)baseN, base, sizeof *baseN);
            baseN->ram = o;
            memcpy(baseN->ram, base->ram, AicaModel::RAM_SIZE);
            baseN->write(ch, 0x4001); baseN->write(ch, 0xC001);
            for (int i = 0; i < 64; i++) baseN->step();
        }
        std::set<int> pre = {0};
        long lt = 0, ld = 0, la = 0, lb = 0, ln = 0;
        for (const Ev *ev : bev) {
            lt++;
            // the accesses: marker windows -> candidates for the accesses under test
            std::vector<int64_t> w1, w2, w3, w4;
            bool ok = window(ev->m[0], ev->m[1], w1) && window(ev->m[1], ev->m[2], w2);
            if (x == 0 || x >= 2) ok = ok && window(ev->m[2], ev->m[3], w3);
            if (x == 0) ok = ok && window(ev->m[3], ev->m[3] + 1, w4);
            if (!ok || w1.empty() || w2.empty()) { ln++; continue; }
            // the simulation's sweep 0: the DSP sample before the first marker's window
            const int64_t S = w1.front() / 512 - 1;
            // exp 0: KYONEX in (M1, M2), KYONB in (M3, M4); exp 1: the RR write in (M1, M2); exp 2/3: the MIXS write in
            // (M1, M2), the read in (M2, M3) (M3 is written after the read returns)
            std::vector<std::vector<Acc>> combos;
            auto between = [](const std::vector<int64_t> &a, const std::vector<int64_t> &b) {
                std::vector<int64_t> r;
                for (int64_t X = a.front() + 4; X <= b.back() - 4; X += 4) r.push_back(X);
                return r;
            };
            const std::vector<int64_t> cw = between(w1, w2);
            const int64_t kbsh = getenv("SCHED_KBSHIFT") ? atoi(getenv("SCHED_KBSHIFT")) : 0;   // the KYONB acts that much earlier
            if (x == 0) {
                const std::vector<int64_t> ck = between(w3, w4);
                for (int64_t a : cw) for (int64_t b : ck)
                    if (b > a) combos.push_back({{a - 512 * S, true, ch, 0x8000u | 0x0001u}, {b - kbsh - 512 * S, true, ch, 0x4001u}});
            } else if (x == 1) {
                /* SCHED_SHIFT=d: the write acts d clocks earlier (= the envelope pass reading its registers d clocks later) */
                const int64_t dsh = getenv("SCHED_SHIFT") ? atoi(getenv("SCHED_SHIFT")) : 0;
                if (getenv("SCHED_MID")) {   /* one candidate: the markers' midpoint (the G2 writes reach the AICA evenly spaced) */
                    int64_t mid = (w1.front() + w1.back() + w2.front() + w2.back()) / 4;
                    mid = (mid & ~3) + 2;
                    combos.push_back({{mid - dsh - 512 * S, true, ch + 0x14, (15u << 10) | 30u}});
                } else
                    for (int64_t a : cw) combos.push_back({{a - dsh - 512 * S, true, ch + 0x14, (15u << 10) | 30u}});
            } else {
                const std::vector<int64_t> cr = between(w2, w3);
                const uint32_t mx = 0x4500 + 8 * bus + 4;
                for (int64_t a : cw) for (int64_t b : cr)
                    if (b > a) combos.push_back({{a - 512 * S, true, mx, ev->value}, {b - 512 * S, false, mx, 0}});
            }
            if (combos.empty()) { ln++; continue; }
            // the console's observation: the bus words of DSP samples S .. S + OBS - 1 (DSP sample n starts at ph 64 of sweep n
            // and shows sweep n - 1; the simulation's sweep s reads it in DSP sample S + s)
            int32_t hw[OBS];
            bool have = true;
            for (int s = 0; s < OBS; s++) {
                auto it = smp.find(S + s);
                if (it == smp.end()) { have = false; break; }
                hw[s] = it->second[bus - 1];
            }
            if (!have) { ln++; continue; }
            // the model at the base state, aligned to sweep S: exp 1 needs the console's MDEC_CT (the envelope clock),
            // exp 2 the bus-2 banks as the log shows them in DSP samples S + 1 (bank of sweep S) and S (sweep S - 1)
            std::set<std::vector<int32_t>> pred;
            std::map<std::vector<int32_t>, std::set<int>> post;   // exp 0: prediction -> the states it leaves
            std::vector<int32_t> obs(hw, hw + OBS);
            if (x >= 2) obs.push_back((int32_t)ev->rd);
            for (int st : (x == 0 ? pre : std::set<int>{0})) {
            AicaModel *b0 = new AicaModel();
            uint8_t *own = b0->ram;
            memcpy((void *)b0, st ? baseN : base, sizeof *b0);
            if ((int64_t)(b0->samples & 1) != (S & 1)) b0->step();
            if (x == 1) {
                auto it = smp.find(S + 1);
                if (it != smp.end()) b0->MDEC_CT = ((uint32_t)it->second[2] + 1) & 0xFFFF;   // DSP sample S: MDEC_CT(S + 1) + 1
                /* the held level before the event, from the log (a reset whose attack had not finished before its key-off
                 * holds above 0): the attenuation whose output word is the logged one */
                for (int a = 0; a < 0x3C0; a++) {
                    const int32_t M = 127 - (a & 63), V16 = (((int64_t)0x8000 * M) >> (7 + (a >> 6))) >> 4 << 4;
                    if (bus_word(V16) == hw[0]) { b0->slot[k].AEG.a = (uint16_t)a; break; }
                }
            }
            if (x == 2) {
                auto i1 = smp.find(S + 1), i0 = smp.find(S);
                if (i1 != smp.end() && i0 != smp.end()) {
                    b0->mixs[b0->samples & 1][2] = -i1->second[1] * 16;              // sweep S's bank (read by DSP sample S + 1)
                    b0->mixs[(b0->samples & 1) ^ 1][2] = -i0->second[1] * 16;       // sweep S - 1's
                }
            }
            for (auto &cmb : combos) {
                int32_t w[OBS]; uint32_t rv = 0; bool kept = false;
                simulate(*b0, cmb, bus, w, &rv, k, &kept);
                std::vector<int32_t> key(w, w + OBS);
                if (x >= 2) key.push_back((int32_t)rv);
                pred.insert(key);
                post[key].insert(kept ? 1 : 0);
            }
            b0->ram = own;
            delete b0;
            }
            if (x == 0 && getenv("SCHED_PRE")) {
                char pb[160]; std::string ps; for (int q : pre) ps += q ? "N" : "R";
                std::string po; if (post.count(obs)) for (int q : post[obs]) po += q ? "N" : "R";
                snprintf(pb, sizeof pb, "PRE k %2d b %d e %2d: pre %s post %s", k, batch, ev->e, ps.c_str(), po.c_str());
#pragma omp critical
                out_lines.push_back(pb);
            }
            if (x == 0) pre = post.count(obs) ? post[obs] : std::set<int>{0, 1};
            if (x == 1 && getenv("SCHED_ENVHIST")) {
                // which envelope pass took the RR write: the one computed in the midpoint's sweep s ("own": the model's
                // write just before frame k of s) or in s + 1 ("next": just after); informative when the two differ.
                // The midpoint relative to slot k's frame start, the console's midpoint sits ~5 clocks before the X0
                // (tests/sub_frame: SA / TL / IMXL thresholds at midpoint -3 / 29 / 53 for X0 2 / 34 / 58).
                int64_t mid = (w1.front() + w1.back() + w2.front() + w2.back()) / 4;
                const int64_t s0 = mid / 512;
                const int rel = (int)(mid - 512 * s0 - 8 * k);
                AicaModel *bm = new AicaModel();
                uint8_t *ow = bm->ram;
                memcpy((void *)bm, base, sizeof *bm);
                if ((int64_t)(bm->samples & 1) != (S & 1)) bm->step();
                { auto it = smp.find(S + 1); if (it != smp.end()) bm->MDEC_CT = ((uint32_t)it->second[2] + 1) & 0xFFFF; }
                int32_t wa[OBS], wb[OBS]; uint32_t rv;
                const int64_t fa = 512 * (s0 - S) + 8 * k;
                simulate(*bm, {{fa - 6, true, ch + 0x14, (15u << 10) | 30u}}, bus, wa, &rv);
                simulate(*bm, {{fa + 2, true, ch + 0x14, (15u << 10) | 30u}}, bus, wb, &rv);
                bm->ram = ow;
                delete bm;
                const bool ia = std::equal(wa, wa + OBS, hw), ib = std::equal(wb, wb + OBS, hw);
                if (ia != ib) {
                    char hb[96];
                    snprintf(hb, sizeof hb, "ENV %d %s", rel, ia ? "own" : "next");
#pragma omp critical
                    out_lines.push_back(hb);
                }
            }
            // exp 0 / 1 compare the first sample that differs from the one before the event only; the rest of the window
            // (the one-shot's end, the release) is the same law and is compared too
            if (x == 1) {   /* DSP sample S shows the sweep before the base point: the held level, set from the log above */
                std::set<std::vector<int32_t>> p2;
                for (auto p : pred) { p[0] = obs[0]; p2.insert(p); }
                pred.swap(p2);
            }
            const bool d = pred.size() == 1, good = pred.count(obs) > 0;
            if (d) ld++;
            if (d && good) la++;
            if (!good) lb++;
            if (x == 0 && getenv("SCHED_KEY")) {
                // KYONEX / KYONB windows relative to slot k's frame start in the sweep after the KYONEX's (the latch
                // sweep), the outcome: keyed in DSP sample (relative to that sweep) or never
                const int64_t ka = cw.front(), kb = cw.back();
                const std::vector<int64_t> ck = between(w3, w4);
                const int64_t L = ka / 512 + 1, fk = 512 * L + 8 * k;
                int kon = -99;
                for (int s = 0; s < OBS; s++) if (hw[s] != 0) { kon = (int)(S + s - L); break; }
                char kb2[200];
                snprintf(kb2, sizeof kb2, "KEY k %2d b %d e %2d kyonex %lld..%lld (latch sweep %s) kyonb %lld..%lld vs frame k: keyed in DSP sample L%+d",
                         k, batch, ev->e, (long long)(ka % 512), (long long)(kb % 512), ka / 512 == kb / 512 ? "one" : "TWO",
                         (long long)(ck.front() - fk), (long long)(ck.back() - fk), kon);
#pragma omp critical
                out_lines.push_back(kb2);
            }
            if (verbose || !good) {
                char buf[512];
                int n = snprintf(buf, sizeof buf, "  k %2d exp %d b %d e %2d: %zu combos (first access X0 %lld..%lld vs frame k: %+lld..%+lld), %zu predictions, observed", k, x, batch, ev->e,
                                 combos.size(), (long long)(combos.front()[0].t % 512), (long long)(combos.back()[0].t % 512), (long long)(combos.front()[0].t % 512 - 8 * k), (long long)(combos.back()[0].t % 512 - 8 * k), pred.size());
                for (int32_t v : obs) n += snprintf(buf + n, sizeof buf - n, " %d", v);
                n += snprintf(buf + n, sizeof buf - n, "; predicted");
                for (auto &p : pred) { n += snprintf(buf + n, sizeof buf - n, " ["); for (int32_t v : p) n += snprintf(buf + n, sizeof buf - n, " %d", v); n += snprintf(buf + n, sizeof buf - n, " ]"); }
#pragma omp critical
                out_lines.push_back(std::string(buf) + (good ? "" : "  <-- FAIL"));
            }
        }
        delete base;
        delete baseN;
#pragma omp critical
        { tot[x] += lt; det[x] += ld; agree[x] += la; bad[x] += lb; nodata[x] += ln; }
    }
    std::sort(out_lines.begin(), out_lines.end());
    for (auto &l : out_lines) printf("%s\n", l.c_str());
    long T = 0, B = 0, N = 0;
    for (int x = 0; x < NEXP; x++) {
        printf("exp %d %-28s %5ld events, %5ld determinate, %5ld agree, %ld fail, %ld without data\n", x, exp_name[x], tot[x],
               det[x], agree[x], bad[x], nodata[x]);
        T += tot[x]; B += bad[x]; N += nodata[x];
    }
    printf("sched_check %s: %ld events, %ld fail, %ld without data\n", dir.c_str(), T, B, N);
    return B || N ? 1 : 0;
}
