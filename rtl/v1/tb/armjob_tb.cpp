// armjob_tb.cpp -- wren7's ARM7DI model (Arm7DI::bus_cycle, one bus cycle per call) running the console's job files
// (wren7-rtl/model/tests/hw/<suite>/jobs.txt) with rtl/v1 as its bus: every memory cycle is driven into the RTL's ARM
// port and its wait is the RTL's.  A shadow DcArmBus (wren7's measured bus model) sees the same cycles and gives, per
// cycle, the wait the model predicts; it also stands in for the interrupt controller (v2 in the RTL: SCIEB .. MCIRE,
// timers, L / M data and nFIQ).  Per job: the RTL run's cycles and readback against wren7's own run (hwjob.h
// run_job_model with DcWaits::dreamcast()), and every cycle's wait against the shadow's.
//   (cd wren7-rtl/model && armjob_tb tests/hw/SUITE/jobs.txt [-j FIRST[:LAST]] [-p NPHASE] [-v])   exit 0 iff all match
// -p: jobs with SH4 traffic run at NPHASE starting phases of the SH4 stream (as hw_suite check_timed averages them);
// every phase is compared exactly and the printed cycles are the phase average.
// -w DIR: write DIR/NAME.bin as the console runner does (read regions, then 5 ms later the rreg words ~1 sample
// apart, the ARM in reset), for wren7's functional checks (hw_suite check collide2 DIR).
// Time: t_rtl = t_wren7 + OFF, OFF = 24 + 512 * WARM (the RTL's DSP step 0 is at ph 64, wren7's 40 after its edge),
// after WARM samples of warm-up (key-on, the fixed pair's look-ahead).
#include "Vtb_top.h"
#include "verilated.h"
#include "hwjob.h"
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <deque>
#include <algorithm>
#include <cstring>
#include <string>

using namespace wren7;

static const int WARM = 8;
static const uint64_t OFF = 24 + 512 * WARM;
// wren7's ADPCM position origin (DcArmBus::adpcm_origin, SGC samples from the ARM's reset release) that matches the
// RTL: the job's KYONEX lands before the RTL's first clock (sample 0), whose envelope pass keys the channel on; sample
// 1 outputs CA 0 without advancing, sample 2 is the first advance; wren7's SGC sample 0 is RTL sample WARM.  So the
// position is 0 at wren7 sample 2 - 1 - WARM (-7; checked: only -7 and -3 match at OCT 0, only -7 at OCT -1 / -2).
static int64_t adpcm_origin = 1 - WARM;
static uint32_t dbg(Vtb_top *T, bool we, bool ram, uint32_t addr, uint32_t v);   // frozen test-port access
static int watch_slot = -1;             // ARMJOB_WATCH=slot: report when the slot's monitor turns off (debug)
static uint32_t trace_lo = 1, trace_hi = 0;   // ARMJOB_TRACE=lo:hi (ARM addresses): print those cycles (debug)
static uint32_t watch_last = 0;

struct RtlBus : Arm7Bus {
    Vtb_top *T;
    DcArmBus shadow;
    uint64_t now = 0;               // RTL clocks run (engine running)
    long cyc = 0, mem = 0, waitbad = 0, bytereg = 0;
    bool verbose = false;
    std::string job;
    // SH4 master (the job's sh4load stream, as the shadow's generator places it)
    int sh_mode = 0;                // 1 reads (two slots, next issue after last + 8 + latency), 2 writes
    double sh_next = -1, sh_period = 0, sh_lat = 0;
    std::deque<uint64_t> sh_q;      // pending arrivals (wren7 time)
    bool sh_active = false;
    double sh_issue = 0;               // the last read's issue time (untruncated, as the generator keeps it)

    explicit RtlBus(Vtb_top *t) : T(t) {}
    void clock() {
        // SH4 arrivals up to this clock (wren7 time now - OFF)
        if (sh_mode && now >= OFF) {
            const double tw = (double)(now - OFF);
            // an arrival counts from its truncated time, as DcArmBus::sh4_advance issues it
            if (sh_mode == 2) while ((double)(uint64_t)sh_next <= tw) { sh_q.push_back((uint64_t)sh_next); sh_next += sh_period; }
            else if (sh_next >= 0 && (double)(uint64_t)sh_next <= tw) {
                sh_q.push_back((uint64_t)sh_next); sh_issue = sh_next; sh_next = -1;
            }
        }
        if (!sh_active && !sh_q.empty() && now >= sh_q.front() + OFF) sh_active = true;
        T->sh_req = sh_active; T->sh_we = sh_mode == 2; T->sh_ram = 1; T->sh_addr = 0x1F0000; T->sh_wdata = 0; T->sh_be = 0xF;
        T->eval();
        const bool shack = T->sh_ack;
        T->clk = 0; T->eval(); T->clk = 1; T->eval(); now++;
        if (watch_slot >= 0 && T->ph == 0 && T->arm_req == 0) {
            const uint32_t eg = dbg(T, false, false, 0x2810, 0) & 0x1FFF;
            if (eg == 0x1FFF && watch_last != 0x1FFF) {
                printf("  WATCH slot %d off at rtl clock %llu (sample %llu), CA %x; regs", watch_slot, (unsigned long long)now,
                    (unsigned long long)(now / 512), dbg(T, false, false, 0x2814, 0));
                for (int r = 0; r < 0x48; r += 4) printf(" %04x", dbg(T, false, false, 0x80 * watch_slot + r, 0));
                printf("\n");
            }
            watch_last = eg;
        }
        if (shack && sh_active) {
            sh_active = false; sh_q.pop_front();
            if (sh_mode == 1) {           // sh_ack is high the clock after the second slot's t3: last = that clock - 4
                const double last = (double)(now - 1 - 4 - OFF);
                sh_next = std::max(last + 8 + sh_lat, sh_issue + sh_period);
            }
        }
    }
    void tick(uint64_t t) override { shadow.tick(t); }
    // one SH4-port access with the engine running (the runner's readout after a job: the ARM is in reset); the frozen
    // test port is only for the sample boundary, where the co-simulation uses it
    uint32_t sh4(bool we, bool ram, uint32_t addr, uint32_t v) {
        T->arm_req = 0;
        uint32_t d = 0;
        for (int guard = 0; guard < 4096; guard++) {
            T->sh_req = 1; T->sh_we = we; T->sh_ram = ram; T->sh_addr = addr; T->sh_wdata = v; T->sh_be = 0xF;
            T->eval();
            const bool ack = T->sh_ack;
            if (ack) d = T->sh_rdata;
            T->clk = 0; T->eval(); T->clk = 1; T->eval(); now++;
            if (ack) { T->sh_req = 0; T->eval(); return d; }
        }
        fprintf(stderr, "%s: no SH4 ack at %06x\n", job.c_str(), addr); exit(3);
    }
    static bool irq_reg(uint32_t off) { return (off >= 0x2890 && off < 0x28C0) || (off & 0x7FF8) == 0x2D00; }
    void cycle(const BusCycle &c, BusResult &r) override {
        BusResult rs{};
        shadow.cycle(c, rs);                               // the model's wait; interrupt side effects
        cyc++;
        r.abort = false; r.rdata = 0; r.wait = 0;
        if (c.type == CYC_I || c.type == CYC_C) return;
        mem++;
        const uint64_t start = c.t + OFF;
        if (now > start) { fprintf(stderr, "%s: time went backwards (rtl %llu, cycle %llu)\n", job.c_str(), (unsigned long long)now, (unsigned long long)start); exit(3); }
        while (now < start) { T->arm_req = 0; clock(); }
        const uint32_t a = c.addr & 0x00FFFFFF;
        const bool wr = c.flags & BF_WRITE, byte = c.flags & BF_BYTE, ram = a < 0x800000;
        const uint32_t off = a & 0x7FFF;
        if (!ram && wr && byte) bytereg++;
        T->arm_we = wr; T->arm_ram = ram; T->arm_lock = (c.flags & BF_LOCK) != 0;
        T->arm_addr = ram ? (a & 0x1FFFFC) : (off & 0x7FFC);
        T->arm_wdata = c.wdata; T->arm_be = byte ? (1u << (a & 3)) : 0xF;
        uint32_t rd = 0; uint64_t end = 0;
        for (;;) {
            T->arm_req = 1;
            T->eval();
            if (T->arm_ack) { rd = T->arm_rdata; end = now; }
            clock();
            if (end || now > start + 400) break;
        }
        T->arm_req = 0;
        if (!end) { fprintf(stderr, "%s: no ack at t %llu addr %06x\n", job.c_str(), (unsigned long long)c.t, a); exit(3); }
        r.wait = (uint32_t)(end - start);
        r.rdata = (!ram && irq_reg(off)) ? rs.rdata : rd;
        if (a >= trace_lo && a <= trace_hi)
            printf("  TRACE t %llu ph %llu %s %06x wdata %08x rtl %08x model %08x wait %u\n", (unsigned long long)c.t,
                (unsigned long long)(start % 512), wr ? "W" : "R", a, c.wdata, rd, rs.rdata, r.wait);
        if (r.wait != rs.wait) {
            if (verbose || waitbad < 5)
                printf("  %s WAIT t %llu (ph %llu) %s%s%s %06x: rtl %u model %u\n", job.c_str(), (unsigned long long)c.t,
                    (unsigned long long)(start % 512), c.type == CYC_N ? "N " : "S ", wr ? "W" : "R", byte ? "B" : "",
                    a, r.wait, rs.wait);
            waitbad++;
        }
    }
};

// frozen test-port access
static uint32_t dbg(Vtb_top *T, bool we, bool ram, uint32_t addr, uint32_t v) {
    T->ce = 0;
    T->dbg_req = 1; T->dbg_we = we; T->dbg_ram = ram; T->dbg_addr = addr; T->dbg_wdata = v;
    T->clk = 0; T->eval(); T->clk = 1; T->eval();
    T->dbg_req = 0;
    uint32_t d = 0;
    for (int i = 0; i < 3; i++) { T->clk = 0; T->eval(); T->clk = 1; T->eval(); if (T->dbg_ack) d = T->dbg_rdata; }
    T->ce = 1;
    return d;
}

int main(int argc, char **argv) {
    Verilated::commandArgs(argc, argv);
    if (argc < 2) { fprintf(stderr, "usage: armjob_tb JOBS [-j FIRST[:LAST]] [-v]\n"); return 2; }
    int first = 0, last = 1 << 30, nphase = 1; bool verbose = false;
    std::string wdir;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "-v")) verbose = true;
        else if (!strcmp(argv[i], "-p") && i + 1 < argc) nphase = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-o") && i + 1 < argc) adpcm_origin = atoll(argv[++i]);
        else if (!strcmp(argv[i], "-w") && i + 1 < argc) wdir = argv[++i];
        else if (!strcmp(argv[i], "-j") && i + 1 < argc) {
            const char *s = argv[++i]; first = atoi(s);
            const char *c = strchr(s, ':'); last = c ? atoi(c + 1) : first;
        }
    }
    if (getenv("ARMJOB_WATCH")) watch_slot = atoi(getenv("ARMJOB_WATCH"));
    if (getenv("ARMJOB_TRACE")) sscanf(getenv("ARMJOB_TRACE"), "%x:%x", &trace_lo, &trace_hi);
    HwJobFile jf;
    if (!parse_jobs(argv[1], jf)) return 2;
    int nbad = 0, njobs = 0;
    long tot_cyc = 0, tot_mem = 0, tot_wait = 0;
    for (int ji = first; ji < (int)jf.jobs.size() && ji <= last; ji++) {
        const HwJob &j = jf.jobs[ji];
        const int np = (j.sh4mode == 1 || j.sh4mode == 2) ? nphase : 1;
        double sum_rtl = 0, sum_ref = 0;
        bool job_ok = true, all_done = true, all_rb = true;
        long jcyc = 0, jmem = 0, jwait = 0, jbyte = 0;
        for (int ph = 0; ph < np; ph++) {
        const double phase = (double)ph / np;
        // wren7's own run
        ModelRun ref = run_job_model(j, DcWaits::dreamcast(), 400000000ull, nullptr, -1, phase, adpcm_origin);
        // the RTL run
        Vtb_top *T = new Vtb_top;
        T->ce = 0; T->eg_k = 0; T->clk = 1; T->arm_req = 0; T->sh_req = 0; T->dbg_req = 0; T->eval();
        RtlBus bus(T);
        bus.job = j.name; bus.verbose = verbose;
        bus.shadow.waits = DcWaits::dreamcast();
        bus.shadow.adpcm_origin = adpcm_origin;
        sh4_setup(bus.shadow, j, phase);
        if (j.sh4mode == 1 || j.sh4mode == 2) {
            bus.sh_mode = j.sh4mode == 1 ? 1 : 2;
            bus.sh_period = bus.shadow.sh4_period; bus.sh_lat = bus.shadow.sh4_read_latency;
            bus.sh_next = bus.shadow.sh4_phase;
        }
        std::vector<uint8_t> img;
        for (auto &l : j.loads) {
            if (!read_all(l.second, img)) { fprintf(stderr, "cannot read %s\n", l.second.c_str()); return 2; }
            bus.shadow.load(l.first, img.data(), img.size());
            img.resize((img.size() + 3) & ~(size_t)3, 0);
            for (size_t i = 0; i < img.size(); i += 4)
                dbg(T, true, true, (l.first + i) & 0x1FFFFC, img[i] | img[i + 1] << 8 | img[i + 2] << 16 | (uint32_t)img[i + 3] << 24);
        }
        for (auto &p : j.words) { bus.shadow.wr32(p.first, p.second); dbg(T, true, true, p.first & 0x1FFFFC, p.second); }
        for (auto &p : j.aregs) { bus.shadow.reg_write(p.first, (uint16_t)p.second); dbg(T, true, false, p.first & 0x7FFC, p.second); }
        while (bus.now < OFF) { T->arm_req = 0; bus.clock(); }
        Arm7DI cpu(&bus);                                   // power-on + reset release: cycles from t = 0
        bus.shadow.attach(&cpu);
        const uint64_t max_cycles = 400000000ull;
        while (!cpu.at_boundary() || (!bus.shadow.scpu_raised && cpu.cycles < max_cycles)) cpu.bus_cycle();
        const bool done = bus.shadow.scpu_raised;
        const uint64_t cycles = done ? bus.shadow.scpu_t : cpu.cycles;
        // readback from the RTL's RAM, through the SH4 port (the ARM stopped, the engine running)
        std::vector<uint8_t> rb;
        for (auto &rd : j.reads)
            for (uint32_t i = 0; i < rd.second; i += 4) {
                uint32_t w = bus.sh4(false, true, (rd.first + i) & 0x1FFFFC, 0);
                for (uint32_t b = 0; b < 4 && i + b < rd.second; b++) rb.push_back(w >> (8 * b));
            }
        const bool same_rb = rb == ref.readback;
        if (!wdir.empty() && ph == 0 && (!j.reads.empty() || !j.rregs.empty())) {
            std::vector<uint8_t> out = rb;
            if (!j.rregs.empty()) for (uint64_t i = 0; i < 112896; i++) { T->arm_req = 0; bus.clock(); }   // 5 ms
            for (auto &g : j.rregs)
                for (uint32_t k = 0; k < g.second; k++) {
                    const uint64_t t0 = bus.now;
                    const uint32_t v = bus.sh4(false, false, g.first & 0x7FFC, 0);
                    for (int b = 0; b < 4; b++) out.push_back(v >> (8 * b));
                    while (bus.now < t0 + 521) { T->arm_req = 0; bus.clock(); }                          // 4600 SH4 cycles
                }
            FILE *f = fopen((wdir + "/" + j.name + ".bin").c_str(), "wb");
            if (f) { fwrite(out.data(), 1, out.size(), f); fclose(f); }
        }
        const bool ok = done == ref.done && cycles == ref.cycles && bus.waitbad == 0;
        if (!ok && np > 1) printf("  %s phase %d/%d: rtl %llu model %llu cycles, %ld wait mismatches\n", j.name.c_str(), ph, np,
            (unsigned long long)cycles, (unsigned long long)ref.cycles, bus.waitbad);
        job_ok = job_ok && ok; all_done = all_done && done; all_rb = all_rb && same_rb;
        sum_rtl += (double)cycles; sum_ref += (double)ref.cycles;
        jcyc += bus.cyc; jmem += bus.mem; jwait += bus.waitbad; jbyte += bus.bytereg;
        delete T;
        }
        printf("%-24s %s %s rtl %.1f model %.1f cycles, %ld bus cycles (%ld memory), %ld wait mismatches, readback %s%s\n",
            j.name.c_str(), job_ok ? "OK  " : "DIFF", all_done ? "done" : "TIMEOUT", sum_rtl / np, sum_ref / np, jcyc, jmem,
            jwait, j.reads.empty() ? "-" : all_rb ? "same" : "DIFFERS",
            jbyte ? (" (" + std::to_string(jbyte) + " byte register writes)").c_str() : "");
        fflush(stdout);
        if (!job_ok) nbad++;
        njobs++; tot_cyc += jcyc; tot_mem += jmem; tot_wait += jwait;
    }
    printf("armjob_tb %s: %d jobs, %d differ; %ld bus cycles, %ld memory cycles, %ld wait mismatches\n", argv[1], njobs, nbad,
        tot_cyc, tot_mem, tot_wait);
    return nbad ? 1 : 0;
}
