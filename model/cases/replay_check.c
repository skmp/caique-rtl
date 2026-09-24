/* replay_check.c -- the replay parameters (TODO 3.3) checked: after the preamble (cases/common/replay.c, sync point 1)
 * the same measurement runs again (sync point 2).  tools/replay_fit on both gives two "mdec X lfsr L K k" lines; on the
 * console they show K constant and the LFSR stepping 64 times per sample in between (replay_fit -same, with the elapsed
 * SH4 time); a model run with CAIQUE_REPLAY=<the console's replay.txt> must give the console's second line (up to the
 * number of MDEC_CT wraps in between, which the two platforms' timing may differ by).
 * Output: check_log.txt, check.hdr / check.bin (as the preamble's). */
#include "aica_io.h"

int test_main(void) {
    spin_us(50000);
    (void)replay_measure("check");
    return 0;
}
