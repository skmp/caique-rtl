/* io_kos.c -- console back-end of the caique test-case API (cases/aica_io.h): AICA through the G2 bus, output files
 * through dcload /pc/ into tests/<CASE>/hw/.  Built per case with -DCASE=\"name\"; run only through run_hw.sh. */
#include <kos.h>
#include <dc/g2bus.h>
#include "../cases/aica_io.h"

#ifndef HWDIR
#define HWDIR "hw"
#endif
#define OUTDIR "/pc" MODEL_ROOT "/tests/" CASE "/" HWDIR "/"
static int output_error;
const char *io_platform = "hw";

uint32_t io_r(uint32_t off) { return g2_read_32(0xA0700000u + off); }
void io_wn(int n, const uint32_t *off, const uint32_t *v) {
    g2_fifo_wait();
    for (int i = 0; i < n; i++) g2_write_32(0xA0700000u + off[i], v[i]);
}
void io_w(uint32_t off, uint32_t v) {
    g2_fifo_wait();
    g2_write_32(0xA0700000u + off, v);
}
uint32_t io_ram_r32(uint32_t off) { return g2_read_32(0xA0800000u + off); }
void io_ram_w32(uint32_t off, uint32_t v) {
    g2_fifo_wait();
    g2_write_32(0xA0800000u + off, v);
}
uint64_t io_now_us(void) { return timer_us_gettime64(); }
void io_wait_us(uint32_t us) {
    uint64_t t = timer_us_gettime64() + us;
    while (timer_us_gettime64() < t) {}
}
int io_write_file(const char *name, const void *data, uint32_t bytes) {
    char p[256];
    snprintf(p, sizeof p, OUTDIR "%s", name);
    FILE *f = fopen(p, "wb");
    if (!f) { output_error = 1; printf("cannot open %s\n", p); return -1; }
    size_t written = fwrite(data, 1, bytes, f);
    if (written != bytes) output_error = 1;
    if (fclose(f)) output_error = 1;
    return 0;
}
void io_print(const char *s) { printf("%s", s); }
void io_replay_point(uint32_t sync_mdec) { (void)sync_mdec; }
uint32_t io_sh4_irq(void) { return ((*(volatile uint32_t *)0xA05F6904) >> 1) & 1; }   /* SB_ISTEXT bit 1: the AICA */   /* the console is what the models replay */

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    replay_preamble();
    int rc = test_main();
    return rc ? rc : output_error;
}
