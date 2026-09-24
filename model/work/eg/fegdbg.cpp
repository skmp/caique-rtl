// fegdbg.cpp -- print the model FEG value per sample next to the tracked u for one eg_lock/feg_krs stream (debug of eg_model)
#include "../../sample-model/aica_model.h"
#include "../../tools/filt_capture.h"
#include <cstdlib>
#include <vector>
using namespace caique;
int main(int argc, char **argv) {
    // feg_odd stream 0: slot 0, KRS 0 OCT 0 FNS 0x200, rates 24 26 28 22, ref slot 3; K 6491, c0 0x0374; key-off at n given
    int koff = atoi(argv[1]), from = atoi(argv[2]), to = atoi(argv[3]);
    auto cp = cap("tests/eg_lock/hw/feg_odd");
    std::vector<int32_t> u; FILE *f = fopen("work/eg/feg_odd_0.u", "rb"); int32_t x; while (fread(&x, 4, 1, f) == 1) u.push_back(x); fclose(f);
    int on = 139; uint32_t md_on = (0x0374 - cp.first - on) & 0xFFFF;
    AicaModel m; m.eg_K = 6491;
    uint32_t seed = 4242; for (int i = 0; i < 8192; i++) { seed = seed * 1103515245u + 12345u; int16_t s = (int16_t)(seed >> 16); if (!s) s = 1; m.ram[0x20000 + 2 * i] = (uint8_t)s; m.ram[0x20000 + 2 * i + 1] = (uint8_t)(s >> 8); }
    auto slot = [&](int ch, int lpoff, int krs, int far, int fd1r, int fd2r, int frr, int flv[5], int isel) {
        auto aw = [&](uint32_t o, uint32_t v) { m.write(0x80 * ch + o, v); };
        aw(0x04, 0); aw(0x08, 0); aw(0x0C, 8192); aw(0x10, 31); aw(0x14, (krs << 10) | (lpoff ? 31 : 0)); aw(0x18, 0x200); aw(0x1C, 0);
        aw(0x20, (15 << 4) | isel); aw(0x24, 0); aw(0x28, (1 << 6) | (lpoff << 5) | 4);
        for (int i = 0; i < 5; i++) aw(0x2C + 4 * i, flv[i]);
        aw(0x40, (far << 8) | fd1r); aw(0x44, (fd2r << 8) | frr); aw(0x00, (1 << 9) | 2);
    };
    int flv[5] = {0x1800, 0x1C00, 0x1800, 0x1A00, 0x1C00}, flvr[5] = {0x1FFE, 0x1FFE, 0x1FFE, 0x1FFE, 0x1FFE};
    slot(0, 0, 0, 24, 26, 28, 22, flv, 0); slot(3, 1, 15, 0, 0, 0, 0, flvr, 3);
    for (int i = 0; i < 16; i++) m.step();
    m.MDEC_CT = md_on;
    m.write(0, m.chr(0, 0) | 0x4000); m.write(0x180, m.chr(3, 0) | 0x4000); m.write(0, m.chr(0, 0) | 0x8000);
    for (int n = 0; n < (int)u.size() && n <= to; n++) {
        if (n == koff) { m.write(0, m.chr(0, 0) & 0x3FFF); m.write(0, m.chr(0, 0) | 0x8000); }
        m.step();
        if (n >= from) printf("n %d MDEC %04x %s: hw u %03x  model v %04x (u %03x) st %d %s\n", n, m.MDEC_CT + 1, ((m.MDEC_CT + 1) & 1) ? "odd " : "EVEN", u[n] < 0 ? 0xfff : u[n], m.slot[0].FEG.v, m.slot[0].FEG.v >> 1, m.slot[0].FEG.state, u[n] >= 0 && u[n] != (m.slot[0].FEG.v >> 1) ? "MISMATCH" : "");
    }
}
