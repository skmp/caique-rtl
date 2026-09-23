// filt_reach2.cpp -- try to beat filt_reach (claim F7): can a mid-drive change of Q / cutoff push |low| or |band|
// past 2^23 with the 24-bit clamp of x - low - damping?  Phase 1 pumps the state at setting A (the largest states of
// the single-setting search: Q 31 resonances at e = 15 with the adaptive pump, and the unity-cutoff Q 0 alternating
// mode); phase 2 switches to setting B (every FLV mantissa at e = emin..15, every Q) and continues with one of four
// drives (zero, dc, alternating, pump) for N2 samples.  Reports the global maxima and how many (A, B, drive)
// combinations exceed 2^22 / 2^23.
//   filt_reach2 [-n1 N1] [-n2 N2] [-emin e]     Build: make -C tools filt_reach2 (-> build/tools/filt_reach2)
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <algorithm>
typedef int64_t I;
static const int qm[32] = {192, 176, 160, 144, 128, 120, 112, 104, 96, 88, 80, 72, 64, 60, 56, 52,
                           48, 44, 40, 36, 32, 30, 28, 26, 24, 22, 20, 18, 16, 15, 14, 13};
static inline I ceilshr(I n, int s) { return -((-n) >> s); }
struct F { I k; int s, q; };
static inline F mk(int flv, int q) { return F{flv >= 0x1FFE ? 512 : 256 + ((flv >> 1) & 255), 24 - (flv >> 9), q}; }
static inline void step(const F &f, I x, I &L, I &B) {
    I D = 2 * ceilshr(qm[f.q] * B, 8);
    I H = x - L - D;
    H = H < -8388608 ? -8388608 : H > 8388607 ? 8388607 : H;
    B += (f.k * H) >> f.s;
    L += ceilshr(f.k * B, f.s);
}
static const I A = 32767 * 8;
int main(int argc, char **argv) {
    int N1 = 4096, N2 = 1024, emin = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-n1")) N1 = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-n2")) N2 = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-emin")) emin = atoi(argv[++i]);
    }
    // phase-1 settings: pump at Q31 for FLV 0x1FE0..0x1FFE, Q 30/29 at 0x1FF4; alt at Q0 for 0x1FF0..0x1FFE
    struct Start { int flv, q, kind; };
    std::vector<Start> starts;
    for (int flv = 0x1FE0; flv <= 0x1FFE; flv += 2) starts.push_back({flv, 31, 3});
    for (int flv = 0x1FF0; flv <= 0x1FFE; flv += 2) starts.push_back({flv, 0, 1});
    starts.push_back({0x1FF4, 30, 3}); starts.push_back({0x1FF4, 29, 3}); starts.push_back({0x1F00, 31, 3});
    starts.push_back({0x1E00, 31, 3}); starts.push_back({0x1C00, 31, 3});
    struct Best { I v; int flvA, qA, kindA, flvB, qB, kind2, at; };
    Best gL{0, 0, 0, 0, 0, 0, 0, 0}, gB{0, 0, 0, 0, 0, 0, 0, 0};
    long over22 = 0, over23 = 0, total = 0;
    std::vector<int> settings;
    for (int e = emin; e <= 15; e++) for (int m = 0; m < 256; m++) for (int q = 0; q < 32; q++) settings.push_back((((e << 9) | (m << 1)) << 5) | q);
    for (auto &st : starts) {
        F fa = mk(st.flv, st.q);
        // phase 1 from rest; keep the trajectory's last state AND the state at the |band| and |low| peaks
        I L = 0, B = 0, pL = 0, pB = 0, L_atB = 0, B_atB = 0, L_atL = 0, B_atL = 0;
        for (int n = 0; n < N1; n++) {
            I x = st.kind == 1 ? ((n & 1) ? -A : A) : (B >= 0 ? A : -A);
            step(fa, x, L, B);
            if (llabs(B) > pB) { pB = llabs(B); L_atB = L; B_atB = B; }
            if (llabs(L) > pL) { pL = llabs(L); L_atL = L; B_atL = B; }
        }
        const I s0[3][2] = {{L, B}, {L_atB, B_atB}, {L_atL, B_atL}};
        printf("start FLV %04x Q %2d %s: end (%lld,%lld) peak|B| %lld at (%lld,%lld) peak|L| %lld at (%lld,%lld)\n", st.flv, st.q,
               st.kind == 1 ? "alt" : "pump", (long long)L, (long long)B, (long long)pB, (long long)L_atB, (long long)B_atB,
               (long long)pL, (long long)L_atL, (long long)B_atL);
        std::vector<Best> bl(settings.size()), bb(settings.size());
        std::vector<int> o22(settings.size()), o23(settings.size());
#pragma omp parallel for schedule(dynamic, 256)
        for (size_t i = 0; i < settings.size(); i++) {
            int flv = settings[i] >> 5, q = settings[i] & 31;
            F fb = mk(flv, q);
            Best mL{0, st.flv, st.q, st.kind, flv, q, 0, 0}, mB = mL;
            for (int from = 0; from < 3; from++)
                for (int kind = 0; kind < 4; kind++) {
                    I l = s0[from][0], b = s0[from][1];
                    for (int n = 0; n < N2; n++) {
                        I x = kind == 0 ? 0 : kind == 1 ? A : kind == 2 ? ((n & 1) ? -A : A) : (b >= 0 ? A : -A);
                        step(fb, x, l, b);
                        if (llabs(l) > mL.v) { mL.v = llabs(l); mL.kind2 = kind * 3 + from; mL.at = n; }
                        if (llabs(b) > mB.v) { mB.v = llabs(b); mB.kind2 = kind * 3 + from; mB.at = n; }
                    }
                }
            bl[i] = mL; bb[i] = mB;
            I m = std::max(mL.v, mB.v);
            o22[i] = m >= (I(1) << 22); o23[i] = m >= (I(1) << 23);
        }
        for (size_t i = 0; i < settings.size(); i++) {
            total++; over22 += o22[i]; over23 += o23[i];
            if (bl[i].v > gL.v) gL = bl[i];
            if (bb[i].v > gB.v) gB = bb[i];
        }
    }
    const char *kn[4] = {"zero", "dc", "alt", "pump"}, *fr[3] = {"end", "peakB", "peakL"};
    printf("filt_reach2: N1 %d N2 %d, %zu starts x %zu second settings x 4 drives x 3 start states\n", N1, N2, starts.size(), settings.size());
    printf("  combos with max >= 2^22: %ld, >= 2^23: %ld of %ld\n", over22, over23, total);
    printf("  max |low|  %lld: A FLV %04x Q %d -> B FLV %04x Q %d, drive %s from %s, at +%d\n", (long long)gL.v, gL.flvA, gL.qA, gL.flvB, gL.qB,
           kn[gL.kind2 / 3], fr[gL.kind2 % 3], gL.at);
    printf("  max |band| %lld: A FLV %04x Q %d -> B FLV %04x Q %d, drive %s from %s, at +%d\n", (long long)gB.v, gB.flvA, gB.qA, gB.flvB, gB.qB,
           kn[gB.kind2 / 3], fr[gB.kind2 % 3], gB.at);
    return 0;
}
