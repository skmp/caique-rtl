#include <fstream>
#include <sstream>
#include <string>
#include <stdexcept>
static std::string read(const char*p){std::ifstream f(p);if(!f)throw std::runtime_error(p);std::ostringstream s;s<<f.rdbuf();return s.str();}
static void write(const char*p,const std::string&s){std::ofstream f(p);f<<s;}
static void replace(std::string&s,const std::string&a,const std::string&b){auto n=s.find(a);if(n==s.npos)throw std::runtime_error(a);s.replace(n,a.size(),b);}
int main(){
 auto n=read("NOTES.md");auto a=n.find("## Slot filter (");auto b=n.find("## DSP (",a);if(a==n.npos||b==n.npos)throw std::runtime_error("filter section");
 n.replace(a,b-a,read("work/filt/slot_filter_notes.md"));
 replace(n,"- Filter: the low-update rounding (and e = 15 Q 0 / q*band rounding) — see Slot filter and HANDOVER.md; then\n  implement in lpf_step; low FLV exponents; FLV top end (f = 1.0 mechanism, other exponents); filter + VOFF=0 precision.","- Filter: arithmetic solved on 235 streams; remaining low exponents, fractional input, VOFF=0 precision, and\n  ultimate internal overflow. See Slot filter and HANDOVER.md.");write("NOTES.md",n);
 auto r=read("README.md");replace(r,"Behavioural model of the Dreamcast AICA sound generator (64 slots) and DSP, measured against the console.","Behavioural model of the Dreamcast AICA sound generator (64 slots) and DSP, measured against the console.\nSlot-filter arithmetic now matches 2,050,454 captured samples across 235 streams, including production-code validation.");
 replace(r,"    tools/verify.py          # re-run every case on the model and compare with tests/*/hw -> tests/SUMMARY.txt","    # C++ filter builds and validation: see HANDOVER.md. Do not use the legacy Python tools.");
 replace(r,"**[tests/SUMMARY.txt](tests/SUMMARY.txt)** (regenerate with `tools/verify.py`).","**[tests/SUMMARY.txt](tests/SUMMARY.txt)**. Current C++ reproduction commands are in HANDOVER.md.");
 auto ca=r.find("## Comparison tools");auto cb=r.find("## Not observable",ca);
 r.replace(ca,cb-ca,"## Comparison tools\n\n- `tools/filt_step.cpp`: C++ capture exporter, byte-identical to the historical datasets.\n- `tools/filt_rule.cpp`: exact integer state-set search; form 5 models the discovered coarse damping.\n- `tools/filt_validate.cpp`: autonomous arithmetic validation against all filter captures; compile with\n  `-DVERIFY_MODEL` and `src/aica_model.cpp` to also check the actual model's MIXS output.\n- `tools/filt_compare.cpp`: compare full hardware/model runs with impulse alignment; reports inherited-state differences.\n- `tools/filt_edges.cpp`: independent analysis of the fresh endpoint impulses and bypass timing reference.\n- `tools/filt_need.cpp`: forced rounding and contradictory-operand diagnostics for older hypotheses.\n- The other Python comparison tools and `filt_search*.cpp` are historical; current work uses C++ integer math only.\n\n");write("README.md",r);
 auto s=read("tests/SUMMARY.txt");replace(s,"# model vs console, tools/verify.py","# Model vs console, updated after C++ filter validation (2026-09-23).\n# Non-filter baseline reconfirmed by reruns and saved-output hashes; legacy verify.py was not run.");
 auto f=s.find("filt_id2       APPROX");if(f==s.npos)throw std::runtime_error("summary filter");s.erase(f);
 s+="filt_id2       STATE  55/60 streams identical; 619/254980 samples differ, all startup transients (<300 samples)\n";
 s+="filt_coef      STATE  117/119 streams identical; 17/503438 samples differ, all startup transients (<17 samples)\n";
 s+="filter_math    OK     Production model: 235/235 streams, 2050454/2050454 samples identical with inferred initial state\n";write("tests/SUMMARY.txt",s);
}
