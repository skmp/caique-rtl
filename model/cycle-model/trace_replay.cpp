// trace_replay.cpp -- the cycle model against the sample model (src/): replays a case's access trace, recorded by the
// sample model's host harness (host/io_model.cpp, CAIQUE_TRACE, CAIQUE_TRACE_OUT=1), into the cycle model with the
// boundary contract of rtl/v1's first co-simulation: an access between the sample model's steps p-1 and p is delivered
// at the cycle model's sample p, between two clocks (backdoor), at
//   ph 0:  channel / common registers, MIXS, KYONEX, monitors, wave RAM writes, and wave RAM reads that a later write of
//          the same gap overwrites;
//   ph 64: DSP buffers (0x2804, 0x3000-0x3FFF, 0x4000-0x44FF, 0x4580-0x45FF) and the other wave RAM reads.
// Every read and every output sample is compared (the output of sample q - 1 is produced at ph 82 of sample q).
//   trace_replay <trace> [-max n]      exit 0 iff everything matches
#include "aica_model.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <map>
#include <string>
#include <algorithm>

struct Rec { uint32_t gap, kind, off, val; };

static bool dsp_class(uint32_t off) {
    off &= 0x7FFC;
    return off == 0x2804 || (off >= 0x3000 && off < 0x4500) || (off >= 0x4580 && off < 0x4600);
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: trace_replay <trace> [-max n]\n"); return 2; }
    long maxshow = 20;
    for (int i = 2; i < argc; i++)
        if (!strcmp(argv[i], "-max") && i + 1 < argc) maxshow = atol(argv[++i]);
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 2; }
    std::vector<Rec> recs;
    Rec r;
    while (fread(&r, 4, 4, f) == 4) recs.push_back(r);
    fclose(f);
    uint32_t K = 6491, last_gap = 0;
    std::vector<std::vector<size_t>> early, late;
    std::map<uint32_t, std::pair<int16_t, int16_t>> outs;
    for (size_t i = 0; i < recs.size(); i++) {
        const Rec &x = recs[i];
        if (x.kind == 'K') { K = x.val; continue; }
        if (x.kind == 'O') { outs[x.gap] = {(int16_t)(x.off & 0xFFFF), (int16_t)(x.off >> 16)}; continue; }
        if (x.gap + 1 > early.size()) { early.resize(x.gap + 1); late.resize(x.gap + 1); }
        last_gap = std::max(last_gap, x.gap);
        bool is_early;
        if (x.kind == 'w') is_early = true;
        else if (x.kind == 'r') {
            is_early = false;
            for (size_t j = i + 1; j < recs.size() && recs[j].gap == x.gap; j++)
                if (recs[j].kind == 'w' && (recs[j].off & ~3u) == (x.off & ~3u)) { is_early = true; break; }
        } else is_early = !dsp_class(x.off);
        (is_early ? early : late)[x.gap].push_back(i);
    }
    caique::AicaModel *M = new caique::AicaModel();
    M->eg_K = K;
    long nread = 0, nbad = 0, nout = 0, noutbad = 0;
    std::map<std::string, long> badby;
    auto run_list = [&](const std::vector<size_t> &lst) {
        for (size_t idx : lst) {
            const Rec &x = recs[idx];
            uint32_t v = 0;
            bool rd = false;
            switch (x.kind) {
            case 'W': M->write(x.off, x.val); break;
            case 'w': M->ram_write32(x.off, x.val); break;
            case 'R': v = M->read(x.off); rd = true; break;
            case 'r': v = M->ram_read32(x.off); rd = true; break;
            }
            if (!rd) continue;
            nread++;
            if (v != x.val) {
                nbad++;
                char key[48];
                const uint32_t a = x.off & 0x7FFC;
                if (x.kind == 'r') snprintf(key, sizeof key, "RAM");
                else if (a < 0x2000) snprintf(key, sizeof key, "chan+%02x", a & 0x7F);
                else snprintf(key, sizeof key, "reg %04x", a >= 0x4000 && a < 0x4500 ? (a & 0xFF04) | 0x4000 : a);
                badby[key]++;
                if (nbad <= maxshow)
                    printf("MISMATCH gap %u %s %06x: sample model %08x cycle model %08x\n", x.gap, x.kind == 'r' ? "RAM" : "reg",
                           x.off, x.val, v);
            }
        }
    };
    static const std::vector<size_t> none;
    const uint32_t end_gap = last_gap + 2;
    for (uint32_t q = 0; q <= end_gap; q++) {
        run_list(q < early.size() ? early[q] : none);
        M->run(caique::AicaModel::DSP_OFFSET);
        run_list(q < late.size() ? late[q] : none);
        const uint64_t o0 = M->outs;
        M->step();
        if (M->outs != o0 + 1) { fprintf(stderr, "no output in sample %u\n", q); return 3; }
        if (q >= 1) {
            auto it = outs.find(q - 1);
            if (it != outs.end()) {
                nout++;
                if (it->second.first != M->outL || it->second.second != M->outR) {
                    noutbad++;
                    if (noutbad <= 10)
                        printf("OUT sample %u: sample model %d %d cycle model %d %d\n", q - 1, it->second.first,
                               it->second.second, M->outL, M->outR);
                }
            }
        }
    }
    printf("trace_replay %s: %u samples, %zu accesses, %ld reads, %ld mismatches, outputs %ld/%ld differ\n", argv[1],
           end_gap + 1, recs.size(), nread, nbad, noutbad, nout);
    for (auto &kv : badby) printf("  %-12s %ld\n", kv.first.c_str(), kv.second);
    delete M;
    return nbad || noutbad ? 1 : 0;
}
