// scratch: sample indices where the recovered u changes (skipping ambiguous samples), with the gaps between them
#include <cstdio>
#include <cstdlib>
#include <vector>
int main(int argc, char **argv) {
    char p[64]; snprintf(p, sizeof p, "work/feg/ft_%s_%s.u", argv[1], argv[2]);
    FILE *f = fopen(p, "rb"); std::vector<int> u; int x; while (fread(&x, 4, 1, f) == 1) u.push_back(x); fclose(f);
    int a = atoi(argv[3]), b = atoi(argv[4]), last = -1, lastn = -1, lastchg = -1;
    for (int n = a; n < b && n < (int)u.size(); n++) {
        if (u[n] < 0) continue;
        if (last >= 0 && u[n] != last) {
            // the change happened somewhere in (lastn, n]; exact only if lastn == n - 1
            printf("%d%s(%+d) ", n, lastn == n - 1 ? "" : "~", u[n] - last);
            if (lastn == n - 1 && lastchg >= 0) printf("[gap %d] ", n - lastchg);
            if (lastn == n - 1) lastchg = n;
        }
        last = u[n]; lastn = n;
    }
    printf("\n");
}
