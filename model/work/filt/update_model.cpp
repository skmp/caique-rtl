#include <fstream>
#include <sstream>
#include <string>
#include <stdexcept>
int main(){const char*p="src/aica_model.cpp";std::ifstream in(p);std::ostringstream ss;ss<<in.rdbuf();std::string s=ss.str();
auto a=s.find("/* Slot filter (tests/filt_id*");auto b=s.find("/* Volume (tests/sgc_level)",a);if(a==s.npos||b==s.npos)throw std::runtime_error("markers");
s.replace(a,b-a,R"CODE(/* Slot filter: two integrators in 1/8-sample units, but the damping product
 * is rounded UP to 1/4 sample before the cutoff multiply. The band increment
 * rounds down and the low increment rounds up. This intermediate quantization
 * explains the small limit cycles, including period three at f=1, q=1.
 * Verified on filt_cyc, filt_id2, filt_imp and filt_edges (see NOTES.md).
 * FLV bit 0 is unused; only 0x1FFE/0x1FFF use unity instead of 511/512.
 * The inverted OUTPUT saturates to signed 19 bits; the integrator states do not
 * saturate there (verified by the clipped, resonant filt_id2 stream). */
static const uint8_t lpf_q128[32] = {192, 176, 160, 144, 128, 120, 112, 104, 96, 88, 80, 72, 64, 60, 56, 52,
                                     48, 44, 40, 36, 32, 30, 28, 26, 24, 22, 20, 18, 16, 15, 14, 13};
static inline int64_t ceil_shr(int64_t v, int sh) { return -((-v) >> sh); }

int32_t AicaModel::lpf_step(int ch, int32_t x8) {
    Slot &c = slot[ch];
    uint32_t v = c.FEG.v;
    int64_t k = v >= 0x1FFE ? 512 : 256 + ((v >> 1) & 0xFF);
    int sh = 24 - (int)(v >> 9);
    int64_t damping = 2 * ceil_shr((int64_t)lpf_q128[chr(ch, 0x28) & 0x1F] * c.lpf_band, 8);
    int64_t band = c.lpf_band + ((k * ((int64_t)x8 - c.lpf_low - damping)) >> sh);
    int64_t low = c.lpf_low + ceil_shr(k * band, sh);
    c.lpf_band = (int32_t)band;
    c.lpf_low = (int32_t)low;
    return low > 262144 ? -262144 : low < -262143 ? 262143 : (int32_t)-low;
}

)CODE");std::ofstream out(p);out<<s;}
