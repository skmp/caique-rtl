// cosim_cycle.cpp -- rtl/v1 against the cycle model (model/cycle-model), clock for clock through the SH4 port.
// The trace comes from the cycle model's host harness (model/cycle-model/io_cycle.cpp, CAIQUE_TRACE, CAIQUE_TRACE_OUT=1):
// every access with the clock its request was raised, the clock its ack came and the data.  The RTL runs from power-on
// with the engine never frozen; each request is raised at its recorded clock and held until sh_ack.  Checked: the ack
// clock, the read data, and every output sample (by clock).  A 'P' record (replay parameters applied by the model before
// that clock, a ph 0) loads the RTL's MDEC_CT (the model's + 1: the RTL counter is the running DSP sample's) and LFSR (one
// step back: the RTL steps a slot's LFSR at its stage B frame, the model at its frame's end) in the clock before, and K.
//   cosim_cycle <trace> [-max n]      exit 0 iff everything matches
#include "Vtb_top.h"
#include "verilated.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <map>
#include <string>

struct Rec { uint64_t clk, ack; uint32_t kind, off, wdata, rdata; };
static Vtb_top *T;
static uint64_t clocks;
static long nwarn;
static std::map<uint64_t, uint32_t> rtl_out;      // output samples of the RTL, by clock
struct Param { uint32_t mdec, lfsr, K; };
static std::map<uint64_t, Param> params;          // 'P' records by the clock they apply before
static uint32_t lfsr_unstep(uint32_t l) { return ((l << 1) & 0x1FFFE) | (((l >> 16) ^ (l >> 4)) & 1); }
static void tick() {
    auto it = params.find(clocks + 1);
    T->ld = it != params.end();
    if (T->ld) {
        T->ld_mdec = (it->second.mdec + 1) & 0xFFFF; T->ld_lfsr = lfsr_unstep(it->second.lfsr);
        T->eg_k = it->second.K & 0x3FFF; T->eg_par = (it->second.K >> 16) & 1;   // read data: K | par << 16
    }
    T->clk = 0; T->eval();
    T->clk = 1; T->eval();
    clocks++;
    if (T->warn_step) nwarn++;
    if (T->out_valid) rtl_out[clocks - 1] = (uint32_t)(uint16_t)T->out_l | ((uint32_t)(uint16_t)T->out_r << 16);
}

int main(int argc, char **argv) {
    Verilated::commandArgs(argc, argv);
    if (argc < 2) { fprintf(stderr, "usage: cosim_cycle <trace> [-max n]\n"); return 2; }
    long maxshow = 20;
    for (int i = 2; i < argc; i++)
        if (!strcmp(argv[i], "-max") && i + 1 < argc) maxshow = atol(argv[++i]);
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 2; }
    std::vector<Rec> recs;
    Rec r;
    while (fread(&r, sizeof r, 1, f) == 1) recs.push_back(r);
    fclose(f);
    T = new Vtb_top;
    for (const Rec &x : recs) if (x.kind == 'P') params[x.clk] = Param{x.off, x.wdata, x.rdata};
    T->ce = 1; T->clk = 0; T->eg_k = 6491; T->eg_par = 0; T->ld = 0;
    T->sh_req = 0; T->arm_req = 0; T->dbg_req = 0; T->eval();
    std::map<uint64_t, uint32_t> model_out;
    long nacc = 0, nread = 0, nbad = 0, nlate = 0;
    uint64_t last = 0;
    std::map<std::string, long> badby;
    for (const Rec &x : recs) {
        if (x.kind == 'K') { T->eg_k = x.wdata; continue; }
        if (x.kind == 'P') continue;                   // applied by tick()
        if (x.kind == 'O') { model_out[x.clk] = x.off; last = std::max(last, x.clk); continue; }
        if (clocks > x.clk) { nlate++; fprintf(stderr, "request at clock %llu, the RTL is at %llu\n", (unsigned long long)x.clk, (unsigned long long)clocks); return 3; }
        while (clocks < x.clk) tick();
        const bool we = x.kind == 'W' || x.kind == 'w', ram = x.kind == 'w' || x.kind == 'r';
        T->sh_req = 1; T->sh_we = we; T->sh_ram = ram; T->sh_addr = x.off & 0x1FFFFF; T->sh_wdata = x.wdata; T->sh_be = 0xF;
        uint64_t limit = clocks + 4096;
        do tick(); while (!T->sh_ack && clocks < limit);
        T->sh_req = 0;
        nacc++;
        const uint32_t v = T->sh_rdata;
        bool bad = clocks != x.ack;
        if (!we) { nread++; bad |= v != x.rdata; }
        if (bad) {
            nbad++;
            char key[48];
            const uint32_t a = x.off & 0x7FFC;
            if (ram) snprintf(key, sizeof key, "RAM %s", we ? "write" : "read");
            else if (a < 0x2000) snprintf(key, sizeof key, "chan+%02x %s", a & 0x7F, we ? "w" : "r");
            else snprintf(key, sizeof key, "reg %04x %s", a >= 0x4000 && a < 0x4500 ? (a & 0xFF04) | 0x4000 : a, we ? "w" : "r");
            badby[key]++;
            if (nbad <= maxshow)
                printf("MISMATCH %c %06x req %llu: model ack %llu data %08x, rtl ack %llu data %08x\n", (char)x.kind, x.off,
                       (unsigned long long)x.clk, (unsigned long long)x.ack, we ? 0 : x.rdata, (unsigned long long)clocks, we ? 0 : v);
        }
        last = std::max(last, x.ack);
    }
    while (clocks <= last + 1) tick();
    long nout = 0, noutbad = 0, nmiss = 0;
    for (auto &kv : model_out) {
        auto it = rtl_out.find(kv.first);
        if (it == rtl_out.end()) { nmiss++; continue; }
        nout++;
        if (it->second != kv.second) {
            noutbad++;
            if (noutbad <= 10)
                printf("OUT clock %llu: model %d %d rtl %d %d\n", (unsigned long long)kv.first, (int16_t)kv.second,
                       (int16_t)(kv.second >> 16), (int16_t)it->second, (int16_t)(it->second >> 16));
        }
    }
    printf("cosim_cycle %s: %llu clocks, %ld accesses, %ld reads, %ld mismatches, outputs %ld/%ld differ", argv[1],
           (unsigned long long)clocks, nacc, nread, nbad, noutbad, nout);
    if (nmiss) printf(", %ld model outputs without an RTL output", nmiss);
    if (nwarn) printf(", %ld stepping warnings", nwarn);
    printf("\n");
    for (auto &kv : badby) printf("  %-16s %ld\n", kv.first.c_str(), kv.second);
    delete T;
    return nbad || noutbad || nmiss ? 1 : 0;
}
