// vcdq.cpp -- print chosen signals of a VCD, one line per clock (odd time stamps = after the rising edge).
//   vcdq <file.vcd> <sig,sig,...> [t0 t1]     a signal matches by suffix of its hierarchical name
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <sstream>
int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: vcdq <vcd> <sigs> [t0 t1]\n"); return 2; }
    std::vector<std::string> sigs; { std::stringstream ss(argv[2]); std::string s; while (std::getline(ss, s, ',')) sigs.push_back(s); }
    long t0 = argc > 3 ? atol(argv[3]) : 0, t1 = argc > 4 ? atol(argv[4]) : (1L << 62);
    std::map<std::string, std::vector<int>> ids;   // vcd id -> signal indices
    std::vector<std::string> val(sigs.size(), "?");
    std::vector<bool> bound(sigs.size(), false);
    std::ifstream f(argv[1]); std::string line; std::vector<std::string> scope; bool hdr = true; long t = 0;
    auto show = [&]() { printf("%ld", t); for (size_t i = 0; i < sigs.size(); i++) printf(" %s=%s", sigs[i].c_str(), val[i].c_str()); printf("\n"); };
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        if (hdr) {
            std::stringstream ss(line); std::string w; ss >> w;
            if (w == "$scope") { std::string k, n; ss >> k >> n; scope.push_back(n); }
            else if (w == "$upscope") scope.pop_back();
            else if (w == "$var") {
                std::string ty, sz, id, nm; ss >> ty >> sz >> id >> nm;
                std::string full; for (size_t i = 1; i < scope.size(); i++) full += scope[i] + ".";
                full += nm;
                for (size_t i = 0; i < sigs.size(); i++)
                    if (!bound[i] && full.size() >= sigs[i].size() && full.compare(full.size() - sigs[i].size(), sigs[i].size(), sigs[i]) == 0)
                        { ids[id].push_back((int)i); bound[i] = true; }
            } else if (w == "$enddefinitions") hdr = false;
            continue;
        }
        if (line[0] == '#') { long nt = atol(line.c_str() + 1); if (t >= t0 && t <= t1 && (t & 1)) show(); t = nt; continue; }
        std::string v, id;
        if (line[0] == 'b') { size_t sp = line.find(' '); v = line.substr(1, sp - 1); id = line.substr(sp + 1); }
        else { v = line.substr(0, 1); id = line.substr(1); }
        auto it = ids.find(id); if (it == ids.end()) continue;
        std::string hx;
        if (v.find_first_of("xz") != std::string::npos) hx = v;
        else {                                     // any width: hex digits from the bit string
            std::string bits = std::string((4 - v.size() % 4) % 4, '0') + v;
            for (size_t i = 0; i < bits.size(); i += 4) hx += "0123456789abcdef"[std::stoi(bits.substr(i, 4), nullptr, 2)];
            size_t nz = hx.find_first_not_of('0'); hx = nz == std::string::npos ? "0" : hx.substr(nz);
        }
        for (int i : it->second) val[i] = hx;
    }
    return 0;
}
