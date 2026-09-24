// cosim.cpp -- rtl/v1 against the model: replays a case's access trace (model/host/io_model.cpp, CAIQUE_TRACE) into
// the RTL and compares every read.  An access between the model's steps p-1 and p is an access at the RTL's sample
// boundary p (the reordered model equals the hardware with every access at the boundary: model/NOTES "Session 7").
// The RTL's sample has two boundary points, both with the engine frozen (ce = 0):
//   ph 0 (the slot sweep): channel / common registers, MIXS, KYONEX, monitors, wave RAM writes, and wave RAM reads
//        that a later write of the same gap overwrites (read-modify-write);
//   ph 64 (the DSP boundary, aica_pkg DSP_OFFSET): DSP buffers (0x2804, 0x3000-0x3FFF, 0x4000-0x44FF, 0x4580-0x45FF)
//        and the other RAM reads.
//   cosim <trace> [-v] [-max n] [-out]      exit 0 iff every read matches
#include "Vtb_top.h"
#include "verilated.h"
#if VM_TRACE
#include "verilated_vcd_c.h"
static VerilatedVcdC *VCD;
static uint32_t vcd_from = 0xFFFFFFFF, vcd_to = 0, cur_gap;
#endif
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <map>
#include <string>

struct Rec { uint32_t gap, kind, off, val; };
static const int DSP_OFFSET = 64;                     // aica_pkg.sv
static Vtb_top *T;
static uint64_t clocks;
static void tick() {
    T->clk = 0; T->eval();
#if VM_TRACE
    bool dump = VCD && cur_gap >= vcd_from && cur_gap <= vcd_to;
    if (dump) VCD->dump(clocks * 2);
#endif
    T->clk = 1; T->eval();
#if VM_TRACE
    if (dump) VCD->dump(clocks * 2 + 1);
#endif
    clocks++;
}

static bool dsp_class(uint32_t off) {
    off &= 0x7FFC;
    return off == 0x2804 || (off >= 0x3000 && off < 0x4000) || (off >= 0x4000 && off < 0x4500) || (off >= 0x4580 && off < 0x4600);
}
static uint32_t access(bool we, bool ram, uint32_t addr, uint32_t wdata) {
    T->dbg_req = 1; T->dbg_we = we; T->dbg_ram = ram; T->dbg_addr = addr; T->dbg_wdata = wdata;
    tick();
    T->dbg_req = 0;
    for (int i = 0; i < 4; i++) { tick(); if (T->dbg_ack) return T->dbg_rdata; }
    fprintf(stderr, "no ack for %s %s %06x\n", we ? "write" : "read", ram ? "RAM" : "reg", addr);
    exit(3);
}

int main(int argc, char **argv) {
    Verilated::commandArgs(argc, argv);
    if (argc < 2) { fprintf(stderr, "usage: cosim <trace> [-v] [-max n] [-out]\n"); return 2; }
    bool verbose = false, cmp_out = false; long maxshow = 20;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "-v")) verbose = true;
        else if (!strcmp(argv[i], "-out")) cmp_out = true;
        else if (!strcmp(argv[i], "-max") && i + 1 < argc) maxshow = atol(argv[++i]);
#if VM_TRACE
        else if (!strcmp(argv[i], "-vcd") && i + 3 < argc) {
            Verilated::traceEverOn(true); vcd_from = atol(argv[++i]); vcd_to = atol(argv[++i]);
            VCD = new VerilatedVcdC; T = nullptr; static std::string fn; fn = argv[++i]; (void)fn;
            setenv("COSIM_VCD", fn.c_str(), 1);
        }
#endif
    }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 2; }
    std::vector<Rec> recs; Rec r;
    while (fread(&r, 4, 4, f) == 4) recs.push_back(r);
    fclose(f);
    uint32_t K = 6491;
    std::vector<std::vector<size_t>> early, late;     // per gap, record indices in program order
    std::map<uint32_t, std::pair<int16_t, int16_t>> outs;
    uint32_t last_gap = 0;
    for (size_t i = 0; i < recs.size(); i++) {
        const Rec &x = recs[i];
        if (x.kind == 'K') { K = x.val; continue; }
        if (x.kind == 'O') { outs[x.gap] = {(int16_t)(x.off & 0xFFFF), (int16_t)(x.off >> 16)}; continue; }
        if (x.gap + 1 > early.size()) { early.resize(x.gap + 1); late.resize(x.gap + 1); }
        last_gap = std::max(last_gap, x.gap);
        bool is_early;
        if (x.kind == 'w') is_early = true;
        else if (x.kind == 'r') {
            is_early = false;                              // unless a later write of this gap overwrites the word
            for (size_t j = i + 1; j < recs.size() && recs[j].gap == x.gap; j++)
                if (recs[j].kind == 'w' && (recs[j].off & ~3u) == (x.off & ~3u)) { is_early = true; break; }
        } else is_early = !dsp_class(x.off);
        (is_early ? early : late)[x.gap].push_back(i);
    }
    T = new Vtb_top;
#if VM_TRACE
    if (VCD) { T->trace(VCD, 99); VCD->open(getenv("COSIM_VCD")); }
#endif
    T->eg_k = K; T->ce = 0; T->clk = 0; T->eval();
    long nread = 0, nbad = 0, nwarn = 0, nout = 0, noutbad = 0;
    std::map<std::string, long> badby;
    auto run_list = [&](const std::vector<size_t> &lst) {
        for (size_t idx : lst) {
            const Rec &x = recs[idx];
            bool we = x.kind == 'W' || x.kind == 'w', ram = x.kind == 'w' || x.kind == 'r';
            uint32_t addr = ram ? (x.off & 0x1FFFFC) : (x.off & 0x7FFC);
            uint32_t v = access(we, ram, addr, x.val);
            if (!we) {
                nread++;
                if (v != x.val) {
                    nbad++;
                    char key[48];
                    if (ram) snprintf(key, sizeof key, "RAM");
                    else if (addr < 0x2000) snprintf(key, sizeof key, "chan+%02x", addr & 0x7F);
                    else snprintf(key, sizeof key, "reg %04x", addr >= 0x4000 && addr < 0x4500 ? (addr & 0xFF04) | 0x4000 : addr);
                    badby[key]++;
                    if (nbad <= maxshow)
                        printf("MISMATCH gap %u %s %06x: model %08x rtl %08x\n", x.gap, ram ? "RAM" : "reg", addr, x.val, v);
                }
            }
        }
    };
    static const std::vector<size_t> none;
    uint32_t end_gap = last_gap + 2;
    for (uint32_t q = 0; q <= end_gap; q++) {
#if VM_TRACE
        cur_gap = q;
#endif
        // ph 0 (frozen): the slot-side accesses of gap q
        T->ce = 0;
        run_list(q < early.size() ? early[q] : none);
        T->ce = 1;
        for (int i = 0; i < DSP_OFFSET; i++) {
            tick();
            if (T->warn_step) nwarn++;
        }
        // ph DSP_OFFSET (frozen): the DSP-side accesses of gap q
        T->ce = 0;
        run_list(q < late.size() ? late[q] : none);
        T->ce = 1;
        for (int i = DSP_OFFSET; i < 512; i++) {
            tick();
            if (T->warn_step) nwarn++;
            if (T->out_valid && cmp_out && q >= 1) {
                auto it = outs.find(q - 1);
                if (it != outs.end()) {
                    nout++;
                    if (it->second.first != (int16_t)T->out_l || it->second.second != (int16_t)T->out_r) {
                        noutbad++;
                        if (verbose && noutbad <= 10) printf("OUT sample %u: model %d %d rtl %d %d\n", q - 1, it->second.first, it->second.second, (int16_t)T->out_l, (int16_t)T->out_r);
                    }
                }
            }
        }
        if (T->ph != 0) { fprintf(stderr, "phase lost at gap %u (ph %u)\n", q, T->ph); return 3; }
    }
    printf("cosim %s: %u samples, %zu accesses, %ld reads, %ld mismatches", argv[1], end_gap + 1, recs.size(), nread, nbad);
    if (cmp_out) printf(", outputs %ld/%ld differ", noutbad, nout);
    if (nwarn) printf(", %ld stepping warnings", nwarn);
    printf(" (%.1f M clocks)\n", clocks / 1e6);
    for (auto &kv : badby) printf("  %-12s %ld\n", kv.first.c_str(), kv.second);
#if VM_TRACE
    if (VCD) VCD->close();
#endif
    delete T;
    return nbad ? 1 : 0;
}
