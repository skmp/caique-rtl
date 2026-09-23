// udump.cpp -- print tracked FEG values u(n) (from feg_lock/feg_track .u files) with ring parity and counter.
//   udump <u file> <c0 hex> <n_first> <onset> <K> <from> <to>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <vector>
int main(int argc, char **argv) {
    FILE *f = fopen(argv[1], "rb"); std::vector<int32_t> u; int32_t x; while (fread(&x, 4, 1, f) == 1) u.push_back(x); fclose(f);
    uint32_t c0 = strtoul(argv[2], 0, 16), first = atoi(argv[3]); int on = atoi(argv[4]); long K = atol(argv[5]); int a = atoi(argv[6]), b = atoi(argv[7]);
    int prev = -2;
    for (int n = a; n <= b && n < (int)u.size(); n++) {
        uint32_t md = (c0 - first - on - n) & 0xFFFF;
        if (u[n] != prev || (md & 1) == 0) {
            printf("n %5d MDEC %04x %s cnt %5ld (&7 %ld, -1&7 %ld): u %03x%s\n", n, md, md & 1 ? "odd " : "EVEN", (K - (md >> 1)) & 0x3FFF, (K - (md >> 1)) & 7, (K - (md >> 1) - 1) & 7, u[n] < 0 ? 0xfff : u[n], u[n] < 0 ? " (amb)" : "");
        }
        prev = u[n];
    }
}
