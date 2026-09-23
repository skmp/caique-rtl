/* io_model.cpp -- model back-end of the caique test-case API (cases/aica_io.h): drives caique::AicaModel, output
 * files into tests/<CASE>/model/.  Time: every register/RAM access costs ACCESS_NS (the measured G2 read cost),
 * io_wait_us adds its argument; the model runs every sample whose start time has passed. */
#include "../cases/aica_io.h"
#include "../src/aica_model.h"
#include <stdlib.h>

static caique::AicaModel *M;
extern "C" {
const char *io_platform = "model";
static const char *g_outdir;

static uint64_t g_ns; /* model time */
static const uint64_t ACCESS_NS = 2400;
static void advance(uint64_t ns) {
    g_ns += ns;
    uint64_t target = (g_ns * 44100) / 1000000000ull;
    while (M->samples < target) M->step();
}
uint32_t io_r(uint32_t off) { advance(ACCESS_NS); return M->read(off); }
void io_w(uint32_t off, uint32_t v) { advance(ACCESS_NS); M->write(off, v); }
static void check_align(uint32_t off) {
    if (off & 3) { fprintf(stderr, "misaligned 32-bit wave RAM access at %08x (the console takes an address error)\n", off); exit(3); }
}
uint32_t io_ram_r32(uint32_t off) {
    check_align(off);
    advance(ACCESS_NS);
    return M->ram_read32(off);
}
void io_ram_w32(uint32_t off, uint32_t v) {
    check_align(off);
    advance(ACCESS_NS);
    off &= caique::AicaModel::RAM_SIZE - 4;
    for (int i = 0; i < 4; i++) M->ram[off + i] = (uint8_t)(v >> (8 * i));
}
uint64_t io_now_us(void) { advance(200); return g_ns / 1000; } /* a timer read costs time too */
void io_wait_us(uint32_t us) { advance((uint64_t)us * 1000); }
int io_write_file(const char *name, const void *data, uint32_t bytes) {
    char p[512];
    snprintf(p, sizeof p, "%s/%s", g_outdir, name);
    FILE *f = fopen(p, "wb");
    if (!f) { fprintf(stderr, "cannot open %s\n", p); return -1; }
    fwrite(data, 1, bytes, f);
    fclose(f);
    return 0;
}
void io_print(const char *s) { fputs(s, stdout); }
}

int main(int argc, char **argv) {
    g_outdir = argc > 1 ? argv[1] : ".";
    M = new caique::AicaModel();
    int rc = test_main();
    delete M;
    return rc;
}
