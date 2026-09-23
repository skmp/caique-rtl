#include "../tools/filt_capture.h"
#include <cstdlib>
int main(int argc, char **argv) { auto c = cap(argv[1]); int from = atoi(argv[2]), to = atoi(argv[3]); int32_t last[4] = {0x7fffffff,0x7fffffff,0x7fffffff,0x7fffffff};
  for (int i = from; i < to && i < (int)c.n; i++) { bool ch = false; for (int k = 0; k < 4; k++) if (c.v[i*4+k] != last[k]) ch = true; if (!ch) continue; printf("%6d:", i); for (int k = 0; k < 4; k++) { printf(" %8d", c.v[i*4+k]); last[k] = c.v[i*4+k]; } printf("\n"); } }
