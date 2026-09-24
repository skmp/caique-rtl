// armjob_console.cpp -- the SH4-timed ARM kernels of a wren7 suite (…_n64 / …_n576 job pairs): MCLK per loop iteration on
// the console (tests/hw/SUITE/hw/results.txt, SH4 cycles, median of the repeats, / U = 8.8352), on wren7's ARM7DI model
// with rtl/v1 as its bus and with wren7's DcArmBus (both from armjob_tb's output), as hw_suite check_timed computes it.
//   armjob_console SUITE_DIR TOL < armjob_tb-output       (run from wren7-rtl/model; exit 0 iff the RTL is within TOL)
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <cmath>

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: armjob_console SUITE_DIR TOL < armjob output\n"); return 2; }
    const std::string dir = argv[1];
    const double tol = atof(argv[2]), U = 8.8352;
    std::map<std::string, double> hw, rtl, mdl;
    char line[1024];
    FILE *f = fopen((dir + "/hw/results.txt").c_str(), "r");
    if (!f) { perror("results.txt"); return 2; }
    while (fgets(line, sizeof line, f)) {
        char n[128], st[32];
        if (sscanf(line, "%127s %31s", n, st) != 2 || strcmp(st, "done")) continue;
        std::vector<double> v;
        char *p = line;
        for (int i = 0; i < 3; i++) { p = strchr(p, ' '); if (p) p++; }
        while (p && *p) { char *e; double x = strtod(p, &e); if (e == p) break; v.push_back(x); p = e; }
        std::sort(v.begin(), v.end());
        if (!v.empty()) hw[n] = v[v.size() / 2];
    }
    fclose(f);
    while (fgets(line, sizeof line, stdin)) {
        char n[128], ok[16], st[16]; double r, m;
        if (sscanf(line, "%127s %15s %15s rtl %lf model %lf", n, ok, st, &r, &m) == 5 && !strcmp(st, "done")) { rtl[n] = r; mdl[n] = m; }
    }
    int total = 0, rbad = 0, mbad = 0;
    for (auto &kv : rtl) {
        const std::string &n = kv.first;
        if (n.size() < 4 || n.compare(n.size() - 4, 4, "_n64")) continue;
        const std::string b = n.substr(0, n.size() - 4);
        if (!rtl.count(b + "_n576") || !hw.count(n) || !hw.count(b + "_n576")) continue;
        const double h = (hw[b + "_n576"] - hw[n]) / 512 / U;
        const double r = (rtl[b + "_n576"] - rtl[n]) / 512, m = (mdl[b + "_n576"] - mdl[n]) / 512;
        const bool rb = fabs(h - r) > tol, mb = fabs(h - m) > tol;
        printf("%-24s console %8.2f rtl %8.2f model %8.2f%s%s\n", b.c_str(), h, r, m, rb ? "  RTL-DIFFERENT" : "",
            mb ? "  MODEL-DIFFERENT" : "");
        total++; rbad += rb; mbad += mb;
    }
    printf("%s: %d kernels; within %.2f MCLK per iteration of the console: rtl %d, model %d\n", dir.c_str(), total, tol,
        total - rbad, total - mbad);
    return rbad ? 1 : 0;
}
