// arb_tb.cpp -- the ARM7DI port of rtl/v1 against wren7's model of the console bus (wren7-rtl/model/src/dc_arm_map.cpp,
// DcWaits::dreamcast(): every number in it was measured on the console, NOTES "Bus timing", "DSP step clock and wave RAM
// slots", "Contention").  Random ARM access streams (RAM, registers incl. TEMP / EFREG, L / M, SWP pairs, random idle
// gaps) run against a random DSP program (MRD / MWT / TWT / EWT), keyed PCM channels and SH4 wave RAM traffic; every
// ARM cycle must end on the clock the wren7 model predicts and RAM reads must return the shadow's data.
//   arb_tb [accesses] [seed]      exit 0 iff no mismatch
// Time bases: the RTL's DSP step 0 is at ph 64 (aica_pkg DSP_OFFSET), wren7's at 40 MCLK after its sample edge, so
// t_wren7 = t_rtl + 488 (= t_rtl - 24 mod 512).
#include "Vtb_top.h"
#include "verilated.h"
#include "dc_arm_map.h"
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <vector>
#include <deque>

using namespace wren7;
static Vtb_top *T;
static uint64_t now;                     // clocks run with ce = 1
#if VM_TRACE
#include "verilated_vcd_c.h"
static VerilatedVcdC *VCD;
static uint64_t vcd_to;
static void tick() {
    T->clk = 0; T->eval(); if (VCD && now < vcd_to) VCD->dump(now * 2);
    T->clk = 1; T->eval(); if (VCD && now < vcd_to) VCD->dump(now * 2 + 1);
    now++;
}
#else
static void tick() { T->clk = 0; T->eval(); T->clk = 1; T->eval(); now++; }
#endif
static uint32_t seed = 1;
static uint32_t rnd() { seed = seed * 1103515245u + 12345u; return seed >> 8; }
static const uint64_t TW = 488;          // t_wren7 - t_rtl

// frozen test-port write (setup)
static void dbg_write(uint32_t off, uint32_t v) {
    T->ce = 0;
    T->dbg_req = 1; T->dbg_we = 1; T->dbg_ram = 0; T->dbg_addr = off; T->dbg_wdata = v;
    T->clk = 0; T->eval(); T->clk = 1; T->eval();
    T->dbg_req = 0;
    for (int i = 0; i < 2; i++) { T->clk = 0; T->eval(); T->clk = 1; T->eval(); }
    T->ce = 1;
}

int main(int argc, char **argv) {
    Verilated::commandArgs(argc, argv);
    int n = argc > 1 ? atoi(argv[1]) : 20000;
    seed = argc > 2 ? (uint32_t)atoi(argv[2]) : 1;
    const uint32_t seed0 = seed;
    T = new Vtb_top;
#if VM_TRACE
    if (getenv("ARB_VCD")) {                                  // ARB_VCD=file ARB_VCD_TO=clocks
        Verilated::traceEverOn(true); VCD = new VerilatedVcdC; T->trace(VCD, 99); VCD->open(getenv("ARB_VCD"));
        vcd_to = getenv("ARB_VCD_TO") ? atoll(getenv("ARB_VCD_TO")) : 8192;
    }
#endif
    T->ce = 0; T->eg_k = 0; T->clk = 1; T->eval();
    DcArmBus bus;
    bus.waits = DcWaits::dreamcast();
    bus.sh4_slot_spacing = 8;
    auto both = [&](uint32_t off, uint16_t v) { dbg_write(off, v); bus.reg_write(off, v); };

    // DSP: ring at 1 MB (away from the ARM's first 64 KB), a random program
    both(0x2804, 0x0200);
    int nmem = 0, ntwt = 0, newt = 0;
    const int pmem = rnd() % 40, ptwt = rnd() % 50, pewt = rnd() % 30;
    for (int st = 0; st < 128; st++) {
        uint16_t w0 = rnd(), w1 = rnd(), w2 = rnd(), w3 = rnd();
        w0 &= ~0x100; w2 &= ~0x7000;
        if ((int)(rnd() % 100) < ptwt) { w0 |= 0x100; ntwt++; }
        if ((int)(rnd() % 100) < pewt) { w2 |= 0x1000; newt++; }
        if ((int)(rnd() % 100) < pmem) { w2 |= (rnd() & 1) ? 0x4000 : 0x2000; nmem++; }
        both(0x3400 + 16 * st + 0, w0); both(0x3400 + 16 * st + 4, w1);
        both(0x3400 + 16 * st + 8, w2); both(0x3400 + 16 * st + 12, w3);
    }
    // channels: looping PCM16 / PCM8 at a random pitch, instant attack, no decay (they play for ever)
    int nch = 0;
    const int pch = rnd() % 60;
    uint16_t r0s[64] = {0};
    for (int k = 0; k < 64; k++) {
        if ((int)(rnd() % 100) >= pch) continue;
        nch++;
        uint32_t b = 0x80 * k;
        r0s[k] = 0x4000 | 0x0200 | ((rnd() & 1) << 7) | 0x18;      // KYONB, LPCTL, PCM16 / PCM8, SA 0x180000
        both(b + 0x04, 0); both(b + 0x08, 0); both(b + 0x0C, 0x100);
        both(b + 0x10, 0x001F); both(b + 0x14, 0x000F);
        both(b + 0x18, (uint16_t)(((rnd() % 10 - 2) & 15) << 11 | (rnd() & 0x3FF)));
        both(b + 0x00, r0s[k]);
    }
    if (nch) dbg_write(0x00, r0s[0] | 0x8000);                    // KYONEX
    printf("arb_tb seed %u: DSP %d MRD/MWT %d TWT %d EWT steps, %d channels:", seed0, nmem, ntwt, newt, nch);
    for (int k = 0; k < 64; k++) if (r0s[k]) printf(" %d", k);
    printf("\n");

    // warm up 8 samples (key-on, the fixed pair's look-ahead)
    for (int i = 0; i < 8 * 512; i++) tick();

    std::vector<uint32_t> ram(1 << 14, 0);                    // the ARM's first 64 KB (words)
    int bad = 0, done = 0, nlocal = 0, nlock = 0, nreg = 0, nram = 0, nsh = 0;
    // SH4 wave RAM traffic (arrivals >= 48 clocks apart; reads take two slots): tell wren7 up front
    struct Sh { uint64_t t; bool we; uint32_t addr; };
    std::deque<Sh> sh;
    {
        const int psh = rnd() % 3;                                // 0: none, 1: sparse, 2: dense
        uint64_t t = now + 100;
        for (int i = 0; psh && i < n / (psh == 1 ? 8 : 3); i++) {
            t += 48 + rnd() % (psh == 1 ? 400 : 60);
            Sh s{t, (rnd() & 1) != 0, 0x20000 + 4 * (rnd() % 4096)};
            sh.push_back(s);
            bus.sh4_access(t + TW, s.we ? 1 : 2);
        }
        nsh = (int)sh.size();
    }
    bool sh_active = false;

    uint64_t t_req = now + 3;
    int k = 0;
    while (k < n) {
        // the next ARM access
        int kind = rnd() % 20;           // 0-9 RAM, 10-12 register, 13-14 TEMP/EFREG, 15 L/M, 16-17 SWP, 18-19 common reg
        bool local = kind == 15, lock = kind == 16 || kind == 17;
        bool ram_acc = kind <= 9 || lock;
        uint32_t widx = rnd() % 16384;
        uint32_t addr;
        if (ram_acc) addr = 4 * widx;
        else if (local) addr = 0x2D00 + 4 * (rnd() & 1);
        else if (kind == 13) addr = 0x4000 + 4 * (rnd() % 256);
        else if (kind == 14) addr = 0x4580 + 4 * (rnd() % 16);
        else addr = 0x2000 + 4 * (rnd() % 16);
        bool we0 = (rnd() & 1) != 0;
        uint32_t wdata = rnd() ^ (rnd() << 16);
        for (int part = 0; part < (lock ? 2 : 1); part++) {
            bool we = lock ? part == 1 : we0;
            if (local && !we && (addr & 4)) we = true;               // M is write-only
            BusCycle c{t_req + TW, (ram_acc ? addr : 0x800000 | addr), wdata, CYC_N,
                       (uint8_t)((we ? BF_WRITE : 0) | (lock ? BF_LOCK : 0))};
            BusResult r{};
            bus.cycle(c, r);
            uint64_t expect = t_req + r.wait + 1;
            // drive the RTL until the ack
            uint64_t end = 0; uint32_t rdata = 0;
            while (!end) {
                // SH4 master
                if (!sh_active && !sh.empty() && now >= sh.front().t) sh_active = true;
                T->sh_req = sh_active; 
                if (sh_active) { T->sh_we = sh.front().we; T->sh_ram = 1; T->sh_addr = sh.front().addr; T->sh_wdata = 0x5A5A0000 | (uint32_t)now; T->sh_be = 0xF; }
                // ARM master
                bool req = now >= t_req;
                T->arm_req = req; T->arm_we = we; T->arm_ram = ram_acc; T->arm_lock = lock; T->arm_addr = addr;
                T->arm_wdata = wdata; T->arm_be = 0xF;
                T->eval();
                bool ack = req && T->arm_ack;
                if (ack) rdata = T->arm_rdata;
                bool shack = T->sh_ack;
                tick();
                if (shack && sh_active) { sh_active = false; sh.pop_front(); }
                if (ack) end = now;
                if (now > t_req + 200) { printf("no ack: k %d\n", k); return 3; }
            }
            T->arm_req = 0;
            if (end != expect) {
                if (bad < 20) printf("TIMING k %d %s%s%s @%04x: request %llu (ph %llu), done %llu, wren7 %llu\n", k,
                    local ? "L/M " : "", lock ? "SWP " : "", we ? "write" : "read", addr,
                    (unsigned long long)t_req, (unsigned long long)(t_req % 512), (unsigned long long)end, (unsigned long long)expect);
                bad++;
            }
            if (ram_acc) {
                if (!we && rdata != ram[widx]) { if (bad < 20) printf("DATA k %d: %08x expected %08x\n", k, rdata, ram[widx]); bad++; }
                if (we) ram[widx] = wdata;
                nram++;
            } else if (!local) nreg++;
            if (local) nlocal++;
            if (lock) nlock++;
            done++;
            t_req = end + ((rnd() % 4 == 0) ? rnd() % 9 : 0);   // I cycles before the next
        }
        k++;
    }
    printf("arb_tb: %d ARM cycles (%d RAM, %d register, %d L/M, %d SWP halves), %d SH4 accesses, %d mismatches\n",
        done, nram, nreg, nlocal, nlock, nsh, bad);
#if VM_TRACE
    if (VCD) VCD->close();
#endif
    delete T;
    return bad ? 1 : 0;
}
