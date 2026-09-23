// filt_unity_sim.cpp -- prediction for the unity-cutoff Q0 alternating mode (tests/filt_overflow): FLV 0x1FFE (k 512, s 9),
// Q0 (q128 192), full-scale alternating input from rest.  Growth of low/band with the recurrence unbounded, and with
// the signed 24-bit clamp of x - low - damping before the cutoff multiply (NOTES.md "Slot filter").
// Build: make -C tools filt_unity_sim (-> build/tools/filt_unity_sim)
#include <cstdio>
#include <cstdint>
#include <cstdlib>
typedef int64_t I;
static I ceilshr(I n, int s) { return -((-n) >> s); }
int main() {
    for (int clamp = 0; clamp < 2; clamp++) {
        I L = 0, B = 0, peakL = 0, peakB = 0;
        int mark[4] = {-1, -1, -1, -1};
        const I lim[4] = {I(1) << 21, I(1) << 22, I(1) << 23, I(1) << 31};
        for (int n = 0; n < 200000; n++) {
            I x = (n & 1) ? -32767 * 8 : 32767 * 8;
            I D = 2 * ceilshr(192 * B, 8);
            I d = x - L - D;
            if (clamp) d = d < -(I(1) << 23) ? -(I(1) << 23) : d > (I(1) << 23) - 1 ? (I(1) << 23) - 1 : d;
            B += (512 * d) >> 9;
            L += ceilshr(512 * B, 9);
            I m = llabs(L) > llabs(B) ? llabs(L) : llabs(B);
            if (llabs(L) > peakL) peakL = llabs(L);
            if (llabs(B) > peakB) peakB = llabs(B);
            for (int j = 0; j < 4; j++) if (mark[j] < 0 && m >= lim[j]) mark[j] = n;
            if (n == 19 || n == 199 || n == 1999 || n == 199999)
                printf("%s n %6d: L %11lld B %11lld\n", clamp ? "clamp24" : "unbound", n + 1, (long long)L, (long long)B);
        }
        printf("%s: max(|L|,|B|) first reaches 2^21 at n=%d, 2^22 at %d, 2^23 at %d, 2^31 at %d; peak |L| %lld |B| %lld\n\n",
               clamp ? "clamp24" : "unbound", mark[0], mark[1], mark[2], mark[3], (long long)peakL, (long long)peakB);
    }
}
