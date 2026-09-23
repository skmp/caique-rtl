// filt_sym.cpp -- sign symmetry of the filt_cyc step responses (work/filt/step.bin, tools/filt_step.py).
// For each +A / -A stream pair (same F, Q) and each sample from `on`, classify y(-A) against y(+A):
//   N: y(-A) == ~y(+A) (= -y - 1)      Z: y(-A) == -y(+A)      other: neither
// Window: from `on` (the step) to the end of the capture; also the rest samples before `on`.
// Build: make -C tools filt_sym (-> build/tools/filt_sym) ; run from caique-rtl/model
#include <cstdio>
#include <cstdint>
#include <vector>

struct DS { int F, Q, A, on, N; std::vector<int32_t> y; };

int main() {
    std::vector<DS> ds;
    FILE *f = fopen("work/filt/step.bin", "rb");
    if (!f) { perror("work/filt/step.bin"); return 1; }
    int hdr[5];
    while (fread(hdr, 4, 5, f) == 5) {
        DS d{hdr[0], hdr[1], hdr[2], hdr[3], hdr[4], {}};
        d.y.resize(d.N);
        if (fread(d.y.data(), 4, d.N, f) != (size_t)d.N) break;
        ds.push_back(d);
    }
    fclose(f);
    for (size_t i = 0; i < ds.size(); i++)
        for (size_t j = 0; j < ds.size(); j++) {
            const DS &a = ds[i], &b = ds[j];
            if (a.F != b.F || a.Q != b.Q || a.A <= 0 || b.A != -a.A) continue;
            int cN = 0, cZ = 0, cO = 0, pN = 0, pZ = 0, pO = 0, firstO = -1;
            for (int n = 0; n < a.N && n < b.N; n++) {
                int ya = a.y[n], yb = b.y[n];
                bool N = yb == ~ya, Z = yb == -ya;
                if (n < a.on) { pN += N; pZ += Z && !N; pO += !N && !Z; continue; }
                cN += N; cZ += Z && !N;
                if (!N && !Z) { cO++; if (firstO < 0) firstO = n - a.on; }
            }
            printf("F %04x Q %2d streams %zu/%zu: from the step N %d Z %d other %d (first other at +%d) | rest before: N %d Z %d other %d\n",
                   a.F, a.Q, i, j, cN, cZ, cO, firstO, pN, pZ, pO);
        }
    return 0;
}
