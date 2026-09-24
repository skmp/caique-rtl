// keytab.cpp -- tests/sub_sched exp 0 (KYONB window): the outcome of every event against where its KYONEX and KYONB
// landed.  Input: the KEY lines of `SCHED_KEY=1 build/tools/sched_check` (work/s9/key_hw.txt).  Prints, per KYONEX phase
// (64-clock buckets of its window midpoint) and KYONB offset from slot k's frame start in the sweep after the KYONEX's
// (64-clock buckets), the counts keyed at L+2 / L+3 / never.
#include <cstdio>
#include <cstring>
#include <map>
#include <array>
int main(int argc, char **argv) {
    FILE *f = fopen(argc > 1 ? argv[1] : "work/s9/key_hw.txt", "r");
    if (!f) return 2;
    char line[512];
    std::map<std::pair<int, int>, std::array<int, 3>> t;
    while (fgets(line, sizeof line, f)) {
        int k, b, e, xa, xb, ya, yb; char one[16], out[16];
        if (sscanf(line, "KEY k %d b %d e %d kyonex %d..%d (latch sweep %15[^)]) kyonb %d..%d vs frame k: keyed in DSP sample %15s",
                   &k, &b, &e, &xa, &xb, one, &ya, &yb, out) != 9) continue;
        const int kx = (xa + xb) / 2, kb = (ya + yb) / 2;
        auto bucket = [](int v) { return v >= 0 ? v / 64 * 64 : -((-v + 63) / 64 * 64); };
        const int o = !strcmp(out, "L+2") ? 0 : !strcmp(out, "L+3") ? 1 : 2;
        t[{bucket(kx), bucket(kb)}][o]++;
    }
    fclose(f);
    printf("KYONEX ph \\ KYONB - frame k (L): keyed L+2 / L+3 / never\n");
    int row = -1;
    for (auto &kv : t) {
        if (kv.first.first != row) { row = kv.first.first; printf("\n%4d:", row); }
        if (kv.first.second < -320 || kv.first.second > 320) continue;
        printf("  %4d %d/%d/%d", kv.first.second, kv.second[0], kv.second[1], kv.second[2]);
    }
    printf("\n");
}
