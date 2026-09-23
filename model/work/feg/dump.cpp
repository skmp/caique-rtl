// scratch: print recovered u(n) (v >> 1) of work/feg/ft_<b>_<k>.u over a sample range
#include <cstdio>
#include <cstdlib>
#include <vector>
int main(int argc, char **argv) {
    char p[64]; snprintf(p, sizeof p, "work/feg/ft_%s_%s.u", argv[1], argv[2]);
    FILE *f = fopen(p, "rb"); std::vector<int> u; int x; while (fread(&x, 4, 1, f) == 1) u.push_back(x); fclose(f);
    int a = atoi(argv[3]), b = atoi(argv[4]);
    for (int n = a; n < b && n < (int)u.size(); n++) printf("%d:%s%03x%s", n, u[n] < 0 ? "?" : "", u[n] < 0 ? 0 : u[n], (n - a) % 12 == 11 ? "\n" : " ");
    printf("\n");
}
